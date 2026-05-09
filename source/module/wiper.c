#include "wiper.h"
#include "random_gen.h"
#include "platform.h"

static uint8_t* gWipeBuffer = NULL;

typedef enum {
    PATTERN_ZERO,
    PATTERN_ONE,
    PATTERN_RANDOM,
    PATTERN_FIXED,
    PATTERN_GUTMANN
} pattern_type_t;

static void fillBufferWithPattern(uint8_t* buffer, size_t size, pattern_type_t patternType,
    const uint8_t* patternData, size_t patternLength)
{
    switch (patternType) {
    case PATTERN_ZERO:
        memset(buffer, 0x00, size);
        break;
    case PATTERN_ONE:
        memset(buffer, 0xFF, size);
        break;
    case PATTERN_FIXED:
        memset(buffer, patternData[0], size);
        break;
    case PATTERN_GUTMANN:
        if (patternLength == 3) {
            for (size_t offset = 0; offset < size; offset += 3) {
                size_t remaining = size - offset;
                if (remaining >= 3) {
                    buffer[offset] = patternData[0];
                    buffer[offset + 1] = patternData[1];
                    buffer[offset + 2] = patternData[2];
                }
                else {
                    for (size_t byteIndex = 0; byteIndex < remaining; byteIndex++) {
                        buffer[offset + byteIndex] = patternData[byteIndex];
                    }
                }
            }
        }
        else if (patternLength == 1) {
            memset(buffer, patternData[0], size);
        }
        break;
    default:
        break;
    }
}

static error_code_t singlePass(device_t* dev, pattern_type_t patternType,
    const uint8_t* patternData, size_t patternLength,
    const analysis_result_t* badSectors,
    progress_callback_t progress,
    int passNumber, int totalPasses,
    const char* description)
{
    if (!dev || !dev->isOpen || !gWipeBuffer) return ERR_INVALID_ARG;

    const device_io_ops_t* ioOps = deviceIoGetOps();
    uint64_t currentSector = 0;
    int lastPercent = -1;
    uint64_t writeErrors = 0;
    char descriptionBuffer[128];

    snprintf(descriptionBuffer, sizeof(descriptionBuffer), "Pass %d/%d: %s",
        passNumber, totalPasses, description);
    LOG_INFO("Starting %s", descriptionBuffer);

    while (currentSector < dev->sectorCount) {
        uint32_t sectorsToWrite = (uint32_t)g_bufferSectors;
        if (currentSector + sectorsToWrite > dev->sectorCount) {
            sectorsToWrite = (uint32_t)(dev->sectorCount - currentSector);
        }

        size_t bufferSize = (size_t)sectorsToWrite * dev->sectorSize;

        if (patternType == PATTERN_RANDOM) {
            if (randomFill(gWipeBuffer, bufferSize) != ERR_OK) {
                LOG_ERROR("Random fill failed at sector %llu", (unsigned long long)currentSector);
                return ERR_RANDOM_GEN;
            }
        }
        else {
            fillBufferWithPattern(gWipeBuffer, bufferSize, patternType, patternData, patternLength);
        }

        if (ioOps->writeSectors(dev, currentSector, sectorsToWrite, gWipeBuffer) != ERR_OK) {
            LOG_WARN("Block write failed at sector %llu, switching to sector-by-sector",
                (unsigned long long)currentSector);
            for (uint32_t offset = 0; offset < sectorsToWrite; offset++) {
                uint64_t targetSector = currentSector + offset;
                if (badSectors && analyzerIsBadSector(badSectors, targetSector)) continue;
                if (patternType == PATTERN_RANDOM) randomFill(gWipeBuffer, dev->sectorSize);
                if (ioOps->writeSectors(dev, targetSector, 1, gWipeBuffer) != ERR_OK) {
                    writeErrors++;
                    logBadSector(targetSector, "write");
                }
            }
        }

        currentSector += sectorsToWrite;

        if (progress) {
            int currentPercent = (int)((currentSector * 100) / dev->sectorCount);
            if (currentPercent != lastPercent) {
                progress(currentSector, dev->sectorCount, passNumber, descriptionBuffer);
                lastPercent = currentPercent;
            }
        }
    }

    ioOps->flush(dev);

    if (writeErrors > 0) {
        LOG_WARN("%s - completed with %llu write errors",
            descriptionBuffer, (unsigned long long)writeErrors);
    }
    else {
        LOG_INFO("%s - completed successfully", descriptionBuffer);
    }

    return ERR_OK;
}

