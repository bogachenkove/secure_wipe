#include "config.h"
#include "common.h"

void configDefault (program_config_t *config)
{
 memset (config, 0, sizeof (program_config_t));
 config->method = WIPE_METHOD_ZERO;
 config->passes = 1;
 config->verify = true;
 config->verbose = true;
 config->bufferSize = DEFAULT_BUFFER_SIZE;
 config->destroyPartitionTable = false;
 config->skipAnalysis = false;
 config->cycles = 1;

 config->emergencyMode = false;
 config->emergencySectors = 0;
 config->blockCount = 0;
 config->blockGiven = false;
 config->bufferGiven = false;

 config->ataSecureErase = false;
 config->ataEnhancedErase = false;
}