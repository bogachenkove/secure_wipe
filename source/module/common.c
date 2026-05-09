#include "common.h"
#include <stdarg.h>

size_t gBufferSize = DEFAULT_BUFFER_SIZE;
size_t gBufferSectors = DEFAULT_BUFFER_SIZE / SECTOR_SIZE;

logger_t gLogger = { NULL, false, "" };

int bufferSetSize(size_t sizeBytes)
{
    if (sizeBytes < MIN_BUFFER_SIZE || sizeBytes > MAX_BUFFER_SIZE)
    {
        LOG_ERROR("Buffer size must be between %d and %d bytes", MIN_BUFFER_SIZE, MAX_BUFFER_SIZE);
        return -1;
    }
    if (sizeBytes % SECTOR_SIZE != 0)
    {
        LOG_ERROR("Buffer size must be multiple of sector size (%d)", SECTOR_SIZE);
        return -1;
    }
    gBufferSize = sizeBytes;
    gBufferSectors = sizeBytes / SECTOR_SIZE;
    LOG_INFO("Buffer size set to %zu bytes (%zu sectors)", gBufferSize, gBufferSectors);
    return 0;
}

void bufferGetSizeInfo(size_t *sizeBytes, size_t *sectors)
{
    if (sizeBytes) *sizeBytes = gBufferSize;
    if (sectors) *sectors = gBufferSectors;
}

void logInit(const char *logPath, bool verbose)
{
    gLogger.verbose = verbose;
    if (logPath && strlen(logPath) > 0)
    {
        snprintf(gLogger.logPath, sizeof(gLogger.logPath), "%s", logPath);
        gLogger.logFile = fopen(logPath, "w");
        if (!gLogger.logFile)
            fprintf(stderr, "Warning: Cannot open log file: %s\n", logPath);
    }
}

void logClose(void)
{
    if (gLogger.logFile)
    {
        fclose(gLogger.logFile);
        gLogger.logFile = NULL;
    }
}

void logMessage(const char *level, const char *fmt, ...)
{
    char timestamp[64];
    time_t now = time(NULL);
    struct tm *tmInfo = localtime(&now);
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tmInfo);

    va_list arguments;

    if (gLogger.verbose || strcmp(level, "ERROR") == 0)
    {
        printf("[%s] [%s] ", timestamp, level);
        va_start(arguments, fmt);
        vprintf(fmt, arguments);
        va_end(arguments);
        printf("\n");
        fflush(stdout);
    }

    if (gLogger.logFile)
    {
        fprintf(gLogger.logFile, "[%s] [%s] ", timestamp, level);
        va_start(arguments, fmt);
        vfprintf(gLogger.logFile, fmt, arguments);
        va_end(arguments);
        fprintf(gLogger.logFile, "\n");
        fflush(gLogger.logFile);
    }
}

void logBadSector(uint64_t sector, const char *operation)
{
    logMessage("WARN", "Bad sector detected: %llu during %s",
               (unsigned long long)sector, operation);
}

const char *errorToString(error_code_t err)
{
    switch (err)
    {
        case ERR_OK:           return "Success";
        case ERR_OPEN_DEVICE:  return "Failed to open device";
        case ERR_READ_DEVICE:  return "Failed to read device";
        case ERR_WRITE_DEVICE: return "Failed to write device";
        case ERR_SEEK_DEVICE:  return "Failed to seek device";
        case ERR_GET_SIZE:     return "Failed to get device size";
        case ERR_MEMORY:       return "Memory allocation failed";
        case ERR_PERMISSION:   return "Insufficient permissions";
        case ERR_INVALID_ARG:  return "Invalid argument";
        case ERR_RANDOM_GEN:   return "Random generation failed";
        default:               return "Unknown error";
    }
}

void formatBytes(uint64_t bytes, char *buffer, size_t bufferSize)
{
    const char *units[] = { "B", "KB", "MB", "GB", "TB" };
    int unitIndex = 0;
    double size = (double)bytes;

    while (size >= 1024.0 && unitIndex < 4)
    {
        size /= 1024.0;
        unitIndex++;
    }

    snprintf(buffer, bufferSize, "%.2f %s", size, units[unitIndex]);
}

void formatTime(time_t seconds, char *buffer, size_t bufferSize)
{
    int hours = (int)(seconds / 3600);
    int minutes = (int)((seconds % 3600) / 60);
    int secs = (int)(seconds % 60);

    if (hours > 0)
        snprintf(buffer, bufferSize, "%dh %dm %ds", hours, minutes, secs);
    else if (minutes > 0)
        snprintf(buffer, bufferSize, "%dm %ds", minutes, secs);
    else
        snprintf(buffer, bufferSize, "%ds", secs);
}