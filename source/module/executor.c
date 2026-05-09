#include "executor.h"
#include "device_io.h"
#include "analyzer.h"
#include "wiper.h"
#include "disk_scanner.h"
#include "platform.h"
#include "ui.h"

int runWipe(const program_config_t* cfg, analysis_result_t* analysis)
{
    device_t* dev = (device_t*)calloc(1, sizeof(device_t));
    extended_analysis_result_t* extAnalysis = (extended_analysis_result_t*)calloc(1, sizeof(extended_analysis_result_t));
    disk_info_t* diskInfo = (disk_info_t*)calloc(1, sizeof(disk_info_t));
    wipe_config_t* wipeConfig = (wipe_config_t*)calloc(1, sizeof(wipe_config_t));
    wipe_stats_t* wipeStats = (wipe_stats_t*)calloc(1, sizeof(wipe_stats_t));

    if (!dev || !extAnalysis || !diskInfo || !wipeConfig || !wipeStats)
    {
        LOG_ERROR("Memory allocation failed");
        free(dev); free(extAnalysis); free(diskInfo); free(wipeConfig); free(wipeStats);
        return 1;
    }

    bool useExtended = cfg->analyzeWriteTest || cfg->quickWpCheck;
    error_code_t wipeError = ERR_OK;

    bool analysisInit = false;
    bool extAnalysisInit = false;

    if (diskScannerGetInfo(cfg->devicePath, diskInfo) == ERR_OK)
    {
        diskScannerPrintDetail(diskInfo);
        if (!cfg->destroyPartitionTable && (diskInfo->isSystem || diskInfo->isBoot))
        {
            fprintf(stderr, "\n*** CRITICAL: System disk - operation aborted.\n");
            free(dev); free(extAnalysis); free(diskInfo); free(wipeConfig); free(wipeStats);
            return 1;
        }
    }

    bool needWriteAccess = useExtended || cfg->destroyPartitionTable;
    if (deviceOpen(dev, cfg->devicePath, !needWriteAccess) != ERR_OK)
    {
        LOG_ERROR("Failed to open device");
        goto cleanup;
    }

    if (isDeviceMounted(cfg->devicePath) && !cfg->destroyPartitionTable)
    {
        LOG_ERROR("Device %s is currently mounted. Unmount it first.", cfg->devicePath);
        deviceClose(dev);
        goto cleanup;
    }

    if (cfg->destroyPartitionTable)
    {
        printf("\n--- Destroying partition table (MBR/GPT) with zeros ---\n");
        error_code_t err = wiperZeroPartitionTable(dev);
        deviceClose(dev);
        if (err == ERR_OK)
            printf("Partition table destroyed successfully.\n");
        else
            printf("Failed to destroy partition table.\n");
        goto cleanup;
    }

    if (cfg->quickWpCheck)
    {
        printf("\n--- Quick write-protection check ---\n");
        uint64_t firstWpSector = 0;
        if (analyzerQuickWpCheck(dev, 16384, &firstWpSector))
        {
            printf("Write-protection detected at sector %llu (%.2f MB)\n",
                (unsigned long long)firstWpSector,
                (double)(firstWpSector * SECTOR_SIZE) / (1024.0 * 1024.0));
        }
        else
        {
            printf("No write-protection detected in first 8MB.\n");
        }
        if (cfg->analyzeOnly && !cfg->analyzeWriteTest)
        {
            deviceClose(dev);
            goto cleanup;
        }
    }

    if (useExtended)
    {
        if (analyzerInitExtendedResult(extAnalysis) != ERR_OK)
        {
            LOG_ERROR("Init extended analysis failed");
            deviceClose(dev);
            goto cleanup;
        }
        extAnalysisInit = true;
    }
    else
    {
        if (analyzerInitResult(analysis) != ERR_OK)
        {
            LOG_ERROR("Init analysis failed");
            deviceClose(dev);
            goto cleanup;
        }
        analysisInit = true;
    }

    printf("\n--- PHASE 1: %s ---\n", useExtended ? "EXTENDED ANALYSIS" : "DEVICE ANALYSIS");
    if (cfg->analyzeWriteTest)
    {
        printf("*** Write test mode - data will be modified! ***\n");
        analyze_flags_t analysisFlags = ANALYZE_WRITE_TEST | ANALYZE_DETECT_WP;
        if (analyzerScanDeviceExtended(dev, extAnalysis, analysisFlags, progressHandler) != ERR_OK)
        {
            LOG_ERROR("Extended analysis failed");
            deviceClose(dev);
            goto cleanup;
        }
        analyzerPrintExtendedReport(extAnalysis);
        memcpy(analysis, &extAnalysis->base, sizeof(analysis_result_t));
    }
    else
    {
        if (analyzerScanDevice(dev, analysis, progressHandler) != ERR_OK)
        {
            LOG_ERROR("Device analysis failed");
            deviceClose(dev);
            goto cleanup;
        }
        analyzerPrintReport(analysis);
    }

    if (cfg->analyzeOnly)
    {
        printf("Analysis complete. Exiting.\n");
        deviceClose(dev);
        goto cleanup;
    }

    deviceClose(dev);

    uint32_t actualPasses;
    if (cfg->method == WIPE_METHOD_RANDOM)
        actualPasses = cfg->passes;
    else
        actualPasses = wiperMethodPasses(cfg->method);
    if (!cfg->autoConfirm && !confirmWipe(cfg->devicePath, analysis->totalBytes, cfg->method, actualPasses))
    {
        printf("Operation cancelled.\n");
        goto cleanup;
    }

    if (deviceOpen(dev, cfg->devicePath, false) != ERR_OK)
    {
        LOG_ERROR("Failed to open device for writing");
        goto cleanup;
    }

    printf("\n--- PHASE 2: Destroy filesystem metadata ---\n");
    wiperPrepareDevice(dev, progressHandler);

    printf("\n--- PHASE 3: Secure wipe ---\n");
    memset(wipeConfig, 0, sizeof(wipe_config_t));
    wipeConfig->method = cfg->method;
    wipeConfig->passes = cfg->passes;
    wipeConfig->skipBadSectors = true;
    wipeConfig->verifyAfterWipe = cfg->verify;
    wipeConfig->badSectors = analysis;
    wipeConfig->progress = progressHandler;

    time_t startTime = time(NULL);
    wipeError = wiperExecute(dev, wipeConfig, wipeStats);
    time_t elapsedTime = time(NULL) - startTime;

    if (wipeError == ERR_OK)
    {
        char timeString[64];
        formatTime(elapsedTime, timeString, sizeof(timeString));
        printf("\n--- Wipe complete ---\nTime: %s\nSectors wiped: %llu\nPasses: %llu\n",
            timeString, (unsigned long long)wipeStats->sectorsWiped, (unsigned long long)wipeStats->totalPasses);
    }
    else
    {
        LOG_ERROR("Wipe failed");
    }

    if (cfg->verify && wipeError == ERR_OK)
    {
        printf("\n--- PHASE 4: Verification ---\n");
        uint64_t verificationErrors = analyzerVerifyWipe(dev, progressHandler);
        if (verificationErrors == 0)
            printf("Verification PASSED.\n");
        else if (verificationErrors == UINT64_MAX)
            printf("Verification FAILED.\n");
        else
            printf("Verification completed with %llu errors.\n", (unsigned long long)verificationErrors);
    }

    deviceClose(dev);

cleanup:
    if (useExtended && extAnalysisInit)
        analyzerFreeExtendedResult(extAnalysis);
    else if (analysisInit)
        analyzerFreeResult(analysis);

    free(dev);
    free(extAnalysis);
    free(diskInfo);
    free(wipeConfig);
    free(wipeStats);
    return (wipeError == ERR_OK) ? 0 : 1;
}