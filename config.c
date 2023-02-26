#include <stdio.h>
#include <stdbool.h>
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <unistd.h>
#include "config.h"

enum config_params {
    config_params_stage = 1,
    config_params_num_routers
};

/* Parse the given config file 
 * I/P - Config file (config_file)
 * O/P - Stage number (stage) and Number of routers (num_routers) */
bool parse_config_file(char *config_file, int *stage, int *num_routers)
{
    FILE *fp = NULL;
    char *line = NULL;
    char *param = NULL;
    size_t len = 0;
    bool skip = false;
    int config_params_id = 0;

    fp = fopen(config_file, "r");
    if (!fp) {
        printf("\n Unable to open config file %s - %s", config_file, strerror(errno));
        return false;
    }

    while (getline(&line, &len, fp) != -1) {
        skip = false;
        config_params_id = 0;

        /* Get the first parameter after a series of spaces */
        param = strtok (line, " ");
        while (param) {
            if (param[0] == '#') {
                /* This line is a comment */
                break;
            }

            /* Check the parameter read previously and set the corresponding
             * variable with the value read. */
            switch (config_params_id)
            {
                case config_params_stage:
                    *stage = atoi(param);
                    skip = true;
                    break;
                case config_params_num_routers:
                    *num_routers = atoi(param);
                    skip = true;
                    break;
            }
            if (skip) {
                break;
            }

            /* Check if the given parameter is stage or num_routers */
            if (strncmp(param, CONFIG_PARAM_STAGE, strlen(CONFIG_PARAM_STAGE)) == 0) {
                config_params_id = config_params_stage;
            } else if (strncmp(param, CONFIG_PARAM_NUM_ROUTERS, strlen(CONFIG_PARAM_NUM_ROUTERS)) == 0) {
                config_params_id = config_params_num_routers;
            }
            param = strtok (NULL, " ");
        }
    }
    if (line) {
        free(line);
    }
    return true;
}
