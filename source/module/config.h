#ifndef CONFIG_H
#define CONFIG_H

#include "wiper.h"

typedef struct {
    char devicePath[MAX_PATH_LEN];
    char logPath[MAX_PATH_LEN];
    wipe_method_t method;
    uint32_t passes;
    bool analyzeOnly;
    bool analyzeWriteTest;
    bool quickWpCheck;
    bool destroyPartitionTable;
    bool listDisks;
    bool selectDisk;
    bool showAllDisks;
    bool verify;
    bool autoConfirm;
    bool verbose;
    size_t bufferSize;
} program_config_t;

void configDefault(program_config_t *cfg);

#endif