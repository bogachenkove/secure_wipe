#include "config.h"
#include "common.h"

void configDefault(program_config_t *cfg)
{
    memset(cfg, 0, sizeof(program_config_t));
    snprintf(cfg->logPath, sizeof(cfg->logPath), "%s", "secure_wipe.log");
    cfg->method = WIPE_METHOD_DOD_SHORT;
    cfg->passes = 3;
    cfg->verify = true;
    cfg->verbose = true;
    cfg->bufferSize = DEFAULT_BUFFER_SIZE;
    cfg->destroyPartitionTable = false;
}