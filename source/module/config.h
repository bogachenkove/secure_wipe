#ifndef CONFIG_H
#define CONFIG_H

#include "wiper.h"

typedef struct
{
 char devicePath[MAX_PATH_LEN];
 wipe_method_t method;
 uint32_t passes;
 bool analyzeOnly;
 bool analyzeWriteTest;
 bool quickWpCheck;
 bool destroyPartitionTable;
 bool listDisks;
 bool selectDisk;
 bool skipAnalysis;
 bool showAllDisks;
 bool verify;
 bool autoConfirm;
 bool verbose;
 size_t bufferSize;
 uint32_t cycles;

 bool emergencyMode;
 uint64_t emergencySectors;
 uint64_t blockCount;
 bool blockGiven;
 bool bufferGiven;

 bool ataSecureErase;
 bool ataEnhancedErase;
} program_config_t;

void configDefault (program_config_t *config);

#endif