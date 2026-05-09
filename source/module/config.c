#include "config.h"
#include "common.h"

void configDefault (program_config_t *config)
{
 memset (config, 0, sizeof (program_config_t));
 snprintf (config->logPath, sizeof (config->logPath), "%s", "secure_wipe.log");
 config->method = WIPE_METHOD_DOD_SHORT;
 config->passes = 3;
 config->verify = true;
 config->verbose = true;
 config->bufferSize = DEFAULT_BUFFER_SIZE;
 config->destroyPartitionTable = false;
}