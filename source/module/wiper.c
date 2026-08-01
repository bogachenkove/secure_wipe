#include "wiper.h"
#include "random_gen.h"
#include "platform.h"
static uint8_t *wipe_buffer = NULL;
typedef enum { PATTERN_ZERO, PATTERN_RANDOM } pattern_type_t;
static void fill_buffer_with_pattern(uint8_t *buffer, size_t buffer_size, pattern_type_t pattern_type, const uint8_t *pattern_data,
                                     size_t pattern_length) {
  (void)pattern_data;
  (void)pattern_length;
  switch (pattern_type) {
  case PATTERN_ZERO:
    memset(buffer, 0x00, buffer_size);
    break;
  case PATTERN_RANDOM:
    random_fill(buffer, buffer_size);
    break;
  default:
    break;
  }
}
static error_code_t single_pass(device_t *device, pattern_type_t pattern_type, const uint8_t *pattern_data, size_t pattern_length,
                                const analysis_result_t *bad_sectors, progress_callback_t progress, int pass_number, int total_passes,
                                const char *description) {
  if (!device || !device->is_open || !wipe_buffer)
    return ERR_INVALID_ARG;
  const device_io_ops_t *io_operations = device_io_get_ops();
  uint64_t current_sector = 0;
  int last_percent = -1;
  uint64_t write_errors = 0;
  char description_buffer[128];
  snprintf(description_buffer, sizeof(description_buffer), "Pass %d/%d: %s", pass_number, total_passes, description);
  LOG_INFO("Starting %s", description_buffer);
  while (current_sector < device->sector_count) {
    uint32_t sectors_to_write = (uint32_t)global_buffer_sectors;
    if (current_sector + sectors_to_write > device->sector_count)
      sectors_to_write = (uint32_t)(device->sector_count - current_sector);
    size_t buffer_size = (size_t)sectors_to_write * device->sector_size;
    if (pattern_type == PATTERN_RANDOM) {
      if (random_fill(wipe_buffer, buffer_size) != ERR_OK) {
        LOG_ERROR("Random fill failed at sector %llu", (unsigned long long)current_sector);
        return ERR_RANDOM_GEN;
      }
    } else {
      fill_buffer_with_pattern(wipe_buffer, buffer_size, pattern_type, pattern_data, pattern_length);
    }
    if (io_operations->write_sectors(device, current_sector, sectors_to_write, wipe_buffer) != ERR_OK) {
      LOG_WARN("Block write failed at sector %llu, switching to sector-by-sector", (unsigned long long)current_sector);
      uint64_t consecutive_errors = 0;
      const uint64_t MAX_CONSECUTIVE_ERRORS = 100;
      for (uint32_t sector_offset = 0; sector_offset < sectors_to_write; sector_offset++) {
        uint64_t target_sector = current_sector + sector_offset;
        if (bad_sectors && analyzer_is_bad_sector(bad_sectors, target_sector))
          continue;
        if (pattern_type == PATTERN_RANDOM)
          random_fill(wipe_buffer, device->sector_size);
        else
          fill_buffer_with_pattern(wipe_buffer, device->sector_size, pattern_type, pattern_data, pattern_length);
        if (io_operations->write_sectors(device, target_sector, 1, wipe_buffer) != ERR_OK) {
          write_errors++;
          consecutive_errors++;
          if (consecutive_errors <= 10)
            log_bad_sector(target_sector, "write");
          else if (consecutive_errors == 11)
            LOG_WARN("Too many consecutive write errors, further errors will "
                     "not be logged individually");
        } else {
          consecutive_errors = 0;
        }
        if (consecutive_errors >= MAX_CONSECUTIVE_ERRORS) {
          LOG_ERROR("Aborting: %llu consecutive write errors (device may be "
                    "write-protected)",
                    (unsigned long long)consecutive_errors);
          return ERR_WRITE_DEVICE;
        }
      }
    }
    current_sector += sectors_to_write;
    if (progress) {
      int current_percent = (int)((current_sector * 100) / device->sector_count);
      if (current_percent != last_percent) {
        progress(current_sector, device->sector_count, pass_number, description_buffer);
        last_percent = current_percent;
      }
    }
  }
  io_operations->flush(device);
  if (write_errors > 0)
    LOG_WARN("%s - completed with %llu write errors", description_buffer, (unsigned long long)write_errors);
  else
    LOG_INFO("%s - completed successfully", description_buffer);
  return ERR_OK;
}
error_code_t wiper_init(void) {
  if (wipe_buffer)
    return ERR_OK;
  wipe_buffer = (uint8_t *)aligned_alloc(global_buffer_size);
  if (!wipe_buffer)
    return ERR_MEMORY;
  LOG_INFO("Wiper module initialized (buffer: %zu bytes, %zu sectors)", global_buffer_size, global_buffer_sectors);
  return ERR_OK;
}
void wiper_cleanup(void) {
  if (wipe_buffer) {
    memset(wipe_buffer, 0, global_buffer_size);
    aligned_free(wipe_buffer);
    wipe_buffer = NULL;
    LOG_INFO("Wiper module cleaned up");
  }
}
const char *wiper_method_name(wipe_method_t method) {
  switch (method) {
  case WIPE_METHOD_ZERO:
    return "Zero Fill";
  case WIPE_METHOD_RANDOM:
    return "Random Fill";
  default:
    return "Unknown";
  }
}
int wiper_method_passes(wipe_method_t method) {
  switch (method) {
  case WIPE_METHOD_ZERO:
    return 1;
  case WIPE_METHOD_RANDOM:
    return 1;
  default:
    return 0;
  }
}
error_code_t wiper_execute(device_t *device, const wipe_config_t *config, wipe_stats_t *stats) {
  if (!device || !device->is_open || !config)
    return ERR_INVALID_ARG;
  if (!wipe_buffer) {
    LOG_ERROR("Wiper not initialized");
    return ERR_MEMORY;
  }
  if (stats) {
    memset(stats, 0, sizeof(wipe_stats_t));
    stats->start_time = time(NULL);
    stats->total_passes = config->passes;
  }
  LOG_INFO("Starting secure wipe: %s on %s", wiper_method_name(config->method), device->path);
  error_code_t result_code = ERR_OK;
  wiper_method_func method_function = NULL;
  switch (config->method) {
  case WIPE_METHOD_ZERO:
    method_function = wiper_method_zero;
    break;
  case WIPE_METHOD_RANDOM:
    method_function = wiper_method_random;
    break;
  default:
    return ERR_INVALID_ARG;
  }
  result_code = method_function(device, config->passes, config->progress);
  if (stats) {
    stats->end_time = time(NULL);
    if (result_code == ERR_OK)
      stats->sectors_wiped = device->sector_count * stats->total_passes;
  }
  if (result_code == ERR_OK)
    LOG_INFO("Wipe completed successfully");
  else
    LOG_ERROR("Wipe failed: %s", error_to_string(result_code));
  return result_code;
}
error_code_t wiper_method_zero(device_t *device, uint32_t passes, progress_callback_t progress) {
  (void)passes;
  uint8_t zero_byte = 0x00;
  return single_pass(device, PATTERN_ZERO, &zero_byte, 1, NULL, progress, 1, 1, "Zero fill (0x00)");
}
error_code_t wiper_method_random(device_t *device, uint32_t passes, progress_callback_t progress) {
  for (uint32_t pass_index = 1; pass_index <= passes; pass_index++) {
    char description[64];
    snprintf(description, sizeof(description), "Random fill (pass %u/%u)", pass_index, passes);
    error_code_t error_code = single_pass(device, PATTERN_RANDOM, NULL, 0, NULL, progress, (int)pass_index, (int)passes, description);
    if (error_code != ERR_OK)
      return error_code;
  }
  return ERR_OK;
}
static void overwrite_sector_range(device_t *device, uint64_t start_sector, uint32_t sector_count, uint8_t fill_byte) {
  if (!device || !device->is_open || device->read_only)
    return;
  const device_io_ops_t *io_operations = device_io_get_ops();
  size_t buffer_size = (size_t)sector_count * SECTOR_SIZE;
  uint8_t *buffer = (uint8_t *)aligned_alloc(buffer_size);
  if (!buffer) {
    LOG_ERROR("Failed to allocate buffer for partition table destruction");
    return;
  }
  memset(buffer, fill_byte, buffer_size);
  if (io_operations->write_sectors(device, start_sector, sector_count, buffer) != ERR_OK) {
    LOG_WARN("Block write failed, trying sector-by-sector");
    for (uint32_t offset = 0; offset < sector_count; offset++) {
      memset(buffer, fill_byte, SECTOR_SIZE);
      io_operations->write_sectors(device, start_sector + offset, 1, buffer);
    }
  }
  aligned_free(buffer);
}
error_code_t wiper_zero_partition_table(device_t *device) {
  if (!device || !device->is_open)
    return ERR_INVALID_ARG;
  if (device->read_only) {
    LOG_ERROR("Device opened read-only, cannot destroy partition table");
    return ERR_PERMISSION;
  }
  LOG_INFO("Destroying partition table (MBR+GPT) with zeros");
  uint32_t sectors_to_write = 34;
  if (sectors_to_write > device->sector_count)
    sectors_to_write = (uint32_t)device->sector_count;
  overwrite_sector_range(device, 0, sectors_to_write, 0x00);
  if (device->sector_count > 33) {
    uint64_t backup_start = device->sector_count - 33;
    LOG_INFO("Overwriting backup GPT (sectors %llu-%llu) with zeros", (unsigned long long)backup_start,
             (unsigned long long)(device->sector_count - 1));
    overwrite_sector_range(device, backup_start, 33, 0x00);
  }
  device_io_get_ops()->flush(device);
  LOG_INFO("Partition table destruction complete");
  return ERR_OK;
}
static void overwrite_metadata_range(device_t *device, uint64_t start_sector, uint64_t end_sector, int pass_index, progress_callback_t progress,
                                     const char *phase_prefix) {
  const device_io_ops_t *io_operations = device_io_get_ops();
  uint8_t *buffer = (uint8_t *)aligned_alloc(global_buffer_size);
  if (!buffer)
    return;
  uint64_t sector_index = start_sector;
  while (sector_index < end_sector) {
    uint32_t sectors_to_write = (uint32_t)global_buffer_sectors;
    if (sector_index + sectors_to_write > end_sector)
      sectors_to_write = (uint32_t)(end_sector - sector_index);
    random_fill(buffer, (size_t)sectors_to_write * SECTOR_SIZE);
    if (io_operations->write_sectors(device, sector_index, sectors_to_write, buffer) != ERR_OK) {
      for (uint32_t offset = 0; offset < sectors_to_write; offset++) {
        random_fill(buffer, SECTOR_SIZE);
        io_operations->write_sectors(device, sector_index + offset, 1, buffer);
      }
    }
    sector_index += sectors_to_write;
    if (progress)
      progress(sector_index, end_sector, pass_index, phase_prefix);
  }
  aligned_free(buffer);
}
static error_code_t wiper_destroy_filesystem_metadata(device_t *device, int passes, progress_callback_t progress) {
  if (!device || !device->is_open || device->read_only || passes <= 0)
    return ERR_INVALID_ARG;
  LOG_INFO("Destroying filesystem metadata (%d passes)", passes);
  uint64_t metadata_sectors = 8192;
  if (metadata_sectors > device->sector_count / 2)
    metadata_sectors = device->sector_count / 2;
  const device_io_ops_t *io_operations = device_io_get_ops();
  for (int pass_index = 1; pass_index <= passes; pass_index++) {
    overwrite_metadata_range(device, 0, metadata_sectors, pass_index, progress, "Destroying metadata (start)");
    if (device->sector_count > metadata_sectors) {
      uint64_t end_start = device->sector_count - metadata_sectors;
      overwrite_metadata_range(device, end_start, device->sector_count, pass_index, progress, "Destroying metadata (end)");
    }
    io_operations->flush(device);
  }
  LOG_INFO("Filesystem metadata destruction complete");
  return ERR_OK;
}
#ifdef _WIN32
static void dismount_volumes_on_disk(int disk_number) {
  wchar_t volume_name[MAX_PATH];
  HANDLE find_handle = FindFirstVolumeW(volume_name, MAX_PATH);
  if (find_handle == INVALID_HANDLE_VALUE)
    return;
  do {
    wchar_t device_path[MAX_PATH];
    DWORD chars_returned;
    if (GetVolumePathNamesForVolumeNameW(volume_name, device_path, MAX_PATH, &chars_returned)) {
      HANDLE volume = CreateFileW(volume_name, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
      if (volume != INVALID_HANDLE_VALUE) {
        VOLUME_DISK_EXTENTS extents = {0};
        DWORD bytes;
        if (DeviceIoControl(volume, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, NULL, 0, &extents, sizeof(extents), &bytes, NULL)) {
          for (DWORD extent_index = 0; extent_index < extents.NumberOfDiskExtents; extent_index++) {
            if ((int)extents.Extents[extent_index].DiskNumber == disk_number) {
              DeviceIoControl(volume, FSCTL_DISMOUNT_VOLUME, NULL, 0, NULL, 0, &bytes, NULL);
              DeviceIoControl(volume, FSCTL_LOCK_VOLUME, NULL, 0, NULL, 0, &bytes, NULL);
              break;
            }
          }
        }
        CloseHandle(volume);
      }
    }
  } while (FindNextVolumeW(find_handle, volume_name, MAX_PATH));
  FindVolumeClose(find_handle);
}
#endif
bool wiper_try_remove_write_protection(device_t *device) {
  if (!device || !device->is_open)
    return false;
  LOG_INFO("Attempting to remove write protection...");
#ifdef _WIN32
  int disk_number = -1;
  if (sscanf(device->path, "\\\\.\\PhysicalDrive%d", &disk_number) == 1)
    dismount_volumes_on_disk(disk_number);
  DWORD bytes_returned;
  typedef struct {
    ULONG Version;
    BOOLEAN Persist;
    BYTE Reserved1[3];
    ULONGLONG Attributes;
    ULONGLONG AttributesMask;
    ULONG Reserved2[4];
  } SET_DISK_ATTRIBUTES;
#ifndef IOCTL_DISK_SET_DISK_ATTRIBUTES
#define IOCTL_DISK_SET_DISK_ATTRIBUTES 0x0007C0F4
#endif
#ifndef DISK_ATTRIBUTE_READ_ONLY
#define DISK_ATTRIBUTE_READ_ONLY 0x0000000000000002ULL
#endif
  SET_DISK_ATTRIBUTES attributes = {0};
  attributes.Version = sizeof(attributes);
  attributes.Persist = TRUE;
  attributes.AttributesMask = DISK_ATTRIBUTE_READ_ONLY;
  DeviceIoControl(device->handle, IOCTL_DISK_SET_DISK_ATTRIBUTES, &attributes, sizeof(attributes), NULL, 0, &bytes_returned, NULL);
  PREVENT_MEDIA_REMOVAL pmr = {FALSE};
  DeviceIoControl(device->handle, IOCTL_STORAGE_MEDIA_REMOVAL, &pmr, sizeof(pmr), NULL, 0, &bytes_returned, NULL);
  DeviceIoControl(device->handle, IOCTL_DISK_UPDATE_PROPERTIES, NULL, 0, NULL, 0, &bytes_returned, NULL);
  CloseHandle(device->handle);
  device->handle =
      CreateFileA(device->path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH, NULL);
  if (device->handle != INVALID_HANDLE_VALUE) {
    LOG_INFO("Device reopened with exclusive access");
    device->is_open = true;
    return true;
  } else {
    LOG_WARN("Failed to reopen device (error %lu)", GetLastError());
    device->is_open = false;
    return false;
  }
#else
  int readonly_flag = 0;
  if (ioctl(device->handle, BLKROSET, &readonly_flag) == 0) {
    LOG_INFO("Write protection removed via BLKROSET");
    return true;
  } else {
    LOG_WARN("BLKROSET failed: %s", strerror(errno));
    return false;
  }
#endif
}
error_code_t wiper_prepare_device(device_t *device, progress_callback_t progress) {
  if (!device || !device->is_open)
    return ERR_INVALID_ARG;
  LOG_INFO("Preparing device for secure wipe");
  if (device->read_only)
    return ERR_PERMISSION;
  if (!wiper_try_remove_write_protection(device))
    LOG_WARN("Could not remove write protection via system call, will attempt "
             "to write anyway");
  return wiper_destroy_filesystem_metadata(device, 3, progress);
}
