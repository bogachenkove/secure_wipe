#ifndef FILE_SCANNER_H
#define FILE_SCANNER_H

#include "common.h"

typedef enum
{
 FILE_TYPE_REGULAR,
 FILE_TYPE_DIRECTORY,
 FILE_TYPE_SYMLINK,
 FILE_TYPE_OTHER
} file_type_t;

typedef struct
{
 char fullPath[MAX_PATH_LEN];
 char name[MAX_PATH_LEN];
 uint64_t size;
 file_type_t type;
 bool isReadOnly;
 bool isHidden;
 bool isSystem;
 time_t accessTime;
 time_t modifyTime;
 time_t createTime;
} file_info_t;

typedef struct
{
 file_info_t *items;
 size_t count;
 size_t capacity;
} file_list_t;

error_code_t fileScannerInitList (file_list_t *list);
void fileScannerFreeList (file_list_t *list);
error_code_t fileScannerAddItem (file_list_t *list, const file_info_t *info);
error_code_t fileScannerScan (const char *path, file_list_t *list, bool recursive, bool followSymlinks);
error_code_t fileScannerGetInfo (const char *path, file_info_t *info);

#endif