error_code_t wiperInit(void)
{
    if (gWipeBuffer) return ERR_OK;
    gWipeBuffer = (uint8_t*)alignedAlloc(g_bufferSize);
    if (!gWipeBuffer) return ERR_MEMORY;
    LOG_INFO("Wiper module initialized (buffer: %zu bytes, %zu sectors)", g_bufferSize, g_bufferSectors);
    return ERR_OK;
}

void wiperCleanup(void)
{
    if (gWipeBuffer) {
        memset(gWipeBuffer, 0, g_bufferSize);
        alignedFree(gWipeBuffer);
        gWipeBuffer = NULL;
        LOG_INFO("Wiper module cleaned up");
    }
}

const char* wiperMethodName(wipe_method_t method)
{
    switch (method) {
    case WIPE_METHOD_ZERO:       return "Zero Fill";
    case WIPE_METHOD_RANDOM:     return "Random Fill";
    case WIPE_METHOD_DOD_SHORT:  return "DoD 5220.22-M (3-pass)";
    case WIPE_METHOD_DOD_FULL:   return "DoD 5220.22-M ECE (7-pass)";
    case WIPE_METHOD_SCHNEIER:   return "Schneier Method (7-pass)";
    case WIPE_METHOD_GUTMANN:    return "Gutmann Method (35-pass)";
    case WIPE_METHOD_CUSTOM:     return "Custom";
    default:                     return "Unknown";
    }
}

int wiperMethodPasses(wipe_method_t method)
{
    switch (method) {
    case WIPE_METHOD_ZERO:       return 1;
    case WIPE_METHOD_DOD_SHORT:  return 3;
    case WIPE_METHOD_DOD_FULL:   return 7;
    case WIPE_METHOD_SCHNEIER:   return 7;
    case WIPE_METHOD_GUTMANN:    return 35;
    default:                     return 0;
    }
}

error_code_t wiperExecute(device_t* dev, const wipe_config_t* config, wipe_stats_t* stats)
{
    if (!dev || !dev->isOpen || !config) return ERR_INVALID_ARG;

    if (!gWipeBuffer) {
        LOG_ERROR("Wiper not initialized");
        return ERR_MEMORY;
    }

    if (stats) {
        memset(stats, 0, sizeof(wipe_stats_t));
        stats->startTime = time(NULL);
        stats->totalPasses = config->passes;
    }

    LOG_INFO("Starting secure wipe: %s on %s", wiperMethodName(config->method), dev->path);

    error_code_t resultCode = ERR_OK;
    wiper_method_func methodFunction = NULL;

    switch (config->method) {
    case WIPE_METHOD_ZERO:      methodFunction = wiperMethodZero; break;
    case WIPE_METHOD_RANDOM:    methodFunction = wiperMethodRandom; break;
    case WIPE_METHOD_DOD_SHORT: methodFunction = wiperMethodDoDShort; break;
    case WIPE_METHOD_DOD_FULL:  methodFunction = wiperMethodDoDFull; break;
    case WIPE_METHOD_SCHNEIER:  methodFunction = wiperMethodSchneier; break;
    case WIPE_METHOD_GUTMANN:   methodFunction = wiperMethodGutmann; break;
    case WIPE_METHOD_CUSTOM:    methodFunction = wiperMethodCustom; break;
    default:                    return ERR_INVALID_ARG;
    }

    resultCode = methodFunction(dev, config->passes, config->progress);

    if (stats) {
        stats->endTime = time(NULL);
        if (resultCode == ERR_OK) stats->sectorsWiped = dev->sectorCount * stats->totalPasses;
    }

    if (resultCode == ERR_OK) LOG_INFO("Wipe completed successfully");
    else LOG_ERROR("Wipe failed: %s", errorToString(resultCode));

    return resultCode;
}

