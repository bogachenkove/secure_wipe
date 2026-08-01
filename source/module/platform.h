#ifndef PLATFORM_H
#define PLATFORM_H
#include "common.h"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <string.h>
#define strcasecmp _stricmp
#include <windows.h>
#include <winioctl.h>
#include <bcrypt.h>
#include <setupapi.h>
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "setupapi.lib")
typedef HANDLE device_handle_t;
#define INVALID_DEVICE_HANDLE INVALID_HANDLE_VALUE
#else
#include <strings.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <errno.h>
#include <linux/fs.h>
typedef int device_handle_t;
#define INVALID_DEVICE_HANDLE (-1)
#endif
void platform_init(void);
void platform_cleanup(void);
bool check_admin_privileges(void);
void *aligned_alloc(size_t size);
void aligned_free(void *pointer);
bool is_device_mounted(const char *device_path);
#endif
