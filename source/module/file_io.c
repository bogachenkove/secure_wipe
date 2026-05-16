#include "file_io.h"
#include "platform.h"

#ifdef _WIN32

static DWORD winAttributesFromFlags (bool readOnly, bool hidden, bool system)
{
 DWORD attributes = FILE_ATTRIBUTE_NORMAL;
 if (readOnly)
  attributes |= FILE_ATTRIBUTE_READONLY;
 if (hidden)
  attributes |= FILE_ATTRIBUTE_HIDDEN;
 if (system)
  attributes |= FILE_ATTRIBUTE_SYSTEM;
 return attributes;
}

error_code_t fileOpen (file_t *file, const char *path, bool readOnly)
{
 if (!file || !path)
  return ERR_INVALID_ARG;
 memset (file, 0, sizeof (file_t));
 snprintf (file->path, sizeof (file->path), "%s", path);
 file->readOnly = readOnly;

 DWORD desiredAccess = readOnly ? GENERIC_READ : (GENERIC_READ | GENERIC_WRITE);
 DWORD shareMode = FILE_SHARE_READ | (readOnly ? FILE_SHARE_WRITE : 0);
 DWORD creationDisposition = OPEN_EXISTING;
 DWORD flagsAndAttributes = FILE_ATTRIBUTE_NORMAL | FILE_FLAG_WRITE_THROUGH;

 file->handle = CreateFileA (path, desiredAccess, shareMode, NULL, creationDisposition, flagsAndAttributes, NULL);
 if (file->handle == INVALID_HANDLE_VALUE)
 {
  DWORD lastError = GetLastError ();
  if (lastError == ERROR_ACCESS_DENIED || lastError == ERROR_SHARING_VIOLATION)
   return ERR_PERMISSION;
  return ERR_OPEN_DEVICE;
 }

 file->isOpen = true;
 LARGE_INTEGER fileSize;
 if (GetFileSizeEx (file->handle, &fileSize))
  file->size = (uint64_t) fileSize.QuadPart;
 else
  file->size = 0;
 return ERR_OK;
}

error_code_t fileClose (file_t *file)
{
 if (!file || !file->isOpen)
  return ERR_INVALID_ARG;
 if (CloseHandle (file->handle))
 {
  file->isOpen = false;
  file->handle = INVALID_HANDLE_VALUE;
  return ERR_OK;
 }
 return ERR_UNKNOWN;
}

error_code_t fileRead (file_t *file, uint64_t offset, uint8_t *buffer, size_t size, size_t *bytesRead)
{
 if (!file || !file->isOpen || !buffer)
  return ERR_INVALID_ARG;
 LARGE_INTEGER liOffset;
 liOffset.QuadPart = offset;
 if (!SetFilePointerEx (file->handle, liOffset, NULL, FILE_BEGIN))
  return ERR_SEEK_DEVICE;
 DWORD readCount = 0;
 if (!ReadFile (file->handle, buffer, (DWORD) size, &readCount, NULL))
  return ERR_READ_DEVICE;
 if (bytesRead)
  *bytesRead = readCount;
 return (readCount == size) ? ERR_OK : ERR_READ_DEVICE;
}

error_code_t fileWrite (file_t *file, uint64_t offset, const uint8_t *buffer, size_t size, size_t *bytesWritten)
{
 if (!file || !file->isOpen || !buffer || file->readOnly)
  return ERR_INVALID_ARG;
 LARGE_INTEGER liOffset;
 liOffset.QuadPart = offset;
 if (!SetFilePointerEx (file->handle, liOffset, NULL, FILE_BEGIN))
  return ERR_SEEK_DEVICE;
 DWORD written = 0;
 if (!WriteFile (file->handle, buffer, (DWORD) size, &written, NULL))
  return ERR_WRITE_DEVICE;
 if (bytesWritten)
  *bytesWritten = written;
 if (offset + written > file->size)
  file->size = offset + written;
 return (written == size) ? ERR_OK : ERR_WRITE_DEVICE;
}

