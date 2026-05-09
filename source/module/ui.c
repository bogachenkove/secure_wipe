#include "ui.h"
#include "common.h"
#include "disk_scanner.h"
#include "wiper.h"
#include <stdio.h>

bool confirmWipe(const char* devicePath, uint64_t sizeBytes, wipe_method_t method, uint32_t actualPasses)
{
    char sizeString[32];
    formatBytes(sizeBytes, sizeString, sizeof(sizeString));
    printf("\n--- WARNING ---\n");
    printf("Device: %s\nSize:   %s\nMethod: %s\nPasses: %u\n",
        devicePath, sizeString, wiperMethodName(method), actualPasses);
    printf("This operation CANNOT be undone!\n");
    printf("Type 'YES' (all caps) to confirm: ");
    fflush(stdout);
    char response[16] = { 0 };
    if (!fgets(response, sizeof(response), stdin)) return false;
    size_t responseLength = strlen(response);
    if (responseLength && response[responseLength - 1] == '\n')
        response[responseLength - 1] = '\0';
    return strcmp(response, "YES") == 0;
}

void progressHandler(uint64_t current, uint64_t total, int pass, const char* phase)
{
    static int lastPercent = -1;
    int currentPercent = (int)((current * 100) / total);
    if (currentPercent != lastPercent)
    {
        printf("\r[%-40s] %3d%% - %s", "########################################", currentPercent, phase);
        fflush(stdout);
        lastPercent = currentPercent;
        if (currentPercent == 100) lastPercent = -1;
    }
}

bool interactiveSelectDisk(program_config_t* cfg)
{
    disk_scan_result_t* scanResult = (disk_scan_result_t*)malloc(sizeof(disk_scan_result_t));
    if (!scanResult)
    {
        fprintf(stderr, "Memory allocation failed\n");
        return false;
    }
    printf("Scanning for available disks...\n\n");
    if (diskScannerScan(scanResult) != ERR_OK)
    {
        fprintf(stderr, "Failed to scan disks\n");
        free(scanResult);
        return false;
    }
    if (scanResult->count == 0)
    {
        fprintf(stderr, "No disks found\n");
        free(scanResult);
        return false;
    }
    diskScannerPrintList(scanResult, cfg->showAllDisks);
    int safeCount = 0;
    for (int diskIndex = 0; diskIndex < scanResult->count; diskIndex++)
        if (diskScannerIsSafeToWipe(&scanResult->disks[diskIndex]))
            safeCount++;
    if (safeCount == 0)
    {
        fprintf(stderr, "No safe disks available.\n");
        free(scanResult);
        return false;
    }
    printf("Enter disk number (1-%d) or 'q': ", scanResult->count);
    fflush(stdout);
    char input[16];
    if (!fgets(input, sizeof(input), stdin))
    {
        free(scanResult);
        return false;
    }
    if (input[0] == 'q' || input[0] == 'Q')
    {
        free(scanResult);
        return false;
    }
    int selection = atoi(input);
    const disk_info_t* selectedDisk = diskScannerGetByIndex(scanResult, selection);
    if (!selectedDisk)
    {
        fprintf(stderr, "Invalid selection\n");
        free(scanResult);
        return false;
    }
    if (!diskScannerIsSafeToWipe(selectedDisk))
    {
        fprintf(stderr, "System disk selected - not allowed.\n");
        free(scanResult);
        return false;
    }
    diskScannerPrintDetail(selectedDisk);
    printf("Confirm this disk? (y/n): ");
    fflush(stdout);
    if (!fgets(input, sizeof(input), stdin))
    {
        free(scanResult);
        return false;
    }
    if (input[0] != 'y' && input[0] != 'Y')
    {
        free(scanResult);
        return false;
    }
    printf("\nSelect wipe method:\n");
    printf("  1. Zero Fill\n  2. DoD 3-pass (default)\n  3. DoD 7-pass\n  4. Schneier\n  5. Gutmann\n  6. Random\n");
    printf("Enter choice (1-6) [2]: ");
    fflush(stdout);
    if (!fgets(input, sizeof(input), stdin))
        input[0] = '2';
    int choice = atoi(input);
    if (choice == 0) choice = 2;
    switch (choice)
    {
    case 1: cfg->method = WIPE_METHOD_ZERO; cfg->passes = 1; break;
    case 2: cfg->method = WIPE_METHOD_DOD_SHORT; cfg->passes = 3; break;
    case 3: cfg->method = WIPE_METHOD_DOD_FULL; cfg->passes = 7; break;
    case 4: cfg->method = WIPE_METHOD_SCHNEIER; cfg->passes = 7; break;
    case 5: cfg->method = WIPE_METHOD_GUTMANN; cfg->passes = 35; break;
    case 6:
        cfg->method = WIPE_METHOD_RANDOM;
        printf("Number of random passes (1-100) [3]: ");
        fflush(stdout);
        if (fgets(input, sizeof(input), stdin))
        {
            int passesValue = atoi(input);
            cfg->passes = (passesValue > 0 && passesValue <= 100) ? (uint32_t)passesValue : 3;
        }
        else
        {
            cfg->passes = 3;
        }
        break;
    default: cfg->method = WIPE_METHOD_DOD_SHORT; cfg->passes = 3; break;
    }
    snprintf(cfg->devicePath, sizeof(cfg->devicePath), "%s", selectedDisk->devicePath);
    free(scanResult);
    return true;
}