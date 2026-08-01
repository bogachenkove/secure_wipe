#include "file_scanner.h"
#include "platform.h"
#ifdef _WIN32
error_code_t file_scanner_init_list(file_list_t *list) {
  if (!list)
    return ERR_INVALID_ARG;
  memset(list, 0, sizeof(file_list_t));
  list->capacity = 1024;
  list->items = (file_info_t *)malloc(list->capacity * sizeof(file_info_t));
  return list->items ? ERR_OK : ERR_MEMORY;
}
void file_scanner_free_list(file_list_t *list) {
  if (list && list->items) {
    free(list->items);
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
  }
}
error_code_t file_scanner_add_item(file_list_t *list, const file_info_t *info) {
  if (!list || !info)
    return ERR_INVALID_ARG;
  if (list->count >= list->capacity) {
    size_t new_capacity = list->capacity * 2;
    file_info_t *new_items = (file_info_t *)realloc(list->items, new_capacity * sizeof(file_info_t));
    if (!new_items)
      return ERR_MEMORY;
    list->items = new_items;
    list->capacity = new_capacity;
  }
  list->items[list->count++] = *info;
  return ERR_OK;
}
static void win_file_time_to_time_t(FILETIME ft, time_t *out_time) {
  ULARGE_INTEGER uli = {0};
  uli.LowPart = ft.dwLowDateTime;
  uli.HighPart = ft.dwHighDateTime;
  *out_time = (time_t)((uli.QuadPart - 116444736000000000ULL) / 10000000);
}
static error_code_t scan_directory_windows(const char *path, file_list_t *list, bool recursive, bool follow_symlinks) {
  (void)follow_symlinks;
  char search_path[MAX_PATH_LEN];
  snprintf(search_path, sizeof(search_path), "%s\\*", path);
  WIN32_FIND_DATAA find_data;
  HANDLE find_handle = FindFirstFileA(search_path, &find_data);
  if (find_handle == INVALID_HANDLE_VALUE)
    return ERR_OPEN_DEVICE;
  do {
    if (strcmp(find_data.cFileName, ".") == 0 || strcmp(find_data.cFileName, "..") == 0)
      continue;
    file_info_t info;
    memset(&info, 0, sizeof(info));
    snprintf(info.full_path, sizeof(info.full_path), "%s\\%s", path, find_data.cFileName);
    snprintf(info.name, sizeof(info.name), "%s", find_data.cFileName);
    info.size = ((uint64_t)find_data.nFileSizeHigh << 32) | find_data.nFileSizeLow;
    if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
      info.type = FILE_TYPE_DIRECTORY;
    else if (find_data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
      info.type = FILE_TYPE_SYMLINK;
    else
      info.type = FILE_TYPE_REGULAR;
    info.is_read_only = (find_data.dwFileAttributes & FILE_ATTRIBUTE_READONLY) != 0;
    info.is_hidden = (find_data.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) != 0;
    info.is_system = (find_data.dwFileAttributes & FILE_ATTRIBUTE_SYSTEM) != 0;
    win_file_time_to_time_t(find_data.ftLastAccessTime, &info.access_time);
    win_file_time_to_time_t(find_data.ftLastWriteTime, &info.modify_time);
    win_file_time_to_time_t(find_data.ftCreationTime, &info.create_time);
    error_code_t add_error = file_scanner_add_item(list, &info);
    if (add_error != ERR_OK) {
      FindClose(find_handle);
      return add_error;
    }
    if (recursive && info.type == FILE_TYPE_DIRECTORY) {
      error_code_t scan_error = scan_directory_windows(info.full_path, list, recursive, follow_symlinks);
      if (scan_error != ERR_OK) {
        FindClose(find_handle);
        return scan_error;
      }
    }
  } while (FindNextFileA(find_handle, &find_data));
  FindClose(find_handle);
  return ERR_OK;
}
error_code_t file_scanner_scan(const char *path, file_list_t *list, bool recursive, bool follow_symlinks) {
  if (!path || !list)
    return ERR_INVALID_ARG;
  return scan_directory_windows(path, list, recursive, follow_symlinks);
}
error_code_t file_scanner_get_info(const char *path, file_info_t *info) {
  if (!path || !info)
    return ERR_INVALID_ARG;
  WIN32_FIND_DATAA find_data;
  HANDLE find_handle = FindFirstFileA(path, &find_data);
  if (find_handle == INVALID_HANDLE_VALUE)
    return ERR_OPEN_DEVICE;
  memset(info, 0, sizeof(file_info_t));
  snprintf(info->full_path, sizeof(info->full_path), "%s", path);
  snprintf(info->name, sizeof(info->name), "%s", find_data.cFileName);
  info->size = ((uint64_t)find_data.nFileSizeHigh << 32) | find_data.nFileSizeLow;
  if (find_data.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)
    info->type = FILE_TYPE_DIRECTORY;
  else if (find_data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
    info->type = FILE_TYPE_SYMLINK;
  else
    info->type = FILE_TYPE_REGULAR;
  info->is_read_only = (find_data.dwFileAttributes & FILE_ATTRIBUTE_READONLY) != 0;
  info->is_hidden = (find_data.dwFileAttributes & FILE_ATTRIBUTE_HIDDEN) != 0;
  info->is_system = (find_data.dwFileAttributes & FILE_ATTRIBUTE_SYSTEM) != 0;
  win_file_time_to_time_t(find_data.ftLastAccessTime, &info->access_time);
  win_file_time_to_time_t(find_data.ftLastWriteTime, &info->modify_time);
  win_file_time_to_time_t(find_data.ftCreationTime, &info->create_time);
  FindClose(find_handle);
  return ERR_OK;
}
#else
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <string.h>
#include <libgen.h>
error_code_t file_scanner_init_list(file_list_t *list) {
  if (!list)
    return ERR_INVALID_ARG;
  memset(list, 0, sizeof(file_list_t));
  list->capacity = 1024;
  list->items = (file_info_t *)malloc(list->capacity * sizeof(file_info_t));
  return list->items ? ERR_OK : ERR_MEMORY;
}
void file_scanner_free_list(file_list_t *list) {
  if (list && list->items) {
    free(list->items);
    list->items = NULL;
    list->count = 0;
    list->capacity = 0;
  }
}
error_code_t file_scanner_add_item(file_list_t *list, const file_info_t *info) {
  if (!list || !info)
    return ERR_INVALID_ARG;
  if (list->count >= list->capacity) {
    size_t new_capacity = list->capacity * 2;
    file_info_t *new_items = (file_info_t *)realloc(list->items, new_capacity * sizeof(file_info_t));
    if (!new_items)
      return ERR_MEMORY;
    list->items = new_items;
    list->capacity = new_capacity;
  }
  list->items[list->count++] = *info;
  return ERR_OK;
}
static error_code_t scan_directory_posix(const char *path, file_list_t *list, bool recursive, bool follow_symlinks) {
  DIR *dir = opendir(path);
  if (!dir)
    return ERR_OPEN_DEVICE;
  struct dirent *entry;
  while ((entry = readdir(dir)) != NULL) {
    if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
      continue;
    char full_path[MAX_PATH_LEN];
    snprintf(full_path, sizeof(full_path), "%s/%s", path, entry->d_name);
    struct stat st;
    int stat_result = follow_symlinks ? stat(full_path, &st) : lstat(full_path, &st);
    if (stat_result != 0)
      continue;
    file_info_t info;
    memset(&info, 0, sizeof(info));
    snprintf(info.full_path, sizeof(info.full_path), "%s", full_path);
    snprintf(info.name, sizeof(info.name), "%s", entry->d_name);
    info.size = (uint64_t)st.st_size;
    if (S_ISDIR(st.st_mode))
      info.type = FILE_TYPE_DIRECTORY;
    else if (S_ISLNK(st.st_mode))
      info.type = FILE_TYPE_SYMLINK;
    else if (S_ISREG(st.st_mode))
      info.type = FILE_TYPE_REGULAR;
    else
      info.type = FILE_TYPE_OTHER;
    info.is_read_only = ((st.st_mode & S_IWUSR) == 0);
    info.is_hidden = (entry->d_name[0] == '.');
    info.is_system = false;
    info.access_time = st.st_atime;
    info.modify_time = st.st_mtime;
    info.create_time = st.st_ctime;
    error_code_t add_error = file_scanner_add_item(list, &info);
    if (add_error != ERR_OK) {
      closedir(dir);
      return add_error;
    }
    if (recursive && info.type == FILE_TYPE_DIRECTORY) {
      error_code_t scan_error = scan_directory_posix(info.full_path, list, recursive, follow_symlinks);
      if (scan_error != ERR_OK) {
        closedir(dir);
        return scan_error;
      }
    }
  }
  closedir(dir);
  return ERR_OK;
}
error_code_t file_scanner_scan(const char *path, file_list_t *list, bool recursive, bool follow_symlinks) {
  if (!path || !list)
    return ERR_INVALID_ARG;
  return scan_directory_posix(path, list, recursive, follow_symlinks);
}
error_code_t file_scanner_get_info(const char *path, file_info_t *info) {
  if (!path || !info)
    return ERR_INVALID_ARG;
  struct stat st;
  if (stat(path, &st) != 0)
    return ERR_OPEN_DEVICE;
  memset(info, 0, sizeof(file_info_t));
  snprintf(info->full_path, sizeof(info->full_path), "%s", path);
  char *base = basename((char *)path);
  snprintf(info->name, sizeof(info->name), "%s", base);
  info->size = (uint64_t)st.st_size;
  if (S_ISDIR(st.st_mode))
    info->type = FILE_TYPE_DIRECTORY;
  else if (S_ISLNK(st.st_mode))
    info->type = FILE_TYPE_SYMLINK;
  else if (S_ISREG(st.st_mode))
    info->type = FILE_TYPE_REGULAR;
  else
    info->type = FILE_TYPE_OTHER;
  info->is_read_only = ((st.st_mode & S_IWUSR) == 0);
  info->is_hidden = (base[0] == '.');
  info->is_system = false;
  info->access_time = st.st_atime;
  info->modify_time = st.st_mtime;
  info->create_time = st.st_ctime;
  return ERR_OK;
}
#endif
