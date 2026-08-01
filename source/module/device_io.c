#include "device_io.h"
#ifdef _WIN32
static error_code_t win_device_open(device_t *device, const char *path, bool read_only) {
  if (!device || !path)
    return ERR_INVALID_ARG;
  memset(device, 0, sizeof(device_t));
  snprintf(device->path, sizeof(device->path), "%s", path);
  device->read_only = read_only;
  device->sector_size = SECTOR_SIZE;
  DWORD access_flags = GENERIC_READ;
  if (!read_only)
    access_flags |= GENERIC_WRITE;
  device->handle = CreateFileA(path, access_flags, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING,
                               FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH, NULL);
  if (device->handle == INVALID_HANDLE_VALUE) {
    DWORD last_error = GetLastError();
    if (last_error == ERROR_ACCESS_DENIED)
      return ERR_PERMISSION;
    return ERR_OPEN_DEVICE;
  }
  device->is_open = true;
  LOG_INFO("Opened device: %s (mode: %s)", path, read_only ? "read-only" : "read-write");
  DISK_GEOMETRY_EX geometry = {0};
  DWORD bytes_returned = 0;
  if (DeviceIoControl(device->handle, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX, NULL, 0, &geometry, sizeof(geometry), &bytes_returned, NULL)) {
    device->size_bytes = geometry.DiskSize.QuadPart;
    device->sector_count = device->size_bytes / device->sector_size;
  } else {
    GET_LENGTH_INFORMATION length_info = {0};
    bytes_returned = 0;
    if (DeviceIoControl(device->handle, IOCTL_DISK_GET_LENGTH_INFO, NULL, 0, &length_info, sizeof(length_info), &bytes_returned, NULL)) {
      device->size_bytes = length_info.Length.QuadPart;
      device->sector_count = device->size_bytes / device->sector_size;
    } else {
      CloseHandle(device->handle);
      device->is_open = false;
      return ERR_GET_SIZE;
    }
  }
  char size_string[32];
  format_bytes(device->size_bytes, size_string, sizeof(size_string));
  LOG_INFO("Device size: %s (%llu sectors)", size_string, (unsigned long long)device->sector_count);
  return ERR_OK;
}
static error_code_t win_device_close(device_t *device) {
  if (!device)
    return ERR_INVALID_ARG;
  if (device->is_open && device->handle != INVALID_HANDLE_VALUE) {
    FlushFileBuffers(device->handle);
    CloseHandle(device->handle);
    device->handle = INVALID_HANDLE_VALUE;
    device->is_open = false;
    LOG_INFO("Device closed: %s", device->path);
  }
  return ERR_OK;
}
static error_code_t win_device_read_sectors(device_t *device, uint64_t start_sector, uint32_t count, uint8_t *buffer) {
  if (!device || !device->is_open || !buffer)
    return ERR_INVALID_ARG;
  LARGE_INTEGER offset = {0};
  offset.QuadPart = (LONGLONG)start_sector * device->sector_size;
  if (!SetFilePointerEx(device->handle, offset, NULL, FILE_BEGIN))
    return ERR_SEEK_DEVICE;
  DWORD bytes_to_read = count * device->sector_size;
  DWORD bytes_read = 0;
  if (!ReadFile(device->handle, buffer, bytes_to_read, &bytes_read, NULL))
    return ERR_READ_DEVICE;
  if (bytes_read != bytes_to_read)
    return ERR_READ_DEVICE;
  return ERR_OK;
}
static error_code_t win_device_write_sectors(device_t *device, uint64_t start_sector, uint32_t count, const uint8_t *buffer) {
  if (!device || !device->is_open || !buffer || device->read_only)
    return ERR_INVALID_ARG;
  LARGE_INTEGER offset = {0};
  offset.QuadPart = (LONGLONG)start_sector * device->sector_size;
  if (!SetFilePointerEx(device->handle, offset, NULL, FILE_BEGIN))
    return ERR_SEEK_DEVICE;
  DWORD bytes_to_write = count * device->sector_size;
  DWORD bytes_written = 0;
  if (!WriteFile(device->handle, buffer, bytes_to_write, &bytes_written, NULL)) {
    DWORD last_error = GetLastError();
    if (last_error == ERROR_WRITE_PROTECT) {
      LOG_ERROR("Device is write-protected (ERROR_WRITE_PROTECT)");
      return ERR_PERMISSION;
    }
    return ERR_WRITE_DEVICE;
  }
  if (bytes_written != bytes_to_write)
    return ERR_WRITE_DEVICE;
  return ERR_OK;
}
static error_code_t win_device_flush(device_t *device) {
  if (!device || !device->is_open)
    return ERR_INVALID_ARG;
  FlushFileBuffers(device->handle);
  return ERR_OK;
}
static error_code_t win_device_get_size(device_t *device) { return (device && device->size_bytes > 0) ? ERR_OK : ERR_GET_SIZE; }
static const device_io_ops_t windows_io_ops = {.open = win_device_open,
                                               .close = win_device_close,
                                               .read_sectors = win_device_read_sectors,
                                               .write_sectors = win_device_write_sectors,
                                               .get_size = win_device_get_size,
                                               .flush = win_device_flush};
