#include "metadata.h"
#include "device_io.h"
#include "analyzer.h"
#include "disk_scanner.h"
#include "random_gen.h"
#include "platform.h"

#ifdef _WIN32
#define strcasecmp _stricmp
#endif

void metadataDefaultConfig(program_config_t* cfg)
{
    memset(cfg, 0, sizeof(program_config_t));
    snprintf(cfg->logPath, sizeof(cfg->logPath), "%s", "secure_wipe.log");
    cfg->method = WIPE_METHOD_DOD_SHORT;
    cfg->passes = 3;
    cfg->verify = true;
    cfg->verbose = true;
    cfg->bufferSize = DEFAULT_BUFFER_SIZE;
    cfg->destroyPartitionTable = false;
}

static void progressHandler(uint64_t current, uint64_t total, int pass, const char* phase)
{
    static int lastPercent = -1;
    int currentPercent = (int)((current * 100) / total);
    if (currentPercent != lastPercent) {
        printf("\r[%-40s] %3d%% - %s", "########################################", currentPercent, phase);
        fflush(stdout);
        lastPercent = currentPercent;
        if (currentPercent == 100) lastPercent = -1;
    }
}

static bool confirmWipe(const char* devicePath, uint64_t sizeBytes, wipe_method_t method)
{
    char sizeString[32];
    formatBytes(sizeBytes, sizeString, sizeof(sizeString));
    printf("\n--- WARNING ---\n");
    printf("Device: %s\nSize:   %s\nMethod: %s\nPasses: %d\n",
        devicePath, sizeString, wiperMethodName(method),
        wiperMethodPasses(method) ? wiperMethodPasses(method) : 0);
    printf("This operation CANNOT be undone!\n");
    printf("Type 'YES' (all caps) to confirm: ");
    fflush(stdout);
    char response[16] = { 0 };
    if (!fgets(response, sizeof(response), stdin)) return false;
    size_t responseLength = strlen(response);
    if (responseLength && response[responseLength - 1] == '\n') response[responseLength - 1] = '\0';
    return strcmp(response, "YES") == 0;
}

static size_t parseSizeWithUnit(const char* arg)
{
    char* endptr;
    unsigned long long value = strtoull(arg, &endptr, 10);
    if (endptr == arg) return 0;
    size_t multiplier = 1;
    while (*endptr == ' ') endptr++;
    if (strcasecmp(endptr, "B") == 0 || *endptr == '\0') multiplier = 1;
    else if (strcasecmp(endptr, "KB") == 0) multiplier = 1024;
    else if (strcasecmp(endptr, "MB") == 0) multiplier = 1024 * 1024;
    else if (strcasecmp(endptr, "GB") == 0) multiplier = 1024 * 1024 * 1024;
    else if (strcasecmp(endptr, "TB") == 0) multiplier = (size_t)1024 * 1024 * 1024 * 1024;
    else return 0;
    return (size_t)(value * multiplier);
}

