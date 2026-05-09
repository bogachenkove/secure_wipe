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

void platformInit (void);
void platformCleanup (void);
bool checkAdminPrivileges (void);
void *alignedAlloc (size_t size);
void alignedFree (void *pointer);
bool isDeviceMounted (const char *devicePath);

#endif