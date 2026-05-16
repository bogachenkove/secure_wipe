#ifndef FILE_WIPER_H
#define FILE_WIPER_H

#include "wiper.h"
#include "file_scanner.h"

typedef struct
{
 wipe_method_t method;
 uint32_t passes;
 bool renameBeforeDelete;
 uint8_t renameCount;
 bool preserveTimestamps;
 progress_callback_t progress;
} file_wipe_config_t;

error_code_t fileWipeSingle (const char *path, const file_wipe_config_t *config);
error_code_t fileWipeList (file_list_t *list, const file_wipe_config_t *config);
error_code_t fileWipeDirectory (const char *path, const file_wipe_config_t *config);
error_code_t fileWipePath (const char *path, const file_wipe_config_t *config);

#endif