#include "common.h"
#include <stdarg.h>
size_t global_buffer_size = DEFAULT_BUFFER_SIZE;
size_t global_buffer_sectors = DEFAULT_BUFFER_SIZE / SECTOR_SIZE;
logger_t global_logger = {NULL, false, ""};
char global_log_file_path[MAX_PATH_LEN] = {0};
bool global_no_log = false;
uint8_t global_default_rename_count = 3;
int buffer_set_size(size_t size_bytes) {
  if (size_bytes < MIN_BUFFER_SIZE || size_bytes > MAX_BUFFER_SIZE) {
    LOG_ERROR("Buffer size must be between %d and %d bytes", MIN_BUFFER_SIZE, MAX_BUFFER_SIZE);
    return -1;
  }
  if (size_bytes % SECTOR_SIZE != 0) {
    LOG_ERROR("Buffer size must be multiple of sector size (%d)", SECTOR_SIZE);
    return -1;
  }
  global_buffer_size = size_bytes;
  global_buffer_sectors = size_bytes / SECTOR_SIZE;
  LOG_INFO("Buffer size set to %zu bytes (%zu sectors)", global_buffer_size, global_buffer_sectors);
  return 0;
}
void buffer_get_size_info(size_t *out_size_bytes, size_t *out_sectors) {
  if (out_size_bytes)
    *out_size_bytes = global_buffer_size;
  if (out_sectors)
    *out_sectors = global_buffer_sectors;
}
void log_init(const char *log_path, bool verbose) {
  global_logger.verbose = verbose;
  if (log_path && strlen(log_path) > 0 && !global_no_log) {
    snprintf(global_logger.log_path, sizeof(global_logger.log_path), "%s", log_path);
    global_logger.log_file = fopen(log_path, "w");
    if (!global_logger.log_file)
      fprintf(stderr, "Warning: Cannot open log file: %s\n", log_path);
  } else {
    global_logger.log_file = NULL;
    global_logger.log_path[0] = '\0';
  }
}
void log_close(void) {
  if (global_logger.log_file) {
    fclose(global_logger.log_file);
    global_logger.log_file = NULL;
  }
}
void log_message(const char *level, const char *format, ...) {
  char timestamp[64];
  time_t current_time = time(NULL);
  struct tm *time_info = localtime(&current_time);
  if (!time_info) {
    fprintf(stderr, "localtime failed\n");
    return;
  }
  strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", time_info);
  va_list argument_list;
  if (global_logger.verbose || strcmp(level, "ERROR") == 0) {
    printf("[%s] [%s] ", timestamp, level);
    va_start(argument_list, format);
    vprintf(format, argument_list);
    va_end(argument_list);
    printf("\n");
    fflush(stdout);
  }
  if (global_logger.log_file) {
    fprintf(global_logger.log_file, "[%s] [%s] ", timestamp, level);
    va_start(argument_list, format);
    vfprintf(global_logger.log_file, format, argument_list);
    va_end(argument_list);
    fprintf(global_logger.log_file, "\n");
    fflush(global_logger.log_file);
  }
}
void log_bad_sector(uint64_t sector, const char *operation) {
  log_message("WARN", "Bad sector detected: %llu during %s", (unsigned long long)sector, operation);
}
const char *error_to_string(error_code_t error_code) {
  switch (error_code) {
  case ERR_OK:
    return "Success";
  case ERR_OPEN_DEVICE:
    return "Failed to open device";
  case ERR_READ_DEVICE:
    return "Failed to read device";
  case ERR_WRITE_DEVICE:
    return "Failed to write device";
  case ERR_SEEK_DEVICE:
    return "Failed to seek device";
  case ERR_GET_SIZE:
    return "Failed to get device size";
  case ERR_MEMORY:
    return "Memory allocation failed";
  case ERR_PERMISSION:
    return "Insufficient permissions";
  case ERR_INVALID_ARG:
    return "Invalid argument";
  case ERR_RANDOM_GEN:
    return "Random generation failed";
  default:
    return "Unknown error";
  }
}
void format_bytes(uint64_t bytes, char *output_buffer, size_t buffer_size) {
  const char *units[] = {"B", "KB", "MB", "GB", "TB"};
  int unit_index = 0;
  double size = (double)bytes;
  while (size >= 1024.0 && unit_index < 4) {
    size /= 1024.0;
    unit_index++;
  }
  snprintf(output_buffer, buffer_size, "%.2f %s", size, units[unit_index]);
}
void format_time(time_t seconds, char *output_buffer, size_t buffer_size) {
  int hours = (int)(seconds / 3600);
  int minutes = (int)((seconds % 3600) / 60);
  int secs = (int)(seconds % 60);
  if (hours > 0)
    snprintf(output_buffer, buffer_size, "%dh %dm %ds", hours, minutes, secs);
  else if (minutes > 0)
    snprintf(output_buffer, buffer_size, "%dm %ds", minutes, secs);
  else
    snprintf(output_buffer, buffer_size, "%ds", secs);
}
