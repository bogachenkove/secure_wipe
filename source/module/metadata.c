#include "metadata.h"
#include "platform.h"
#include "common.h"
#include "wiper.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <errno.h>
#define SECURE_WIPE_NAME "Secure Wipe"
#define SECURE_WIPE_VERSION "2.0.12.0"
#define SECURE_WIPE_DESCRIPTION "Data Destruction Tool"
#define SECURE_WIPE_AUTHOR "Bogachenko Vyacheslav"
#define SECURE_WIPE_CONTACT "bogachenkove@outlook.com"
#define SECURE_WIPE_HOMEPAGE "https://github.com/bogachenkove/securewipe"
#define SECURE_WIPE_LICENSE "MIT License"
#define SECURE_WIPE_LICENSE_FILE "docs/LICENSE.txt"
static size_t parse_size_with_unit(const char *argument) {
  char *end_pointer;
  unsigned long long value = strtoull(argument, &end_pointer, 10);
  if (end_pointer == argument)
    return 0;
  while (*end_pointer == ' ')
    end_pointer++;
  size_t multiplier = 1;
  if (strcasecmp(end_pointer, "B") == 0 || *end_pointer == '\0')
    multiplier = 1;
  else if (strcasecmp(end_pointer, "KB") == 0)
    multiplier = 1024;
  else if (strcasecmp(end_pointer, "MB") == 0)
    multiplier = 1024 * 1024;
  else
    return 0;
  if (value > SIZE_MAX / multiplier)
    return 0;
  size_t result = (size_t)(value * multiplier);
  if (result > MAX_BUFFER_SIZE)
    return 0;
  return result;
}
void print_methods(void) {
  printf("Available wipe methods:\n\n");
  printf("  Zero        : 1 pass of zeros (0x00)\n");
  printf("  Random      : N passes of cryptographically secure random data\n");
}
void print_usage(const char *program_name) {
  printf("Usage: %s [options] <device>\n", program_name);
  printf("       %s --list\n", program_name);
  printf("       %s --select\n\n", program_name);
  printf("Information flags:\n");
  printf("  --version             Show version information\n");
  printf("  --about               Show information about the program\n");
  printf("  --license             Show license information\n");
  printf("  --support             Show support information\n");
  printf("  --help                Show this help message\n");
  printf("\nDisk Discovery:\n");
  printf("  -L, --list            List all disks (including system disks)\n");
  printf("  -S, --select          Interactive disk selection (shows all disks)\n");
  printf("\nWipe Methods:\n");
  printf("  --zero                Zero fill (1 pass)\n");
  printf("  --random N            Random data (N passes)\n");
  printf("\nATA Security Commands:\n");
  printf("  --ata-erase           Perform ATA Secure Erase (normal)\n");
  printf("  --ata-enhanced-erase  Perform ATA Enhanced Secure Erase\n");
  printf("  Note: Requires --device, incompatible with other wipe methods\n");
  printf("\nAnalysis Options:\n");
  printf("  --analyze             Read-only analysis (no wiping)\n");
  printf("  --skip-analysis       Skip analysis and proceed directly to wipe\n");
  printf("  --analyze-write       Analysis with write test (destroys data!)\n");
  printf("  --wp-check            Quick write-protection check\n");
  printf("  --destroy-partition-table   Destroy MBR/GPT only (zero out, no "
         "data wipe)\n");
  printf("\nFile and Directory Wiping:\n");
  printf("  --wipe-file <path>    Securely wipe a single file\n");
  printf("  --wipe-dir <path>     Securely wipe a directory recursively\n");
  printf("  --rename-count <N>    Number of renames before deletion (default "
         "3)\n");
  printf("  --no-rename           Disable renaming before deletion\n");
  printf("\nLogging Options:\n");
  printf("  --log FILE            Write log to specified file\n");
  printf("  --no-log              Disable log file creation\n");
  printf("\nOther Options:\n");
  printf("  -b, --buffer SIZE     I/O buffer size (e.g. 1MB, 512KB)\n");
  printf("  -v, --verify          Verify after wipe (default)\n");
  printf("  -n, --no-verify       Skip verification\n");
  printf("  -y, --yes             Auto-confirm (dangerous)\n");
  printf("  -q, --quiet           Quiet mode\n");
  printf("  --methods             List wipe methods with descriptions\n");
  printf("  --cycle N             Repeat full wipe cycle N times (default 1, "
         "max 100)\n");
  printf("\nEmergency Mode (write zeros to device):\n");
  printf("  --emergency           Enable emergency zero-write mode\n");
  printf("                        Without --sector/--block: overwrite entire "
         "device\n");
  printf("                        With --sector N: overwrite first N sectors\n");
  printf("                        With --block N: overwrite N blocks (block "
         "size = --buffer)\n");
  printf("  --sector N            Number of sectors to overwrite (must be >0)\n");
  printf("  --block N             Number of blocks to overwrite (block size = "
         "--buffer)\n");
  printf("  --resume N            Resume emergency wipe from sector N "
         "(requires --emergency)\n");
  printf("  -b, --buffer SIZE     I/O buffer size (optional for full disk "
         "wipe)\n");
}
void print_version(void) { printf(SECURE_WIPE_NAME " " SECURE_WIPE_VERSION "\n"); }
void print_about(void) {
  printf(SECURE_WIPE_NAME " " SECURE_WIPE_VERSION " - " SECURE_WIPE_DESCRIPTION "\n");
  printf("Copyright (c) 2026 " SECURE_WIPE_AUTHOR "\n\n");
  printf("Author:   " SECURE_WIPE_AUTHOR "\n");
  printf("Contact:  " SECURE_WIPE_CONTACT "\n");
  printf("Homepage: " SECURE_WIPE_HOMEPAGE "\n");
}
void print_license(void) {
  printf(SECURE_WIPE_NAME " " SECURE_WIPE_VERSION " - " SECURE_WIPE_DESCRIPTION "\n");
  printf("Copyright (c) 2026 " SECURE_WIPE_AUTHOR "\n\n");
  printf("This software is released under the " SECURE_WIPE_LICENSE ".\n");
  printf("You are free to use, modify, and distribute it in accordance with "
         "the license terms.\n\n");
  FILE *license_file = fopen(SECURE_WIPE_LICENSE_FILE, "r");
  if (license_file) {
    char line_buffer[1024];
    while (fgets(line_buffer, sizeof(line_buffer), license_file))
      printf("%s", line_buffer);
    fclose(license_file);
  } else {
    printf("License file not found locally in the " SECURE_WIPE_LICENSE_FILE " directory.\n");
    printf("Please read the license agreement online:\n");
    printf("https://raw.githubusercontent.com/bogachenkove/securewipe/stable/%s\n", SECURE_WIPE_LICENSE_FILE);
  }
}
void print_support(void) {
  printf(SECURE_WIPE_NAME " " SECURE_WIPE_VERSION " - " SECURE_WIPE_DESCRIPTION "\n");
  printf("Copyright (c) 2026 " SECURE_WIPE_AUTHOR "\n\n");
  printf("Donating is an act of generosity.\n");
  printf("Your support, however modest it might be, is necessary and you can "
         "provide it, ");
  printf("because you love the " SECURE_WIPE_NAME " project and enjoy it.\n");
  printf("Your donations help to continue to support and improve this "
         "project!\n\n");
  printf("At the same time, the " SECURE_WIPE_NAME " project remains free to use ");
  printf("and is distributed under the " SECURE_WIPE_LICENSE ", so making a donation is completely optional.\n");
  printf("You can continue enjoying and using it without paying anything.\n");
  printf("Contributions are a voluntary way to show appreciation and help the "
         "project grow, ");
  printf("but there is absolutely no obligation to donate.\n\n\n");
  printf("Bitcoin: 18NTjAZhiiwioSN1w6JBkvPQDkDqPiDt5T\n");
  printf("Litecoin: LMuEHujV3cCYMzjYxKXdVHadrpB4YtwgeF\n");
  printf("Ethereum: 0xEC2feBbA54050801E57946a1a7bfF66AF222d330\n");
  printf("Tron: TUmv3VLiPS8RjLEFywUfn4XGVoL4B3m8jE\n");
  printf("Solana: 6Ap7RzP8y8HNNuFUhuMKNoMuVqBzMmA3YpkxBsbY8ctY\n");
  printf("Zcash: t1XNSbWCx9N6S6bHEMpoeMmo8iai6j5NSz7\n");
  printf("Ripple: rw8nx6k6MD5jiR9cWtWfMeXGW6pJ2QVuYG\n");
  printf("Dash: XbYWcSL76G1rBKwGmfGXx5B9RFYmYqhdtm\n\n");
}
bool parse_arguments(int argc, char *argv[], program_config_t *config) {
  config_default(config);
  if (argc < 2) {
    config->select_disk = true;
    return true;
  }
  for (int argument_index = 1; argument_index < argc; argument_index++) {
    if (strcmp(argv[argument_index], "--help") == 0) {
      print_usage(argv[0]);
      exit(0);
    } else if (strcmp(argv[argument_index], "--methods") == 0) {
      print_methods();
      exit(0);
    } else if (strcmp(argv[argument_index], "--version") == 0) {
      print_version();
      exit(0);
    } else if (strcmp(argv[argument_index], "--about") == 0) {
      print_about();
      exit(0);
    } else if (strcmp(argv[argument_index], "--license") == 0) {
      print_license();
      exit(0);
    } else if (strcmp(argv[argument_index], "--support") == 0) {
      print_support();
      exit(0);
    } else if (strcmp(argv[argument_index], "-L") == 0 || strcmp(argv[argument_index], "--list-storage") == 0) {
      config->list_disks = true;
    } else if (strcmp(argv[argument_index], "-S") == 0 || strcmp(argv[argument_index], "--select-storage") == 0) {
      config->select_disk = true;
    } else if (strcmp(argv[argument_index], "--zero") == 0) {
      config->method = WIPE_METHOD_ZERO;
      config->passes = 1;
    } else if (strcmp(argv[argument_index], "--random") == 0) {
      config->method = WIPE_METHOD_RANDOM;
      if (argument_index + 1 < argc && argv[argument_index + 1][0] != '-') {
        int next_argument = ++argument_index;
        config->passes = (uint32_t)atoi(argv[next_argument]);
        if (config->passes < 1 || config->passes > 100) {
          fprintf(stderr, "ERROR: --random requires a number between 1 and "
                          "100. See --help.\n");
          return false;
        }
      } else {
        fprintf(stderr, "ERROR: --random requires a number of passes (e.g., "
                        "--random 3). See --help.\n");
        return false;
      }
    } else if (strcmp(argv[argument_index], "-b") == 0 || strcmp(argv[argument_index], "--buffer") == 0) {
      if (argument_index + 1 < argc) {
        size_t new_size = parse_size_with_unit(argv[++argument_index]);
        if (new_size == 0 || buffer_set_size(new_size) != 0) {
          fprintf(stderr, "ERROR: Invalid buffer size. Use format like 1MB, "
                          "512KB. See --help.\n");
          return false;
        }
        config->buffer_size = new_size;
        config->buffer_given = true;
      } else {
        fprintf(stderr, "ERROR: --buffer requires a size argument (e.g., "
                        "--buffer 1M). See --help.\n");
        return false;
      }
    } else if (strcmp(argv[argument_index], "--analyze") == 0) {
      config->analyze_only = true;
    } else if (strcmp(argv[argument_index], "--analyze-write") == 0) {
      config->analyze_only = true;
      config->analyze_write_test = true;
    } else if (strcmp(argv[argument_index], "--wp-check") == 0) {
      config->analyze_only = true;
      config->quick_wp_check = true;
    } else if (strcmp(argv[argument_index], "--skip-analysis") == 0) {
      config->skip_analysis = true;
    } else if (strcmp(argv[argument_index], "--destroy-partition-table") == 0) {
      config->destroy_partition_table = true;
      config->analyze_only = true;
    } else if (strcmp(argv[argument_index], "--log") == 0) {
      if (argument_index + 1 < argc && argv[argument_index + 1][0] != '-') {
        snprintf(global_log_file_path, sizeof(global_log_file_path), "%s", argv[++argument_index]);
      } else {
        fprintf(stderr, "ERROR: --log requires a file path. See --help.\n");
        return false;
      }
    } else if (strcmp(argv[argument_index], "--no-log") == 0) {
      global_no_log = true;
      global_log_file_path[0] = '\0';
    } else if (strcmp(argv[argument_index], "-v") == 0 || strcmp(argv[argument_index], "--verify") == 0) {
      config->verify = true;
    } else if (strcmp(argv[argument_index], "-n") == 0 || strcmp(argv[argument_index], "--no-verify") == 0) {
      config->verify = false;
    } else if (strcmp(argv[argument_index], "-y") == 0 || strcmp(argv[argument_index], "--yes") == 0) {
      config->auto_confirm = true;
    } else if (strcmp(argv[argument_index], "-q") == 0 || strcmp(argv[argument_index], "--quiet") == 0) {
      config->verbose = false;
    } else if (strcmp(argv[argument_index], "--cycle") == 0) {
      if (argument_index + 1 < argc) {
        config->cycles = (uint32_t)atoi(argv[++argument_index]);
        if (config->cycles < 1 || config->cycles > 100) {
          fprintf(stderr, "ERROR: --cycle requires a number between 1 and 100. "
                          "See --help.\n");
          return false;
        }
      } else {
        fprintf(stderr, "ERROR: --cycle requires a number. See --help.\n");
        return false;
      }
    } else if (strcmp(argv[argument_index], "--sector") == 0) {
      if (argument_index + 1 < argc) {
        char *end;
        errno = 0;
        config->emergency_sectors = strtoull(argv[++argument_index], &end, 10);
        if (errno != 0 || *end != '\0' || config->emergency_sectors == 0) {
          fprintf(stderr, "ERROR: --sector must be a positive number. See --help.\n");
          return false;
        }
      } else {
        fprintf(stderr, "ERROR: --sector requires a number. See --help.\n");
        return false;
      }
    } else if (strcmp(argv[argument_index], "--block") == 0) {
      if (argument_index + 1 < argc) {
        char *end;
        errno = 0;
        config->block_count = strtoull(argv[++argument_index], &end, 10);
        if (errno != 0 || *end != '\0' || config->block_count == 0) {
          fprintf(stderr, "ERROR: --block must be a positive number. See --help.\n");
          return false;
        }
        config->block_given = true;
      } else {
        fprintf(stderr, "ERROR: --block requires a number. See --help.\n");
        return false;
      }
    } else if (strcmp(argv[argument_index], "--resume") == 0) {
      if (argument_index + 1 < argc) {
        char *end;
        errno = 0;
        config->resume_sector = strtoull(argv[++argument_index], &end, 10);
        if (errno != 0 || *end != '\0' || config->resume_sector == 0) {
          fprintf(stderr, "ERROR: --resume requires a positive sector number.\n");
          return false;
        }
        config->resume_given = true;
      } else {
        fprintf(stderr, "ERROR: --resume requires a sector number.\n");
        return false;
      }
    } else if (strcmp(argv[argument_index], "--emergency") == 0) {
      config->emergency_mode = true;
    } else if (strcmp(argv[argument_index], "--ata-erase") == 0) {
      config->ata_secure_erase = true;
      config->ata_enhanced_erase = false;
      config->analyze_only = true;
    } else if (strcmp(argv[argument_index], "--ata-enhanced-erase") == 0) {
      config->ata_secure_erase = true;
      config->ata_enhanced_erase = true;
      config->analyze_only = true;
    } else if (strcmp(argv[argument_index], "--wipe-file") == 0) {
      if (argument_index + 1 < argc) {
        config->wipe_file_mode = true;
        snprintf(config->wipe_file_path, sizeof(config->wipe_file_path), "%s", argv[++argument_index]);
      } else {
        fprintf(stderr, "ERROR: --wipe-file requires a file path\n");
        return false;
      }
    } else if (strcmp(argv[argument_index], "--wipe-dir") == 0) {
      if (argument_index + 1 < argc) {
        config->wipe_dir_mode = true;
        snprintf(config->wipe_file_path, sizeof(config->wipe_file_path), "%s", argv[++argument_index]);
      } else {
        fprintf(stderr, "ERROR: --wipe-dir requires a directory path\n");
        return false;
      }
    } else if (strcmp(argv[argument_index], "--rename-count") == 0) {
      if (argument_index + 1 < argc) {
        int count = atoi(argv[++argument_index]);
        if (count < 0 || count > 255) {
          fprintf(stderr, "ERROR: --rename-count must be between 0 and 255\n");
          return false;
        }
        global_default_rename_count = (uint8_t)count;
      } else {
        fprintf(stderr, "ERROR: --rename-count requires a number\n");
        return false;
      }
    } else if (strcmp(argv[argument_index], "--no-rename") == 0) {
      global_default_rename_count = 0;
    } else if (argv[argument_index][0] != '-') {
      snprintf(config->device_path, sizeof(config->device_path), "%s", argv[argument_index]);
    } else {
      fprintf(stderr, "ERROR: Unknown option '%s'. See --help for usage.\n", argv[argument_index]);
      return false;
    }
  }
  if (config->emergency_mode) {
    if (!config->buffer_given) {
      config->buffer_size = global_buffer_size;
      config->buffer_given = true;
      LOG_INFO("Emergency mode: using default buffer size %zu bytes", global_buffer_size);
    }
    if (config->block_given && config->emergency_sectors != 0) {
      fprintf(stderr, "ERROR: --emergency cannot use both --sector and "
                      "--block. Choose one. See --help.\n");
      return false;
    }
    if (config->block_given) {
      size_t bytes_per_block = config->buffer_size;
      if (bytes_per_block % SECTOR_SIZE != 0) {
        fprintf(stderr,
                "ERROR: buffer size (%zu) must be multiple of sector size "
                "(%d). See --help.\n",
                bytes_per_block, SECTOR_SIZE);
        return false;
      }
      uint64_t sectors_per_block = bytes_per_block / SECTOR_SIZE;
      if (config->block_count > UINT64_MAX / sectors_per_block) {
        fprintf(stderr, "ERROR: block count too large, would overflow. See --help.\n");
        return false;
      }
      config->emergency_sectors = config->block_count * sectors_per_block;
    } else if (config->emergency_sectors == 0) {
      LOG_INFO("Emergency mode: full disk zeroing (no sector/block limit)");
    }
    if (config->list_disks || config->select_disk || config->analyze_only || config->destroy_partition_table || config->quick_wp_check ||
        config->skip_analysis) {
      fprintf(stderr, "ERROR: --emergency is incompatible with other operation flags "
                      "(--list, --select, --analyze, --destroy-partition-table, "
                      "--wp-check, --skip-analysis). See --help.\n");
      return false;
    }
    if (config->cycles > 1) {
      fprintf(stderr, "ERROR: --cycle cannot be used with --emergency. See --help.\n");
      return false;
    }
    if (strlen(config->device_path) == 0) {
      fprintf(stderr, "ERROR: --emergency requires a device path. See --help.\n");
      return false;
    }
  }
  if (config->ata_secure_erase && strlen(config->device_path) == 0) {
    fprintf(stderr, "ERROR: --ata-erase requires a device path.\n");
    return false;
  }
  return true;
}
