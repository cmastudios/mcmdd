//
//  server.c
//  mcmdd
//
//  Created by Connor Monahan on 8/16/14.
//  Copyright (c) 2014 Connor Monahan. All rights reserved.
//

#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <sys/wait.h>
#include <err.h>
#include <stdlib.h>
#include <signal.h>
#include <assert.h>

#include "server.h"

struct server_t *server_new(const char *path, const char *command, const char *id)
{
    struct server_t *server = malloc(sizeof(struct server_t));
    server->id = strdup(id);
    server->path = strdup(path);
    char **argv = malloc(sizeof(char *) * strlen(command));

	size_t len = 0;
	char *str_ = malloc(strlen(command) + 1);
	size_t i, tokpos;
	for (i = 0, tokpos = 0; i < strlen(command); i++, tokpos++) {
		if (command[i] == '\\') {
			str_[tokpos] = command[++i];
		} else if (command[i] == ' ') {
			if (tokpos == 0) {
				tokpos = -1;
				continue;
			}
            str_[tokpos] = '\0';
            argv[len++] = strdup(str_);
			tokpos = -1;
		} else {
			str_[tokpos] = command[i];
        }
	}
    if (tokpos > 0) {
        str_[tokpos] = '\0';
        argv[len++] = strdup(str_);
    }
    argv[len] = NULL; // must be null terminated for execv
    server->argv = argv;
    server->linsp = 0;
    bzero(server->lines, SERVER_MAXLINES);
	free(str_);
    return server;
}

static inline void server_cleanup(struct server_t *server)
{
    size_t i;
    char *str;
    
    i = 0;
    str = server->lines[i++];
    while (str) {
        free(str);
        str = server->lines[i++];
    }
}

void server_free(struct server_t *server)
{
    size_t i;
    char *str;
    free(server->id);
    free(server->path);
    i = 0;
    str = server->argv[i++];
    while (str) {
        free(str);
        str = server->argv[i++];
    }
    server_cleanup(server);
    free(server);
}

static void add_line(struct server_t *server, const char *line)
{
    if (server->linsp >= SERVER_MAXLINES)
        server->linsp = 0;
    if (server->lines[server->linsp])
        free(server->lines[server->linsp]);
    server->lines[server->linsp++] = strdup(line);
}

static inline void process_line(struct server_t *server, const char *line)
{
    add_line(server, line);
    printf("[%s] #%2d: %s\n", server->id, server->linsp, line);
    if (server->status == STATUS_STARTING && strstr(line, "Done"))
        server->status = STATUS_RUNNING;
    time(&server->last_read);
}

static void read_line(int fd, struct server_t *server)
{
    char ch, buf[SERVER_LINEMAX];
    int sp;
    
    sp = 0;
    while (read(fd, &ch, 1) != 0) {
        // lines longer than max are cycled around
        if (ch == '\n' || sp >= SERVER_LINEMAX) {
            // null-terminate
            buf[sp++] = '\0';
            process_line(server, buf);
            sp = 0;
        } else {
            buf[sp++] = ch;
        }
    }
}

int server_send(struct server_t *server, const char *message)
{
    if (server->status == STATUS_STOPPED)
        return -1;
    add_line(server, message);
    printf("[%s] < %s", server->id, message);
    write(server->pipein, message, strlen(message));
    return 0;
}

void server_stop(struct server_t *server, int exit)
{
    if (exit == EXIT_FULL)
        server->ctrl = CTRL_EXIT;
    else if (exit == EXIT_PAUSE)
        server->ctrl = CTRL_PAUSE;
    if (server->status == STATUS_STOPPED)
        return;
    server->status = STATUS_STOPPING;
    server_send(server, SHUTDOWN_COMMAND);
}

void server_stop_kill(struct server_t *server, int exit, int wait)
{
    int time_waited_ms = 0;
    server_stop(server, exit);
    printf("[%s] Waiting (max %d seconds) for server to stop.\n", server->id, wait);
    while (server->status == STATUS_STOPPING) {
        usleep(100000);
        time_waited_ms += 100;
        if (time_waited_ms > (wait * 1000)) {
            server_kill(server, exit);
        }
    }
}

int server_kill(struct server_t *server, int exit)
{
    if (exit == EXIT_FULL)
        server->ctrl = CTRL_EXIT;
    else if (exit == EXIT_PAUSE)
        server->ctrl = CTRL_PAUSE;
    if (server->status == STATUS_STOPPED)
        return -1;
    server->status = STATUS_STOPPED;
    printf("[%s] Killing server process %d\n", server->id, server->pid);
    add_line(server, "Server process killed");
    return kill(server->pid, SIGKILL);
}

void server_resume(struct server_t *server)
{
    if (server->status == STATUS_BACKUP) {
        // do not resume the server during a backup
        return;
    }
    server->ctrl = CTRL_LAUNCH;
}

void server_set_backup(struct server_t *server, int flag)
{
    if (flag) {
        server->status = STATUS_BACKUP;
    } else {
        server->status = STATUS_STOPPED;
    }
}

int server_start(struct server_t *server)
{
    pid_t cpid, pid;
    int pipeout[2], pipein[2];
    // pipe for reading from the server console
    pipe(pipeout);
    // pipe for sending commands and messages to the server
    pipe(pipein);
    // status field is to allow other threads to check how the server is doing
    server->status = STATUS_STARTING;
    server->pipein = pipein[1];
    time(&server->start);
    cpid = fork(); // program execution flow 'forks' here
    if (cpid == 0) {
        // child process started for server
        // assign the standard in, out, and error so the server can be monitored
        dup2(pipein[0], 0);
        dup2(pipeout[1], 1);
        dup2(pipeout[1], 2);
        // close the original pipes (the functions above created clones)
        close(pipeout[0]);
        close(pipeout[1]);
        close(pipein[1]);
        close(pipeout[1]);
        // go to the server's data directory
        chdir(server->path);
        // launch it, calling java (or whatever) from the server's PATH
        execvp(server->argv[0], server->argv);
        err(1, "server exec failed for %s", server->id);
    } else if (cpid > 0) {
        // parent process to monitor server
        int status;
        // close the ends of the pipe that we shouldn't be using
        // e.g. writing to the pipe that returns the child's output
        close(pipeout[1]);
        close(pipein[0]);
        server->pid = cpid;
        printf("[%s] Starting on PID %d.\n", server->id, cpid);
        // read until the server stops
        read_line(pipeout[0], server);
        // close the pipes as we are all done
        close(pipeout[0]);
        close(pipein[1]);
        // get the status and prevent creating zombies
        pid = wait(&status);
        server->status = STATUS_STOPPED;
        printf("[%s] PID %d exists with %d.\n", server->id, pid, status);
        return status;
    } else {
        err(1, "server fork failed for %s", server->id);
    }
}