#ifndef ATA_ERASE_H
#define ATA_ERASE_H

#include "common.h"
#include "device_io.h"

typedef enum
{
 ATA_ERASE_NORMAL = 0x01,
 ATA_ERASE_ENHANCED = 0x02
} ata_erase_type_t;

typedef struct
{
 bool supported;
 bool enhancedSupported;
 uint16_t maxSectorsPerCommand;
 uint32_t maxLba28;
 uint64_t maxLba48;
 char model[41];
 char firmware[9];
 char serial[21];
} ata_security_info_t;

error_code_t ataGetSecurityInfo (device_t *device, ata_security_info_t *info);
error_code_t ataSecureErase (device_t *device, ata_erase_type_t eraseType, progress_callback_t progress);
bool ataIsFrozen (device_t *device);
error_code_t ataThawDevice (device_t *device);

#endif