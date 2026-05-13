#ifndef EXECUTOR_H
#define EXECUTOR_H

#include "config.h"
#include "analyzer.h"

int runWipe (const program_config_t *config, analysis_result_t *analysis);
int emergencyWipe (const program_config_t *config);

#endif