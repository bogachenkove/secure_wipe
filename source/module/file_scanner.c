#include "file_scanner.h"
#include "platform.h"

#ifdef _WIN32

error_code_t fileScannerInitList (file_list_t *list)
{
 if (!list)
  return ERR_INVALID_ARG;
 memset (list, 0, sizeof (file_list_t));
 list->capacity = 1024;
 list->items = (file_info_t *) malloc (list->capacity * sizeof (file_info_t));
 return list->items ? ERR_OK : ERR_MEMORY;
}

void fileScannerFreeList (file_list_t *list)
{
 if (list && list->items)
 {
  free (list->items);
  list->items = NULL;
  list->count = 0;
  list->capacity = 0;
 }
}

error_code_t fileScannerAddItem (file_list_t *list, const file_info_t *info)
{
 if (!list || !info)
  return ERR_INVALID_ARG;
 if (list->count >= list->capacity)
 {
  size_t newCapacity = list->capacity * 2;
  file_info_t *newItems = (file_info_t *) realloc (list->items, newCapacity * sizeof (file_info_t));
  if (!newItems)
   return ERR_MEMORY;
  list->items = newItems;
  list->capacity = newCapacity;
 }
 list->items[list->count++] = *info;
 return ERR_OK;
}

static void winFileTimeToTimeT (FILETIME ft, time_t *outTime)
{
 ULARGE_INTEGER uli = {0};
 uli.LowPart = ft.dwLowDateTime;
 uli.HighPart = ft.dwHighDateTime;
 *outTime = (time_t) ((uli.QuadPart - 116444736000000000ULL) / 10000000);
}

