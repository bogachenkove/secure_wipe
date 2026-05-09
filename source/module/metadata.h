#ifndef METADATA_H
#define METADATA_H

#include "config.h"

bool parseArguments(int argc, char* argv[], program_config_t* cfg);
void printUsage(const char* programName);
void printMethods(void);
void printVersion(void);
void printAbout(void);
void printLicense(void);
void printSupport(void);

#endif