error_code_t wiperMethodZero(device_t* dev, uint32_t passes, progress_callback_t progress)
{
    (void)passes;
    uint8_t zeroByte = 0x00;
    return singlePass(dev, PATTERN_ZERO, &zeroByte, 1, NULL, progress, 1, 1, "Zero fill (0x00)");
}

error_code_t wiperMethodRandom(device_t* dev, uint32_t passes, progress_callback_t progress)
{
    for (uint32_t passIndex = 1; passIndex <= passes; passIndex++) {
        char description[64];
        snprintf(description, sizeof(description), "Random fill (pass %u/%u)", passIndex, passes);
        error_code_t errorCode = singlePass(dev, PATTERN_RANDOM, NULL, 0, NULL,
            progress, (int)passIndex, (int)passes, description);
        if (errorCode != ERR_OK) return errorCode;
    }
    return ERR_OK;
}

error_code_t wiperMethodDoDShort(device_t* dev, uint32_t passes, progress_callback_t progress)
{
    (void)passes;
    uint8_t zeroByte = 0x00, oneByte = 0xFF;
    error_code_t errorCode;

    errorCode = singlePass(dev, PATTERN_ZERO, &zeroByte, 1, NULL, progress, 1, 3, "DoD: Zero fill (0x00)");
    if (errorCode != ERR_OK) return errorCode;
    errorCode = singlePass(dev, PATTERN_ONE, &oneByte, 1, NULL, progress, 2, 3, "DoD: One fill (0xFF)");
    if (errorCode != ERR_OK) return errorCode;
    return singlePass(dev, PATTERN_RANDOM, NULL, 0, NULL, progress, 3, 3, "DoD: Random data");
}

error_code_t wiperMethodDoDFull(device_t* dev, uint32_t passes, progress_callback_t progress)
{
    (void)passes;
    uint8_t zeroByte = 0x00, oneByte = 0xFF;
    error_code_t errorCode;
    const int totalPasses = 7;

    for (int passIndex = 0; passIndex < totalPasses; passIndex++) {
        const char* description;
        pattern_type_t patternType;
        const uint8_t* patternData = NULL;
        size_t patternLength = 0;

        switch (passIndex) {
        case 0: patternType = PATTERN_ZERO; patternData = &zeroByte; patternLength = 1; description = "DoD ECE: Zero fill #1"; break;
        case 1: patternType = PATTERN_ONE;  patternData = &oneByte;  patternLength = 1; description = "DoD ECE: One fill #1"; break;
        case 2: patternType = PATTERN_RANDOM; description = "DoD ECE: Random #1"; break;
        case 3: patternType = PATTERN_ZERO; patternData = &zeroByte; patternLength = 1; description = "DoD ECE: Zero fill #2"; break;
        case 4: patternType = PATTERN_ONE;  patternData = &oneByte;  patternLength = 1; description = "DoD ECE: One fill #2"; break;
        case 5: patternType = PATTERN_RANDOM; description = "DoD ECE: Random #2"; break;
        default: patternType = PATTERN_RANDOM; description = "DoD ECE: Random verification"; break;
        }

        errorCode = singlePass(dev, patternType, patternData, patternLength, NULL, progress,
            passIndex + 1, totalPasses, description);
        if (errorCode != ERR_OK) return errorCode;
    }
    return ERR_OK;
}

