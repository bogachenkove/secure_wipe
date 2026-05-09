#ifndef DEVICE_IO_H
#define DEVICE_IO_H

#include "platform.h"

typedef struct {
    device_handle_t handle;
    char path[MAX_PATH_LEN];
    uint64_t sizeBytes;
    uint64_t sectorCount;
    uint32_t sectorSize;
    bool isOpen;
    bool readOnly;
} device_t;

typedef struct {
    error_code_t(*open)(device_t* dev, const char* path, bool readOnly);
    error_code_t(*close)(device_t* dev);
    error_code_t(*readSectors)(device_t* dev, uint64_t startSector, uint32_t count, uint8_t* buffer);
    error_code_t(*writeSectors)(device_t* dev, uint64_t startSector, uint32_t count, const uint8_t* buffer);
    error_code_t(*getSize)(device_t* dev);
    error_code_t(*flush)(device_t* dev);
} device_io_ops_t;

const device_io_ops_t* deviceIoGetOps(void);
error_code_t deviceOpen(device_t* dev, const char* path, bool readOnly);
error_code_t deviceClose(device_t* dev);
error_code_t deviceReadSectors(device_t* dev, uint64_t startSector, uint32_t count, uint8_t* buffer);
error_code_t deviceWriteSectors(device_t* dev, uint64_t startSector, uint32_t count, const uint8_t* buffer);

#endif