#include "file_wiper.h"
#include "file_io.h"
#include "random_gen.h"
#include "platform.h"
typedef enum { PATTERN_ZERO, PATTERN_RANDOM } pattern_type_t;
static void fill_buffer_with_pattern(uint8_t *buffer, size_t buffer_size, pattern_type_t pattern_type, const uint8_t *pattern_data,
                                     size_t pattern_length) {
  (void)pattern_data;
  (void)pattern_length;
  switch (pattern_type) {
  case PATTERN_ZERO:
    memset(buffer, 0x00, buffer_size);
    break;
  case PATTERN_RANDOM:
    random_fill(buffer, buffer_size);
    break;
  default:
    break;
  }
}
static error_code_t wipe_file_content(file_t *file, const file_wipe_config_t *config) {
  if (!file || !file->is_open || file->read_only)
    return ERR_INVALID_ARG;
  uint64_t file_size;
  error_code_t error = file_get_size(file, &file_size);
  if (error != ERR_OK)
    return error;
  if (file_size == 0)
    return ERR_OK;
  size_t buffer_size = global_buffer_size;
  if (buffer_size > file_size)
    buffer_size = (size_t)file_size;
  uint8_t *buffer = (uint8_t *)aligned_alloc(buffer_size);
  if (!buffer)
    return ERR_MEMORY;
  const wipe_method_t method = config->method;
  const uint32_t passes = (method == WIPE_METHOD_RANDOM) ? config->passes : wiper_method_passes(method);
  for (uint32_t pass = 1; pass <= passes; pass++) {
    uint64_t offset = 0;
    while (offset < file_size) {
      size_t chunk = buffer_size;
      if (offset + chunk > file_size)
        chunk = (size_t)(file_size - offset);
      pattern_type_t pattern_type = PATTERN_ZERO;
      switch (method) {
      case WIPE_METHOD_ZERO:
        pattern_type = PATTERN_ZERO;
        break;
      case WIPE_METHOD_RANDOM:
        pattern_type = PATTERN_RANDOM;
        break;
      default:
        aligned_free(buffer);
        return ERR_INVALID_ARG;
      }
      fill_buffer_with_pattern(buffer, chunk, pattern_type, NULL, 0);
      size_t bytes_written = 0;
      error = file_write(file, offset, buffer, chunk, &bytes_written);
      if (error != ERR_OK || bytes_written != chunk) {
        aligned_free(buffer);
        return ERR_WRITE_DEVICE;
      }
      offset += chunk;
      if (config->progress)
        config->progress(offset, file_size, (int)pass, "Wiping file");
    }
    file_flush(file);
  }
  aligned_free(buffer);
  return ERR_OK;
}
static error_code_t rename_file_multiple(file_t *file, uint8_t rename_count) {
  if (!file || rename_count == 0)
    return ERR_INVALID_ARG;
  char original_path[MAX_PATH_LEN];
  snprintf(original_path, sizeof(original_path), "%s", file->path);
  for (uint8_t step = 1; step <= rename_count; step++) {
    uint64_t random_value;
    if (random_uint64(&random_value) != ERR_OK)
      random_value = (uint64_t)time(NULL) ^ step;
    char new_path[MAX_PATH_LEN];
    snprintf(new_path, sizeof(new_path), "%s.%016llX.~%u", original_path, (unsigned long long)random_value, step);
    error_code_t error = file_rename(file, new_path);
    if (error != ERR_OK)
      return error;
  }
  return ERR_OK;
}
error_code_t file_wipe_single(const char *path, const file_wipe_config_t *config) {
  if (!path || !config)
    return ERR_INVALID_ARG;
  file_info_t info;
  error_code_t error = file_scanner_get_info(path, &info);
  if (error != ERR_OK)
    return error;
  if (info.type == FILE_TYPE_DIRECTORY)
    return file_wipe_directory(path, config);
  file_t file;
  error = file_open(&file, path, false);
  if (error != ERR_OK)
    return error;
  if (!config->preserve_timestamps)
    file_set_times(&file, 0, 0);
  error = wipe_file_content(&file, config);
  if (error != ERR_OK) {
    file_close(&file);
    return error;
  }
  if (config->rename_before_delete && config->rename_count > 0)
    rename_file_multiple(&file, config->rename_count);
  error = file_delete(&file);
  return error;
}
error_code_t file_wipe_list(file_list_t *list, const file_wipe_config_t *config) {
  if (!list || !config)
    return ERR_INVALID_ARG;
  for (size_t index = 0; index < list->count; index++) {
    error_code_t error = file_wipe_single(list->items[index].full_path, config);
    if (error != ERR_OK)
      return error;
  }
  return ERR_OK;
}
error_code_t file_wipe_directory(const char *path, const file_wipe_config_t *config) {
  if (!path || !config)
    return ERR_INVALID_ARG;
  file_list_t list;
  error_code_t error = file_scanner_init_list(&list);
  if (error != ERR_OK)
    return error;
  error = file_scanner_scan(path, &list, true, false);
  if (error != ERR_OK) {
    file_scanner_free_list(&list);
    return error;
  }
  error = file_wipe_list(&list, config);
  file_scanner_free_list(&list);
  if (error != ERR_OK)
    return error;
  file_t dir_file;
  error = file_open(&dir_file, path, false);
  if (error != ERR_OK)
    return error;
  error = file_delete(&dir_file);
  return error;
}
error_code_t file_wipe_path(const char *path, const file_wipe_config_t *config) {
  if (!path || !config)
    return ERR_INVALID_ARG;
  file_info_t info;
  error_code_t error = file_scanner_get_info(path, &info);
  if (error != ERR_OK)
    return error;
  if (info.type == FILE_TYPE_DIRECTORY)
    return file_wipe_directory(path, config);
  else
    return file_wipe_single(path, config);
}
