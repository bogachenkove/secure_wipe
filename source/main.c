#include "module/metadata.h"
#include "module/platform.h"
#include "module/common.h"
#include "module/config.h"
#include "module/ui.h"
#include "module/executor.h"
#include "module/random_gen.h"
#include "module/wiper.h"
#include "module/disk_scanner.h"
#include "module/device_io.h"
#include "module/ata_erase.h"
#include "module/file_wiper.h"
int main(int argument_count, char *argument_vector[]) {
  platform_init();
  program_config_t configuration;
  config_default(&configuration);
  if (!parse_arguments(argument_count, argument_vector, &configuration)) {
    platform_cleanup();
    return 1;
  }
  if (!check_admin_privileges()) {
#ifdef _WIN32
    fprintf(stderr, "ERROR: This program requires Administrator privileges.\n");
    fprintf(stderr, "Please run as Administrator.\n");
#else
    fprintf(stderr, "ERROR: This program requires root privileges.\n");
    fprintf(stderr, "Please run with sudo.\n");
#endif
    platform_cleanup();
    return 1;
  }
  if (configuration.wipe_file_mode || configuration.wipe_dir_mode) {
    file_wipe_config_t file_config = {0};
    file_config.method = configuration.method;
    file_config.passes = configuration.passes;
    file_config.rename_before_delete = (global_default_rename_count > 0);
    file_config.rename_count = global_default_rename_count;
    file_config.preserve_timestamps = false;
    file_config.progress = progress_handler;
    error_code_t error = file_wipe_path(configuration.wipe_file_path, &file_config);
    if (error != ERR_OK)
      fprintf(stderr, "Failed to wipe: %s\n", error_to_string(error));
    else
      printf("Wipe completed successfully.\n");
    platform_cleanup();
    return (error == ERR_OK) ? 0 : 1;
  }
  if (configuration.emergency_mode) {
    int exit_code = emergency_wipe(&configuration);
    platform_cleanup();
    return exit_code;
  }
  if (configuration.ata_secure_erase) {
    device_t device;
    error_code_t open_status = device_open(&device, configuration.device_path, false);
    if (open_status != ERR_OK) {
      fprintf(stderr, "Failed to open device %s: %s\n", configuration.device_path, error_to_string(open_status));
      platform_cleanup();
      return 1;
    }
    ata_security_info_t info;
    if (ata_get_security_info(&device, &info) != ERR_OK) {
      fprintf(stderr, "ERROR: Cannot query ATA security features.\n");
      fprintf(stderr, "This may be because:\n");
      fprintf(stderr, "  - Device is connected via USB (many USB bridges block ATA commands)\n");
      fprintf(stderr, "  - Device is virtual or does not support ATA Secure Erase\n");
      fprintf(stderr, "  - Driver/antivirus is blocking low-level access\n");
      fprintf(stderr, "Try using standard wipe methods instead.\n");
      device_close(&device);
      platform_cleanup();
      return 1;
    }
    if (!info.supported) {
      fprintf(stderr, "ERROR: ATA Security not supported by this device.\n");
      device_close(&device);
      platform_cleanup();
      return 1;
    }
    ata_erase_type_t erase_type = configuration.ata_enhanced_erase ? ATA_ERASE_ENHANCED : ATA_ERASE_NORMAL;
    int exit_code = ata_secure_erase(&device, erase_type, progress_handler);
    device_close(&device);
    platform_cleanup();
    return exit_code;
  }
  if (configuration.list_disks) {
    disk_scan_result_t *scan_result = (disk_scan_result_t *)malloc(sizeof(disk_scan_result_t));
    if (!scan_result) {
      fprintf(stderr, "Memory allocation failed\n");
      platform_cleanup();
      return 1;
    }
    if (disk_scanner_scan(scan_result) == ERR_OK)
      disk_scanner_print_list(scan_result);
    else
      fprintf(stderr, "Failed to scan disks\n");
    free(scan_result);
    platform_cleanup();
    return 0;
  }
  if (configuration.select_disk || strlen(configuration.device_path) == 0) {
    if (!interactive_select_disk(&configuration)) {
      platform_cleanup();
      return 1;
    }
  }
  if (!global_no_log) {
    if (strlen(global_log_file_path) == 0) {
      const char *temporary_directory = NULL;
#ifdef _WIN32
      char temp_path_buffer[MAX_PATH_LEN];
      temporary_directory = getenv("TEMP");
      if (!temporary_directory) {
        DWORD length = GetTempPathA(sizeof(temp_path_buffer), temp_path_buffer);
        if (length > 0 && length < sizeof(temp_path_buffer))
          temporary_directory = temp_path_buffer;
        else {
          char system_drive[4] = "C:";
          GetEnvironmentVariableA("SystemDrive", system_drive, sizeof(system_drive));
          snprintf(temp_path_buffer, sizeof(temp_path_buffer), "%s\\Windows\\Temp", system_drive);
          temporary_directory = temp_path_buffer;
        }
      }
#else
      temporary_directory = "/tmp";
#endif
      time_t current_time = time(NULL);
      struct tm *time_info = localtime(&current_time);
      if (!time_info) {
        fprintf(stderr, "localtime failed\n");
        platform_cleanup();
        return 1;
      }
      char timestamp[32];
      strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", time_info);
      snprintf(global_log_file_path, sizeof(global_log_file_path), "%s/securewipe_%s.log", temporary_directory, timestamp);
    }
    log_init(global_log_file_path, configuration.verbose);
    LOG_INFO("Secure Wipe started on %s", configuration.device_path);
  } else {
    log_init(NULL, configuration.verbose);
    if (configuration.verbose)
      printf("Logging to file disabled (--no-log).\n");
  }
  if (random_init() != ERR_OK) {
    LOG_ERROR("Random init failed");
    log_close();
    platform_cleanup();
    return 1;
  }
  if (wiper_init() != ERR_OK) {
    LOG_ERROR("Wiper init failed");
    random_cleanup();
    log_close();
    platform_cleanup();
    return 1;
  }
  analysis_result_t analysis_result;
  int final_exit_code = run_wipe(&configuration, &analysis_result);
  wiper_cleanup();
  random_cleanup();
  LOG_INFO("Secure Wipe finished with code %d", final_exit_code);
  log_close();
  platform_cleanup();
  return final_exit_code;
}
