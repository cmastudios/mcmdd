//
//  main.c
//  mcmdd
//
//  Created by Connor Monahan on 8/15/14.
//  Copyright (c) 2014 Connor Monahan. All rights reserved.
//

#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <err.h>
#include <sys/stat.h>
#include <pthread.h>
#include <sys/types.h>
#include <pwd.h>

#include "config.h"
#include "server.h"

const char *program_name;
struct config_t *config;
struct server_t **servers;
pthread_t **threads;
pthread_t backup_thread;
int servers_sp, threads_sp;

void control_init();
void control_accept();
void control_stop();

static void usage(void)
{
    fprintf(stderr, "usage: %s [-nf] [-d path]\n", program_name);
    exit(1);
}

static void load_config()
{
    config = config_new();
    FILE *config_file = fopen("mcmdd.conf", "r");
    if (!config_file)
        err(EXIT_FAILURE, "Failed to load config");
    config_load(config, config_file);
    fclose(config_file);
}

struct server_t *get_server(const char *id)
{
    size_t i;
    for (i = 0; i < servers_sp; ++i)
        if (strcmp(id, servers[i]->id) == 0)
            return servers[i];
    return NULL;
}

static void load_server(const char *name)
{
    const char *path, *command;

    printf("[%s] Loading\n", name);
    
    path = config_get(config, name, "path", "");
    command = config_get(config, name, "command", DEFAULT_COMMAND);
    
    servers[servers_sp++] = server_new(path, command, name);
}

static void load_servers()
{
    char *svr, *string, *tofree;
    const char *svrs;
    
    svrs = config_get(config, NULL, "servers", NULL);
    if (!svrs)
        errx(1, "No servers list found in config");
    
    servers_sp = 0;
    servers = malloc(sizeof(struct server_t *) * strlen(svrs));
    tofree = string = strdup(svrs);
    if (!string)
        err(1, "strdup failed");
    
    while ((svr = strsep(&string, " ")) != NULL)
        load_server(svr);
    
    free(tofree);
    servers = realloc(servers, sizeof(struct server_t *) * servers_sp);
}

/*!
 * @return 0 for exit immediate, 1 for resume
 */
static inline int server_pause_loop(struct server_t *server)
{
    while (1) {
        if (server->ctrl == CTRL_EXIT)
            return 0;
        // resume the server, called by control
        if (server->ctrl == CTRL_LAUNCH)
            return 1;
        sleep(1);
    }
}

static inline int server_has_warmed_up(struct server_t *server)
{
    time_t now, duration;
    const char *warmup_s;
    int warmup;
    
    time(&now);
    duration = now - server->start;
    warmup_s = config_get(config, server->id, "warmup", "0");
    sscanf(warmup_s, "%d", &warmup);
    return duration > warmup;
}

void *thread_start_wrapper(void *ptr)
{
    struct server_t *server = ptr;
#ifdef __APPLE__
    pthread_setname_np(server->id);
#endif
    while (1) {
        server->ctrl = CTRL_CLEAN;
        server_start(server);
        // set when the daemon is killing all other servers, so quit
        if (server->ctrl == CTRL_EXIT)
            return NULL;
        // do not start the server up immediately unless x seconds have passed
        if (server->ctrl == CTRL_CLEAN && !server_has_warmed_up(server)) {
            printf("[%s] Paused - failed to keep server running long enough.\n", server->id);
            server->ctrl = CTRL_PAUSE;
        }
        // set by control when a shutdown is anticipated
        if (server->ctrl == CTRL_PAUSE
            && server_pause_loop(server) == 0)
            return NULL;
    }
}

static void run_server(struct server_t *server, int id)
{
    pthread_t *thread;
    int rc;
    
    thread = malloc(sizeof(pthread_t *));
    rc = pthread_create(thread, NULL, thread_start_wrapper, server);
#ifdef __linux__
    pthread_setname_np(*thread, server->id);
#endif
    if (rc)
        err(1, "pthread_create for %s", server->id);
    threads[threads_sp] = thread;
}

static void run_servers()
{
    size_t i;
    
    threads = malloc(sizeof(pthread_t *) * servers_sp);
    for (i = 0, threads_sp = 0; i < servers_sp; ++i, ++threads_sp)
        run_server(servers[i], i);
}

static void kill_servers()
{
    size_t i;
    void *status;
    
    puts("[daemon] Killing all servers");
    for (i = 0; i < servers_sp; ++i) {
        server_kill(servers[i], EXIT_FULL);
        pthread_join(*threads[i], &status);
    }
}

static void stop_servers()
{
    size_t i;
    void *status;
    
    puts("[daemon] Stopping all servers");
    for (i = 0; i < servers_sp; ++i) {
        server_stop_kill(servers[i], EXIT_FULL, MAX_WAIT);
        pthread_join(*threads[i], &status);
    }
}

