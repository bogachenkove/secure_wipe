#ifndef UI_H
#define UI_H

#include "config.h"
#include "device_io.h"

bool interactiveSelectDisk (program_config_t *config);
bool confirmWipe (const char *devicePath, uint64_t sizeBytes, wipe_method_t method, uint32_t actualPasses);
void progressHandler (uint64_t current, uint64_t total, int pass, const char *phase);

#endif