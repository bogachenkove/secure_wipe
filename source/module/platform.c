#include "platform.h"
#include <locale.h>
void platform_init(void) {
#ifdef _WIN32
  SetConsoleOutputCP(CP_UTF8);
  SetConsoleCP(CP_UTF8);
#else
  setlocale(LC_ALL, "C.UTF-8");
#endif
}
void platform_cleanup(void) {}
bool check_admin_privileges(void) {
#ifdef _WIN32
  BOOL is_admin = FALSE;
  PSID admin_group = NULL;
  SID_IDENTIFIER_AUTHORITY nt_authority = SECURITY_NT_AUTHORITY;
  if (AllocateAndInitializeSid(&nt_authority, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &admin_group)) {
    CheckTokenMembership(NULL, admin_group, &is_admin);
    FreeSid(admin_group);
  }
  return is_admin == TRUE;
#else
  return (geteuid() == 0);
#endif
}
void *aligned_alloc(size_t size) {
#ifdef _WIN32
  return _aligned_malloc(size, SECTOR_SIZE);
#else
  return aligned_alloc(SECTOR_SIZE, size);
#endif
}
void aligned_free(void *pointer) {
#ifdef _WIN32
  _aligned_free(pointer);
#else
  free(pointer);
#endif
}
#ifdef _WIN32
static bool is_physical_drive_mounted(int disk_number) {
  HANDLE volume_handle = FindFirstVolumeW(NULL, 0);
  if (volume_handle == INVALID_HANDLE_VALUE)
    return false;
  wchar_t volume_name[MAX_PATH];
  DWORD bytes_returned = 0;
  bool found = false;
  while (FindNextVolumeW(volume_handle, volume_name, MAX_PATH)) {
    wchar_t volume_path[MAX_PATH];
    if (GetVolumePathNamesForVolumeNameW(volume_name, volume_path, MAX_PATH, &bytes_returned)) {
      HANDLE current_volume = CreateFileW(volume_name, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
      if (current_volume != INVALID_HANDLE_VALUE) {
        VOLUME_DISK_EXTENTS extents = {0};
        bytes_returned = 0;
        if (DeviceIoControl(current_volume, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, NULL, 0, &extents, sizeof(extents), &bytes_returned, NULL)) {
          for (DWORD extent_index = 0; extent_index < extents.NumberOfDiskExtents; extent_index++) {
            if ((int)extents.Extents[extent_index].DiskNumber == disk_number) {
              found = true;
              break;
            }
          }
        }
        CloseHandle(current_volume);
        if (found)
          break;
      }
    }
  }
  FindVolumeClose(volume_handle);
  return found;
}
#endif
bool is_device_mounted(const char *device_path) {
#ifdef _WIN32
  int disk_number = -1;
  if (sscanf(device_path, "\\\\.\\PhysicalDrive%d", &disk_number) == 1)
    return is_physical_drive_mounted(disk_number);
  return false;
#else
  FILE *mounts_file = fopen("/proc/mounts", "r");
  if (!mounts_file)
    return false;
  char line[512];
  bool found = false;
  while (fgets(line, sizeof(line), mounts_file)) {
    char mount_device[256], mount_point[256];
    if (sscanf(line, "%255s %255s", mount_device, mount_point) == 2) {
      if (strcmp(mount_device, device_path) == 0) {
        LOG_WARN("Device %s is mounted at %s", device_path, mount_point);
        found = true;
        break;
      }
    }
  }
  fclose(mounts_file);
  return found;
#endif
}
