#ifndef EXECUTOR_H
#define EXECUTOR_H
#include "config.h"
#include "analyzer.h"
int run_wipe(const program_config_t *config, analysis_result_t *analysis);
int emergency_wipe(const program_config_t *config);
#endif
