#ifndef METADATA_H
#define METADATA_H
#include "config.h"
bool parse_arguments(int argc, char *argv[], program_config_t *config);
void print_usage(const char *program_name);
void print_methods(void);
void print_version(void);
void print_about(void);
void print_license(void);
void print_support(void);
#endif
