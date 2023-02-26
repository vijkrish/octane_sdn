#ifndef CONFIG
#define CONFIG

#include <stdbool.h>

/* Configuration */
#define MAX_FILE_LEN 255

#define MAX_STAGE                2
#define MAX_ROUTERS              2
#define CONFIG_PARAM_STAGE       "stage"
#define CONFIG_PARAM_NUM_ROUTERS "num_routers"

bool parse_config_file(char *config_file, int *stage, int *num_routers);

#endif
