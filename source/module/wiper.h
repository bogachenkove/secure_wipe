#ifndef WIPER_H
#define WIPER_H
#include "common.h"
#include "device_io.h"
#include "analyzer.h"
typedef enum { WIPE_METHOD_ZERO, WIPE_METHOD_RANDOM } wipe_method_t;
typedef struct {
  wipe_method_t method;
  uint32_t passes;
  bool skip_bad_sectors;
  bool verify_after_wipe;
  analysis_result_t *bad_sectors;
  progress_callback_t progress;
} wipe_config_t;
error_code_t wiper_init(void);
void wiper_cleanup(void);
error_code_t wiper_execute(device_t *device, const wipe_config_t *config, wipe_stats_t *stats);
const char *wiper_method_name(wipe_method_t method);
int wiper_method_passes(wipe_method_t method);
typedef error_code_t (*wiper_method_func)(device_t *device, uint32_t passes, progress_callback_t progress);
error_code_t wiper_method_zero(device_t *device, uint32_t passes, progress_callback_t progress);
error_code_t wiper_method_random(device_t *device, uint32_t passes, progress_callback_t progress);
error_code_t wiper_zero_partition_table(device_t *device);
error_code_t wiper_prepare_device(device_t *device, progress_callback_t progress);
bool wiper_try_remove_write_protection(device_t *device);
#endif
