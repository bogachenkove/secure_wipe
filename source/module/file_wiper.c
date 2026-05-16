#include "file_wiper.h"
#include "file_io.h"
#include "random_gen.h"
#include "platform.h"

typedef enum
{
 PATTERN_ZERO,
 PATTERN_RANDOM
} pattern_type_t;

static void fillBufferWithPattern (uint8_t *buffer, size_t bufferSize, pattern_type_t patternType, const uint8_t *patternData, size_t patternLength)
{
 (void) patternData;
 (void) patternLength;
 switch (patternType)
 {
 case PATTERN_ZERO:
  memset (buffer, 0x00, bufferSize);
  break;
 case PATTERN_RANDOM:
  randomFill (buffer, bufferSize);
  break;
 default:
  break;
 }
}

static error_code_t wipeFileContent (file_t *file, const file_wipe_config_t *config)
{
 if (!file || !file->isOpen || file->readOnly)
  return ERR_INVALID_ARG;

 uint64_t fileSize;
 error_code_t error = fileGetSize (file, &fileSize);
 if (error != ERR_OK)
  return error;
 if (fileSize == 0)
  return ERR_OK;

 size_t bufferSize = gBufferSize;
 if (bufferSize > fileSize)
  bufferSize = (size_t) fileSize;

 uint8_t *buffer = (uint8_t *) alignedAlloc (bufferSize);
 if (!buffer)
  return ERR_MEMORY;

 const wipe_method_t method = config->method;
 const uint32_t passes = (method == WIPE_METHOD_RANDOM) ? config->passes : wiperMethodPasses (method);

 for (uint32_t pass = 1; pass <= passes; pass++)
 {
  uint64_t offset = 0;
  while (offset < fileSize)
  {
   size_t chunk = bufferSize;
   if (offset + chunk > fileSize)
	chunk = (size_t) (fileSize - offset);

   pattern_type_t patternType = PATTERN_ZERO;

   switch (method)
   {
   case WIPE_METHOD_ZERO:
	patternType = PATTERN_ZERO;
	break;
   case WIPE_METHOD_RANDOM:
	patternType = PATTERN_RANDOM;
	break;
   default:
	alignedFree (buffer);
	return ERR_INVALID_ARG;
   }

   fillBufferWithPattern (buffer, chunk, patternType, NULL, 0);

   size_t bytesWritten = 0;
   error = fileWrite (file, offset, buffer, chunk, &bytesWritten);
   if (error != ERR_OK || bytesWritten != chunk)
   {
	alignedFree (buffer);
	return ERR_WRITE_DEVICE;
   }
   offset += chunk;
   if (config->progress)
	config->progress (offset, fileSize, (int) pass, "Wiping file");
  }
  fileFlush (file);
 }

 alignedFree (buffer);
 return ERR_OK;
}

static error_code_t renameFileMultiple (file_t *file, uint8_t renameCount)
{
 if (!file || renameCount == 0)
  return ERR_INVALID_ARG;

 char originalPath[MAX_PATH_LEN];
 snprintf (originalPath, sizeof (originalPath), "%s", file->path);

 for (uint8_t step = 1; step <= renameCount; step++)
 {
  uint64_t randomValue;
  if (randomUint64 (&randomValue) != ERR_OK)
   randomValue = (uint64_t) time (NULL) ^ step;

  char newPath[MAX_PATH_LEN];
  snprintf (newPath, sizeof (newPath), "%s.%016llX.~%u", originalPath, (unsigned long long) randomValue, step);

  error_code_t error = fileRename (file, newPath);
  if (error != ERR_OK)
   return error;
 }
 return ERR_OK;
}

error_code_t fileWipeSingle (const char *path, const file_wipe_config_t *config)
{
 if (!path || !config)
  return ERR_INVALID_ARG;

 file_info_t info;
 error_code_t error = fileScannerGetInfo (path, &info);
 if (error != ERR_OK)
  return error;

 if (info.type == FILE_TYPE_DIRECTORY)
  return fileWipeDirectory (path, config);

 file_t file;
 error = fileOpen (&file, path, false);
 if (error != ERR_OK)
  return error;

 if (!config->preserveTimestamps)
  fileSetTimes (&file, 0, 0);

 error = wipeFileContent (&file, config);
 if (error != ERR_OK)
 {
  fileClose (&file);
  return error;
 }

 if (config->renameBeforeDelete && config->renameCount > 0)
  renameFileMultiple (&file, config->renameCount);

 error = fileDelete (&file);
 return error;
}

error_code_t fileWipeList (file_list_t *list, const file_wipe_config_t *config)
{
 if (!list || !config)
  return ERR_INVALID_ARG;

 for (size_t index = 0; index < list->count; index++)
 {
  error_code_t error = fileWipeSingle (list->items[index].fullPath, config);
  if (error != ERR_OK)
   return error;
 }
 return ERR_OK;
}

error_code_t fileWipeDirectory (const char *path, const file_wipe_config_t *config)
{
 if (!path || !config)
  return ERR_INVALID_ARG;

 file_list_t list;
 error_code_t error = fileScannerInitList (&list);
 if (error != ERR_OK)
  return error;

 error = fileScannerScan (path, &list, true, false);
 if (error != ERR_OK)
 {
  fileScannerFreeList (&list);
  return error;
 }

 error = fileWipeList (&list, config);
 fileScannerFreeList (&list);
 if (error != ERR_OK)
  return error;

 file_t dirFile;
 error = fileOpen (&dirFile, path, false);
 if (error != ERR_OK)
  return error;

 error = fileDelete (&dirFile);
 return error;
}

error_code_t fileWipePath (const char *path, const file_wipe_config_t *config)
{
 if (!path || !config)
  return ERR_INVALID_ARG;

 file_info_t info;
 error_code_t error = fileScannerGetInfo (path, &info);
 if (error != ERR_OK)
  return error;

 if (info.type == FILE_TYPE_DIRECTORY)
  return fileWipeDirectory (path, config);
 else
  return fileWipeSingle (path, config);
}