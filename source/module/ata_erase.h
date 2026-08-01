#ifndef ATA_ERASE_H
#define ATA_ERASE_H
#include "common.h"
#include "device_io.h"
typedef enum { ATA_ERASE_NORMAL = 0x01, ATA_ERASE_ENHANCED = 0x02 } ata_erase_type_t;
typedef struct {
  bool supported;
  bool enhanced_supported;
  uint16_t max_sectors_per_command;
  uint32_t max_lba28;
  uint64_t max_lba48;
  char model[41];
  char firmware[9];
  char serial[21];
} ata_security_info_t;
error_code_t ata_get_security_info(device_t *device, ata_security_info_t *info);
error_code_t ata_secure_erase(device_t *device, ata_erase_type_t erase_type, progress_callback_t progress);
bool ata_is_frozen(device_t *device);
error_code_t ata_thaw_device(device_t *device);
#endif