#else
#include <sys/stat.h>
#include <errno.h>
#include <linux/fs.h>
static error_code_t posix_device_open(device_t *device, const char *path, bool read_only) {
  if (!device || !path)
    return ERR_INVALID_ARG;
  memset(device, 0, sizeof(device_t));
  snprintf(device->path, sizeof(device->path), "%s", path);
  device->read_only = read_only;
  device->sector_size = SECTOR_SIZE;
  int flags = read_only ? O_RDONLY : O_RDWR;
  flags |= O_SYNC | O_DIRECT;
  device->handle = open(path, flags);
  if (device->handle < 0) {
    if (errno == EACCES || errno == EPERM)
      return ERR_PERMISSION;
    return ERR_OPEN_DEVICE;
  }
  device->is_open = true;
  LOG_INFO("Opened device: %s (mode: %s)", path, read_only ? "read-only" : "read-write");
  uint64_t size = 0;
  if (ioctl(device->handle, BLKGETSIZE64, &size) == 0) {
    device->size_bytes = size;
    device->sector_count = size / device->sector_size;
  } else {
    off_t end_position = lseek(device->handle, 0, SEEK_END);
    if (end_position > 0) {
      device->size_bytes = (uint64_t)end_position;
      device->sector_count = device->size_bytes / device->sector_size;
      lseek(device->handle, 0, SEEK_SET);
    } else {
      close(device->handle);
      device->is_open = false;
      return ERR_GET_SIZE;
    }
  }
  char size_string[32];
  format_bytes(device->size_bytes, size_string, sizeof(size_string));
  LOG_INFO("Device size: %s (%llu sectors)", size_string, (unsigned long long)device->sector_count);
  return ERR_OK;
}
static error_code_t posix_device_close(device_t *device) {
  if (!device)
    return ERR_INVALID_ARG;
  if (device->is_open && device->handle >= 0) {
    fsync(device->handle);
    close(device->handle);
    device->handle = -1;
    device->is_open = false;
    LOG_INFO("Device closed: %s", device->path);
  }
  return ERR_OK;
}
static error_code_t posix_device_read_sectors(device_t *device, uint64_t start_sector, uint32_t count, uint8_t *buffer) {
  if (!device || !device->is_open || !buffer)
    return ERR_INVALID_ARG;
  off_t offset = (off_t)start_sector * device->sector_size;
  if (lseek(device->handle, offset, SEEK_SET) != offset)
    return ERR_SEEK_DEVICE;
  size_t bytes_to_read = (size_t)count * device->sector_size;
  ssize_t bytes_read = read(device->handle, buffer, bytes_to_read);
  if (bytes_read < 0 || (size_t)bytes_read != bytes_to_read)
    return ERR_READ_DEVICE;
  return ERR_OK;
}
static error_code_t posix_device_write_sectors(device_t *device, uint64_t start_sector, uint32_t count, const uint8_t *buffer) {
  if (!device || !device->is_open || !buffer || device->read_only)
    return ERR_INVALID_ARG;
  off_t offset = (off_t)start_sector * device->sector_size;
  if (lseek(device->handle, offset, SEEK_SET) != offset)
    return ERR_SEEK_DEVICE;
  size_t bytes_to_write = (size_t)count * device->sector_size;
  ssize_t bytes_written = write(device->handle, buffer, bytes_to_write);
  if (bytes_written < 0 || (size_t)bytes_written != bytes_to_write)
    return ERR_WRITE_DEVICE;
  return ERR_OK;
}
static error_code_t posix_device_flush(device_t *device) {
  if (!device || !device->is_open)
    return ERR_INVALID_ARG;
  fsync(device->handle);
  return ERR_OK;
}
static error_code_t posix_device_get_size(device_t *device) { return (device && device->size_bytes > 0) ? ERR_OK : ERR_GET_SIZE; }
static const device_io_ops_t posix_io_ops = {.open = posix_device_open,
                                             .close = posix_device_close,
                                             .read_sectors = posix_device_read_sectors,
                                             .write_sectors = posix_device_write_sectors,
                                             .get_size = posix_device_get_size,
                                             .flush = posix_device_flush};
#endif
const device_io_ops_t *device_io_get_ops(void) {
#ifdef _WIN32
  return &windows_io_ops;
#else
  return &posix_io_ops;
#endif
}
error_code_t device_open(device_t *device, const char *path, bool read_only) { return device_io_get_ops()->open(device, path, read_only); }
error_code_t device_close(device_t *device) { return device_io_get_ops()->close(device); }
error_code_t device_read_sectors(device_t *device, uint64_t start_sector, uint32_t count, uint8_t *buffer) {
  return device_io_get_ops()->read_sectors(device, start_sector, count, buffer);
}
error_code_t device_write_sectors(device_t *device, uint64_t start_sector, uint32_t count, const uint8_t *buffer) {
  return device_io_get_ops()->write_sectors(device, start_sector, count, buffer);
}