error_code_t fileFlush (file_t *file)
{
 if (!file || !file->isOpen)
  return ERR_INVALID_ARG;
 return FlushFileBuffers (file->handle) ? ERR_OK : ERR_UNKNOWN;
}

error_code_t fileGetSize (file_t *file, uint64_t *size)
{
 if (!file || !file->isOpen || !size)
  return ERR_INVALID_ARG;
 LARGE_INTEGER fileSize;
 if (GetFileSizeEx (file->handle, &fileSize))
 {
  *size = (uint64_t) fileSize.QuadPart;
  return ERR_OK;
 }
 return ERR_GET_SIZE;
}

error_code_t fileTruncate (file_t *file, uint64_t newSize)
{
 if (!file || !file->isOpen || file->readOnly)
  return ERR_INVALID_ARG;
 LARGE_INTEGER liDistance;
 liDistance.QuadPart = newSize;
 if (!SetFilePointerEx (file->handle, liDistance, NULL, FILE_BEGIN))
  return ERR_SEEK_DEVICE;
 if (!SetEndOfFile (file->handle))
  return ERR_WRITE_DEVICE;
 file->size = newSize;
 return ERR_OK;
}

error_code_t fileRename (file_t *file, const char *newPath)
{
 if (!file || !file->isOpen || !newPath)
  return ERR_INVALID_ARG;
 if (!MoveFileExA (file->path, newPath, MOVEFILE_REPLACE_EXISTING))
  return ERR_PERMISSION;
 snprintf (file->path, sizeof (file->path), "%s", newPath);
 return ERR_OK;
}

error_code_t fileDelete (file_t *file)
{
 if (!file || !file->isOpen)
  return ERR_INVALID_ARG;
 fileClose (file);
 if (DeleteFileA (file->path))
  return ERR_OK;
 return ERR_PERMISSION;
}

error_code_t fileSetTimes (file_t *file, time_t accessTime, time_t modifyTime)
{
 if (!file || !file->isOpen)
  return ERR_INVALID_ARG;
 FILETIME ftAccess, ftModify;
 LARGE_INTEGER li;
 li.QuadPart = (LONGLONG) accessTime * 10000000 + 116444736000000000;
 ftAccess.dwLowDateTime = li.LowPart;
 ftAccess.dwHighDateTime = li.HighPart;
 li.QuadPart = (LONGLONG) modifyTime * 10000000 + 116444736000000000;
 ftModify.dwLowDateTime = li.LowPart;
 ftModify.dwHighDateTime = li.HighPart;
 if (SetFileTime (file->handle, NULL, &ftAccess, &ftModify))
  return ERR_OK;
 return ERR_PERMISSION;
}

error_code_t fileSetAttributes (file_t *file, bool readOnly, bool hidden, bool system)
{
 if (!file || !file->isOpen)
  return ERR_INVALID_ARG;
 DWORD attrs = winAttributesFromFlags (readOnly, hidden, system);
 if (SetFileAttributesA (file->path, attrs))
  return ERR_OK;
 return ERR_PERMISSION;
}

#else // POSIX

error_code_t fileOpen (file_t *file, const char *path, bool readOnly)
{
 if (!file || !path)
  return ERR_INVALID_ARG;
 memset (file, 0, sizeof (file_t));
 snprintf (file->path, sizeof (file->path), "%s", path);
 file->readOnly = readOnly;

 int flags = readOnly ? O_RDONLY : (O_RDWR | O_SYNC);
 file->handle = open (path, flags);
 if (file->handle < 0)
 {
  if (errno == EACCES || errno == EPERM)
   return ERR_PERMISSION;
  return ERR_OPEN_DEVICE;
 }

 file->isOpen = true;
 struct stat st;
 if (fstat (file->handle, &st) == 0)
  file->size = (uint64_t) st.st_size;
 else
  file->size = 0;
 return ERR_OK;
}

error_code_t fileClose (file_t *file)
{
 if (!file || !file->isOpen)
  return ERR_INVALID_ARG;
 if (close (file->handle) == 0)
 {
  file->isOpen = false;
  file->handle = -1;
  return ERR_OK;
 }
 return ERR_UNKNOWN;
}

