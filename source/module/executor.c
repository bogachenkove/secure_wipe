#include "executor.h"
#include "device_io.h"
#include "analyzer.h"
#include "wiper.h"
#include "disk_scanner.h"
#include "platform.h"
#include "ui.h"
#include "ata_erase.h"
static error_code_t run_single_cycle(const program_config_t *config, analysis_result_t *analysis, int cycle_number) {
  device_t *device = NULL;
  extended_analysis_result_t *extended_analysis = NULL;
  disk_info_t *disk_info = NULL;
  wipe_config_t *wipe_config = NULL;
  wipe_stats_t *wipe_stats = NULL;
  error_code_t wipe_error = ERR_OK;
  error_code_t result_code = ERR_OK;
  bool use_extended = config->analyze_write_test || config->quick_wp_check;
  bool analysis_initialized = false;
  bool extended_analysis_initialized = false;
  device = (device_t *)calloc(1, sizeof(device_t));
  extended_analysis = (extended_analysis_result_t *)calloc(1, sizeof(extended_analysis_result_t));
  disk_info = (disk_info_t *)calloc(1, sizeof(disk_info_t));
  wipe_config = (wipe_config_t *)calloc(1, sizeof(wipe_config_t));
  wipe_stats = (wipe_stats_t *)calloc(1, sizeof(wipe_stats_t));
  if (!device || !extended_analysis || !disk_info || !wipe_config || !wipe_stats) {
    LOG_ERROR("Memory allocation failed");
    result_code = ERR_MEMORY;
    goto cleanup;
  }
  if (disk_scanner_get_info(config->device_path, disk_info) == ERR_OK) {
    if (cycle_number == 1)
      disk_scanner_print_detail(disk_info);
    if (!config->destroy_partition_table && (disk_info->is_system || disk_info->is_boot)) {
      fprintf(stderr, "\n*** CRITICAL: System disk - operation aborted.\n");
      result_code = ERR_PERMISSION;
      goto cleanup;
    }
  }
  if (cycle_number == 1 && !config->destroy_partition_table && !config->skip_analysis && !config->analyze_only && !use_extended) {
    device_t ata_device;
    error_code_t open_status = device_open(&ata_device, config->device_path, false);
    if (open_status == ERR_OK) {
      ata_security_info_t ata_info;
      if (ata_get_security_info(&ata_device, &ata_info) == ERR_OK && ata_info.supported) {
        if (prompt_ata_erase(&ata_info, config->device_path)) {
          ata_erase_type_t erase_type = ATA_ERASE_NORMAL;
          if (ata_info.enhanced_supported) {
            printf("Choose erase type (1=Enhanced, 2=Normal) [2]: ");
            fflush(stdout);
            char input[16];
            if (fgets(input, sizeof(input), stdin)) {
              if (atoi(input) == 1)
                erase_type = ATA_ERASE_ENHANCED;
            }
          }
          printf("\nStarting ATA Secure Erase...\n");
          error_code_t ata_result = ata_secure_erase(&ata_device, erase_type, progress_handler);
          device_close(&ata_device);
          if (ata_result == ERR_OK) {
            printf("\nATA Secure Erase completed successfully.\n");
            result_code = ERR_OK;
            goto cleanup;
          } else {
            printf("\nATA Secure Erase failed (error: %s). Falling back to "
                   "software wipe.\n",
                   error_to_string(ata_result));
          }
        }
      }
      device_close(&ata_device);
    } else {
      LOG_WARN("Could not open device for ATA security check (will use "
               "software wipe)");
    }
  }
  bool need_write_access = use_extended || config->destroy_partition_table;
  if (device_open(device, config->device_path, !need_write_access) != ERR_OK) {
    LOG_ERROR("Failed to open device");
    result_code = ERR_OPEN_DEVICE;
    goto cleanup;
  }
  if (is_device_mounted(config->device_path) && !config->destroy_partition_table) {
    LOG_ERROR("Device %s is currently mounted. Unmount it first.", config->device_path);
    device_close(device);
    result_code = ERR_PERMISSION;
    goto cleanup;
  }
  if (config->destroy_partition_table) {
    printf("\n--- Destroying partition table (MBR/GPT) with zeros ---\n");
    disk_info_t local_disk_info;
    if (disk_info->is_system == false && disk_info->is_boot == false && disk_info->device_path[0] == '\0') {
      if (disk_scanner_get_info(config->device_path, &local_disk_info) == ERR_OK)
        memcpy(disk_info, &local_disk_info, sizeof(disk_info_t));
    }
    if (disk_info->is_system || disk_info->is_boot) {
      printf("*** WARNING: This is a SYSTEM/BOOT disk! Wiping will make system "
             "unbootable! ***\n\n");
    } else {
      printf("This operation will erase the MBR/GPT partition table (first 34 "
             "and last 33 sectors).\n");
      printf("Data on the disk will become inaccessible, but the content "
             "itself will remain.\n");
    }
    printf("Type 'YES' (all caps) to confirm: ");
    fflush(stdout);
    char confirm[16] = {0};
    if (!fgets(confirm, sizeof(confirm), stdin) || strcmp(confirm, "YES\n") != 0) {
      printf("Operation cancelled.\n");
      device_close(device);
      result_code = ERR_PERMISSION;
      goto cleanup;
    }
    error_code_t error_code = wiper_zero_partition_table(device);
    device_close(device);
    if (error_code == ERR_OK)
      printf("Partition table destroyed successfully.\n");
    else
      printf("Failed to destroy partition table (error: %s).\n", error_to_string(error_code));
    result_code = (error_code == ERR_OK) ? ERR_OK : error_code;
    goto cleanup;
  }
  if (config->quick_wp_check) {
    printf("\n--- Quick write-protection check ---\n");
    uint64_t first_wp_sector = 0;
    if (analyzer_quick_wp_check(device, 16384, &first_wp_sector)) {
      printf("Write-protection detected at sector %llu (%.2f MB)\n", (unsigned long long)first_wp_sector,
             (double)(first_wp_sector * SECTOR_SIZE) / (1024.0 * 1024.0));
    } else {
      printf("No write-protection detected in first 8MB.\n");
    }
    if (config->analyze_only && !config->analyze_write_test) {
      device_close(device);
      goto after_analysis;
    }
  }
  if (config->skip_analysis && !config->analyze_only) {
    LOG_INFO("Skipping device analysis (--skip-analysis)");
    device_close(device);
    goto after_analysis;
  }
  if (use_extended) {
    if (analyzer_init_extended_result(extended_analysis) != ERR_OK) {
      LOG_ERROR("Init extended analysis failed");
      device_close(device);
      result_code = ERR_MEMORY;
      goto cleanup;
    }
    extended_analysis_initialized = true;
  } else {
    if (analyzer_init_result(analysis) != ERR_OK) {
      LOG_ERROR("Init analysis failed");
      device_close(device);
      result_code = ERR_MEMORY;
      goto cleanup;
    }
    analysis_initialized = true;
  }
  printf("\n--- PHASE 1: %s ---\n", use_extended ? "EXTENDED ANALYSIS" : "DEVICE ANALYSIS");
  if (config->analyze_write_test) {
    printf("*** Write test mode - data will be modified! ***\n");
    analyze_flags_t analysis_flags = ANALYZE_WRITE_TEST | ANALYZE_DETECT_WP;
    if (analyzer_scan_device_extended(device, extended_analysis, analysis_flags, progress_handler) != ERR_OK) {
      LOG_ERROR("Extended analysis failed");
      device_close(device);
      result_code = ERR_READ_DEVICE;
      goto cleanup;
    }
    analyzer_print_extended_report(extended_analysis);
    memcpy(analysis, &extended_analysis->base, sizeof(analysis_result_t));
  } else if (!config->skip_analysis) {
    if (analyzer_scan_device(device, analysis, progress_handler) != ERR_OK) {
      LOG_ERROR("Device analysis failed");
      device_close(device);
      result_code = ERR_READ_DEVICE;
      goto cleanup;
    }
    analyzer_print_report(analysis);
  }
  if (config->analyze_only) {
    printf("Analysis complete. Exiting.\n");
    device_close(device);
    goto after_analysis;
  }
  device_close(device);
after_analysis:
  uint32_t actual_passes;
  if (config->method == WIPE_METHOD_RANDOM)
    actual_passes = config->passes;
  else
    actual_passes = wiper_method_passes(config->method);
  if (cycle_number == 1 && !config->auto_confirm && !confirm_wipe(config->device_path, analysis->total_bytes, config->method, actual_passes)) {
    printf("Operation cancelled.\n");
    result_code = ERR_PERMISSION;
    goto cleanup;
  }
  if (device_open(device, config->device_path, false) != ERR_OK) {
    LOG_ERROR("Failed to open device for writing");
    result_code = ERR_OPEN_DEVICE;
    goto cleanup;
  }
  if (is_device_mounted(config->device_path)) {
    LOG_ERROR("Device %s became mounted after open, aborting to avoid FS corruption.", config->device_path);
    device_close(device);
    result_code = ERR_PERMISSION;
    goto cleanup;
  }
  uint8_t *test_buffer = (uint8_t *)aligned_alloc(SECTOR_SIZE);
  if (!test_buffer) {
    LOG_ERROR("Cannot allocate test buffer");
    device_close(device);
    result_code = ERR_MEMORY;
    goto cleanup;
  }
  memset(test_buffer, 0xAA, SECTOR_SIZE);
  if (device_write_sectors(device, 0, 1, test_buffer) != ERR_OK) {
    LOG_ERROR("Device write test failed. Device is write-protected or inaccessible.");
    aligned_free(test_buffer);
    device_close(device);
    result_code = ERR_WRITE_DEVICE;
    goto cleanup;
  }
  aligned_free(test_buffer);
  printf("\n--- PHASE 2: Destroy filesystem metadata ---\n");
  wiper_prepare_device(device, progress_handler);
  printf("\n--- PHASE 3: Secure wipe ---\n");
  memset(wipe_config, 0, sizeof(wipe_config_t));
  wipe_config->method = config->method;
  wipe_config->passes = config->passes;
  wipe_config->skip_bad_sectors = true;
  wipe_config->verify_after_wipe = config->verify;
  wipe_config->bad_sectors = analysis;
  wipe_config->progress = progress_handler;
  time_t start_time = time(NULL);
  wipe_error = wiper_execute(device, wipe_config, wipe_stats);
  time_t elapsed_time = time(NULL) - start_time;
  if (wipe_error == ERR_OK) {
    char time_string[64];
    format_time(elapsed_time, time_string, sizeof(time_string));
    printf("\n--- Wipe complete (cycle %d) ---\nTime: %s\nSectors wiped: "
           "%llu\nPasses: %llu\n",
           cycle_number, time_string, (unsigned long long)wipe_stats->sectors_wiped, (unsigned long long)wipe_stats->total_passes);
  } else {
    LOG_ERROR("Wipe failed");
    result_code = wipe_error;
  }
  if (config->verify && wipe_error == ERR_OK) {
    printf("\n--- PHASE 4: Verification ---\n");
    uint64_t verification_errors = analyzer_verify_wipe(device, progress_handler);
    if (verification_errors == 0)
      printf("Verification PASSED.\n");
    else if (verification_errors == UINT64_MAX)
      printf("Verification FAILED.\n");
    else
      printf("Verification completed with %llu errors.\n", (unsigned long long)verification_errors);
  }
  device_close(device);
  result_code = ERR_OK;
cleanup:
  if (use_extended && extended_analysis_initialized)
    analyzer_free_extended_result(extended_analysis);
  else if (analysis_initialized)
    analyzer_free_result(analysis);
  free(device);
  free(extended_analysis);
  free(disk_info);
  free(wipe_config);
  free(wipe_stats);
  return (result_code == ERR_OK) ? ERR_OK : ERR_UNKNOWN;
}
int run_wipe(const program_config_t *config, analysis_result_t *analysis) {
  error_code_t last_error = ERR_OK;
  int final_exit_code = 0;
  for (uint32_t cycle = 1; cycle <= config->cycles; cycle++) {
    if (config->cycles > 1) {
      printf("\n========== CYCLE %u / %u ==========\n", cycle, config->cycles);
      LOG_INFO("Starting wipe cycle %u of %u", cycle, config->cycles);
    }
    last_error = run_single_cycle(config, analysis, (int)cycle);
    if (last_error != ERR_OK) {
      final_exit_code = 1;
      break;
    }
    if (config->cycles > 1 && cycle < config->cycles) {
      printf("\n=== Cycle %u completed successfully. Starting next cycle... ===\n", cycle);
      analyzer_free_result(analysis);
      memset(analysis, 0, sizeof(analysis_result_t));
    }
  }
  return (final_exit_code == 0 && last_error == ERR_OK) ? 0 : 1;
}
int emergency_wipe(const program_config_t *config) {
  device_t device;
  error_code_t open_status = device_open(&device, config->device_path, false);
  if (open_status != ERR_OK) {
    fprintf(stderr, "Failed to open device %s: %s\n", config->device_path, error_to_string(open_status));
    return 1;
  }
  uint64_t sectors_to_write = config->emergency_sectors;
  uint64_t total_sectors = device.sector_count;
  if (sectors_to_write == 0) {
    sectors_to_write = total_sectors;
    char size_string[32];
    format_bytes(device.size_bytes, size_string, sizeof(size_string));
    printf("Emergency zero-write: FULL DISK (%llu sectors, %s)\n", (unsigned long long)total_sectors, size_string);
  } else {
    if (sectors_to_write > device.sector_count) {
      fprintf(stderr, "Requested %llu sectors, but device has only %llu sectors\n", (unsigned long long)sectors_to_write,
              (unsigned long long)device.sector_count);
      device_close(&device);
      return 1;
    }
    printf("Emergency zero-write: first %llu sectors, buffer %zu bytes\n", (unsigned long long)sectors_to_write, config->buffer_size);
  }
  uint64_t start_sector = config->resume_given ? config->resume_sector : 0;
  if (start_sector >= sectors_to_write) {
    printf("Resume sector %llu is beyond or equal to total sectors to write "
           "(%llu). Nothing to do.\n",
           (unsigned long long)start_sector, (unsigned long long)sectors_to_write);
    device_close(&device);
    return 0;
  }
  if (config->resume_given) {
    printf("Resuming: %llu / %llu sectors (%.1f%%)\n", (unsigned long long)start_sector, (unsigned long long)sectors_to_write,
           100.0 * start_sector / sectors_to_write);
  }
  size_t buffer_bytes = config->buffer_size;
  if (buffer_bytes % SECTOR_SIZE != 0) {
    fprintf(stderr, "Buffer size (%zu) must be multiple of sector size (%d)\n", buffer_bytes, SECTOR_SIZE);
    device_close(&device);
    return 1;
  }
  uint32_t sectors_per_buffer = (uint32_t)(buffer_bytes / SECTOR_SIZE);
  uint8_t *zero_buffer = (uint8_t *)aligned_alloc(buffer_bytes);
  if (!zero_buffer) {
    fprintf(stderr, "Failed to allocate %zu bytes for buffer\n", buffer_bytes);
    device_close(&device);
    return 1;
  }
  memset(zero_buffer, 0, buffer_bytes);
  uint64_t written_sectors = start_sector;
  printf("Progress: %llu / %llu sectors (%.1f%%)", (unsigned long long)written_sectors, (unsigned long long)sectors_to_write,
         100.0 * written_sectors / sectors_to_write);
  fflush(stdout);
  while (written_sectors < sectors_to_write) {
    uint32_t chunk_sectors = sectors_per_buffer;
    if (written_sectors + chunk_sectors > sectors_to_write)
      chunk_sectors = (uint32_t)(sectors_to_write - written_sectors);
    error_code_t write_status = device_write_sectors(&device, written_sectors, chunk_sectors, zero_buffer);
    if (write_status != ERR_OK) {
      fprintf(stderr, "\nWrite error at sector %llu: %s\n", (unsigned long long)written_sectors, error_to_string(write_status));
      aligned_free(zero_buffer);
      device_close(&device);
      return 1;
    }
    written_sectors += chunk_sectors;
    printf("\rProgress: %llu / %llu sectors (%.1f%%)", (unsigned long long)written_sectors, (unsigned long long)sectors_to_write,
           100.0 * written_sectors / sectors_to_write);
    fflush(stdout);
  }
  printf("\nEmergency wipe completed successfully.\n");
  aligned_free(zero_buffer);
  device_close(&device);
  return 0;
}