void *backup_monitor(void *ptr)
{
    size_t i;
    const char *freq_s, *backup_command;
    char backup_folder[256], backup_name[256], command[256];
    int freq, tmin, rc;
    time_t now;
    struct tm *timeinfo;
    
#ifdef __APPLE__
    pthread_setname_np("mcmdd [backup]");
#elif __linux__
    pthread_setname_np(backup_thread, "mcmdd [backup]");
#endif
    if (access(BACKUP_DIRECTORY, F_OK) == -1 && mkdir(BACKUP_DIRECTORY, 0777) < 0) {
        err(1, "Failed to create backup directory");
    }
    backup_command = config_get(config, NULL, "backup_command", DEFAULT_BACKUP);
    while (1) {
        // every minute check
        sleep(60);
        time(&now);
        tmin = now / 60;
        timeinfo = localtime(&now);
        strftime(backup_name, sizeof(backup_name), BACKUP_DATE, timeinfo);
        for (i = 0; i < servers_sp; ++i) {
            struct server_t *server = servers[i];
            freq_s = config_get(config, server->id, "backup_frequency", "0");
            freq = 0;
            sscanf(freq_s, "%d", &freq);
            if (freq == 0) {
                continue;
            }
            if (tmin % freq != 0) {
                continue;
            }
            // if freq is 30 (every 30 minutes) it will run every half-hour
            snprintf(backup_folder, sizeof(backup_folder), BACKUP_DIRECTORY "/%s/", server->id);
            
            if (access(backup_folder, F_OK) == -1 && mkdir(backup_folder, 0777) < 0) {
                warn("Failed to make backup directory for server %s", server->id);
                continue;
            }
            strncat(backup_folder, backup_name, sizeof(backup_folder) - strlen(backup_folder) - 1);
            snprintf(command, sizeof(command), backup_command, backup_folder, server->id);
            // stop the server
            server_stop_kill(server, EXIT_PAUSE, MAX_WAIT);
            // prevent clients from starting the server during a backup
            server_set_backup(server, 1);
            printf("[%s] Running scheduled backup.\n", server->id);
            printf("[%s] >%s\n", server->id, command);
            rc = system(command);
            if (rc == 0) {
                printf("[%s] Backup succeeded!\n", server->id);
            } else if (rc != -1) {
                printf("[%s] Backup failed with code %d.\n", server->id, rc);
            } else {
                warn("Failed to run command %s", command);
            }
            // unlock and bring the server back online
            server_set_backup(server, 0);
            server_resume(server);
        }
    }
}

static void start_backup_monitor()
{
    int rc;
    
    rc = pthread_create(&backup_thread, NULL, backup_monitor, NULL);
    if (rc)
        err(1, "pthread_create for backup monitor");
}

static void cleanup()
{
    size_t i;

    // these are reassigned to files if forked
    fclose(stdout);
    fclose(stderr);
    
    config_free(config);
    for (i = 0; i < servers_sp; ++i) {
        server_free(servers[i]);
    }
    for (i = 0; i < threads_sp; ++i) {
        free(threads[i]);
    }
    free(servers);
    free(threads);
}

void signal_handler(int signum)
{
    // ignore notices about terminating child processes
    if (signum == SIGCHLD)
        return;
    
    printf("[daemon] Caught signal %d.\n", signum);
    // SIGINT is regarded as a safe shutdown signal
    if (signum == SIGINT)
        stop_servers();
    // SIGTERM is regarded as an emergency shutdown maneuver
    // such as at shutdown, when we receive SIGTERM and then only have
    // a few seconds before we are killed
    if (signum == SIGTERM)
        kill_servers();
    control_stop();
    pthread_cancel(backup_thread);
    puts("[daemon] Cleaning up");
    cleanup();
    exit(1);
}

static inline void fork_background()
{
    pid_t pid, sid;

    pid = fork();
    if (pid < 0)
        err(EXIT_FAILURE, "Failed to fork daemon to background");
    else if (pid > 0)
        // Is parent, so exit
        exit(EXIT_SUCCESS);
    // create files with mode 666
    umask(0);
    // turn the child process into a normal process
    sid = setsid();
    if (sid < 0)
        // failed to assert independence :)
        exit(EXIT_FAILURE);
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);
    freopen("mcmdd.log", "a", stdout);
    freopen("mcmdd.err", "a", stderr);
    fchmod(0, S_IRUSR | S_IWUSR);
    fchmod(1, S_IRUSR | S_IWUSR);
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);
    FILE *pidfile = fopen("mcmdd.pid", "w");
    if (!pidfile)
        err(1, "Failed to write pid file");
    fprintf(pidfile, "%d\n", sid);
    fclose(pidfile);
    chmod("mcmdd.pid", S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH);
}

static inline void change_user(const char *name)
{
    struct passwd *user = getpwnam(name);
    if (!user)
        errx(1, "User %s not found", name);
    if (setgid(user->pw_gid) < 0)
        err(1, "Changing group");
    if (setuid(user->pw_uid) < 0)
        err(1, "Changing user");
}

int main(int argc, char **argv)
{
    int ch, dofork;
    char *data_dir;
    
    program_name = argv[0];
    dofork = 0; // change to 1 for release
    data_dir = NULL;
    setbuf(stdout, NULL);
    
    while ((ch = getopt(argc, argv, "nfd:u:")) != -1) {
        switch (ch) {
            case 'n':
                dofork = 0;
                break;
            case 'f':
                dofork = 1;
                break;
            case 'd':
                data_dir = strdup(optarg);
                break;
            case 'u':
                change_user(optarg);
                break;
            case '?':
            default:
                usage();
        }
    }
    argc -= optind;
    argv += optind;
    
    if (data_dir && chdir(data_dir) < 0)
        err(EXIT_FAILURE, "Failed to open data dir %s", data_dir);
    free(data_dir);

    if (dofork)
        fork_background();
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGCHLD, signal_handler);
    
    load_config();
    control_init();
    load_servers();
    run_servers();
    start_backup_monitor();
    control_accept();
}