error_code_t wiperMethodSchneier(device_t* dev, uint32_t passes, progress_callback_t progress)
{
    (void)passes;
    uint8_t zeroByte = 0x00, oneByte = 0xFF;
    error_code_t errorCode;

    errorCode = singlePass(dev, PATTERN_ZERO, &zeroByte, 1, NULL, progress, 1, 7, "Schneier: Zero fill");
    if (errorCode != ERR_OK) return errorCode;
    errorCode = singlePass(dev, PATTERN_ONE, &oneByte, 1, NULL, progress, 2, 7, "Schneier: One fill");
    if (errorCode != ERR_OK) return errorCode;

    for (int passIndex = 3; passIndex <= 7; passIndex++) {
        char description[64];
        snprintf(description, sizeof(description), "Schneier: Random #%d", passIndex - 2);
        errorCode = singlePass(dev, PATTERN_RANDOM, NULL, 0, NULL, progress,
            passIndex, 7, description);
        if (errorCode != ERR_OK) return errorCode;
    }
    return ERR_OK;
}

error_code_t wiperMethodGutmann(device_t* dev, uint32_t passes, progress_callback_t progress)
{
    (void)passes;
    static const uint8_t gutmannPatterns[35][3] = {
        {0x00,0x00,0x00},{0x00,0x00,0x00},{0x00,0x00,0x00},{0x00,0x00,0x00},
        {0x55,0x55,0x55},{0xAA,0xAA,0xAA},{0x92,0x49,0x24},{0x49,0x24,0x92},
        {0x24,0x92,0x49},{0x00,0x00,0x00},{0x11,0x11,0x11},{0x22,0x22,0x22},
        {0x33,0x33,0x33},{0x44,0x44,0x44},{0x55,0x55,0x55},{0x66,0x66,0x66},
        {0x77,0x77,0x77},{0x88,0x88,0x88},{0x99,0x99,0x99},{0xAA,0xAA,0xAA},
        {0xBB,0xBB,0xBB},{0xCC,0xCC,0xCC},{0xDD,0xDD,0xDD},{0xEE,0xEE,0xEE},
        {0xFF,0xFF,0xFF},{0x92,0x49,0x24},{0x49,0x24,0x92},{0x24,0x92,0x49},
        {0x6D,0xB6,0xDB},{0xB6,0xDB,0x6D},{0xDB,0x6D,0xB6},
        {0x00,0x00,0x00},{0x00,0x00,0x00},{0x00,0x00,0x00},{0x00,0x00,0x00}
    };
    static const bool isRandom[35] = {
        true,true,true,true, false,false,false,false, false,false,false,false,
        false,false,false,false, false,false,false,false, false,false,false,false,
        false,false,false,false, false,false,false, true,true,true,true
    };
    static const char* descriptions[35] = {
        "Random 1/4","Random 2/4","Random 3/4","Random 4/4",
        "0x55","0xAA","0x924924","0x492492","0x249249","0x00","0x11","0x22",
        "0x33","0x44","0x55","0x66","0x77","0x88","0x99","0xAA","0xBB","0xCC",
        "0xDD","0xEE","0xFF","0x924924","0x492492","0x249249","0x6DB6DB","0xB6DB6D","0xDB6DB6",
        "Random final 1/4","Random final 2/4","Random final 3/4","Random final 4/4"
    };
    error_code_t errorCode;

    for (int patternIndex = 0; patternIndex < 35; patternIndex++) {
        pattern_type_t patternType = isRandom[patternIndex] ? PATTERN_RANDOM : PATTERN_GUTMANN;
        errorCode = singlePass(dev, patternType, gutmannPatterns[patternIndex], 3, NULL, progress,
            patternIndex + 1, 35, descriptions[patternIndex]);
        if (errorCode != ERR_OK) return errorCode;
    }
    return ERR_OK;
}