bool metadataInteractiveSelectDisk(program_config_t* cfg)
{
    disk_scan_result_t* scanResult = (disk_scan_result_t*)malloc(sizeof(disk_scan_result_t));
    if (!scanResult) {
        fprintf(stderr, "Memory allocation failed\n");
        return false;
    }
    printf("Scanning for available disks...\n\n");
    if (diskScannerScan(scanResult) != ERR_OK) {
        fprintf(stderr, "Failed to scan disks\n");
        free(scanResult);
        return false;
    }
    if (scanResult->count == 0) {
        fprintf(stderr, "No disks found\n");
        free(scanResult);
        return false;
    }
    diskScannerPrintList(scanResult, cfg->showAllDisks);
    int safeCount = 0;
    for (int diskIndex = 0; diskIndex < scanResult->count; diskIndex++) {
        if (diskScannerIsSafeToWipe(&scanResult->disks[diskIndex])) safeCount++;
    }
    if (safeCount == 0) {
        fprintf(stderr, "No safe disks available.\n");
        free(scanResult);
        return false;
    }
    printf("Enter disk number (1-%d) or 'q': ", scanResult->count);
    fflush(stdout);
    char input[16];
    if (!fgets(input, sizeof(input), stdin)) {
        free(scanResult);
        return false;
    }
    if (input[0] == 'q' || input[0] == 'Q') {
        free(scanResult);
        return false;
    }
    int selection = atoi(input);
    const disk_info_t* selectedDisk = diskScannerGetByIndex(scanResult, selection);
    if (!selectedDisk) {
        fprintf(stderr, "Invalid selection\n");
        free(scanResult);
        return false;
    }
    if (!diskScannerIsSafeToWipe(selectedDisk)) {
        fprintf(stderr, "System disk selected - not allowed.\n");
        free(scanResult);
        return false;
    }
    diskScannerPrintDetail(selectedDisk);
    printf("Confirm this disk? (y/n): ");
    fflush(stdout);
    if (!fgets(input, sizeof(input), stdin)) {
        free(scanResult);
        return false;
    }
    if (input[0] != 'y' && input[0] != 'Y') {
        free(scanResult);
        return false;
    }
    printf("\nSelect wipe method:\n");
    printf("  1. Zero Fill\n  2. DoD 3-pass (default)\n  3. DoD 7-pass\n  4. Schneier\n  5. Gutmann\n  6. Random\n");
    printf("Enter choice (1-6) [2]: ");
    fflush(stdout);
    if (!fgets(input, sizeof(input), stdin)) input[0] = '2';
    int choice = atoi(input);
    if (choice == 0) choice = 2;
    switch (choice) {
    case 1: cfg->method = WIPE_METHOD_ZERO; cfg->passes = 1; break;
    case 2: cfg->method = WIPE_METHOD_DOD_SHORT; cfg->passes = 3; break;
    case 3: cfg->method = WIPE_METHOD_DOD_FULL; cfg->passes = 7; break;
    case 4: cfg->method = WIPE_METHOD_SCHNEIER; cfg->passes = 7; break;
    case 5: cfg->method = WIPE_METHOD_GUTMANN; cfg->passes = 35; break;
    case 6:
        cfg->method = WIPE_METHOD_RANDOM;
        printf("Number of random passes (1-100) [3]: ");
        fflush(stdout);
        if (fgets(input, sizeof(input), stdin)) {
            int passesValue = atoi(input);
            cfg->passes = (passesValue > 0 && passesValue <= 100) ? (uint32_t)passesValue : 3;
        }
        else {
            cfg->passes = 3;
        }
        break;
    default: cfg->method = WIPE_METHOD_DOD_SHORT; cfg->passes = 3; break;
    }
    snprintf(cfg->devicePath, sizeof(cfg->devicePath), "%s", selectedDisk->devicePath);
    free(scanResult);
    return true;
}

void metadataPrintUsage(const char* programName)
{
    printf("Usage: %s [options] <device>\n", programName);
    printf("       %s --list [-A]\n", programName);
    printf("       %s --select\n\n", programName);
    printf("Options:\n");
    printf("  -L, --list           List disks\n");
    printf("  -S, --select         Interactive disk selection\n");
    printf("  -A                   Show all disks (including system)\n");
    printf("  -z, --zero           Zero fill (1 pass)\n");
    printf("  -r N, --random N     Random data (N passes)\n");
    printf("  -d, --dod            DoD 5220.22-M short (3 passes) [default]\n");
    printf("  -D, --dod-full       DoD 5220.22-M ECE (7 passes)\n");
    printf("  -s, --schneier       Schneier method (7 passes)\n");
    printf("  -g, --gutmann        Gutmann method (35 passes)\n");
    printf("  -b, --buffer SIZE    I/O buffer size (e.g. 1MB, 512KB, 4GB)\n");
    printf("  -a, --analyze        Read-only analysis\n");
    printf("  --analyze-write      Analysis with write test (destroys data!)\n");
    printf("  --wp-check           Quick write-protection check\n");
    printf("  --destroy-partition-table   Destroy MBR/GPT only (zero out, no data wipe)\n");
    printf("  -v, --verify         Verify after wipe (default)\n");
    printf("  -n, --no-verify      Skip verification\n");
    printf("  -l FILE              Log file (default: secure_wipe.log)\n");
    printf("  -y, --yes            Auto-confirm (dangerous)\n");
    printf("  -q, --quiet          Quiet mode\n");
    printf("  -h, --help           This help\n");
    printf("  --methods            List wipe methods\n");
    printf("\nNote: --destroy-partition-table does not require confirmation and does not overwrite data.\n");
}

