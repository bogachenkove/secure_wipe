#include "disk_scanner.h"
#ifdef _WIN32
#include <windows.h>
#include <ntddscsi.h>
#include <cfgmgr32.h>
#pragma comment(lib, "cfgmgr32.lib")
#endif
const char *disk_type_to_string(disk_type_t type) {
  switch (type) {
  case DISK_TYPE_HDD:
    return "HDD";
  case DISK_TYPE_SSD:
    return "SSD";
  case DISK_TYPE_USB:
    return "USB";
  case DISK_TYPE_NVME:
    return "NVMe";
  case DISK_TYPE_SD_CARD:
    return "SD Card";
  case DISK_TYPE_OPTICAL:
    return "Optical";
  case DISK_TYPE_VIRTUAL:
    return "Virtual";
  default:
    return "Unknown";
  }
}
const disk_info_t *disk_scanner_get_by_index(const disk_scan_result_t *result, int index) {
  if (!result || index < 1 || index > result->count)
    return NULL;
  return &result->disks[index - 1];
}
#ifdef _WIN32
static disk_type_t get_disk_type_from_bus(STORAGE_BUS_TYPE bus_type, bool is_removable) {
  switch (bus_type) {
  case BusTypeUsb:
    return DISK_TYPE_USB;
  case BusTypeNvme:
    return DISK_TYPE_NVME;
  case BusTypeSata:
  case BusTypeAta:
    return is_removable ? DISK_TYPE_USB : DISK_TYPE_HDD;
  case BusTypeSd:
  case BusTypeMmc:
    return DISK_TYPE_SD_CARD;
  case BusTypeVirtual:
  case BusTypeFileBackedVirtual:
    return DISK_TYPE_VIRTUAL;
  case BusTypeAtapi:
    return DISK_TYPE_OPTICAL;
  default:
    return DISK_TYPE_UNKNOWN;
  }
}
static error_code_t get_disk_info_windows(int disk_number, disk_info_t *info) {
  char device_path[64];
  snprintf(device_path, sizeof(device_path), "\\\\.\\PhysicalDrive%d", disk_number);
  HANDLE device_handle = CreateFileA(device_path, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
  if (device_handle == INVALID_HANDLE_VALUE)
    return ERR_OPEN_DEVICE;
  memset(info, 0, sizeof(disk_info_t));
  snprintf(info->device_path, sizeof(info->device_path), "%s", device_path);
  info->disk_number = disk_number;
  info->sector_size = SECTOR_SIZE;
  DWORD bytes_returned = 0;
  DISK_GEOMETRY_EX geometry = {0};
  if (DeviceIoControl(device_handle, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX, NULL, 0, &geometry, sizeof(geometry), &bytes_returned, NULL)) {
    info->size_bytes = geometry.DiskSize.QuadPart;
    info->sector_size = geometry.Geometry.BytesPerSector;
  }
  STORAGE_PROPERTY_QUERY query;
  memset(&query, 0, sizeof(query));
  query.PropertyId = StorageDeviceProperty;
  query.QueryType = PropertyStandardQuery;
  uint8_t property_buffer[4096] = {0};
  bytes_returned = 0;
  if (DeviceIoControl(device_handle, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof(query), property_buffer, sizeof(property_buffer), &bytes_returned,
                      NULL)) {
    STORAGE_DEVICE_DESCRIPTOR *descriptor = (STORAGE_DEVICE_DESCRIPTOR *)property_buffer;
    info->is_removable = descriptor->RemovableMedia;
    info->type = get_disk_type_from_bus(descriptor->BusType, info->is_removable);
    if (descriptor->VendorIdOffset > 0) {
      char *vendor = (char *)property_buffer + descriptor->VendorIdOffset;
      snprintf(info->vendor, sizeof(info->vendor), "%s", vendor);
      size_t vendor_length = strlen(info->vendor);
      while (vendor_length > 0 && (info->vendor[vendor_length - 1] == ' ' || info->vendor[vendor_length - 1] == '\0'))
        info->vendor[--vendor_length] = '\0';
    }
    if (descriptor->ProductIdOffset > 0) {
      char *product = (char *)property_buffer + descriptor->ProductIdOffset;
      snprintf(info->model, sizeof(info->model), "%s", product);
      size_t model_length = strlen(info->model);
      while (model_length > 0 && (info->model[model_length - 1] == ' ' || info->model[model_length - 1] == '\0'))
        info->model[--model_length] = '\0';
    }
    switch (descriptor->BusType) {
    case BusTypeUsb:
      snprintf(info->bus_type, sizeof(info->bus_type), "USB");
      break;
    case BusTypeSata:
      snprintf(info->bus_type, sizeof(info->bus_type), "SATA");
      break;
    case BusTypeNvme:
      snprintf(info->bus_type, sizeof(info->bus_type), "NVMe");
      break;
    case BusTypeAta:
      snprintf(info->bus_type, sizeof(info->bus_type), "ATA");
      break;
    case BusTypeSd:
      snprintf(info->bus_type, sizeof(info->bus_type), "SD");
      break;
    case BusTypeScsi:
      snprintf(info->bus_type, sizeof(info->bus_type), "SCSI");
      break;
    default:
      snprintf(info->bus_type, sizeof(info->bus_type), "Unknown");
      break;
    }
  }
  char system_path[MAX_PATH];
  if (GetSystemDirectoryA(system_path, sizeof(system_path))) {
    char system_drive[4] = {system_path[0], ':', '\\', '\0'};
    char volume_path[64];
    snprintf(volume_path, sizeof(volume_path), "\\\\.\\%s", system_drive);
    HANDLE volume_handle = CreateFileA(volume_path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (volume_handle != INVALID_HANDLE_VALUE) {
      bytes_returned = 0;
      VOLUME_DISK_EXTENTS extents = {0};
      if (DeviceIoControl(volume_handle, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, NULL, 0, &extents, sizeof(extents), &bytes_returned, NULL)) {
        if (extents.NumberOfDiskExtents > 0 && (int)extents.Extents[0].DiskNumber == disk_number) {
          info->is_system = true;
          info->is_boot = true;
        }
      }
      CloseHandle(volume_handle);
    }
  } else {
    LOG_WARN("GetSystemDirectoryA failed, falling back to default C:");
    char system_drive[4] = "C:";
    char volume_path[64];
    snprintf(volume_path, sizeof(volume_path), "\\\\.\\%s", system_drive);
    HANDLE volume_handle = CreateFileA(volume_path, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
    if (volume_handle != INVALID_HANDLE_VALUE) {
      bytes_returned = 0;
      VOLUME_DISK_EXTENTS extents = {0};
      if (DeviceIoControl(volume_handle, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, NULL, 0, &extents, sizeof(extents), &bytes_returned, NULL)) {
        if (extents.NumberOfDiskExtents > 0 && (int)extents.Extents[0].DiskNumber == disk_number) {
          info->is_system = true;
          info->is_boot = true;
        }
      }
      CloseHandle(volume_handle);
    }
  }
  CloseHandle(device_handle);
  return ERR_OK;
}
error_code_t disk_scanner_scan(disk_scan_result_t *result) {
  if (!result)
    return ERR_INVALID_ARG;
  memset(result, 0, sizeof(disk_scan_result_t));
  for (int disk_index = 0; disk_index < MAX_DISKS; disk_index++) {
    disk_info_t info;
    if (get_disk_info_windows(disk_index, &info) == ERR_OK && info.size_bytes > 0)
      result->disks[result->count++] = info;
  }
  return (result->count > 0) ? ERR_OK : ERR_OPEN_DEVICE;
}
error_code_t disk_scanner_get_info(const char *device_path, disk_info_t *info) {
  if (!device_path || !info)
    return ERR_INVALID_ARG;
  int disk_number = -1;
  if (sscanf(device_path, "\\\\.\\PhysicalDrive%d", &disk_number) == 1)
    return get_disk_info_windows(disk_number, info);
  return ERR_INVALID_ARG;
}
#else
#include <dirent.h>
#include <sys/sysmacros.h>
#include <mntent.h>
static bool read_sysfs_string(const char *file_path, char *output_buffer, size_t buffer_size) {
  FILE *file = fopen(file_path, "r");
  if (!file)
    return false;
  if (fgets(output_buffer, (int)buffer_size, file) != NULL) {
    size_t length = strlen(output_buffer);
    if (length && output_buffer[length - 1] == '\n')
      output_buffer[length - 1] = '\0';
    fclose(file);
    return true;
  }
  fclose(file);
  return false;
}
static uint64_t read_sysfs_uint64(const char *file_path) {
  char value_buffer[64];
  if (read_sysfs_string(file_path, value_buffer, sizeof(value_buffer)))
    return strtoull(value_buffer, NULL, 10);
  return 0;
}
static disk_type_t detect_disk_type_linux(const char *device_name) {
  char sys_path[512];
  char temp_buffer[256];
  if (strncmp(device_name, "nvme", 4) == 0)
    return DISK_TYPE_NVME;
  if (strncmp(device_name, "mmcblk", 6) == 0)
    return DISK_TYPE_SD_CARD;
  if (strncmp(device_name, "sr", 2) == 0)
    return DISK_TYPE_OPTICAL;
  snprintf(sys_path, sizeof(sys_path), "/sys/block/%s/removable", device_name);
  if (read_sysfs_string(sys_path, temp_buffer, sizeof(temp_buffer)) && temp_buffer[0] == '1')
    return DISK_TYPE_USB;
  snprintf(sys_path, sizeof(sys_path), "/sys/block/%s/device/../../driver", device_name);
  char resolved[PATH_MAX];
  if (realpath(sys_path, resolved) && strstr(resolved, "usb"))
    return DISK_TYPE_USB;
  snprintf(sys_path, sizeof(sys_path), "/sys/block/%s/queue/rotational", device_name);
  if (read_sysfs_string(sys_path, temp_buffer, sizeof(temp_buffer)))
    return (temp_buffer[0] == '0') ? DISK_TYPE_SSD : DISK_TYPE_HDD;
  return DISK_TYPE_UNKNOWN;
}
static bool is_real_disk(const char *name) {
  if (strncmp(name, "loop", 4) == 0)
    return false;
  if (strncmp(name, "ram", 3) == 0)
    return false;
  if (strncmp(name, "dm-", 3) == 0)
    return false;
  if (strncmp(name, "zram", 4) == 0)
    return false;
  return true;
}
static void check_system_disk(disk_info_t *info) {
  FILE *mounts_file = setmntent("/proc/mounts", "r");
  if (!mounts_file)
    return;
  struct mntent *mount_entry;
  char device_base[64];
  snprintf(device_base, sizeof(device_base), "%s", info->device_path);
  while ((mount_entry = getmntent(mounts_file)) != NULL) {
    if ((strcmp(mount_entry->mnt_dir, "/") == 0 || strcmp(mount_entry->mnt_dir, "/boot") == 0) &&
        strstr(mount_entry->mnt_fsname, device_base) != NULL) {
      info->is_system = true;
      info->is_boot = true;
      break;
    }
  }
  endmntent(mounts_file);
}
error_code_t disk_scanner_scan(disk_scan_result_t *result) {
  if (!result)
    return ERR_INVALID_ARG;
  memset(result, 0, sizeof(disk_scan_result_t));
  DIR *sys_block_dir = opendir("/sys/block");
  if (!sys_block_dir)
    return ERR_OPEN_DEVICE;
  struct dirent *entry;
  while ((entry = readdir(sys_block_dir)) != NULL && result->count < MAX_DISKS) {
    if (entry->d_name[0] == '.')
      continue;
    if (!is_real_disk(entry->d_name))
      continue;
    disk_info_t *info = &result->disks[result->count];
    memset(info, 0, sizeof(disk_info_t));
    snprintf(info->device_path, sizeof(info->device_path), "/dev/%s", entry->d_name);
    char sys_path[512];
    snprintf(sys_path, sizeof(sys_path), "/sys/block/%s/size", entry->d_name);
    uint64_t sector_count = read_sysfs_uint64(sys_path);
    info->size_bytes = sector_count * 512;
    if (info->size_bytes == 0)
      continue;
    snprintf(sys_path, sizeof(sys_path), "/sys/block/%s/queue/hw_sector_size", entry->d_name);
    info->sector_size = (uint32_t)read_sysfs_uint64(sys_path);
    if (info->sector_size == 0)
      info->sector_size = 512;
    info->type = detect_disk_type_linux(entry->d_name);
    snprintf(sys_path, sizeof(sys_path), "/sys/block/%s/removable", entry->d_name);
    char removable_buffer[8];
    if (read_sysfs_string(sys_path, removable_buffer, sizeof(removable_buffer)))
      info->is_removable = (removable_buffer[0] == '1');
    snprintf(sys_path, sizeof(sys_path), "/sys/block/%s/device/vendor", entry->d_name);
    read_sysfs_string(sys_path, info->vendor, sizeof(info->vendor));
    snprintf(sys_path, sizeof(sys_path), "/sys/block/%s/device/model", entry->d_name);
    read_sysfs_string(sys_path, info->model, sizeof(info->model));
    if (info->type == DISK_TYPE_USB)
      snprintf(info->bus_type, sizeof(info->bus_type), "USB");
    else if (info->type == DISK_TYPE_NVME)
      snprintf(info->bus_type, sizeof(info->bus_type), "NVMe");
    else if (info->type == DISK_TYPE_SD_CARD)
      snprintf(info->bus_type, sizeof(info->bus_type), "SD/MMC");
    else
      snprintf(info->bus_type, sizeof(info->bus_type), "SATA/ATA");
    info->disk_number = result->count;
    check_system_disk(info);
    result->count++;
  }
  closedir(sys_block_dir);
  return (result->count > 0) ? ERR_OK : ERR_OPEN_DEVICE;
}
error_code_t disk_scanner_get_info(const char *device_path, disk_info_t *info) {
  if (!device_path || !info)
    return ERR_INVALID_ARG;
  disk_scan_result_t scan_result;
  error_code_t scan_error = disk_scanner_scan(&scan_result);
  if (scan_error != ERR_OK)
    return scan_error;
  for (int index = 0; index < scan_result.count; index++) {
    if (strcmp(scan_result.disks[index].device_path, device_path) == 0) {
      *info = scan_result.disks[index];
      return ERR_OK;
    }
  }
  return ERR_OPEN_DEVICE;
}
#endif
void disk_scanner_print_list(const disk_scan_result_t *result) {
  if (!result || result->count == 0) {
    printf("No disks found.\n");
    return;
  }
  printf("\n--- Available disks ---\n");
  printf("#  Device                  Size       Type     Bus       Status\n");
  for (int index = 0; index < result->count; index++) {
    const disk_info_t *disk = &result->disks[index];
    char size_string[16];
    format_bytes(disk->size_bytes, size_string, sizeof(size_string));
    const char *status_text;
    if (disk->is_system || disk->is_boot)
      status_text = "SYSTEM";
    else if (disk->is_removable)
      status_text = "Removable";
    else
      status_text = "Fixed";
    char device_display[25];
    if (strlen(disk->device_path) > 24)
      snprintf(device_display, sizeof(device_display), "..%s", disk->device_path + strlen(disk->device_path) - 22);
    else
      snprintf(device_display, sizeof(device_display), "%s", disk->device_path);
    printf("%2d  %-24s %10s %-8s %-8s %s\n", index + 1, device_display, size_string, disk_type_to_string(disk->type), disk->bus_type, status_text);
  }
  printf("\nTotal: %d disk(s) found\n\n", result->count);
}
void disk_scanner_print_detail(const disk_info_t *info) {
  if (!info)
    return;
  char size_string[32];
  format_bytes(info->size_bytes, size_string, sizeof(size_string));
  printf("\n--- Disk Information ---\n");
  printf("Device Path:   %s\n", info->device_path);
  printf("Vendor:        %s\n", strlen(info->vendor) ? info->vendor : "(unknown)");
  printf("Model:         %s\n", strlen(info->model) ? info->model : "(unknown)");
  printf("Size:          %s\n", size_string);
  printf("Sector Size:   %u\n", info->sector_size);
  printf("Type:          %s\n", disk_type_to_string(info->type));
  printf("Bus Type:      %s\n", info->bus_type);
  printf("Removable:     %s\n", info->is_removable ? "Yes" : "No");
  printf("System Disk:   %s\n", info->is_system ? "YES - DO NOT WIPE!" : "No");
  printf("Boot Disk:     %s\n", info->is_boot ? "YES - DO NOT WIPE!" : "No");
  printf("\n");
  if (info->is_system || info->is_boot)
    printf("*** WARNING: This is a SYSTEM/BOOT disk! Wiping will make system "
           "unbootable! ***\n\n");
}