static error_code_t scanDirectoryWindows (const char *path, file_list_t *list, bool recursive, bool followSymlinks)
{
 (void) followSymlinks;
 char searchPath[MAX_PATH_LEN];
 snprintf (searchPath, sizeof (searchPath), "%s\\*", path);

 WIN32_FIND_DATAA findData;
 HANDLE findHandle = FindFirstFileA (searchPath, &findData);
 if (findHandle == INVALID_HANDLE_VALUE)
  return ERR_OPEN_DEVICE;

 do
 {
  if (strcmp (findData.cFileName, ".") == 0 || strcmp (findData.cFileName, "..") == 0)
   continue;

  file_info_t info;
  memset (&info, 0, sizeof (info));
  snprintf (info.fullPath, sizeof (info.fullPath), "%s\\%s", path, findData.cFileName);
  snprintf (info.name, sizeof (info.name), "%s", findData.cFileName);
  info.size = ((uint64_t) findData.nFileSizeHigh << 32) | findData.nFileSizeLow;

  if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
   info.type = FILE_TYPE_DIRECTORY;
  else if (findData.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
   info.type = FILE_TYPE_SYMLINK;
  else
   info.type = FILE_TYPE_REGULAR;

  info.isReadOnly = (findData.dwFileAttributes & FILE_ATTRIBUTE_READONLY) != 0;
  info.isHidden = (findData.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) != 0;
  info.isSystem = (findData.dwFileAttributes & FILE_ATTRIBUTE_SYSTEM) != 0;

  winFileTimeToTimeT (findData.ftLastAccessTime, &info.accessTime);
  winFileTimeToTimeT (findData.ftLastWriteTime, &info.modifyTime);
  winFileTimeToTimeT (findData.ftCreationTime, &info.createTime);

  error_code_t addError = fileScannerAddItem (list, &info);
  if (addError != ERR_OK)
  {
   FindClose (findHandle);
   return addError;
  }

  if (recursive && info.type == FILE_TYPE_DIRECTORY)
  {
   error_code_t scanError = scanDirectoryWindows (info.fullPath, list, recursive, followSymlinks);
   if (scanError != ERR_OK)
   {
	FindClose (findHandle);
	return scanError;
   }
  }

 } while (FindNextFileA (findHandle, &findData));

 FindClose (findHandle);
 return ERR_OK;
}

error_code_t fileScannerScan (const char *path, file_list_t *list, bool recursive, bool followSymlinks)
{
 if (!path || !list)
  return ERR_INVALID_ARG;
 return scanDirectoryWindows (path, list, recursive, followSymlinks);
}

error_code_t fileScannerGetInfo (const char *path, file_info_t *info)
{
 if (!path || !info)
  return ERR_INVALID_ARG;
 WIN32_FIND_DATAA findData;
 HANDLE findHandle = FindFirstFileA (path, &findData);
 if (findHandle == INVALID_HANDLE_VALUE)
  return ERR_OPEN_DEVICE;

 memset (info, 0, sizeof (file_info_t));
 snprintf (info->fullPath, sizeof (info->fullPath), "%s", path);
 snprintf (info->name, sizeof (info->name), "%s", findData.cFileName);
 info->size = ((uint64_t) findData.nFileSizeHigh << 32) | findData.nFileSizeLow;

 if (findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
  info->type = FILE_TYPE_DIRECTORY;
 else if (findData.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
  info->type = FILE_TYPE_SYMLINK;
 else
  info->type = FILE_TYPE_REGULAR;

 info->isReadOnly = (findData.dwFileAttributes & FILE_ATTRIBUTE_READONLY) != 0;
 info->isHidden = (findData.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) != 0;
 info->isSystem = (findData.dwFileAttributes & FILE_ATTRIBUTE_SYSTEM) != 0;

 winFileTimeToTimeT (findData.ftLastAccessTime, &info->accessTime);
 winFileTimeToTimeT (findData.ftLastWriteTime, &info->modifyTime);
 winFileTimeToTimeT (findData.ftCreationTime, &info->createTime);

 FindClose (findHandle);
 return ERR_OK;
}

#else // POSIX

#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <libgen.h>

error_code_t fileScannerInitList (file_list_t *list)
{
 if (!list)
  return ERR_INVALID_ARG;
 memset (list, 0, sizeof (file_list_t));
 list->capacity = 1024;
 list->items = (file_info_t *) malloc (list->capacity * sizeof (file_info_t));
 return list->items ? ERR_OK : ERR_MEMORY;
}

void fileScannerFreeList (file_list_t *list)
{
 if (list && list->items)
 {
  free (list->items);
  list->items = NULL;
  list->count = 0;
  list->capacity = 0;
 }
}

error_code_t fileScannerAddItem (file_list_t *list, const file_info_t *info)
{
 if (!list || !info)
  return ERR_INVALID_ARG;
 if (list->count >= list->capacity)
 {
  size_t newCapacity = list->capacity * 2;
  file_info_t *newItems = (file_info_t *) realloc (list->items, newCapacity * sizeof (file_info_t));
  if (!newItems)
   return ERR_MEMORY;
  list->items = newItems;
  list->capacity = newCapacity;
 }
 list->items[list->count++] = *info;
 return ERR_OK;
}

static error_code_t scanDirectoryPosix (const char *path, file_list_t *list, bool recursive, bool followSymlinks)
{
 DIR *dir = opendir (path);
 if (!dir)
  return ERR_OPEN_DEVICE;

 struct dirent *entry;
 while ((entry = readdir (dir)) != NULL)
 {
  if (strcmp (entry->d_name, ".") == 0 || strcmp (entry->d_name, "..") == 0)
   continue;

  char fullPath[MAX_PATH_LEN];
  snprintf (fullPath, sizeof (fullPath), "%s/%s", path, entry->d_name);

  struct stat st;
  int statResult = followSymlinks ? stat (fullPath, &st) : lstat (fullPath, &st);
  if (statResult != 0)
   continue;

  file_info_t info;
  memset (&info, 0, sizeof (info));
  snprintf (info.fullPath, sizeof (info.fullPath), "%s", fullPath);
  snprintf (info.name, sizeof (info.name), "%s", entry->d_name);
  info.size = (uint64_t) st.st_size;

  if (S_ISDIR (st.st_mode))
   info.type = FILE_TYPE_DIRECTORY;
  else if (S_ISLNK (st.st_mode))
   info.type = FILE_TYPE_SYMLINK;
  else if (S_ISREG (st.st_mode))
   info.type = FILE_TYPE_REGULAR;
  else
   info.type = FILE_TYPE_OTHER;

  info.isReadOnly = ((st.st_mode & S_IWUSR) == 0);
  info.isHidden = (entry->d_name[0] == '.');
  info.isSystem = false;
  info.accessTime = st.st_atime;
  info.modifyTime = st.st_mtime;
  info.createTime = st.st_ctime;

  error_code_t addError = fileScannerAddItem (list, &info);
  if (addError != ERR_OK)
  {
   closedir (dir);
   return addError;
  }

  if (recursive && info.type == FILE_TYPE_DIRECTORY)
  {
   error_code_t scanError = scanDirectoryPosix (info.fullPath, list, recursive, followSymlinks);
   if (scanError != ERR_OK)
   {
	closedir (dir);
	return scanError;
   }
  }
 }
 closedir (dir);
 return ERR_OK;
}

error_code_t fileScannerScan (const char *path, file_list_t *list, bool recursive, bool followSymlinks)
{
 if (!path || !list)
  return ERR_INVALID_ARG;
 return scanDirectoryPosix (path, list, recursive, followSymlinks);
}

error_code_t fileScannerGetInfo (const char *path, file_info_t *info)
{
 if (!path || !info)
  return ERR_INVALID_ARG;
 struct stat st;
 if (stat (path, &st) != 0)
  return ERR_OPEN_DEVICE;

 memset (info, 0, sizeof (file_info_t));
 snprintf (info->fullPath, sizeof (info->fullPath), "%s", path);
 char *base = basename ((char *) path);
 snprintf (info->name, sizeof (info->name), "%s", base);
 info->size = (uint64_t) st.st_size;

 if (S_ISDIR (st.st_mode))
  info->type = FILE_TYPE_DIRECTORY;
 else if (S_ISLNK (st.st_mode))
  info->type = FILE_TYPE_SYMLINK;
 else if (S_ISREG (st.st_mode))
  info->type = FILE_TYPE_REGULAR;
 else
  info->type = FILE_TYPE_OTHER;

 info->isReadOnly = ((st.st_mode & S_IWUSR) == 0);
 info->isHidden = (base[0] == '.');
 info->isSystem = false;
 info->accessTime = st.st_atime;
 info->modifyTime = st.st_mtime;
 info->createTime = st.st_ctime;

 return ERR_OK;
}

#endif