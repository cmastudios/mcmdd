//
//  config.h
//  mcmdd
//
//  Created by Connor Monahan on 8/16/14.
//  Copyright (c) 2014 Connor Monahan. All rights reserved.
//

#ifndef mcmdd_config_h
#define mcmdd_config_h

#include <stdio.h>

struct config_t {
    char **keys;
    char **values;
    char **sections;
    int len;
    int max;
};

#define DEFAULT_COMMAND "java -jar server.jar nogui"

struct config_t *config_new(void);
int config_load(struct config_t *config, FILE *file);
const char *config_get(struct config_t *config, const char *section, const char *key, const char *default_value);
void config_free(struct config_t *config);

#endif
