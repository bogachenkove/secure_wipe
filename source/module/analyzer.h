#ifndef ANALYZER_H
#define ANALYZER_H
#include "common.h"
#include "device_io.h"
error_code_t analyzer_init_result(analysis_result_t *result);
void analyzer_free_result(analysis_result_t *result);
error_code_t analyzer_scan_device(device_t *device, analysis_result_t *result, progress_callback_t progress);
error_code_t analyzer_add_bad_sector(analysis_result_t *result, uint64_t sector);
bool analyzer_is_bad_sector(const analysis_result_t *result, uint64_t sector);
void analyzer_print_report(const analysis_result_t *result);
uint64_t analyzer_verify_wipe(device_t *device, progress_callback_t progress);
typedef enum { ANALYZE_READ_ONLY = 0x00, ANALYZE_WRITE_TEST = 0x01, ANALYZE_DETECT_WP = 0x02 } analyze_flags_t;
typedef struct {
  analysis_result_t base;
  uint64_t *write_protected;
  uint64_t wp_count;
  uint64_t wp_capacity;
  uint64_t write_errors;
  uint64_t read_errors;
  uint64_t verify_errors;
  uint64_t first_wp_sector;
  uint64_t last_wp_sector;
  bool has_wp_regions;
} extended_analysis_result_t;
error_code_t analyzer_init_extended_result(extended_analysis_result_t *result);
void analyzer_free_extended_result(extended_analysis_result_t *result);
error_code_t analyzer_scan_device_extended(device_t *device, extended_analysis_result_t *result, analyze_flags_t flags, progress_callback_t progress);
void analyzer_print_extended_report(const extended_analysis_result_t *result);
bool analyzer_quick_wp_check(device_t *device, uint64_t test_sectors, uint64_t *first_wp_sector);
#endif
