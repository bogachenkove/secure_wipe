#include "common.h"
#include <stdarg.h>

size_t g_bufferSize = DEFAULT_BUFFER_SIZE;
size_t g_bufferSectors = DEFAULT_BUFFER_SIZE / SECTOR_SIZE;

logger_t g_logger = { NULL, false, "" };

int bufferSetSize(size_t sizeBytes)
{
    if (sizeBytes < MIN_BUFFER_SIZE || sizeBytes > MAX_BUFFER_SIZE) {
        LOG_ERROR("Buffer size must be between %d and %d bytes", MIN_BUFFER_SIZE, MAX_BUFFER_SIZE);
        return -1;
    }
    if (sizeBytes % SECTOR_SIZE != 0) {
        LOG_ERROR("Buffer size must be multiple of sector size (%d)", SECTOR_SIZE);
        return -1;
    }
    g_bufferSize = sizeBytes;
    g_bufferSectors = sizeBytes / SECTOR_SIZE;
    LOG_INFO("Buffer size set to %zu bytes (%zu sectors)", g_bufferSize, g_bufferSectors);
    return 0;
}

void bufferGetSizeInfo(size_t* sizeBytes, size_t* sectors)
{
    if (sizeBytes) *sizeBytes = g_bufferSize;
    if (sectors) *sectors = g_bufferSectors;
}

void logInit(const char* logPath, bool verbose)
{
    g_logger.verbose = verbose;
    if (logPath && strlen(logPath) > 0) {
        snprintf(g_logger.logPath, sizeof(g_logger.logPath), "%s", logPath);
        g_logger.logFile = fopen(logPath, "w");
        if (!g_logger.logFile) {
            fprintf(stderr, "Warning: Cannot open log file: %s\n", logPath);
        }
    }
}

void logClose(void)
{
    if (g_logger.logFile) {
        fclose(g_logger.logFile);
        g_logger.logFile = NULL;
    }
}

void logMessage(const char* level, const char* fmt, ...)
{
    char timestamp[64];
    time_t now = time(NULL);
    struct tm* tmInfo = localtime(&now);
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", tmInfo);

    va_list arguments;

    if (g_logger.verbose || strcmp(level, "ERROR") == 0) {
        printf("[%s] [%s] ", timestamp, level);
        va_start(arguments, fmt);
        vprintf(fmt, arguments);
        va_end(arguments);
        printf("\n");
        fflush(stdout);
    }

    if (g_logger.logFile) {
        fprintf(g_logger.logFile, "[%s] [%s] ", timestamp, level);
        va_start(arguments, fmt);
        vfprintf(g_logger.logFile, fmt, arguments);
        va_end(arguments);
        fprintf(g_logger.logFile, "\n");
        fflush(g_logger.logFile);
    }
}

void logBadSector(uint64_t sector, const char* operation)
{
    logMessage("WARN", "Bad sector detected: %llu during %s",
        (unsigned long long)sector, operation);
}

const char* errorToString(error_code_t err)
{
    switch (err) {
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

void formatBytes(uint64_t bytes, char* buffer, size_t bufferSize)
{
    const char* units[] = { "B", "KB", "MB", "GB", "TB" };
    int unitIndex = 0;
    double size = (double)bytes;

    while (size >= 1024.0 && unitIndex < 4) {
        size /= 1024.0;
        unitIndex++;
    }

    snprintf(buffer, bufferSize, "%.2f %s", size, units[unitIndex]);
}

void formatTime(time_t seconds, char* buffer, size_t bufferSize)
{
    int hours = (int)(seconds / 3600);
    int minutes = (int)((seconds % 3600) / 60);
    int secs = (int)(seconds % 60);

    if (hours > 0) {
        snprintf(buffer, bufferSize, "%dh %dm %ds", hours, minutes, secs);
    }
    else if (minutes > 0) {
        snprintf(buffer, bufferSize, "%dm %ds", minutes, secs);
    }
    else {
        snprintf(buffer, bufferSize, "%ds", secs);
    }
}