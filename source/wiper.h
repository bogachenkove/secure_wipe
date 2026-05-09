#ifndef WIPER_H
#define WIPER_H

#include "common.h"
#include "device_io.h"
#include "analyzer.h"

typedef enum {
    WIPE_METHOD_ZERO,
    WIPE_METHOD_RANDOM,
    WIPE_METHOD_DOD_SHORT,
    WIPE_METHOD_DOD_FULL,
    WIPE_METHOD_SCHNEIER,
    WIPE_METHOD_GUTMANN,
    WIPE_METHOD_CUSTOM
} wipe_method_t;

typedef struct {
    wipe_method_t method;
    uint32_t passes;
    bool skipBadSectors;
    bool verifyAfterWipe;
    analysis_result_t* badSectors;
    progress_callback_t progress;
} wipe_config_t;

error_code_t wiperInit(void);
void wiperCleanup(void);
error_code_t wiperExecute(device_t* dev, const wipe_config_t* config, wipe_stats_t* stats);
const char* wiperMethodName(wipe_method_t method);
int wiperMethodPasses(wipe_method_t method);

typedef error_code_t(*wiper_method_func)(device_t* dev, uint32_t passes, progress_callback_t progress);

error_code_t wiperMethodZero(device_t* dev, uint32_t passes, progress_callback_t progress);
error_code_t wiperMethodRandom(device_t* dev, uint32_t passes, progress_callback_t progress);
error_code_t wiperMethodDoDShort(device_t* dev, uint32_t passes, progress_callback_t progress);
error_code_t wiperMethodDoDFull(device_t* dev, uint32_t passes, progress_callback_t progress);
error_code_t wiperMethodSchneier(device_t* dev, uint32_t passes, progress_callback_t progress);
error_code_t wiperMethodGutmann(device_t* dev, uint32_t passes, progress_callback_t progress);
error_code_t wiperMethodCustom(device_t* dev, uint32_t passes, progress_callback_t progress);

error_code_t wiperPrepareDevice(device_t* dev, progress_callback_t progress);
bool wiperTryRemoveWriteProtection(device_t* dev);

#endif