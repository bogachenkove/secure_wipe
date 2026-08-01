#ifndef CONFIG_H
#define CONFIG_H
#include "wiper.h"
typedef struct {
  char device_path[MAX_PATH_LEN];
  wipe_method_t method;
  uint32_t passes;
  bool analyze_only;
  bool analyze_write_test;
  bool quick_wp_check;
  bool destroy_partition_table;
  bool list_disks;
  bool select_disk;
  bool skip_analysis;
  bool verify;
  bool auto_confirm;
  bool verbose;
  size_t buffer_size;
  uint32_t cycles;
  bool emergency_mode;
  uint64_t emergency_sectors;
  uint64_t block_count;
  bool block_given;
  bool buffer_given;
  bool resume_given;
  uint64_t resume_sector;
  bool ata_secure_erase;
  bool ata_enhanced_erase;
  bool wipe_file_mode;
  bool wipe_dir_mode;
  char wipe_file_path[MAX_PATH_LEN];
} program_config_t;
void config_default(program_config_t *config);
#endif
