#ifndef COMMON_H
#define COMMON_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#define DEFAULT_BUFFER_SIZE (512 * 1024)
#define MIN_BUFFER_SIZE (512)
#define MAX_BUFFER_SIZE (1024 * 1024 * 256)
extern size_t global_buffer_size;
extern size_t global_buffer_sectors;
int buffer_set_size(size_t size_bytes);
void buffer_get_size_info(size_t *out_size_bytes, size_t *out_sectors);
#define SECTOR_SIZE 512
#define MAX_PATH_LEN 512
#define MAX_BAD_SECTORS 100000
typedef enum {
  ERR_OK = 0,
  ERR_OPEN_DEVICE,
  ERR_READ_DEVICE,
  ERR_WRITE_DEVICE,
  ERR_SEEK_DEVICE,
  ERR_GET_SIZE,
  ERR_MEMORY,
  ERR_PERMISSION,
  ERR_INVALID_ARG,
  ERR_RANDOM_GEN,
  ERR_UNKNOWN
} error_code_t;
typedef struct {
  uint64_t total_sectors;
  uint64_t total_bytes;
  uint64_t readable_sectors;
  uint64_t bad_sector_count;
  uint64_t *bad_sectors;
  size_t bad_sectors_capacity;
} analysis_result_t;
typedef struct {
  uint64_t sectors_wiped;
  uint64_t sectors_failed;
  uint64_t current_pass;
  uint64_t total_passes;
  time_t start_time;
  time_t end_time;
} wipe_stats_t;
typedef void (*progress_callback_t)(uint64_t current, uint64_t total, int pass, const char *phase);
typedef struct {
  FILE *log_file;
  bool verbose;
  char log_path[MAX_PATH_LEN];
} logger_t;
extern logger_t global_logger;
extern char global_log_file_path[MAX_PATH_LEN];
extern bool global_no_log;
extern uint8_t global_default_rename_count;
void log_init(const char *log_path, bool verbose);
void log_close(void);
void log_message(const char *level, const char *format, ...);
void log_bad_sector(uint64_t sector, const char *operation);
#define LOG_INFO(...) log_message("INFO", __VA_ARGS__)
#define LOG_WARN(...) log_message("WARN", __VA_ARGS__)
#define LOG_ERROR(...) log_message("ERROR", __VA_ARGS__)
const char *error_to_string(error_code_t error_code);
void format_bytes(uint64_t bytes, char *output_buffer, size_t buffer_size);
void format_time(time_t seconds, char *output_buffer, size_t buffer_size);
#endif
