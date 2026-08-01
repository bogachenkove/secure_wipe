#ifndef FILE_IO_H
#define FILE_IO_H
#include "common.h"
#ifdef _WIN32
#include <windows.h>
typedef HANDLE file_handle_t;
#define INVALID_FILE_HANDLE_VAL INVALID_HANDLE_VALUE
#else
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
typedef int file_handle_t;
#define INVALID_FILE_HANDLE_VAL (-1)
#endif
typedef struct {
  file_handle_t handle;
  char path[MAX_PATH_LEN];
  bool is_open;
  bool read_only;
  uint64_t size;
} file_t;
error_code_t file_open(file_t *file, const char *path, bool read_only);
error_code_t file_close(file_t *file);
error_code_t file_read(file_t *file, uint64_t offset, uint8_t *buffer, size_t size, size_t *bytes_read);
error_code_t file_write(file_t *file, uint64_t offset, const uint8_t *buffer, size_t size, size_t *bytes_written);
error_code_t file_flush(file_t *file);
error_code_t file_get_size(file_t *file, uint64_t *size);
error_code_t file_truncate(file_t *file, uint64_t new_size);
error_code_t file_rename(file_t *file, const char *new_path);
error_code_t file_delete(file_t *file);
error_code_t file_set_times(file_t *file, time_t access_time, time_t modify_time);
error_code_t file_set_attributes(file_t *file, bool read_only, bool hidden, bool system);
#endif
