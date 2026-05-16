#ifndef WIPER_H
#define WIPER_H

#include "common.h"
#include "device_io.h"
#include "analyzer.h"

typedef enum
{
 WIPE_METHOD_ZERO,
 WIPE_METHOD_RANDOM
} wipe_method_t;

typedef struct
{
 wipe_method_t method;
 uint32_t passes;
 bool skipBadSectors;
 bool verifyAfterWipe;
 analysis_result_t *badSectors;
 progress_callback_t progress;
} wipe_config_t;

error_code_t wiperInit (void);
void wiperCleanup (void);
error_code_t wiperExecute (device_t *device, const wipe_config_t *config, wipe_stats_t *stats);
const char *wiperMethodName (wipe_method_t method);
int wiperMethodPasses (wipe_method_t method);

typedef error_code_t (*wiper_method_func) (device_t *device, uint32_t passes, progress_callback_t progress);

error_code_t wiperMethodZero (device_t *device, uint32_t passes, progress_callback_t progress);
error_code_t wiperMethodRandom (device_t *device, uint32_t passes, progress_callback_t progress);

error_code_t wiperZeroPartitionTable (device_t *device);
error_code_t wiperPrepareDevice (device_t *device, progress_callback_t progress);
bool wiperTryRemoveWriteProtection (device_t *device);

#endif