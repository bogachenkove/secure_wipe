#ifndef RANDOM_GEN_H
#define RANDOM_GEN_H
#include "common.h"
error_code_t random_init(void);
void random_cleanup(void);
error_code_t random_fill(uint8_t *buffer, size_t size);
error_code_t random_uint32(uint32_t *value);
error_code_t random_uint64(uint64_t *value);
#endif