void metadataPrintMethods(void)
{
    printf("Wipe methods:\n");
    printf("  Zero     : 1 pass of zeros\n");
    printf("  Random   : N passes of random data\n");
    printf("  DoD short: 3 passes: 0x00, 0xFF, random\n");
    printf("  DoD full : 7 passes extended DoD\n");
    printf("  Schneier : 7 passes (0x00, 0xFF, 5x random)\n");
    printf("  Gutmann  : 35 passes (MFM/RLL patterns + random)\n");
}

bool metadataParseArguments(int argc, char* argv[], program_config_t* cfg)
{
    metadataDefaultConfig(cfg);
    if (argc < 2) {
        cfg->selectDisk = true;
        return true;
    }
    for (int argIndex = 1; argIndex < argc; argIndex++) {
        if (strcmp(argv[argIndex], "-h") == 0 || strcmp(argv[argIndex], "--help") == 0) {
            metadataPrintUsage(argv[0]);
            exit(0);
        }
        else if (strcmp(argv[argIndex], "--methods") == 0) {
            metadataPrintMethods();
            exit(0);
        }
        else if (strcmp(argv[argIndex], "-L") == 0 || strcmp(argv[argIndex], "--list") == 0) {
            cfg->listDisks = true;
        }
        else if (strcmp(argv[argIndex], "-S") == 0 || strcmp(argv[argIndex], "--select") == 0) {
            cfg->selectDisk = true;
        }
        else if (strcmp(argv[argIndex], "-A") == 0) {
            cfg->showAllDisks = true;
        }
        else if (strcmp(argv[argIndex], "-z") == 0 || strcmp(argv[argIndex], "--zero") == 0) {
            cfg->method = WIPE_METHOD_ZERO;
            cfg->passes = 1;
        }
        else if (strcmp(argv[argIndex], "-r") == 0 || strcmp(argv[argIndex], "--random") == 0) {
            cfg->method = WIPE_METHOD_RANDOM;
            if (argIndex + 1 < argc && argv[argIndex + 1][0] != '-') {
                cfg->passes = (uint32_t)atoi(argv[++argIndex]);
                if (cfg->passes < 1 || cfg->passes > 100) return false;
            }
            else {
                cfg->passes = 3;
            }
        }
        else if (strcmp(argv[argIndex], "-d") == 0 || strcmp(argv[argIndex], "--dod") == 0) {
            cfg->method = WIPE_METHOD_DOD_SHORT;
            cfg->passes = 3;
        }
        else if (strcmp(argv[argIndex], "-D") == 0 || strcmp(argv[argIndex], "--dod-full") == 0) {
            cfg->method = WIPE_METHOD_DOD_FULL;
            cfg->passes = 7;
        }
        else if (strcmp(argv[argIndex], "-s") == 0 || strcmp(argv[argIndex], "--schneier") == 0) {
            cfg->method = WIPE_METHOD_SCHNEIER;
            cfg->passes = 7;
        }
        else if (strcmp(argv[argIndex], "-g") == 0 || strcmp(argv[argIndex], "--gutmann") == 0) {
            cfg->method = WIPE_METHOD_GUTMANN;
            cfg->passes = 35;
        }
        else if (strcmp(argv[argIndex], "-b") == 0 || strcmp(argv[argIndex], "--buffer") == 0) {
            if (argIndex + 1 < argc) {
                size_t newSize = parseSizeWithUnit(argv[++argIndex]);
                if (newSize == 0 || bufferSetSize(newSize) != 0) {
                    fprintf(stderr, "Invalid buffer size. Use format like 1MB, 512KB, 4GB.\n");
                    return false;
                }
                cfg->bufferSize = newSize;
            }
            else {
                return false;
            }
        }
        else if (strcmp(argv[argIndex], "-a") == 0 || strcmp(argv[argIndex], "--analyze") == 0) {
            cfg->analyzeOnly = true;
        }
        else if (strcmp(argv[argIndex], "--analyze-write") == 0) {
            cfg->analyzeOnly = true;
            cfg->analyzeWriteTest = true;
        }
        else if (strcmp(argv[argIndex], "--wp-check") == 0) {
            cfg->analyzeOnly = true;
            cfg->quickWpCheck = true;
        }
        else if (strcmp(argv[argIndex], "--destroy-partition-table") == 0) {
            cfg->destroyPartitionTable = true;
            cfg->analyzeOnly = true;
        }
        else if (strcmp(argv[argIndex], "-v") == 0 || strcmp(argv[argIndex], "--verify") == 0) {
            cfg->verify = true;
        }
        else if (strcmp(argv[argIndex], "-n") == 0 || strcmp(argv[argIndex], "--no-verify") == 0) {
            cfg->verify = false;
        }
        else if (strcmp(argv[argIndex], "-l") == 0) {
            if (argIndex + 1 < argc) snprintf(cfg->logPath, sizeof(cfg->logPath), "%s", argv[++argIndex]);
        }
        else if (strcmp(argv[argIndex], "-y") == 0 || strcmp(argv[argIndex], "--yes") == 0) {
            cfg->autoConfirm = true;
        }
        else if (strcmp(argv[argIndex], "-q") == 0 || strcmp(argv[argIndex], "--quiet") == 0) {
            cfg->verbose = false;
        }
        else if (argv[argIndex][0] != '-') {
            snprintf(cfg->devicePath, sizeof(cfg->devicePath), "%s", argv[argIndex]);
        }
        else {
            return false;
        }
    }
    return true;
}

