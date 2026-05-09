#ifndef METADATA_H
#define METADATA_H

#include "wiper.h"

typedef struct {
    char devicePath[MAX_PATH_LEN];
    char logPath[MAX_PATH_LEN];
    wipe_method_t method;
    uint32_t passes;
    bool analyzeOnly;
    bool analyzeWriteTest;
    bool quickWpCheck;
    bool listDisks;
    bool selectDisk;
    bool showAllDisks;
    bool verify;
    bool autoConfirm;
    bool verbose;
} program_config_t;

void metadataDefaultConfig(program_config_t* config);
bool metadataParseArguments(int argc, char* argv[], program_config_t* config);
void metadataPrintUsage(const char* programName);
void metadataPrintMethods(void);
bool metadataInteractiveSelectDisk(program_config_t* config);
int metadataRun(program_config_t* config);

#endif