//
//  server.h
//  mcmdd
//
//  Created by Connor Monahan on 8/16/14.
//  Copyright (c) 2014 Connor Monahan. All rights reserved.
//

#ifndef mcmdd_server_h
#define mcmdd_server_h

#include <time.h>
#include <sys/types.h>

enum server_status_t {
    // server not running
    STATUS_STOPPED = 0,
    // listener has not received the "Done" message yet from minecraft
    STATUS_STARTING,
    // server has reported a successful start
    STATUS_RUNNING,
    // server_stop was used
    STATUS_STOPPING,
};

enum server_control_t {
    // internal
    CTRL_CLEAN = 0,
    // instructs restarter to quit completely
    CTRL_EXIT,
    // alerts a restarter listening to start again
    CTRL_LAUNCH,
    // causes the restarter to pause until receiving a launch
    CTRL_PAUSE
};

enum exit {
    EXIT_PAUSE = 0,
    EXIT_FULL = 1,
    EXIT_RESTART = 2
};

#define SERVER_MAXLINES 1024
#define SERVER_LINEMAX 1024

struct server_t {
    pid_t pid;
    char *path;
    char *const *argv;
    char *id;
    enum server_status_t status;
    enum server_control_t ctrl;
    int pipein, linsp;
    char *lines[SERVER_MAXLINES];
    time_t start, last_read;
};

struct server_t *server_new(const char *path, const char *command, const char *id);
void server_free(struct server_t *server);
int server_start(struct server_t *server);
int server_send(struct server_t *server, const char *message);
void server_stop(struct server_t *server, int exit);
int server_kill(struct server_t *server, int exit);
void server_resume(struct server_t *server);

#endif
