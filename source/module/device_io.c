#include "device_io.h"

#ifdef _WIN32
static error_code_t winDeviceOpen(device_t* dev, const char* path, bool readOnly)
{
    if (!dev || !path) return ERR_INVALID_ARG;
    memset(dev, 0, sizeof(device_t));
    snprintf(dev->path, sizeof(dev->path), "%s", path);
    dev->readOnly = readOnly;
    dev->sectorSize = SECTOR_SIZE;

    DWORD access = GENERIC_READ;
    if (!readOnly) access |= GENERIC_WRITE;

    dev->handle = CreateFileA(path, access, FILE_SHARE_READ | FILE_SHARE_WRITE,
        NULL, OPEN_EXISTING,
        FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH, NULL);
    if (dev->handle == INVALID_HANDLE_VALUE) {
        DWORD err = GetLastError();
        if (err == ERROR_ACCESS_DENIED) return ERR_PERMISSION;
        return ERR_OPEN_DEVICE;
    }
    dev->isOpen = true;
    LOG_INFO("Opened device: %s (mode: %s)", path, readOnly ? "read-only" : "read-write");

    DISK_GEOMETRY_EX geom;
    DWORD bytesReturned;
    if (DeviceIoControl(dev->handle, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX,
        NULL, 0, &geom, sizeof(geom), &bytesReturned, NULL)) {
        dev->sizeBytes = geom.DiskSize.QuadPart;
        dev->sectorCount = dev->sizeBytes / dev->sectorSize;
    }
    else {
        GET_LENGTH_INFORMATION lenInfo;
        if (DeviceIoControl(dev->handle, IOCTL_DISK_GET_LENGTH_INFO,
            NULL, 0, &lenInfo, sizeof(lenInfo), &bytesReturned, NULL)) {
            dev->sizeBytes = lenInfo.Length.QuadPart;
            dev->sectorCount = dev->sizeBytes / dev->sectorSize;
        }
        else {
            CloseHandle(dev->handle);
            dev->isOpen = false;
            return ERR_GET_SIZE;
        }
    }
    char sizeStr[32];
    formatBytes(dev->sizeBytes, sizeStr, sizeof(sizeStr));
    LOG_INFO("Device size: %s (%llu sectors)", sizeStr, (unsigned long long)dev->sectorCount);
    return ERR_OK;
}

static error_code_t winDeviceClose(device_t* dev)
{
    if (!dev) return ERR_INVALID_ARG;
    if (dev->isOpen && dev->handle != INVALID_HANDLE_VALUE) {
        FlushFileBuffers(dev->handle);
        CloseHandle(dev->handle);
        dev->handle = INVALID_HANDLE_VALUE;
        dev->isOpen = false;
        LOG_INFO("Device closed: %s", dev->path);
    }
    return ERR_OK;
}

static error_code_t winDeviceReadSectors(device_t* dev, uint64_t startSector,
    uint32_t count, uint8_t* buffer)
{
    if (!dev || !dev->isOpen || !buffer) return ERR_INVALID_ARG;
    LARGE_INTEGER offset;
    offset.QuadPart = (LONGLONG)startSector * dev->sectorSize;
    if (!SetFilePointerEx(dev->handle, offset, NULL, FILE_BEGIN)) return ERR_SEEK_DEVICE;
    DWORD bytesToRead = count * dev->sectorSize;
    DWORD bytesRead = 0;
    if (!ReadFile(dev->handle, buffer, bytesToRead, &bytesRead, NULL)) return ERR_READ_DEVICE;
    if (bytesRead != bytesToRead) return ERR_READ_DEVICE;
    return ERR_OK;
}

static error_code_t winDeviceWriteSectors(device_t* dev, uint64_t startSector,
    uint32_t count, const uint8_t* buffer)
{
    if (!dev || !dev->isOpen || !buffer || dev->readOnly) return ERR_INVALID_ARG;
    LARGE_INTEGER offset;
    offset.QuadPart = (LONGLONG)startSector * dev->sectorSize;
    if (!SetFilePointerEx(dev->handle, offset, NULL, FILE_BEGIN)) return ERR_SEEK_DEVICE;
    DWORD bytesToWrite = count * dev->sectorSize;
    DWORD bytesWritten = 0;
    if (!WriteFile(dev->handle, buffer, bytesToWrite, &bytesWritten, NULL)) return ERR_WRITE_DEVICE;
    if (bytesWritten != bytesToWrite) return ERR_WRITE_DEVICE;
    return ERR_OK;
}

static error_code_t winDeviceFlush(device_t* dev)
{
    if (!dev || !dev->isOpen) return ERR_INVALID_ARG;
    FlushFileBuffers(dev->handle);
    return ERR_OK;
}

static error_code_t winDeviceGetSize(device_t* dev)
{
    return (dev && dev->sizeBytes > 0) ? ERR_OK : ERR_GET_SIZE;
}

static const device_io_ops_t winIoOps = {
    .open = winDeviceOpen,
    .close = winDeviceClose,
    .readSectors = winDeviceReadSectors,
    .writeSectors = winDeviceWriteSectors,
    .getSize = winDeviceGetSize,
    .flush = winDeviceFlush
};

