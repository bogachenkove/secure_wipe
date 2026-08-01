#include "file_io.h"
#include "platform.h"
#ifdef _WIN32
static DWORD win_attributes_from_flags(bool read_only, bool hidden, bool system) {
  DWORD attributes = FILE_ATTRIBUTE_NORMAL;
  if (read_only)
    attributes |= FILE_ATTRIBUTE_READONLY;
  if (hidden)
    attributes |= FILE_ATTRIBUTE_HIDDEN;
  if (system)
    attributes |= FILE_ATTRIBUTE_SYSTEM;
  return attributes;
}
error_code_t file_open(file_t *file, const char *path, bool read_only) {
  if (!file || !path)
    return ERR_INVALID_ARG;
  memset(file, 0, sizeof(file_t));
  snprintf(file->path, sizeof(file->path), "%s", path);
  file->read_only = read_only;
  DWORD desired_access = read_only ? GENERIC_READ : (GENERIC_READ | GENERIC_WRITE);
  DWORD share_mode = FILE_SHARE_READ | (read_only ? FILE_SHARE_WRITE : 0);
  DWORD creation_disposition = OPEN_EXISTING;
  DWORD flags_and_attributes = FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH;
  file->handle = CreateFileA(path, desired_access, share_mode, NULL, creation_disposition, flags_and_attributes, NULL);
  if (file->handle == INVALID_HANDLE_VALUE) {
    DWORD last_error = GetLastError();
    if (last_error == ERROR_ACCESS_DENIED || last_error == ERROR_SHARING_VIOLATION)
      return ERR_PERMISSION;
    return ERR_OPEN_DEVICE;
  }
  file->is_open = true;
  LARGE_INTEGER file_size;
  if (GetFileSizeEx(file->handle, &file_size))
    file->size = (uint64_t)file_size.QuadPart;
  else
    file->size = 0;
  return ERR_OK;
}
error_code_t file_close(file_t *file) {
  if (!file || !file->is_open)
    return ERR_INVALID_ARG;
  if (CloseHandle(file->handle)) {
    file->is_open = false;
    file->handle = INVALID_HANDLE_VALUE;
    return ERR_OK;
  }
  return ERR_UNKNOWN;
}
error_code_t file_read(file_t *file, uint64_t offset, uint8_t *buffer, size_t size, size_t *bytes_read) {
  if (!file || !file->is_open || !buffer)
    return ERR_INVALID_ARG;
  LARGE_INTEGER li_offset;
  li_offset.QuadPart = offset;
  if (!SetFilePointerEx(file->handle, li_offset, NULL, FILE_BEGIN))
    return ERR_SEEK_DEVICE;
  DWORD read_count = 0;
  if (!ReadFile(file->handle, buffer, (DWORD)size, &read_count, NULL))
    return ERR_READ_DEVICE;
  if (bytes_read)
    *bytes_read = read_count;
  return (read_count == size) ? ERR_OK : ERR_READ_DEVICE;
}
error_code_t file_write(file_t *file, uint64_t offset, const uint8_t *buffer, size_t size, size_t *bytes_written) {
  if (!file || !file->is_open || !buffer || file->read_only)
    return ERR_INVALID_ARG;
  LARGE_INTEGER li_offset;
  li_offset.QuadPart = offset;
  if (!SetFilePointerEx(file->handle, li_offset, NULL, FILE_BEGIN))
    return ERR_SEEK_DEVICE;
  DWORD written = 0;
  if (!WriteFile(file->handle, buffer, (DWORD)size, &written, NULL))
    return ERR_WRITE_DEVICE;
  if (bytes_written)
    *bytes_written = written;
  if (offset + written > file->size)
    file->size = offset + written;
  return (written == size) ? ERR_OK : ERR_WRITE_DEVICE;
}
error_code_t file_flush(file_t *file) {
  if (!file || !file->is_open)
    return ERR_INVALID_ARG;
  return FlushFileBuffers(file->handle) ? ERR_OK : ERR_UNKNOWN;
}
error_code_t file_get_size(file_t *file, uint64_t *size) {
  if (!file || !file->is_open || !size)
    return ERR_INVALID_ARG;
  LARGE_INTEGER file_size;
  if (GetFileSizeEx(file->handle, &file_size)) {
    *size = (uint64_t)file_size.QuadPart;
    return ERR_OK;
  }
  return ERR_GET_SIZE;
}
error_code_t file_truncate(file_t *file, uint64_t new_size) {
  if (!file || !file->is_open || file->read_only)
    return ERR_INVALID_ARG;
  LARGE_INTEGER li_distance;
  li_distance.QuadPart = new_size;
  if (!SetFilePointerEx(file->handle, li_distance, NULL, FILE_BEGIN))
    return ERR_SEEK_DEVICE;
  if (!SetEndOfFile(file->handle))
    return ERR_WRITE_DEVICE;
  file->size = new_size;
  return ERR_OK;
}
error_code_t file_rename(file_t *file, const char *new_path) {
  if (!file || !file->is_open || !new_path)
    return ERR_INVALID_ARG;
  if (!MoveFileExA(file->path, new_path, MOVEFILE_REPLACE_EXISTING))
    return ERR_PERMISSION;
  snprintf(file->path, sizeof(file->path), "%s", new_path);
  return ERR_OK;
}
error_code_t file_delete(file_t *file) {
  if (!file || !file->is_open)
    return ERR_INVALID_ARG;
  file_close(file);
  if (DeleteFileA(file->path))
    return ERR_OK;
  return ERR_PERMISSION;
}
error_code_t file_set_times(file_t *file, time_t access_time, time_t modify_time) {
  if (!file || !file->is_open)
    return ERR_INVALID_ARG;
  FILETIME ft_access, ft_modify;
  LARGE_INTEGER li;
  li.QuadPart = (LONGLONG)access_time * 10000000 + 116444736000000000;
  ft_access.dwLowDateTime = li.LowPart;
  ft_access.dwHighDateTime = li.HighPart;
  li.QuadPart = (LONGLONG)modify_time * 10000000 + 116444736000000000;
  ft_modify.dwLowDateTime = li.LowPart;
  ft_modify.dwHighDateTime = li.HighPart;
  if (SetFileTime(file->handle, NULL, &ft_access, &ft_modify))
    return ERR_OK;
  return ERR_PERMISSION;
}
error_code_t file_set_attributes(file_t *file, bool read_only, bool hidden, bool system) {
  if (!file || !file->is_open)
    return ERR_INVALID_ARG;
  DWORD attrs = win_attributes_from_flags(read_only, hidden, system);
  if (SetFileAttributesA(file->path, attrs))
    return ERR_OK;
  return ERR_PERMISSION;
}
#else
error_code_t file_open(file_t *file, const char *path, bool read_only) {
  if (!file || !path)
    return ERR_INVALID_ARG;
  memset(file, 0, sizeof(file_t));
  snprintf(file->path, sizeof(file->path), "%s", path);
  file->read_only = read_only;
  int flags = read_only ? O_RDONLY : (O_RDWR | O_SYNC);
  file->handle = open(path, flags);
  if (file->handle < 0) {
    if (errno == EACCES || errno == EPERM)
      return ERR_PERMISSION;
    return ERR_OPEN_DEVICE;
  }
  file->is_open = true;
  struct stat st;
  if (fstat(file->handle, &st) == 0)
    file->size = (uint64_t)st.st_size;
  else
    file->size = 0;
  return ERR_OK;
}
error_code_t file_close(file_t *file) {
  if (!file || !file->is_open)
    return ERR_INVALID_ARG;
  if (close(file->handle) == 0) {
    file->is_open = false;
    file->handle = -1;
    return ERR_OK;
  }
  return ERR_UNKNOWN;
}
error_code_t file_read(file_t *file, uint64_t offset, uint8_t *buffer, size_t size, size_t *bytes_read) {
  if (!file || !file->is_open || !buffer)
    return ERR_INVALID_ARG;
  if (lseek(file->handle, (off_t)offset, SEEK_SET) != (off_t)offset)
    return ERR_SEEK_DEVICE;
  ssize_t read_count = read(file->handle, buffer, size);
  if (read_count < 0)
    return ERR_READ_DEVICE;
  if (bytes_read)
    *bytes_read = (size_t)read_count;
  return ((size_t)read_count == size) ? ERR_OK : ERR_READ_DEVICE;
}
error_code_t file_write(file_t *file, uint64_t offset, const uint8_t *buffer, size_t size, size_t *bytes_written) {
  if (!file || !file->is_open || !buffer || file->read_only)
    return ERR_INVALID_ARG;
  if (lseek(file->handle, (off_t)offset, SEEK_SET) != (off_t)offset)
    return ERR_SEEK_DEVICE;
  ssize_t written = write(file->handle, buffer, size);
  if (written < 0)
    return ERR_WRITE_DEVICE;
  if (bytes_written)
    *bytes_written = (size_t)written;
  if (offset + written > file->size)
    file->size = offset + written;
  return ((size_t)written == size) ? ERR_OK : ERR_WRITE_DEVICE;
}
error_code_t file_flush(file_t *file) {
  if (!file || !file->is_open)
    return ERR_INVALID_ARG;
  return (fsync(file->handle) == 0) ? ERR_OK : ERR_UNKNOWN;
}
error_code_t file_get_size(file_t *file, uint64_t *size) {
  if (!file || !file->is_open || !size)
    return ERR_INVALID_ARG;
  struct stat st;
  if (fstat(file->handle, &st) == 0) {
    *size = (uint64_t)st.st_size;
    return ERR_OK;
  }
  return ERR_GET_SIZE;
}
error_code_t file_truncate(file_t *file, uint64_t new_size) {
  if (!file || !file->is_open || file->read_only)
    return ERR_INVALID_ARG;
  if (ftruncate(file->handle, (off_t)new_size) == 0) {
    file->size = new_size;
    return ERR_OK;
  }
  return ERR_WRITE_DEVICE;
}
error_code_t file_rename(file_t *file, const char *new_path) {
  if (!file || !file->is_open || !new_path)
    return ERR_INVALID_ARG;
  if (rename(file->path, new_path) == 0) {
    snprintf(file->path, sizeof(file->path), "%s", new_path);
    return ERR_OK;
  }
  return ERR_PERMISSION;
}
error_code_t file_delete(file_t *file) {
  if (!file || !file->is_open)
    return ERR_INVALID_ARG;
  file_close(file);
  if (remove(file->path) == 0)
    return ERR_OK;
  return ERR_PERMISSION;
}
error_code_t file_set_times(file_t *file, time_t access_time, time_t modify_time) {
  if (!file || !file->is_open)
    return ERR_INVALID_ARG;
  struct timespec times[2];
  times[0].tv_sec = access_time;
  times[0].tv_nsec = 0;
  times[1].tv_sec = modify_time;
  times[1].tv_nsec = 0;
  if (futimens(file->handle, times) == 0)
    return ERR_OK;
  return ERR_PERMISSION;
}
error_code_t file_set_attributes(file_t *file, bool read_only, bool hidden, bool system) {
  (void)hidden;
  (void)system;
  if (!file || !file->is_open)
    return ERR_INVALID_ARG;
  mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
  if (read_only)
    mode &= ~S_IWUSR;
  if (fchmod(file->handle, mode) == 0)
    return ERR_OK;
  return ERR_PERMISSION;
}
#endif
