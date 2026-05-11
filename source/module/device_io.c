#include "device_io.h"

#ifdef _WIN32
static error_code_t winDeviceOpen (device_t *device, const char *path, bool readOnly)
{
 if (!device || !path)
  return ERR_INVALID_ARG;

 memset (device, 0, sizeof (device_t));
 snprintf (device->path, sizeof (device->path), "%s", path);
 device->readOnly = readOnly;
 device->sectorSize = SECTOR_SIZE;

 DWORD accessFlags = GENERIC_READ;
 if (!readOnly)
  accessFlags |= GENERIC_WRITE;

 device->handle =
	 CreateFileA (path, accessFlags, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH, NULL);

 if (device->handle == INVALID_HANDLE_VALUE)
 {
  DWORD errorCode = GetLastError ();
  if (errorCode == ERROR_ACCESS_DENIED)
   return ERR_PERMISSION;
  return ERR_OPEN_DEVICE;
 }

 device->isOpen = true;
 LOG_INFO ("Opened device: %s (mode: %s)", path, readOnly ? "read-only" : "read-write");

 DISK_GEOMETRY_EX geometry;
 DWORD bytesReturned;
 if (DeviceIoControl (device->handle, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX, NULL, 0, &geometry, sizeof (geometry), &bytesReturned, NULL))
 {
  device->sizeBytes = geometry.DiskSize.QuadPart;
  device->sectorCount = device->sizeBytes / device->sectorSize;
 }
 else
 {
  GET_LENGTH_INFORMATION lengthInfo;
  if (DeviceIoControl (device->handle, IOCTL_DISK_GET_LENGTH_INFO, NULL, 0, &lengthInfo, sizeof (lengthInfo), &bytesReturned, NULL))
  {
   device->sizeBytes = lengthInfo.Length.QuadPart;
   device->sectorCount = device->sizeBytes / device->sectorSize;
  }
  else
  {
   CloseHandle (device->handle);
   device->isOpen = false;
   return ERR_GET_SIZE;
  }
 }

 char sizeString[32];
 formatBytes (device->sizeBytes, sizeString, sizeof (sizeString));
 LOG_INFO ("Device size: %s (%llu sectors)", sizeString, (unsigned long long) device->sectorCount);
 return ERR_OK;
}

static error_code_t winDeviceClose (device_t *device)
{
 if (!device)
  return ERR_INVALID_ARG;

 if (device->isOpen && device->handle != INVALID_HANDLE_VALUE)
 {
  FlushFileBuffers (device->handle);
  CloseHandle (device->handle);
  device->handle = INVALID_HANDLE_VALUE;
  device->isOpen = false;
  LOG_INFO ("Device closed: %s", device->path);
 }
 return ERR_OK;
}

static error_code_t winDeviceReadSectors (device_t *device, uint64_t startSector, uint32_t count, uint8_t *buffer)
{
 if (!device || !device->isOpen || !buffer)
  return ERR_INVALID_ARG;

 LARGE_INTEGER offset;
 offset.QuadPart = (LONGLONG) startSector * device->sectorSize;

 if (!SetFilePointerEx (device->handle, offset, NULL, FILE_BEGIN))
  return ERR_SEEK_DEVICE;

 DWORD bytesToRead = count * device->sectorSize;
 DWORD bytesRead = 0;

 if (!ReadFile (device->handle, buffer, bytesToRead, &bytesRead, NULL))
  return ERR_READ_DEVICE;

 if (bytesRead != bytesToRead)
  return ERR_READ_DEVICE;

 return ERR_OK;
}

static error_code_t winDeviceWriteSectors (device_t *device, uint64_t startSector, uint32_t count, const uint8_t *buffer)
{
 if (!device || !device->isOpen || !buffer || device->readOnly)
  return ERR_INVALID_ARG;

 LARGE_INTEGER offset;
 offset.QuadPart = (LONGLONG) startSector * device->sectorSize;

 if (!SetFilePointerEx (device->handle, offset, NULL, FILE_BEGIN))
  return ERR_SEEK_DEVICE;

 DWORD bytesToWrite = count * device->sectorSize;
 DWORD bytesWritten = 0;

 if (!WriteFile (device->handle, buffer, bytesToWrite, &bytesWritten, NULL))
 {
  DWORD errorCode = GetLastError ();
  if (errorCode == ERROR_WRITE_PROTECT)
  {
   LOG_ERROR ("Device is write-protected (ERROR_WRITE_PROTECT)");
   return ERR_PERMISSION;
  }
  return ERR_WRITE_DEVICE;
 }

 if (bytesWritten != bytesToWrite)
  return ERR_WRITE_DEVICE;

 return ERR_OK;
}

static error_code_t winDeviceFlush (device_t *device)
{
 if (!device || !device->isOpen)
  return ERR_INVALID_ARG;
 FlushFileBuffers (device->handle);
 return ERR_OK;
}

static error_code_t winDeviceGetSize (device_t *device)
{
 return (device && device->sizeBytes > 0) ? ERR_OK : ERR_GET_SIZE;
}

static const device_io_ops_t winIoOps = {.open = winDeviceOpen,
										 .close = winDeviceClose,
										 .readSectors = winDeviceReadSectors,
										 .writeSectors = winDeviceWriteSectors,
										 .getSize = winDeviceGetSize,
										 .flush = winDeviceFlush};