error_code_t fileRead (file_t *file, uint64_t offset, uint8_t *buffer, size_t size, size_t *bytesRead)
{
 if (!file || !file->isOpen || !buffer)
  return ERR_INVALID_ARG;
 if (lseek (file->handle, (off_t) offset, SEEK_SET) != (off_t) offset)
  return ERR_SEEK_DEVICE;
 ssize_t readCount = read (file->handle, buffer, size);
 if (readCount < 0)
  return ERR_READ_DEVICE;
 if (bytesRead)
  *bytesRead = (size_t) readCount;
 return ((size_t) readCount == size) ? ERR_OK : ERR_READ_DEVICE;
}

error_code_t fileWrite (file_t *file, uint64_t offset, const uint8_t *buffer, size_t size, size_t *bytesWritten)
{
 if (!file || !file->isOpen || !buffer || file->readOnly)
  return ERR_INVALID_ARG;
 if (lseek (file->handle, (off_t) offset, SEEK_SET) != (off_t) offset)
  return ERR_SEEK_DEVICE;
 ssize_t written = write (file->handle, buffer, size);
 if (written < 0)
  return ERR_WRITE_DEVICE;
 if (bytesWritten)
  *bytesWritten = (size_t) written;
 if (offset + written > file->size)
  file->size = offset + written;
 return ((size_t) written == size) ? ERR_OK : ERR_WRITE_DEVICE;
}

error_code_t fileFlush (file_t *file)
{
 if (!file || !file->isOpen)
  return ERR_INVALID_ARG;
 return (fsync (file->handle) == 0) ? ERR_OK : ERR_UNKNOWN;
}

error_code_t fileGetSize (file_t *file, uint64_t *size)
{
 if (!file || !file->isOpen || !size)
  return ERR_INVALID_ARG;
 struct stat st;
 if (fstat (file->handle, &st) == 0)
 {
  *size = (uint64_t) st.st_size;
  return ERR_OK;
 }
 return ERR_GET_SIZE;
}

error_code_t fileTruncate (file_t *file, uint64_t newSize)
{
 if (!file || !file->isOpen || file->readOnly)
  return ERR_INVALID_ARG;
 if (ftruncate (file->handle, (off_t) newSize) == 0)
 {
  file->size = newSize;
  return ERR_OK;
 }
 return ERR_WRITE_DEVICE;
}

error_code_t fileRename (file_t *file, const char *newPath)
{
 if (!file || !file->isOpen || !newPath)
  return ERR_INVALID_ARG;
 if (rename (file->path, newPath) == 0)
 {
  snprintf (file->path, sizeof (file->path), "%s", newPath);
  return ERR_OK;
 }
 return ERR_PERMISSION;
}

error_code_t fileDelete (file_t *file)
{
 if (!file || !file->isOpen)
  return ERR_INVALID_ARG;
 fileClose (file);
 if (remove (file->path) == 0)
  return ERR_OK;
 return ERR_PERMISSION;
}

error_code_t fileSetTimes (file_t *file, time_t accessTime, time_t modifyTime)
{
 if (!file || !file->isOpen)
  return ERR_INVALID_ARG;
 struct timespec times[2];
 times[0].tv_sec = accessTime;
 times[0].tv_nsec = 0;
 times[1].tv_sec = modifyTime;
 times[1].tv_nsec = 0;
 if (futimens (file->handle, times) == 0)
  return ERR_OK;
 return ERR_PERMISSION;
}

error_code_t fileSetAttributes (file_t *file, bool readOnly, bool hidden, bool system)
{
 (void) hidden;
 (void) system;
 if (!file || !file->isOpen)
  return ERR_INVALID_ARG;
 mode_t mode = S_IRUSR | S_IWUSR | S_IRGRP | S_IROTH;
 if (readOnly)
  mode &= ~S_IWUSR;
 if (fchmod (file->handle, mode) == 0)
  return ERR_OK;
 return ERR_PERMISSION;
}

#endif