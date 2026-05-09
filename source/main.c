#include "platform.h"
#include "common.h"
#include "metadata.h"
#include "wiper.h"
#include "random_gen.h"
#include "disk_scanner.h"

int main(int argc, char* argv[])
{
    platformInit();

    program_config_t* config = (program_config_t*)malloc(sizeof(program_config_t));
    if (!config) {
        fprintf(stderr, "Memory allocation failed\n");
        platformCleanup();
        return 1;
    }
    metadataDefaultConfig(config);

    if (!metadataParseArguments(argc, argv, config)) {
        metadataPrintUsage(argv[0]);
        free(config);
        platformCleanup();
        return 1;
    }

    if (config->listDisks) {
        disk_scan_result_t* scan = (disk_scan_result_t*)malloc(sizeof(disk_scan_result_t));
        if (!scan) {
            fprintf(stderr, "Memory allocation failed\n");
            free(config);
            platformCleanup();
            return 1;
        }
        if (diskScannerScan(scan) == ERR_OK)
            diskScannerPrintList(scan, config->showAllDisks);
        else
            fprintf(stderr, "Failed to scan disks\n");
        free(scan);
        free(config);
        platformCleanup();
        return 0;
    }

    if (config->selectDisk || strlen(config->devicePath) == 0) {
        if (!metadataInteractiveSelectDisk(config)) {
            free(config);
            platformCleanup();
            return 1;
        }
    }

    logInit(config->logPath, config->verbose);
    LOG_INFO("Secure Wipe started on %s", config->devicePath);

    if (randomInit() != ERR_OK) {
        LOG_ERROR("Random init failed");
        logClose();
        free(config);
        platformCleanup();
        return 1;
    }
    if (wiperInit() != ERR_OK) {
        LOG_ERROR("Wiper init failed");
        randomCleanup();
        logClose();
        free(config);
        platformCleanup();
        return 1;
    }

    int exitCode = metadataRun(config);

    wiperCleanup();
    randomCleanup();
    LOG_INFO("Secure Wipe finished with code %d", exitCode);
    logClose();
    free(config);
    platformCleanup();
    return exitCode;
}