const device_io_ops_t *deviceIoGetOps (void)
{
 return &winIoOps;
}

#else

static error_code_t posixDeviceOpen (device_t *device, const char *path, bool readOnly)
{
 if (!device || !path)
  return ERR_INVALID_ARG;

 memset (device, 0, sizeof (device_t));
 snprintf (device->path, sizeof (device->path), "%s", path);
 device->readOnly = readOnly;
 device->sectorSize = SECTOR_SIZE;

 int flags = readOnly ? O_RDONLY : O_RDWR;
 flags |= O_SYNC | O_DIRECT;

 device->handle = open (path, flags);
 if (device->handle < 0)
 {
  if (errno == EACCES || errno == EPERM)
   return ERR_PERMISSION;
  return ERR_OPEN_DEVICE;
 }

 device->isOpen = true;
 LOG_INFO ("Opened device: %s (mode: %s)", path, readOnly ? "read-only" : "read-write");

 uint64_t size = 0;
 if (ioctl (device->handle, BLKGETSIZE64, &size) == 0)
 {
  device->sizeBytes = size;
  device->sectorCount = size / device->sectorSize;
 }
 else
 {
  off_t endPosition = lseek (device->handle, 0, SEEK_END);
  if (endPosition > 0)
  {
   device->sizeBytes = (uint64_t) endPosition;
   device->sectorCount = device->sizeBytes / device->sectorSize;
   lseek (device->handle, 0, SEEK_SET);
  }
  else
  {
   close (device->handle);
   device->isOpen = false;
   return ERR_GET_SIZE;
  }
 }

 char sizeString[32];
 formatBytes (device->sizeBytes, sizeString, sizeof (sizeString));
 LOG_INFO ("Device size: %s (%llu sectors)", sizeString, (unsigned long long) device->sectorCount);
 return ERR_OK;
}

static error_code_t posixDeviceClose (device_t *device)
{
 if (!device)
  return ERR_INVALID_ARG;

 if (device->isOpen && device->handle >= 0)
 {
  fsync (device->handle);
  close (device->handle);
  device->handle = -1;
  device->isOpen = false;
  LOG_INFO ("Device closed: %s", device->path);
 }
 return ERR_OK;
}

static error_code_t posixDeviceReadSectors (device_t *device, uint64_t startSector, uint32_t count, uint8_t *buffer)
{
 if (!device || !device->isOpen || !buffer)
  return ERR_INVALID_ARG;

 off_t offset = (off_t) startSector * device->sectorSize;
 if (lseek (device->handle, offset, SEEK_SET) != offset)
  return ERR_SEEK_DEVICE;

 size_t bytesToRead = (size_t) count * device->sectorSize;
 ssize_t bytesRead = read (device->handle, buffer, bytesToRead);

 if (bytesRead < 0 || (size_t) bytesRead != bytesToRead)
  return ERR_READ_DEVICE;

 return ERR_OK;
}

static error_code_t posixDeviceWriteSectors (device_t *device, uint64_t startSector, uint32_t count, const uint8_t *buffer)
{
 if (!device || !device->isOpen || !buffer || device->readOnly)
  return ERR_INVALID_ARG;

 off_t offset = (off_t) startSector * device->sectorSize;
 if (lseek (device->handle, offset, SEEK_SET) != offset)
  return ERR_SEEK_DEVICE;

 size_t bytesToWrite = (size_t) count * device->sectorSize;
 ssize_t bytesWritten = write (device->handle, buffer, bytesToWrite);

 if (bytesWritten < 0 || (size_t) bytesWritten != bytesToWrite)
  return ERR_WRITE_DEVICE;

 return ERR_OK;
}

static error_code_t posixDeviceFlush (device_t *device)
{
 if (!device || !device->isOpen)
  return ERR_INVALID_ARG;
 fsync (device->handle);
 return ERR_OK;
}

static error_code_t posixDeviceGetSize (device_t *device)
{
 return (device && device->sizeBytes > 0) ? ERR_OK : ERR_GET_SIZE;
}

static const device_io_ops_t posixIoOps = {.open = posixDeviceOpen,
										   .close = posixDeviceClose,
										   .readSectors = posixDeviceReadSectors,
										   .writeSectors = posixDeviceWriteSectors,
										   .getSize = posixDeviceGetSize,
										   .flush = posixDeviceFlush};

const device_io_ops_t *deviceIoGetOps (void)
{
 return &posixIoOps;
}

#endif

error_code_t deviceOpen (device_t *device, const char *path, bool readOnly)
{
 return deviceIoGetOps ()->open (device, path, readOnly);
}

error_code_t deviceClose (device_t *device)
{
 return deviceIoGetOps ()->close (device);
}

error_code_t deviceReadSectors (device_t *device, uint64_t startSector, uint32_t count, uint8_t *buffer)
{
 return deviceIoGetOps ()->readSectors (device, startSector, count, buffer);
}

error_code_t deviceWriteSectors (device_t *device, uint64_t startSector, uint32_t count, const uint8_t *buffer)
{
 return deviceIoGetOps ()->writeSectors (device, startSector, count, buffer);
}