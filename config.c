//
//  config.c
//  mcmdd
//
//  Created by Connor Monahan on 8/16/14.
//  Copyright (c) 2014 Connor Monahan. All rights reserved.
//

#include <stdio.h>
#include <err.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"

struct config_t *config_new(void)
{
    struct config_t *config = malloc(sizeof(struct config_t));
    if (!config)
        err(EXIT_FAILURE, "Failed to allocate memory");
    config->len = 0;
    config->max = 0;
    return config;
}

void config_free(struct config_t *config)
{
    size_t i;
    for (i = 0; i < config->len; ++i) {
        free(config->keys[i]);
        free(config->values[i]);
        free(config->sections[i]);
    }
    free(config->keys);
    free(config->values);
    free(config->sections);
    free(config);
}

static inline void config_add(struct config_t *config, const char *key, const char *value, const char *section)
{
    if (config->len >= config->max) {
        if (config->max < 1)
            config->max = 10;
        config->max *= 2; // double
        config->keys = realloc(config->keys, sizeof(char *) * config->max);
        config->values = realloc(config->values, sizeof(char *) * config->max);
        config->sections = realloc(config->sections, sizeof(char *) * config->max);
        if (!config->keys || !config->values || !config->sections)
            err(EXIT_FAILURE, "Failed to allocate memory");
    }
    config->keys[config->len] = strdup(key);
    config->values[config->len] = strdup(value);
    if (section && section[0]) {
        config->sections[config->len] = strdup(section);
    } else {
        config->sections[config->len] = NULL;
    }
    config->len++;
}

const char *config_get(struct config_t *config, const char *section, const char *key, const char *default_value)
{
    size_t i;
    for (i = 0; i < config->len; ++i) {
        const char *lkey = config->keys[i], *lsec = config->sections[i];
        if (section && !lsec)
            continue;
        if (!section && lsec)
            continue;
        if (section && strcmp(section, lsec) != 0)
            continue;
        if (strcmp(key, lkey) == 0)
            return config->values[i];
    }
    return default_value;
}

enum config_state_t {
    STATE_KEY = 0, // first part of a config option
    STATE_VAL, // second part, the value
    STATE_REM, // comment lines
    STATE_SEC // introduction of a new section
};

int config_load(struct config_t *config, FILE *file)
{
    config->len = 0;
    config->max = 10;
    config->keys = malloc(sizeof(char *) * config->max);
    config->values = malloc(sizeof(char *) * config->max);
    config->sections = malloc(sizeof(char *) * config->max);
    if (!config->keys || !config->values || !config->sections)
        err(EXIT_FAILURE, "Failed to allocate memory");
    char ch;
    int state;
    int pos;
    char key[64], value[1024], section[64];
    state = 0;
    pos = 0;
    section[0] = '\0';
    while ((ch = fgetc(file)) != EOF) {
        if (ch == '\r' || ch == '\n') {
            if (state == STATE_VAL && pos > 0 && strlen(key) > 0) {
                // terminate a value and push it to the config
                value[pos] = '\0';
                config_add(config, key, value, section);
            }
            // reset current state
            state = STATE_KEY;
            pos = 0;
            key[0] = '\0';
            value[0] = '\0';
        } else if (state == STATE_KEY && (ch == '#' || ch == ';')) {
            // comments. checked here because key is at the beginning of the line
            state = STATE_REM;
        } else if (state == STATE_KEY && ch == '=') {
            // at an equals sign, we are done loading the key and need to look for the value
            state = STATE_VAL;
            key[pos] = '\0';
            pos = 0;
        } else if (state == STATE_KEY && (ch == ' ' || ch == '\t')) {
            // ignore spaces in key
        } else if (state == STATE_KEY && ch == '[') {
            state = STATE_SEC;
        } else if (state == STATE_KEY) {
            if (pos > 63)
                return -1;
            key[pos++] = ch;
        } else if (state == STATE_VAL && pos == 0 && (ch == ' ' || ch == '\t')) {
            // ignore space at start of value
        } else if (state == STATE_VAL) {
            if (pos > 1023)
                return -1;
            value[pos++] = ch;
        } else if (state == STATE_SEC && ch == ']') {
            // terminate the section. note it is not reset, as it will stay the section until there is a new section
            // it is set to comment mode so everything else is ignored
            section[pos] = '\0';
            state = STATE_REM;
            pos = 0;
            key[0] = '\0';
            value[0] = '\0';
        } else if (state == STATE_SEC) {
            if (pos > 63)
                return -1;
            section[pos++] = ch;
        }
    }
    return 0;
}
