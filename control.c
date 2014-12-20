//
//  control.c
//  mcmdd
//
//  Created by Connor Monahan on 8/17/14.
//  Copyright (c) 2014 Connor Monahan. All rights reserved.
//

#include <stdio.h>
#include <arpa/inet.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <poll.h>
//#include <sys/un.h>
#include <err.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <stdlib.h>
#include <errno.h>
#include "config.h"
#include "server.h"

static struct sockaddr_in sin;
//static struct sockaddr_un sun;
static int listener;
extern struct config_t *config;

struct server_t *get_server(const char *id);

void control_init()
{
    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = INADDR_ANY;
    sin.sin_port = htons(atoi(config_get(config, NULL, "port", "8361")));
    
//    sun.sun_family = AF_UNIX;
//    strcpy(sun.sun_path, "mcmdd.sock");
//    unlink(sun.sun_path);
    
    listener = socket(AF_INET, SOCK_STREAM, 0);
    
    if (bind(listener, (struct sockaddr *) &sin, sizeof(sin)) < 0)
        err(1, "bind to IP socket");
    
//    if (bind(listener, (struct sockaddr *) &sun, sizeof(sin)) < 0)
//        err(1, "bind to Unix socket");
//    
    if (listen(listener, 5) < 0)
        err(1, "listen");
}

/*!
 * @return number of bytes read, or status. Returns -1 on overflow
 *   or 0 on end of file.
 */
static int read_line(char *out, int fd, int max)
{
    int bytes, status;
    char ch;
    struct pollfd pfd;
    
    pfd.fd = fd;
    pfd.events = POLLIN;
    bytes = 0;
    status = 1;
    while (status > 0) {
        status = poll(&pfd, 1, 10000);
        if (status == 0)
            return -2;
        else if (status == -1 && errno == EINTR)
            continue; // interrupted sys call
        else if (status == -1)
            err(1, "read_line poll");
        status = recv(fd, &ch, 1, 0);
        if (status == 0)
            return -1;
        else if (status == -1)
            err(1, "read_line recv");
        
        if (bytes >= max)
            return -1;
        if (ch == '\n') {
            out[bytes] = '\0';
            return bytes;
        } else {
            out[bytes++] = ch;
        }
    }
    return 0;
}

int valid(const char *key, const char *server)
{
    if (!key || !server || strlen(key) < 1 || strlen(server) < 1)
        return 0;
    if (strstr(config_get(config, NULL, "servers", ""), server) == NULL)
        // Nonexistant server
        return 0;
    char *token, *string, *tofree;
    int valid;
    
    tofree = string = strdup(config_get(config, server, "auth", config_get(config, NULL, "auth", "")));
    if (!string)
        err(1, "strdup");
    
    valid = 0;
    while ((token = strsep(&string, " ")) != NULL)
        if (strcmp(key, token) == 0)
            valid = 1;
    
    free(tofree);
    return valid;
}

#define APPNAME "mcmdd/1.0.1\n"
#define INVALID "ERR Invalid command.\n"
#define SVNEXT "OK Need key.\n"
#define KYNEXT "OK Need server.\n"
#define BADKEY "ERR Bad login.\n"
#define OKKEY "OK Logged in.\n"
#define INTERR "ERR Internal error.\n"
#define OKEXEC "OK Command sent.\n"
#define STATF "OK Stats %d %.f\n"
#define EOFF "ERR Server is off.\n"
#define TSTART "OK Send start.\n"
#define TEND "OK Send end.\n"

static inline void qwrite(int fd, const char *message)
{
    write(fd, message, strlen(message));
}

static inline void send_log(int fd, struct server_t *server, const char *start_line)
{
    int sp, bufsp, found_line;
    char buf[(SERVER_LINEMAX + 2) * SERVER_MAXLINES];

    bufsp = 0;
    found_line = 0;
    qwrite(fd, TSTART);
    if (server->lines[server->linsp]) {
        // means data has been overwritten, so start reading from linsp forward
        for (sp = server->linsp; sp < SERVER_MAXLINES; ++sp) {
            // add line to buffer at current position
            sprintf(buf + bufsp, "%s\n", server->lines[sp]);
            // buffer is written into like one giant block
            bufsp += strlen(server->lines[sp]) + 1; // +1 to overwrite the last \0
            if (start_line && !found_line && strstr(start_line, server->lines[sp]) == start_line) {
                found_line = 1;
                bufsp = 0;
            }
        }
    }
    for (sp = 0; sp < server->linsp; ++sp) {
        sprintf(buf + bufsp, "%s\n", server->lines[sp]);
        bufsp += strlen(server->lines[sp]) + 1;
        if (start_line && !found_line && strstr(start_line, server->lines[sp]) == start_line) {
            found_line = 1;
            bufsp = 0;
        }
    }
    // send the whole buffer
    write(fd, buf, bufsp);
    qwrite(fd, TEND);
}

