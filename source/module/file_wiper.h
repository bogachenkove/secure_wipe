#ifndef FILE_WIPER_H
#define FILE_WIPER_H
#include "wiper.h"
#include "file_scanner.h"
typedef struct {
  wipe_method_t method;
  uint32_t passes;
  bool rename_before_delete;
  uint8_t rename_count;
  bool preserve_timestamps;
  progress_callback_t progress;
} file_wipe_config_t;
error_code_t file_wipe_single(const char *path, const file_wipe_config_t *config);
error_code_t file_wipe_list(file_list_t *list, const file_wipe_config_t *config);
error_code_t file_wipe_directory(const char *path, const file_wipe_config_t *config);
error_code_t file_wipe_path(const char *path, const file_wipe_config_t *config);
#endif
