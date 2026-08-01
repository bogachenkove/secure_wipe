#include "analyzer.h"
#include "platform.h"
static error_code_t grow_uint64_array(uint64_t **array, size_t *capacity, size_t *count) {
  if (*count >= *capacity) {
    if (*capacity > SIZE_MAX / 2) {
      LOG_ERROR("Capacity overflow detected");
      return ERR_MEMORY;
    }
    size_t new_capacity = *capacity * 2;
    if (new_capacity > MAX_BAD_SECTORS)
      new_capacity = MAX_BAD_SECTORS;
    if (*count >= MAX_BAD_SECTORS) {
      LOG_WARN("Maximum bad sector count reached");
      return ERR_MEMORY;
    }
    uint64_t *new_array = (uint64_t *)realloc(*array, new_capacity * sizeof(uint64_t));
    if (!new_array)
      return ERR_MEMORY;
    *array = new_array;
    *capacity = new_capacity;
  }
  return ERR_OK;
}
error_code_t analyzer_init_result(analysis_result_t *result) {
  if (!result)
    return ERR_INVALID_ARG;
  memset(result, 0, sizeof(analysis_result_t));
  result->bad_sectors_capacity = 1024;
  result->bad_sectors = (uint64_t *)malloc(result->bad_sectors_capacity * sizeof(uint64_t));
  return result->bad_sectors ? ERR_OK : ERR_MEMORY;
}
void analyzer_free_result(analysis_result_t *result) {
  if (result) {
    free(result->bad_sectors);
    result->bad_sectors = NULL;
    result->bad_sectors_capacity = 0;
    result->bad_sector_count = 0;
  }
}
error_code_t analyzer_add_bad_sector(analysis_result_t *result, uint64_t sector) {
  if (!result)
    return ERR_INVALID_ARG;
  error_code_t grow_result = grow_uint64_array(&result->bad_sectors, &result->bad_sectors_capacity, &result->bad_sector_count);
  if (grow_result != ERR_OK)
    return grow_result;
  result->bad_sectors[result->bad_sector_count++] = sector;
  log_bad_sector(sector, "scan");
  return ERR_OK;
}
bool analyzer_is_bad_sector(const analysis_result_t *result, uint64_t sector) {
  if (!result || !result->bad_sectors)
    return false;
  for (uint64_t sector_index = 0; sector_index < result->bad_sector_count; sector_index++) {
    if (result->bad_sectors[sector_index] == sector)
      return true;
  }
  return false;
}
error_code_t analyzer_scan_device(device_t *device, analysis_result_t *result, progress_callback_t progress) {
  if (!device || !device->is_open || !result)
    return ERR_INVALID_ARG;
  if (device->sector_count == UINT64_MAX) {
    LOG_ERROR("Device size too large (UINT64_MAX sectors) – cannot scan safely");
    return ERR_INVALID_ARG;
  }
  LOG_INFO("Starting device analysis...");
  LOG_INFO("Total sectors to scan: %llu", (unsigned long long)device->sector_count);
  result->total_sectors = device->sector_count;
  result->total_bytes = device->size_bytes;
  result->readable_sectors = 0;
  uint8_t *read_buffer = (uint8_t *)aligned_alloc(global_buffer_size);
  if (!read_buffer)
    return ERR_MEMORY;
  const device_io_ops_t *io_operations = device_io_get_ops();
  uint64_t current_sector = 0;
  uint64_t last_progress_percent = 0;
  while (current_sector < device->sector_count) {
    uint32_t sectors_to_read = (uint32_t)global_buffer_sectors;
    if (current_sector > device->sector_count - sectors_to_read)
      sectors_to_read = (uint32_t)(device->sector_count - current_sector);
    if (io_operations->read_sectors(device, current_sector, sectors_to_read, read_buffer) == ERR_OK) {
      result->readable_sectors += sectors_to_read;
    } else {
      for (uint32_t sector_offset = 0; sector_offset < sectors_to_read; sector_offset++) {
        uint64_t check_sector = current_sector + sector_offset;
        if (io_operations->read_sectors(device, check_sector, 1, read_buffer) == ERR_OK)
          result->readable_sectors++;
        else
          analyzer_add_bad_sector(result, check_sector);
      }
    }
    current_sector += sectors_to_read;
    if (progress) {
      uint64_t current_percent = (current_sector * 100) / device->sector_count;
      if (current_percent > last_progress_percent) {
        progress(current_sector, device->sector_count, 0, "Analyzing");
        last_progress_percent = current_percent;
      }
    }
  }
  aligned_free(read_buffer);
  LOG_INFO("Analysis complete - Readable: %llu, Bad: %llu", (unsigned long long)result->readable_sectors,
           (unsigned long long)result->bad_sector_count);
  return ERR_OK;
}
void analyzer_print_report(const analysis_result_t *result) {
  if (!result)
    return;
  char total_size_string[32], readable_size_string[32];
  format_bytes(result->total_bytes, total_size_string, sizeof(total_size_string));
  format_bytes(result->readable_sectors * SECTOR_SIZE, readable_size_string, sizeof(readable_size_string));
  printf("\n========== ANALYSIS REPORT ==========\n");
  printf("Total capacity:     %s\n", total_size_string);
  printf("Total sectors:      %llu\n", (unsigned long long)result->total_sectors);
  printf("Readable sectors:   %llu\n", (unsigned long long)result->readable_sectors);
  printf("Readable data:      %s\n", readable_size_string);
  printf("Bad sectors:        %llu\n", (unsigned long long)result->bad_sector_count);
  if (result->bad_sector_count > 0) {
    printf("\nBad sector addresses (first 20):\n");
    uint64_t show_count = result->bad_sector_count > 20 ? 20 : result->bad_sector_count;
    for (uint64_t sector_index = 0; sector_index < show_count; sector_index++) {
      printf("  Sector %llu (offset 0x%llX)\n", (unsigned long long)result->bad_sectors[sector_index],
             (unsigned long long)(result->bad_sectors[sector_index] * SECTOR_SIZE));
    }
    if (result->bad_sector_count > 20)
      printf("  ... and %llu more\n", (unsigned long long)(result->bad_sector_count - 20));
  }
  printf("======================================\n\n");
}
uint64_t analyzer_verify_wipe(device_t *device, progress_callback_t progress) {
  if (!device || !device->is_open)
    return UINT64_MAX;
  if (device->sector_count == UINT64_MAX) {
    LOG_ERROR("Device size too large (UINT64_MAX sectors) – cannot verify safely");
    return UINT64_MAX;
  }
  LOG_INFO("Starting wipe verification...");
  uint8_t *read_buffer = (uint8_t *)aligned_alloc(global_buffer_size);
  if (!read_buffer)
    return UINT64_MAX;
  const device_io_ops_t *io_operations = device_io_get_ops();
  uint64_t error_count = 0;
  uint64_t current_sector = 0;
  uint64_t last_progress_percent = 0;
  while (current_sector < device->sector_count) {
    uint32_t sectors_to_read = (uint32_t)global_buffer_sectors;
    if (current_sector > device->sector_count - sectors_to_read)
      sectors_to_read = (uint32_t)(device->sector_count - current_sector);
    if (io_operations->read_sectors(device, current_sector, sectors_to_read, read_buffer) != ERR_OK) {
      for (uint32_t sector_offset = 0; sector_offset < sectors_to_read; sector_offset++) {
        uint64_t check_sector = current_sector + sector_offset;
        if (io_operations->read_sectors(device, check_sector, 1, read_buffer) != ERR_OK) {
          error_count++;
          log_bad_sector(check_sector, "verification");
        }
      }
    }
    current_sector += sectors_to_read;
    if (progress) {
      uint64_t current_percent = (current_sector * 100) / device->sector_count;
      if (current_percent > last_progress_percent) {
        progress(current_sector, device->sector_count, 0, "Verifying");
        last_progress_percent = current_percent;
      }
    }
  }
  aligned_free(read_buffer);
  LOG_INFO("Verification complete. Errors: %llu", (unsigned long long)error_count);
  return error_count;
}
error_code_t analyzer_init_extended_result(extended_analysis_result_t *result) {
  if (!result)
    return ERR_INVALID_ARG;
  memset(result, 0, sizeof(extended_analysis_result_t));
  error_code_t base_error = analyzer_init_result(&result->base);
  if (base_error != ERR_OK)
    return base_error;
  result->wp_capacity = 1024;
  result->write_protected = (uint64_t *)malloc(result->wp_capacity * sizeof(uint64_t));
  if (!result->write_protected) {
    analyzer_free_result(&result->base);
    return ERR_MEMORY;
  }
  result->first_wp_sector = UINT64_MAX;
  result->last_wp_sector = 0;
  return ERR_OK;
}
void analyzer_free_extended_result(extended_analysis_result_t *result) {
  if (result) {
    analyzer_free_result(&result->base);
    free(result->write_protected);
    result->write_protected = NULL;
    result->wp_capacity = 0;
    result->wp_count = 0;
  }
}
static error_code_t add_write_protected_sector(extended_analysis_result_t *result, uint64_t sector) {
  if (!result)
    return ERR_INVALID_ARG;
  error_code_t grow_result = grow_uint64_array(&result->write_protected, &result->wp_capacity, &result->wp_count);
  if (grow_result != ERR_OK)
    return grow_result;
  result->write_protected[result->wp_count++] = sector;
  if (sector < result->first_wp_sector)
    result->first_wp_sector = sector;
  if (sector > result->last_wp_sector)
    result->last_wp_sector = sector;
  result->has_wp_regions = true;
  return ERR_OK;
}
static int test_single_sector_write(device_t *device, const device_io_ops_t *io_operations, uint64_t sector, uint8_t *write_buffer,
                                    uint8_t *read_buffer, uint8_t *backup_buffer) {
  if (backup_buffer)
    io_operations->read_sectors(device, sector, 1, backup_buffer);
  for (int byte_index = 0; byte_index < SECTOR_SIZE; byte_index++)
    write_buffer[byte_index] = (uint8_t)((sector ^ 0xAA ^ byte_index) & 0xFF);
  if (io_operations->write_sectors(device, sector, 1, write_buffer) != ERR_OK) {
    if (backup_buffer)
      io_operations->write_sectors(device, sector, 1, backup_buffer);
    return 1;
  }
  if (io_operations->read_sectors(device, sector, 1, read_buffer) != ERR_OK) {
    if (backup_buffer)
      io_operations->write_sectors(device, sector, 1, backup_buffer);
    return 2;
  }
  if (memcmp(write_buffer, read_buffer, SECTOR_SIZE) != 0) {
    if (backup_buffer)
      io_operations->write_sectors(device, sector, 1, backup_buffer);
    return 3;
  }
  if (backup_buffer)
    io_operations->write_sectors(device, sector, 1, backup_buffer);
  return 0;
}
static void process_sectors_with_fallback(device_t *device, uint64_t start_sector, uint32_t sector_count, uint8_t *read_buffer,
                                          analysis_result_t *base_result, bool do_write_test, bool detect_wp,
                                          extended_analysis_result_t *extended_result, uint8_t *write_buffer, uint8_t *verify_buffer,
                                          uint8_t *backup_buffer) {
  const device_io_ops_t *io_operations = device_io_get_ops();
  for (uint32_t offset = 0; offset < sector_count; offset++) {
    uint64_t current_sector = start_sector + offset;
    if (io_operations->read_sectors(device, current_sector, 1, read_buffer) == ERR_OK) {
      base_result->readable_sectors++;
      if (do_write_test || detect_wp) {
        int write_result = test_single_sector_write(device, io_operations, current_sector, write_buffer, verify_buffer, backup_buffer);
        switch (write_result) {
        case 1:
          extended_result->write_errors++;
          add_write_protected_sector(extended_result, current_sector);
          break;
        case 2:
          extended_result->read_errors++;
          analyzer_add_bad_sector(base_result, current_sector);
          break;
        case 3:
          extended_result->verify_errors++;
          break;
        }
      }
    } else {
      extended_result->read_errors++;
      analyzer_add_bad_sector(base_result, current_sector);
    }
  }
}
error_code_t analyzer_scan_device_extended(device_t *device, extended_analysis_result_t *result, analyze_flags_t flags,
                                           progress_callback_t progress) {
  if (!device || !device->is_open || !result)
    return ERR_INVALID_ARG;
  if (device->sector_count == UINT64_MAX) {
    LOG_ERROR("Device size too large (UINT64_MAX sectors) – cannot scan safely");
    return ERR_INVALID_ARG;
  }
  bool do_write_test = (flags & ANALYZE_WRITE_TEST) != 0;
  bool detect_wp = (flags & ANALYZE_DETECT_WP) != 0 || do_write_test;
  if (do_write_test)
    LOG_WARN("=== WRITE TEST MODE - DATA WILL BE DESTROYED! ===");
  LOG_INFO("Starting extended analysis... Sectors: %llu", (unsigned long long)device->sector_count);
  result->base.total_sectors = device->sector_count;
  result->base.total_bytes = device->size_bytes;
  result->base.readable_sectors = 0;
  uint8_t *read_buffer = (uint8_t *)aligned_alloc(global_buffer_size);
  uint8_t *write_buffer = NULL, *verify_buffer = NULL, *backup_buffer = NULL;
  if (!read_buffer)
    return ERR_MEMORY;
  if (do_write_test || detect_wp) {
    write_buffer = aligned_alloc(SECTOR_SIZE);
    verify_buffer = aligned_alloc(SECTOR_SIZE);
    if (detect_wp && !do_write_test)
      backup_buffer = aligned_alloc(SECTOR_SIZE);
    if (!write_buffer || !verify_buffer) {
      aligned_free(read_buffer);
      if (write_buffer)
        aligned_free(write_buffer);
      if (verify_buffer)
        aligned_free(verify_buffer);
      if (backup_buffer)
        aligned_free(backup_buffer);
      return ERR_MEMORY;
    }
  }
  const device_io_ops_t *io_operations = device_io_get_ops();
  uint64_t current_sector = 0;
  uint64_t last_progress_percent = 0;
  while (current_sector < device->sector_count) {
    uint32_t sectors_to_process = (uint32_t)global_buffer_sectors;
    if (current_sector > device->sector_count - sectors_to_process)
      sectors_to_process = (uint32_t)(device->sector_count - current_sector);
    if (io_operations->read_sectors(device, current_sector, sectors_to_process, read_buffer) == ERR_OK) {
      result->base.readable_sectors += sectors_to_process;
      if (do_write_test || detect_wp) {
        uint32_t step = do_write_test ? 1 : 64;
        for (uint32_t offset = 0; offset < sectors_to_process; offset += step) {
          uint64_t test_sector = current_sector + offset;
          int write_result = test_single_sector_write(device, io_operations, test_sector, write_buffer, verify_buffer, backup_buffer);
          switch (write_result) {
          case 1:
            result->write_errors++;
            add_write_protected_sector(result, test_sector);
            break;
          case 2:
            result->read_errors++;
            analyzer_add_bad_sector(&result->base, test_sector);
            break;
          case 3:
            result->verify_errors++;
            break;
          }
        }
      }
    } else {
      process_sectors_with_fallback(device, current_sector, sectors_to_process, read_buffer, &result->base, do_write_test, detect_wp, result,
                                    write_buffer, verify_buffer, backup_buffer);
    }
    current_sector += sectors_to_process;
    if (progress) {
      uint64_t current_percent = (current_sector * 100) / device->sector_count;
      if (current_percent > last_progress_percent) {
        progress(current_sector, device->sector_count, 0, do_write_test ? "Write testing" : "Analyzing");
        last_progress_percent = current_percent;
      }
    }
  }
  aligned_free(read_buffer);
  if (write_buffer)
    aligned_free(write_buffer);
  if (verify_buffer)
    aligned_free(verify_buffer);
  if (backup_buffer)
    aligned_free(backup_buffer);
  LOG_INFO("Extended analysis complete - Readable: %llu, Read errors: %llu, "
           "Write errors: %llu, Verify errors: %llu",
           (unsigned long long)result->base.readable_sectors, (unsigned long long)result->read_errors, (unsigned long long)result->write_errors,
           (unsigned long long)result->verify_errors);
  return ERR_OK;
}
void analyzer_print_extended_report(const extended_analysis_result_t *result) {
  if (!result)
    return;
  analyzer_print_report(&result->base);
  printf("========= EXTENDED ANALYSIS =========\n");
  printf("Read errors:        %llu\n", (unsigned long long)result->read_errors);
  printf("Write errors:       %llu\n", (unsigned long long)result->write_errors);
  printf("Verify errors:      %llu\n", (unsigned long long)result->verify_errors);
  if (result->has_wp_regions) {
    printf("\n*** WRITE-PROTECTED REGIONS DETECTED ***\n");
    printf("WP sectors count:   %llu\n", (unsigned long long)result->wp_count);
    printf("First WP sector:    %llu (offset 0x%llX, ~%.2f MB)\n", (unsigned long long)result->first_wp_sector,
           (unsigned long long)(result->first_wp_sector * SECTOR_SIZE), (double)(result->first_wp_sector * SECTOR_SIZE) / (1024.0 * 1024.0));
    printf("Last WP sector:     %llu (offset 0x%llX, ~%.2f MB)\n", (unsigned long long)result->last_wp_sector,
           (unsigned long long)(result->last_wp_sector * SECTOR_SIZE), (double)(result->last_wp_sector * SECTOR_SIZE) / (1024.0 * 1024.0));
    if (result->wp_count > 0) {
      printf("\nFirst 10 WP sectors:\n");
      uint64_t show_count = result->wp_count > 10 ? 10 : result->wp_count;
      for (uint64_t sector_index = 0; sector_index < show_count; sector_index++)
        printf("  Sector %llu\n", (unsigned long long)result->write_protected[sector_index]);
      if (result->wp_count > 10)
        printf("  ... and %llu more\n", (unsigned long long)(result->wp_count - 10));
    }
  } else {
    printf("\nNo write-protected regions detected.\n");
  }
  printf("======================================\n\n");
}
bool analyzer_quick_wp_check(device_t *device, uint64_t test_sectors, uint64_t *first_wp_sector) {
  if (!device || !device->is_open)
    return false;
  if (test_sectors == 0)
    test_sectors = 8192;
  if (test_sectors > device->sector_count)
    test_sectors = device->sector_count;
  LOG_INFO("Quick write-protection check (first %llu sectors)...", (unsigned long long)test_sectors);
  uint8_t *write_buffer = aligned_alloc(SECTOR_SIZE);
  uint8_t *read_buffer = aligned_alloc(SECTOR_SIZE);
  uint8_t *backup_buffer = aligned_alloc(SECTOR_SIZE);
  if (!write_buffer || !read_buffer || !backup_buffer) {
    if (write_buffer)
      aligned_free(write_buffer);
    if (read_buffer)
      aligned_free(read_buffer);
    if (backup_buffer)
      aligned_free(backup_buffer);
    return false;
  }
  const device_io_ops_t *io_operations = device_io_get_ops();
  bool found_wp = false;
  for (uint64_t sector = 0; sector < test_sectors && !found_wp; sector += 128) {
    int write_result = test_single_sector_write(device, io_operations, sector, write_buffer, read_buffer, backup_buffer);
    if (write_result == 1) {
      found_wp = true;
      if (first_wp_sector)
        *first_wp_sector = sector;
      LOG_WARN("Write-protection detected at sector %llu", (unsigned long long)sector);
    }
  }
  aligned_free(write_buffer);
  aligned_free(read_buffer);
  aligned_free(backup_buffer);
  if (!found_wp)
    LOG_INFO("No write-protection detected in tested range");
  return found_wp;
}
