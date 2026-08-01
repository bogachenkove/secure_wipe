#ifndef DISK_SCANNER_H
#define DISK_SCANNER_H
#include "common.h"
#define MAX_DISKS 64
typedef enum {
  DISK_TYPE_UNKNOWN = 0,
  DISK_TYPE_HDD,
  DISK_TYPE_SSD,
  DISK_TYPE_USB,
  DISK_TYPE_NVME,
  DISK_TYPE_SD_CARD,
  DISK_TYPE_OPTICAL,
  DISK_TYPE_VIRTUAL
} disk_type_t;
typedef struct {
  char device_path[MAX_PATH_LEN];
  char vendor[64];
  char model[128];
  char bus_type[32];
  uint64_t size_bytes;
  uint32_t sector_size;
  disk_type_t type;
  bool is_removable;
  bool is_system;
  bool is_boot;
  int disk_number;
} disk_info_t;
typedef struct {
  disk_info_t disks[MAX_DISKS];
  int count;
} disk_scan_result_t;
error_code_t disk_scanner_scan(disk_scan_result_t *result);
error_code_t disk_scanner_get_info(const char *device_path, disk_info_t *info);
void disk_scanner_print_list(const disk_scan_result_t *result);
void disk_scanner_print_detail(const disk_info_t *info);
const char *disk_type_to_string(disk_type_t type);
const disk_info_t *disk_scanner_get_by_index(const disk_scan_result_t *result, int index);
#endif
