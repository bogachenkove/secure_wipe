#include "random_gen.h"

#ifdef _WIN32
#include <windows.h>
#include <bcrypt.h>
static BCRYPT_ALG_HANDLE gRngAlgorithm = NULL;
#else
#include <unistd.h>
#include <fcntl.h>
static int gUrandomDescriptor = -1;
#endif

error_code_t randomInit (void)
{
#ifdef _WIN32
 if (BCryptOpenAlgorithmProvider (&gRngAlgorithm, BCRYPT_RNG_ALGORITHM, NULL, 0) != 0)
  return ERR_RANDOM_GEN;
 return ERR_OK;
#else
 gUrandomDescriptor = open ("/dev/urandom", O_RDONLY);
 return (gUrandomDescriptor >= 0) ? ERR_OK : ERR_RANDOM_GEN;
#endif
}

void randomCleanup (void)
{
#ifdef _WIN32
 if (gRngAlgorithm)
  BCryptCloseAlgorithmProvider (gRngAlgorithm, 0);
 gRngAlgorithm = NULL;
#else
 if (gUrandomDescriptor >= 0)
  close (gUrandomDescriptor);
 gUrandomDescriptor = -1;
#endif
}

error_code_t randomFill (uint8_t *buffer, size_t size)
{
 if (!buffer || size == 0)
  return ERR_INVALID_ARG;

#ifdef _WIN32
 return (BCryptGenRandom (gRngAlgorithm, buffer, (ULONG) size, 0) == 0) ? ERR_OK : ERR_RANDOM_GEN;
#else
 size_t totalRead = 0;
 while (totalRead < size)
 {
  ssize_t bytesRead = read (gUrandomDescriptor, buffer + totalRead, size - totalRead);
  if (bytesRead < 0)
   return ERR_RANDOM_GEN;
  totalRead += (size_t) bytesRead;
 }
 return ERR_OK;
#endif
}

error_code_t randomUint32 (uint32_t *value)
{
 return randomFill ((uint8_t *) value, sizeof (uint32_t));
}

error_code_t randomUint64 (uint64_t *value)
{
 return randomFill ((uint8_t *) value, sizeof (uint64_t));
}