#ifndef FILE_SCANNER_H
#define FILE_SCANNER_H
#include "common.h"
typedef enum { FILE_TYPE_REGULAR, FILE_TYPE_DIRECTORY, FILE_TYPE_SYMLINK, FILE_TYPE_OTHER } file_type_t;
typedef struct {
  char full_path[MAX_PATH_LEN];
  char name[MAX_PATH_LEN];
  uint64_t size;
  file_type_t type;
  bool is_read_only;
  bool is_hidden;
  bool is_system;
  time_t access_time;
  time_t modify_time;
  time_t create_time;
} file_info_t;
typedef struct {
  file_info_t *items;
  size_t count;
  size_t capacity;
} file_list_t;
error_code_t file_scanner_init_list(file_list_t *list);
void file_scanner_free_list(file_list_t *list);
error_code_t file_scanner_add_item(file_list_t *list, const file_info_t *info);
error_code_t file_scanner_scan(const char *path, file_list_t *list, bool recursive, bool follow_symlinks);
error_code_t file_scanner_get_info(const char *path, file_info_t *info);
#endif
