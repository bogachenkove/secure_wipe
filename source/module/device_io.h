#ifndef DEVICE_IO_H
#define DEVICE_IO_H
#include "platform.h"
typedef struct {
  device_handle_t handle;
  char path[MAX_PATH_LEN];
  uint64_t size_bytes;
  uint64_t sector_count;
  uint32_t sector_size;
  bool is_open;
  bool read_only;
} device_t;
typedef struct {
  error_code_t (*open)(device_t *device, const char *path, bool read_only);
  error_code_t (*close)(device_t *device);
  error_code_t (*read_sectors)(device_t *device, uint64_t start_sector, uint32_t count, uint8_t *buffer);
  error_code_t (*write_sectors)(device_t *device, uint64_t start_sector, uint32_t count, const uint8_t *buffer);
  error_code_t (*get_size)(device_t *device);
  error_code_t (*flush)(device_t *device);
} device_io_ops_t;
const device_io_ops_t *device_io_get_ops(void);
error_code_t device_open(device_t *device, const char *path, bool read_only);
error_code_t device_close(device_t *device);
error_code_t device_read_sectors(device_t *device, uint64_t start_sector, uint32_t count, uint8_t *buffer);
error_code_t device_write_sectors(device_t *device, uint64_t start_sector, uint32_t count, const uint8_t *buffer);
#endif