error_code_t wiperMethodCustom(device_t* dev, uint32_t passes, progress_callback_t progress)
{
    error_code_t errorCode;
    for (uint32_t passIndex = 1; passIndex <= passes; passIndex++) {
        int patternMod = (passIndex - 1) % 4;
        pattern_type_t patternType;
        const uint8_t* patternData = NULL;
        size_t patternLength = 0;
        const char* baseDescription;

        switch (patternMod) {
        case 0:
            patternType = PATTERN_ZERO;
            patternData = (const uint8_t*)"\x00";
            patternLength = 1;
            baseDescription = "Zero fill (0x00)";
            break;
        case 1:
            patternType = PATTERN_RANDOM;
            baseDescription = "Random data";
            break;
        case 2:
            patternType = PATTERN_ONE;
            patternData = (const uint8_t*)"\xFF";
            patternLength = 1;
            baseDescription = "One fill (0xFF)";
            break;
        default:
            patternType = PATTERN_RANDOM;
            baseDescription = "Random data";
            break;
        }

        char descriptionBuffer[64];
        snprintf(descriptionBuffer, sizeof(descriptionBuffer), "%s (pass %u/%u)",
            baseDescription, passIndex, passes);
        errorCode = singlePass(dev, patternType, patternData, patternLength, NULL, progress,
            (int)passIndex, (int)passes, descriptionBuffer);
        if (errorCode != ERR_OK) return errorCode;
    }
    return ERR_OK;
}

static void overwriteSectorRange(device_t* dev, uint64_t startSector, uint32_t sectorCount,
    uint8_t fillByte)
{
    if (!dev || !dev->isOpen || dev->readOnly) return;
    const device_io_ops_t* ioOps = deviceIoGetOps();
    size_t bufferSize = (size_t)sectorCount * SECTOR_SIZE;
    uint8_t* buffer = (uint8_t*)alignedAlloc(bufferSize);
    if (!buffer) {
        LOG_ERROR("Failed to allocate buffer for partition table destruction");
        return;
    }
    memset(buffer, fillByte, bufferSize);
    if (ioOps->writeSectors(dev, startSector, sectorCount, buffer) != ERR_OK) {
        LOG_WARN("Block write failed, trying sector-by-sector");
        for (uint32_t offset = 0; offset < sectorCount; offset++) {
            memset(buffer, fillByte, SECTOR_SIZE);
            ioOps->writeSectors(dev, startSector + offset, 1, buffer);
        }
    }
    alignedFree(buffer);
}

error_code_t wiperZeroPartitionTable(device_t* dev)
{
    if (!dev || !dev->isOpen) return ERR_INVALID_ARG;
    if (dev->readOnly) {
        LOG_ERROR("Device opened read-only, cannot destroy partition table");
        return ERR_PERMISSION;
    }
    LOG_INFO("Destroying partition table (MBR+GPT) with zeros");
    uint32_t sectorsToWrite = 34;
    if (sectorsToWrite > dev->sectorCount)
        sectorsToWrite = (uint32_t)dev->sectorCount;
    overwriteSectorRange(dev, 0, sectorsToWrite, 0x00);
    if (dev->sectorCount > 33) {
        uint64_t backupStart = dev->sectorCount - 33;
        LOG_INFO("Overwriting backup GPT (sectors %llu-%llu) with zeros",
            (unsigned long long)backupStart, (unsigned long long)(dev->sectorCount - 1));
        overwriteSectorRange(dev, backupStart, 33, 0x00);
    }
    deviceIoGetOps()->flush(dev);
    LOG_INFO("Partition table destruction complete");
    return ERR_OK;
}

