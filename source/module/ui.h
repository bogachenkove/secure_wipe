#ifndef UI_H
#define UI_H
#include "config.h"
#include "device_io.h"
#include "ata_erase.h"
bool interactive_select_disk(program_config_t *config);
bool confirm_wipe(const char *device_path, uint64_t size_bytes, wipe_method_t method, uint32_t actual_passes);
void progress_handler(uint64_t current, uint64_t total, int pass, const char *phase);
bool prompt_ata_erase(const ata_security_info_t *info, const char *device_path);
#endif
