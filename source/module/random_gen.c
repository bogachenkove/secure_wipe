#include "random_gen.h"
#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
static BCRYPT_ALG_HANDLE random_algorithm_handle = NULL;
#else
#include <unistd.h>
#include <fcntl.h>
static int urandom_descriptor = -1;
#endif
error_code_t random_init(void) {
#ifdef _WIN32
  if (BCryptOpenAlgorithmProvider(&random_algorithm_handle, BCRYPT_RNG_ALGORITHM, NULL, 0) != 0)
    return ERR_RANDOM_GEN;
  return ERR_OK;
#else
  urandom_descriptor = open("/dev/urandom", O_RDONLY);
  return (urandom_descriptor >= 0) ? ERR_OK : ERR_RANDOM_GEN;
#endif
}
void random_cleanup(void) {
#ifdef _WIN32
  if (random_algorithm_handle)
    BCryptCloseAlgorithmProvider(random_algorithm_handle, 0);
  random_algorithm_handle = NULL;
#else
  if (urandom_descriptor >= 0)
    close(urandom_descriptor);
  urandom_descriptor = -1;
#endif
}
error_code_t random_fill(uint8_t *buffer, size_t size) {
  if (!buffer || size == 0)
    return ERR_INVALID_ARG;
#ifdef _WIN32
  return (BCryptGenRandom(random_algorithm_handle, buffer, (ULONG)size, 0) == 0) ? ERR_OK : ERR_RANDOM_GEN;
#else
  size_t total_read = 0;
  while (total_read < size) {
    ssize_t bytes_read = read(urandom_descriptor, buffer + total_read, size - total_read);
    if (bytes_read < 0)
      return ERR_RANDOM_GEN;
    total_read += (size_t)bytes_read;
  }
  return ERR_OK;
#endif
}
error_code_t random_uint32(uint32_t *value) { return random_fill((uint8_t *)value, sizeof(uint32_t)); }
error_code_t random_uint64(uint64_t *value) { return random_fill((uint8_t *)value, sizeof(uint64_t)); }
