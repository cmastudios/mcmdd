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

#include "config.h"
#include "server.h"

const char *program_name;
struct config_t *config;
struct server_t **servers;
pthread_t **threads;
pthread_t control_thread;
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
    
    servers[servers_sp++] = server_new(path, command, "main");
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

void *thread_start_wrapper(void *ptr)
{
    struct server_t *server = ptr;
    while (1) {
        server->ctrl = CTRL_CLEAN;
        server_start(server);
        // set when the daemon is killing all other servers, so quit
        if (server->ctrl == CTRL_EXIT)
            return NULL;
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
        server_stop(servers[i], EXIT_FULL);
        pthread_join(*threads[i], &status);
    }
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
    setbuf(stdout, NULL);
    setbuf(stderr, NULL);
}

int main(int argc, char **argv)
{
    int ch, dofork;
    char *data_dir;
    
    program_name = argv[0];
    dofork = 0; // change to 1 for release
    data_dir = NULL;
    
    while ((ch = getopt(argc, argv, "nfd:")) != -1) {
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
            case '?':
            default:
                usage();
        }
    }
    argc -= optind;
    argv += optind;
    
    if (dofork)
        fork_background();
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    signal(SIGCHLD, signal_handler);
    
    if (data_dir && chdir(data_dir) < 0)
        err(EXIT_FAILURE, "Failed to open data dir %s", data_dir);
    free(data_dir);
    
    load_config();
    control_init();
    load_servers();
    run_servers();
    control_accept();
}
