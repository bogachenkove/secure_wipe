#ifndef RANDOM_GEN_H
#define RANDOM_GEN_H

#include "common.h"

error_code_t randomInit(void);
void randomCleanup(void);
error_code_t randomFill(uint8_t *buffer, size_t size);
error_code_t randomUint32(uint32_t *value);
error_code_t randomUint64(uint64_t *value);

#endif