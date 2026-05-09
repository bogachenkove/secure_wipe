#ifndef DISK_SCANNER_H
#define DISK_SCANNER_H

#include "common.h"

#define MAX_DISKS 64

typedef enum
{
 DISK_TYPE_UNKNOWN = 0,
 DISK_TYPE_HDD,
 DISK_TYPE_SSD,
 DISK_TYPE_USB,
 DISK_TYPE_NVME,
 DISK_TYPE_SD_CARD,
 DISK_TYPE_OPTICAL,
 DISK_TYPE_VIRTUAL
} disk_type_t;

typedef struct
{
 char devicePath[MAX_PATH_LEN];
 char friendlyName[128];
 char vendor[64];
 char model[128];
 char serial[64];
 char busType[32];
 uint64_t sizeBytes;
 uint32_t sectorSize;
 disk_type_t type;
 bool isRemovable;
 bool isSystem;
 bool isBoot;
 int diskNumber;
} disk_info_t;

typedef struct
{
 disk_info_t disks[MAX_DISKS];
 int count;
} disk_scan_result_t;

error_code_t diskScannerScan (disk_scan_result_t *result);
error_code_t diskScannerGetInfo (const char *devicePath, disk_info_t *info);
void diskScannerPrintList (const disk_scan_result_t *result, bool showAll);
void diskScannerPrintDetail (const disk_info_t *info);
const char *diskTypeToString (disk_type_t type);
bool diskScannerIsSafeToWipe (const disk_info_t *info);
const disk_info_t *diskScannerGetByIndex (const disk_scan_result_t *result, int index);

#endif