void control_read(int fd)
{
    qwrite(fd, APPNAME);
    char tmp[256], msg[256];
    char *key, *server;
    int vald;
    int ka;
    int r;
    struct server_t *serv;
    
    key = server = NULL;
    vald = 0;
    serv = NULL;
    ka = 0;
    while (1) {
        r = read_line(tmp, fd, 256);
        if (r == -2 && ka)
            continue;
        else if (r < 1)
            goto clean;
        if (strstr(tmp, "SERVER ") == tmp) {
            free(server);
            server = strdup(tmp + 7);
            vald = valid(key, server);
            if (vald)
                qwrite(fd, OKKEY);
            else if (key)
                qwrite(fd, BADKEY);
            else
                // purposely doesn't notify for invalid server, for security
                qwrite(fd, SVNEXT);
        } else if (strstr(tmp, "KEY ") == tmp) {
            free(key);
            key = strdup(tmp + 4);
            vald = valid(key, server);
            if (vald)
                qwrite(fd, OKKEY);
            else if (key)
                qwrite(fd, BADKEY);
            else
                qwrite(fd, KYNEXT);
        } else if (strstr(tmp, "EXEC ") == tmp) {
            if (!serv) {
                qwrite(fd, BADKEY);
                continue;
            }
            sprintf(msg, "%s\n", tmp + 5);
            if (server_send(serv, msg) == 0)
                qwrite(fd, OKEXEC);
            else
                qwrite(fd, EOFF);
        } else if (strstr(tmp, "KILL") == tmp) {
            if (!serv) {
                qwrite(fd, BADKEY);
                continue;
            }
            if (server_kill(serv, EXIT_PAUSE) == 0)
                qwrite(fd, OKEXEC);
            else
                qwrite(fd, INTERR);
        } else if (strstr(tmp, "STOP") == tmp) {
            if (!serv) {
                qwrite(fd, BADKEY);
                continue;
            }
            server_stop(serv, EXIT_PAUSE);
            qwrite(fd, OKEXEC);
        } else if (strstr(tmp, "RESTART") == tmp) {
            if (!serv) {
                qwrite(fd, BADKEY);
                continue;
            }
            server_stop(serv, EXIT_RESTART);
            qwrite(fd, OKEXEC);
        } else if (strstr(tmp, "START") == tmp) {
            if (!serv) {
                qwrite(fd, BADKEY);
                continue;
            }
            server_resume(serv);
            qwrite(fd, OKEXEC);
        } else if (strstr(tmp, "STATUS") == tmp) {
            if (!serv) {
                qwrite(fd, BADKEY);
                continue;
            }
            sprintf(msg, STATF, serv->status, difftime(time(NULL), serv->start));
            qwrite(fd, msg);
        } else if (strstr(tmp, "LOG") == tmp) {
            if (!serv) {
                qwrite(fd, BADKEY);
                continue;
            }
            if (tmp[3] == ' ') {
                send_log(fd, serv, tmp + 4);
            } else {
                send_log(fd, serv, NULL);
            }
        } else if (strstr(tmp, "KEEPALIVE") == tmp) {
            ka = 1;
        } else {
            qwrite(fd, INVALID);
        }
        if (vald) {
            serv = get_server(server);
            if (!serv) {
                qwrite(fd, INTERR);
                goto clean;
            }
        } else {
            serv = NULL;
        }
    }
clean:
    close(fd);
    free(key);
    free(server);
}

void *control_thread(void *data)
{
    control_read(*((int *)data));
    pthread_exit(NULL);
}

void control_accept()
{
    int fd, rc;
    void *status;
    struct sockaddr_in ss;
    socklen_t slen = sizeof(ss);
    while (1) {
        fd = accept(listener, (struct sockaddr *) &ss, &slen);
        if (fd == -1 && errno == EINTR)
            continue; // interrupted system call
        else if (fd == -1)
            err(1, "accept");
        pthread_t threadId;
        rc = pthread_create(&threadId, NULL, &control_thread, (void *) &fd);
        if (pthread_detach(threadId) != 0) {
            warn("Failed to detach control thread, running sync");
            pthread_join(threadId, &status);
        }
    }
}

void control_stop()
{
    shutdown(listener, SHUT_RDWR);
    close(listener);
}
