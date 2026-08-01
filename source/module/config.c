#include "config.h"
#include "common.h"
void config_default(program_config_t *config) {
  memset(config, 0, sizeof(program_config_t));
  config->method = WIPE_METHOD_ZERO;
  config->passes = 1;
  config->verify = true;
  config->verbose = true;
  config->buffer_size = DEFAULT_BUFFER_SIZE;
  config->destroy_partition_table = false;
  config->skip_analysis = false;
  config->cycles = 1;
  config->emergency_mode = false;
  config->emergency_sectors = 0;
  config->block_count = 0;
  config->block_given = false;
  config->buffer_given = false;
  config->resume_given = false;
  config->resume_sector = 0;
  config->ata_secure_erase = false;
  config->ata_enhanced_erase = false;
  config->wipe_file_mode = false;
  config->wipe_dir_mode = false;
  config->wipe_file_path[0] = '\0';
}