const device_io_ops_t* deviceIoGetOps(void) { return &winIoOps; }

#else

static error_code_t posixDeviceOpen(device_t* dev, const char* path, bool readOnly)
{
    if (!dev || !path) return ERR_INVALID_ARG;
    memset(dev, 0, sizeof(device_t));
    snprintf(dev->path, sizeof(dev->path), "%s", path);
    dev->readOnly = readOnly;
    dev->sectorSize = SECTOR_SIZE;
    int flags = readOnly ? O_RDONLY : O_RDWR;
    flags |= O_SYNC | O_DIRECT;
    dev->handle = open(path, flags);
    if (dev->handle < 0) {
        if (errno == EACCES || errno == EPERM) return ERR_PERMISSION;
        return ERR_OPEN_DEVICE;
    }
    dev->isOpen = true;
    LOG_INFO("Opened device: %s (mode: %s)", path, readOnly ? "read-only" : "read-write");

    uint64_t size = 0;
    if (ioctl(dev->handle, BLKGETSIZE64, &size) == 0) {
        dev->sizeBytes = size;
        dev->sectorCount = size / dev->sectorSize;
    }
    else {
        off_t end = lseek(dev->handle, 0, SEEK_END);
        if (end > 0) {
            dev->sizeBytes = (uint64_t)end;
            dev->sectorCount = dev->sizeBytes / dev->sectorSize;
            lseek(dev->handle, 0, SEEK_SET);
        }
        else {
            close(dev->handle);
            dev->isOpen = false;
            return ERR_GET_SIZE;
        }
    }
    char sizeStr[32];
    formatBytes(dev->sizeBytes, sizeStr, sizeof(sizeStr));
    LOG_INFO("Device size: %s (%llu sectors)", sizeStr, (unsigned long long)dev->sectorCount);
    return ERR_OK;
}

static error_code_t posixDeviceClose(device_t* dev)
{
    if (!dev) return ERR_INVALID_ARG;
    if (dev->isOpen && dev->handle >= 0) {
        fsync(dev->handle);
        close(dev->handle);
        dev->handle = -1;
        dev->isOpen = false;
        LOG_INFO("Device closed: %s", dev->path);
    }
    return ERR_OK;
}

static error_code_t posixDeviceReadSectors(device_t* dev, uint64_t startSector,
    uint32_t count, uint8_t* buffer)
{
    if (!dev || !dev->isOpen || !buffer) return ERR_INVALID_ARG;
    off_t offset = (off_t)startSector * dev->sectorSize;
    if (lseek(dev->handle, offset, SEEK_SET) != offset) return ERR_SEEK_DEVICE;
    size_t bytesToRead = (size_t)count * dev->sectorSize;
    ssize_t bytesRead = read(dev->handle, buffer, bytesToRead);
    if (bytesRead < 0 || (size_t)bytesRead != bytesToRead) return ERR_READ_DEVICE;
    return ERR_OK;
}

static error_code_t posixDeviceWriteSectors(device_t* dev, uint64_t startSector,
    uint32_t count, const uint8_t* buffer)
{
    if (!dev || !dev->isOpen || !buffer || dev->readOnly) return ERR_INVALID_ARG;
    off_t offset = (off_t)startSector * dev->sectorSize;
    if (lseek(dev->handle, offset, SEEK_SET) != offset) return ERR_SEEK_DEVICE;
    size_t bytesToWrite = (size_t)count * dev->sectorSize;
    ssize_t bytesWritten = write(dev->handle, buffer, bytesToWrite);
    if (bytesWritten < 0 || (size_t)bytesWritten != bytesToWrite) return ERR_WRITE_DEVICE;
    return ERR_OK;
}

static error_code_t posixDeviceFlush(device_t* dev)
{
    if (!dev || !dev->isOpen) return ERR_INVALID_ARG;
    fsync(dev->handle);
    return ERR_OK;
}

static error_code_t posixDeviceGetSize(device_t* dev)
{
    return (dev && dev->sizeBytes > 0) ? ERR_OK : ERR_GET_SIZE;
}

static const device_io_ops_t posixIoOps = {
    .open = posixDeviceOpen,
    .close = posixDeviceClose,
    .readSectors = posixDeviceReadSectors,
    .writeSectors = posixDeviceWriteSectors,
    .getSize = posixDeviceGetSize,
    .flush = posixDeviceFlush
};

const device_io_ops_t* deviceIoGetOps(void) { return &posixIoOps; }

#endif

error_code_t deviceOpen(device_t* dev, const char* path, bool readOnly)
{
    return deviceIoGetOps()->open(dev, path, readOnly);
}

error_code_t deviceClose(device_t* dev)
{
    return deviceIoGetOps()->close(dev);
}

error_code_t deviceReadSectors(device_t* dev, uint64_t startSector,
    uint32_t count, uint8_t* buffer)
{
    return deviceIoGetOps()->readSectors(dev, startSector, count, buffer);
}

error_code_t deviceWriteSectors(device_t* dev, uint64_t startSector,
    uint32_t count, const uint8_t* buffer)
{
    return deviceIoGetOps()->writeSectors(dev, startSector, count, buffer);
}