error_code_t wiperDestroyFilesystemMetadata(device_t* dev, int passes, progress_callback_t progress)
{
    if (!dev || !dev->isOpen || dev->readOnly || passes <= 0) return ERR_INVALID_ARG;

    LOG_INFO("Destroying filesystem metadata (%d passes)", passes);
    uint64_t metadataSectors = 8192;
    if (metadataSectors > dev->sectorCount / 2) metadataSectors = dev->sectorCount / 2;

    const device_io_ops_t* ioOps = deviceIoGetOps();
    uint8_t* buffer = (uint8_t*)alignedAlloc(g_bufferSize);
    if (!buffer) return ERR_MEMORY;

    for (int passIndex = 1; passIndex <= passes; passIndex++) {
        uint64_t currentSector = 0;
        while (currentSector < metadataSectors) {
            uint32_t sectorsToWrite = (uint32_t)g_bufferSectors;
            if (currentSector + sectorsToWrite > metadataSectors) {
                sectorsToWrite = (uint32_t)(metadataSectors - currentSector);
            }
            randomFill(buffer, (size_t)sectorsToWrite * SECTOR_SIZE);
            if (ioOps->writeSectors(dev, currentSector, sectorsToWrite, buffer) != ERR_OK) {
                for (uint32_t offset = 0; offset < sectorsToWrite; offset++) {
                    randomFill(buffer, SECTOR_SIZE);
                    ioOps->writeSectors(dev, currentSector + offset, 1, buffer);
                }
            }
            currentSector += sectorsToWrite;
            if (progress) {
                progress(currentSector, metadataSectors * 2, passIndex, "Destroying metadata (start)");
            }
        }

        if (dev->sectorCount > metadataSectors) {
            uint64_t endStart = dev->sectorCount - metadataSectors;
            currentSector = endStart;
            while (currentSector < dev->sectorCount) {
                uint32_t sectorsToWrite = (uint32_t)g_bufferSectors;
                if (currentSector + sectorsToWrite > dev->sectorCount) {
                    sectorsToWrite = (uint32_t)(dev->sectorCount - currentSector);
                }
                randomFill(buffer, (size_t)sectorsToWrite * SECTOR_SIZE);
                if (ioOps->writeSectors(dev, currentSector, sectorsToWrite, buffer) != ERR_OK) {
                    for (uint32_t offset = 0; offset < sectorsToWrite; offset++) {
                        randomFill(buffer, SECTOR_SIZE);
                        ioOps->writeSectors(dev, currentSector + offset, 1, buffer);
                    }
                }
                currentSector += sectorsToWrite;
                if (progress) {
                    progress(metadataSectors + (currentSector - endStart), metadataSectors * 2,
                        passIndex, "Destroying metadata (end)");
                }
            }
        }
        ioOps->flush(dev);
    }

    alignedFree(buffer);
    LOG_INFO("Filesystem metadata destruction complete");
    return ERR_OK;
}

bool wiperTryRemoveWriteProtection(device_t* dev)
{
    if (!dev || !dev->isOpen) return false;

    LOG_INFO("Attempting to remove write protection...");

#ifdef _WIN32
    DWORD bytesReturned;
    typedef struct {
        ULONG Version;
        BOOLEAN Persist;
        BYTE Reserved1[3];
        ULONGLONG Attributes;
        ULONGLONG AttributesMask;
        ULONG Reserved2[4];
    } SET_DISK_ATTRIBUTES;
#define IOCTL_DISK_SET_DISK_ATTRIBUTES 0x0007C0F4
#define DISK_ATTRIBUTE_READ_ONLY 0x0000000000000002ULL

    SET_DISK_ATTRIBUTES attributes = { 0 };
    attributes.Version = sizeof(attributes);
    attributes.Persist = TRUE;
    attributes.AttributesMask = DISK_ATTRIBUTE_READ_ONLY;

    if (DeviceIoControl(dev->handle, IOCTL_DISK_SET_DISK_ATTRIBUTES,
        &attributes, sizeof(attributes), NULL, 0, &bytesReturned, NULL)) {
        return true;
    }

    CloseHandle(dev->handle);
    dev->handle = CreateFileA(dev->path,
        GENERIC_READ | GENERIC_WRITE,
        0,
        NULL,
        OPEN_EXISTING,
        FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH,
        NULL);
    if (dev->handle != INVALID_HANDLE_VALUE) {
        dev->isOpen = true;
        return true;
    }
    dev->isOpen = false;
#else
    int readonlyFlag = 0;
    if (ioctl(dev->handle, BLKROSET, &readonlyFlag) == 0) return true;
    LOG_WARN("BLKROSET failed: %s", strerror(errno));
#endif
    return false;
}

error_code_t wiperPrepareDevice(device_t* dev, progress_callback_t progress)
{
    if (!dev || !dev->isOpen) return ERR_INVALID_ARG;
    LOG_INFO("Preparing device for secure wipe");
    if (dev->readOnly) return ERR_PERMISSION;

    wiperTryRemoveWriteProtection(dev);
    return wiperDestroyFilesystemMetadata(dev, 3, progress);
}