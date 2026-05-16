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

typedef struct
{
 file_handle_t handle;
 char path[MAX_PATH_LEN];
 bool isOpen;
 bool readOnly;
 uint64_t size;
} file_t;

error_code_t fileOpen (file_t *file, const char *path, bool readOnly);
error_code_t fileClose (file_t *file);
error_code_t fileRead (file_t *file, uint64_t offset, uint8_t *buffer, size_t size, size_t *bytesRead);
error_code_t fileWrite (file_t *file, uint64_t offset, const uint8_t *buffer, size_t size, size_t *bytesWritten);
error_code_t fileFlush (file_t *file);
error_code_t fileGetSize (file_t *file, uint64_t *size);
error_code_t fileTruncate (file_t *file, uint64_t newSize);
error_code_t fileRename (file_t *file, const char *newPath);
error_code_t fileDelete (file_t *file);
error_code_t fileSetTimes (file_t *file, time_t accessTime, time_t modifyTime);
error_code_t fileSetAttributes (file_t *file, bool readOnly, bool hidden, bool system);

#endif