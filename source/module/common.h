#ifndef COMMON_H
#define COMMON_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define DEFAULT_BUFFER_SIZE     (512 * 1024)
#define MIN_BUFFER_SIZE         (512)
#define MAX_BUFFER_SIZE         (1024 * 1024 * 256)

extern size_t gBufferSize;
extern size_t gBufferSectors;

int bufferSetSize(size_t sizeBytes);
void bufferGetSizeInfo(size_t *sizeBytes, size_t *sectors);

#define SECTOR_SIZE         512
#define MAX_PATH_LEN        260
#define MAX_BAD_SECTORS     100000

typedef enum {
    ERR_OK = 0,
    ERR_OPEN_DEVICE,
    ERR_READ_DEVICE,
    ERR_WRITE_DEVICE,
    ERR_SEEK_DEVICE,
    ERR_GET_SIZE,
    ERR_MEMORY,
    ERR_PERMISSION,
    ERR_INVALID_ARG,
    ERR_RANDOM_GEN,
    ERR_UNKNOWN
} error_code_t;

typedef struct {
    uint64_t totalSectors;
    uint64_t totalBytes;
    uint64_t readableSectors;
    uint64_t badSectorCount;
    uint64_t *badSectors;
    size_t badSectorsCapacity;
} analysis_result_t;

typedef struct {
    uint64_t sectorsWiped;
    uint64_t sectorsFailed;
    uint64_t currentPass;
    uint64_t totalPasses;
    time_t startTime;
    time_t endTime;
} wipe_stats_t;

typedef void (*progress_callback_t)(uint64_t current, uint64_t total, int pass, const char *phase);

typedef struct {
    FILE *logFile;
    bool verbose;
    char logPath[MAX_PATH_LEN];
} logger_t;

extern logger_t gLogger;

void logInit(const char *logPath, bool verbose);
void logClose(void);
void logMessage(const char *level, const char *fmt, ...);
void logBadSector(uint64_t sector, const char *operation);

#define LOG_INFO(...)  logMessage("INFO", __VA_ARGS__)
#define LOG_WARN(...)  logMessage("WARN", __VA_ARGS__)
#define LOG_ERROR(...) logMessage("ERROR", __VA_ARGS__)

const char *errorToString(error_code_t err);
void formatBytes(uint64_t bytes, char *buffer, size_t bufferSize);
void formatTime(time_t seconds, char *buffer, size_t bufferSize);

#endif