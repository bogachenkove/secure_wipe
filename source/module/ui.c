#include "ui.h"
#include "common.h"
#include "disk_scanner.h"
#include "wiper.h"
#include <stdio.h>
bool confirm_wipe(const char *device_path, uint64_t size_bytes, wipe_method_t method, uint32_t actual_passes) {
  char size_string[32];
  format_bytes(size_bytes, size_string, sizeof(size_string));
  printf("\n--- WARNING ---\n");
  printf("Device: %s\nSize:   %s\nMethod: %s\nPasses: %u\n", device_path, size_string, wiper_method_name(method), actual_passes);
  printf("This operation CANNOT be undone!\n");
  printf("Type 'YES' (all caps) to confirm: ");
  fflush(stdout);
  char response[16] = {0};
  if (!fgets(response, sizeof(response), stdin))
    return false;
  size_t response_length = strlen(response);
  if (response_length && response[response_length - 1] == '\n')
    response[response_length - 1] = '\0';
  return strcmp(response, "YES") == 0;
}
void progress_handler(uint64_t current, uint64_t total, int pass, const char *phase) {
  (void)pass;
  static int last_percent = -1;
  int current_percent = (int)((current * 100) / total);
  if (current_percent != last_percent) {
    int bar_width = 40;
    int filled_width = (current_percent * bar_width) / 100;
    printf("\r[");
    for (int bar_index = 0; bar_index < bar_width; bar_index++) {
      if (bar_index < filled_width)
        printf("#");
      else
        printf(" ");
    }
    printf("] %3d%% - %s", current_percent, phase);
    fflush(stdout);
    last_percent = current_percent;
    if (current_percent == 100) {
      printf("\n");
      last_percent = -1;
    }
  }
}
bool prompt_ata_erase(const ata_security_info_t *info, const char *device_path) {
  printf("\n=== ATA SECURITY FEATURES DETECTED ===\n");
  printf("Device: %s\n", device_path);
  printf("Model:  %s\n", info->model);
  printf("Serial: %s\n", info->serial);
  printf("Firmware: %s\n", info->firmware);
  printf("\nThis device supports ATA Secure Erase");
  if (info->enhanced_supported)
    printf(" and Enhanced Secure Erase");
  printf(".\n");
  printf("ATA Secure Erase is a built-in hardware command that\n");
  printf("can completely erase the drive in seconds/minutes,\n");
  printf("often faster and more thoroughly than software overwrite.\n");
  printf("\nDo you want to use ATA Secure Erase instead of the selected "
         "software method?\n");
  if (info->enhanced_supported) {
    printf("Choose option:\n");
    printf("  1) ATA Enhanced Secure Erase (most secure, if supported)\n");
    printf("  2) ATA Normal Secure Erase\n");
    printf("  3) Skip ATA Erase, use software method\n");
    printf("Enter choice (1-3) [3]: ");
    fflush(stdout);
    char input[16];
    if (!fgets(input, sizeof(input), stdin))
      return false;
    int choice = atoi(input);
    if (choice == 1) {
      printf("\n--- ATA ENHANCED SECURE ERASE ---\n");
      printf("This will completely erase all data on %s.\n", device_path);
      printf("Operation cannot be stopped once started.\n");
      printf("Type 'YES' (all caps) to confirm: ");
      fflush(stdout);
      char confirm[16];
      if (!fgets(confirm, sizeof(confirm), stdin))
        return false;
      if (strcmp(confirm, "YES\n") != 0) {
        printf("Operation cancelled.\n");
        return false;
      }
      return true;
    } else if (choice == 2) {
      printf("\n--- ATA NORMAL SECURE ERASE ---\n");
      printf("This will completely erase all data on %s.\n", device_path);
      printf("Operation cannot be stopped once started.\n");
      printf("Type 'YES' (all caps) to confirm: ");
      fflush(stdout);
      char confirm[16];
      if (!fgets(confirm, sizeof(confirm), stdin))
        return false;
      if (strcmp(confirm, "YES\n") != 0) {
        printf("Operation cancelled.\n");
        return false;
      }
      return true;
    } else {
      printf("Skipping ATA Erase.\n");
      return false;
    }
  } else {
    printf("Do you want to perform ATA Secure Erase? (y/n): ");
    fflush(stdout);
    char input[16];
    if (!fgets(input, sizeof(input), stdin))
      return false;
    if (input[0] == 'y' || input[0] == 'Y') {
      printf("\n--- ATA SECURE ERASE ---\n");
      printf("This will completely erase all data on %s.\n", device_path);
      printf("Operation cannot be stopped once started.\n");
      printf("Type 'YES' (all caps) to confirm: ");
      fflush(stdout);
      char confirm[16];
      if (!fgets(confirm, sizeof(confirm), stdin))
        return false;
      if (strcmp(confirm, "YES\n") != 0) {
        printf("Operation cancelled.\n");
        return false;
      }
      return true;
    } else {
      printf("Skipping ATA Erase.\n");
      return false;
    }
  }
}
bool interactive_select_disk(program_config_t *config) {
  disk_scan_result_t *scan_result = (disk_scan_result_t *)malloc(sizeof(disk_scan_result_t));
  if (!scan_result) {
    fprintf(stderr, "Memory allocation failed\n");
    return false;
  }
  printf("Scanning for available disks...\n\n");
  if (disk_scanner_scan(scan_result) != ERR_OK) {
    fprintf(stderr, "Failed to scan disks\n");
    free(scan_result);
    return false;
  }
  if (scan_result->count == 0) {
    fprintf(stderr, "No disks found\n");
    free(scan_result);
    return false;
  }
  disk_scanner_print_list(scan_result);
  printf("Enter disk number (1-%d) or 'q': ", scan_result->count);
  fflush(stdout);
  char input_buffer[16];
  if (!fgets(input_buffer, sizeof(input_buffer), stdin)) {
    free(scan_result);
    return false;
  }
  if (input_buffer[0] == 'q' || input_buffer[0] == 'Q') {
    free(scan_result);
    return false;
  }
  int selection = atoi(input_buffer);
  const disk_info_t *selected_disk = disk_scanner_get_by_index(scan_result, selection);
  if (!selected_disk) {
    fprintf(stderr, "Invalid selection\n");
    free(scan_result);
    return false;
  }
  disk_scanner_print_detail(selected_disk);
  printf("Confirm this disk? (y/n): ");
  fflush(stdout);
  if (!fgets(input_buffer, sizeof(input_buffer), stdin)) {
    free(scan_result);
    return false;
  }
  if (input_buffer[0] != 'y' && input_buffer[0] != 'Y') {
    free(scan_result);
    return false;
  }
  printf("\nSelect wipe method:\n");
  printf("  1. Zero Fill (1 pass)\n");
  printf("  2. Random (custom passes)\n");
  printf("Enter choice (1-2) [2]: ");
  fflush(stdout);
  if (!fgets(input_buffer, sizeof(input_buffer), stdin))
    input_buffer[0] = '2';
  int method_choice = atoi(input_buffer);
  if (method_choice == 1) {
    config->method = WIPE_METHOD_ZERO;
    config->passes = 1;
  } else {
    config->method = WIPE_METHOD_RANDOM;
    printf("Number of random passes (1-100) [3]: ");
    fflush(stdout);
    if (fgets(input_buffer, sizeof(input_buffer), stdin)) {
      int passes_value = atoi(input_buffer);
      config->passes = (passes_value > 0 && passes_value <= 100) ? (uint32_t)passes_value : 3;
    } else {
      config->passes = 3;
    }
  }
  printf("\nNumber of full wipe cycles (1-100) [1]: ");
  fflush(stdout);
  char cycle_buffer[16];
  if (fgets(cycle_buffer, sizeof(cycle_buffer), stdin)) {
    int cycles_value = atoi(cycle_buffer);
    if (cycles_value >= 1 && cycles_value <= 100)
      config->cycles = (uint32_t)cycles_value;
    else
      config->cycles = 1;
  } else {
    config->cycles = 1;
  }
  snprintf(config->device_path, sizeof(config->device_path), "%s", selected_disk->device_path);
  free(scan_result);
  return true;
}