int metadataRun(program_config_t* cfg)
{
    device_t* dev = (device_t*)calloc(1, sizeof(device_t));
    analysis_result_t* analysis = (analysis_result_t*)calloc(1, sizeof(analysis_result_t));
    extended_analysis_result_t* extAnalysis = (extended_analysis_result_t*)calloc(1, sizeof(extended_analysis_result_t));
    disk_info_t* diskInfo = (disk_info_t*)calloc(1, sizeof(disk_info_t));
    wipe_config_t* wipeConfig = (wipe_config_t*)calloc(1, sizeof(wipe_config_t));
    wipe_stats_t* wipeStats = (wipe_stats_t*)calloc(1, sizeof(wipe_stats_t));

    if (!dev || !analysis || !extAnalysis || !diskInfo || !wipeConfig || !wipeStats) {
        LOG_ERROR("Memory allocation failed");
        free(dev); free(analysis); free(extAnalysis); free(diskInfo); free(wipeConfig); free(wipeStats);
        return 1;
    }

    bool useExtended = cfg->analyzeWriteTest || cfg->quickWpCheck;
    error_code_t wipeError = ERR_OK;

    if (diskScannerGetInfo(cfg->devicePath, diskInfo) == ERR_OK) {
        diskScannerPrintDetail(diskInfo);
        if (!cfg->destroyPartitionTable && (diskInfo->isSystem || diskInfo->isBoot)) {
            fprintf(stderr, "\n*** CRITICAL: System disk - operation aborted.\n");
            free(dev); free(analysis); free(extAnalysis); free(diskInfo); free(wipeConfig); free(wipeStats);
            return 1;
        }
    }

    bool needWriteAccess = useExtended || cfg->destroyPartitionTable;
    if (deviceOpen(dev, cfg->devicePath, !needWriteAccess) != ERR_OK) {
        LOG_ERROR("Failed to open device");
        goto cleanup;
    }

    if (isDeviceMounted(cfg->devicePath) && !cfg->destroyPartitionTable) {
        LOG_ERROR("Device %s is currently mounted. Unmount it first.", cfg->devicePath);
        deviceClose(dev);
        goto cleanup;
    }

    if (cfg->destroyPartitionTable) {
        printf("\n--- Destroying partition table (MBR/GPT) with zeros ---\n");
        error_code_t err = wiperZeroPartitionTable(dev);
        deviceClose(dev);
        if (err == ERR_OK) printf("Partition table destroyed successfully.\n");
        else printf("Failed to destroy partition table.\n");
        goto cleanup;
    }

    if (cfg->quickWpCheck) {
        printf("\n--- Quick write-protection check ---\n");
        uint64_t firstWpSector = 0;
        if (analyzerQuickWpCheck(dev, 16384, &firstWpSector)) {
            printf("Write-protection detected at sector %llu (%.2f MB)\n",
                (unsigned long long)firstWpSector,
                (double)(firstWpSector * SECTOR_SIZE) / (1024.0 * 1024.0));
        }
        else {
            printf("No write-protection detected in first 8MB.\n");
        }
        if (cfg->analyzeOnly && !cfg->analyzeWriteTest) {
            deviceClose(dev);
            goto cleanup;
        }
    }

    if (useExtended) {
        if (analyzerInitExtendedResult(extAnalysis) != ERR_OK) {
            LOG_ERROR("Init extended analysis failed");
            deviceClose(dev);
            goto cleanup;
        }
    }
    else {
        if (analyzerInitResult(analysis) != ERR_OK) {
            LOG_ERROR("Init analysis failed");
            deviceClose(dev);
            goto cleanup;
        }
    }

    printf("\n--- PHASE 1: %s ---\n", useExtended ? "EXTENDED ANALYSIS" : "DEVICE ANALYSIS");
    if (cfg->analyzeWriteTest) {
        printf("*** Write test mode - data will be modified! ***\n");
        analyze_flags_t analysisFlags = ANALYZE_WRITE_TEST | ANALYZE_DETECT_WP;
        if (analyzerScanDeviceExtended(dev, extAnalysis, analysisFlags, progressHandler) != ERR_OK) {
            LOG_ERROR("Extended analysis failed");
            deviceClose(dev);
            goto cleanup;
        }
        analyzerPrintExtendedReport(extAnalysis);
        memcpy(analysis, &extAnalysis->base, sizeof(analysis_result_t));
    }
    else {
        if (analyzerScanDevice(dev, analysis, progressHandler) != ERR_OK) {
            LOG_ERROR("Device analysis failed");
            deviceClose(dev);
            goto cleanup;
        }
        analyzerPrintReport(analysis);
    }

    if (cfg->analyzeOnly) {
        printf("Analysis complete. Exiting.\n");
        deviceClose(dev);
        goto cleanup;
    }

    deviceClose(dev);
    if (!cfg->autoConfirm && !confirmWipe(cfg->devicePath, analysis->totalBytes, cfg->method)) {
        printf("Operation cancelled.\n");
        goto cleanup;
    }

    if (deviceOpen(dev, cfg->devicePath, false) != ERR_OK) {
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

    if (wipeError == ERR_OK) {
        char timeString[64];
        formatTime(elapsedTime, timeString, sizeof(timeString));
        printf("\n--- Wipe complete ---\nTime: %s\nSectors wiped: %llu\nPasses: %llu\n",
            timeString, (unsigned long long)wipeStats->sectorsWiped, (unsigned long long)wipeStats->totalPasses);
    }
    else {
        LOG_ERROR("Wipe failed");
    }

    if (cfg->verify && wipeError == ERR_OK) {
        printf("\n--- PHASE 4: Verification ---\n");
        uint64_t verificationErrors = analyzerVerifyWipe(dev, progressHandler);
        if (verificationErrors == 0) {
            printf("Verification PASSED.\n");
        }
        else if (verificationErrors == UINT64_MAX) {
            printf("Verification FAILED.\n");
        }
        else {
            printf("Verification completed with %llu errors.\n", (unsigned long long)verificationErrors);
        }
    }

    deviceClose(dev);

cleanup:
    if (useExtended) analyzerFreeExtendedResult(extAnalysis);
    else analyzerFreeResult(analysis);
    free(dev);
    free(analysis);
    free(extAnalysis);
    free(diskInfo);
    free(wipeConfig);
    free(wipeStats);
    return (wipeError == ERR_OK) ? 0 : 1;
}