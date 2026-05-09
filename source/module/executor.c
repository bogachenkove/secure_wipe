#include "executor.h"
#include "device_io.h"
#include "analyzer.h"
#include "wiper.h"
#include "disk_scanner.h"
#include "platform.h"
#include "ui.h"

int runWipe (const program_config_t *config, analysis_result_t *analysis)
{
 device_t *device = (device_t *) calloc (1, sizeof (device_t));
 extended_analysis_result_t *extendedAnalysis = (extended_analysis_result_t *) calloc (1, sizeof (extended_analysis_result_t));
 disk_info_t *diskInfo = (disk_info_t *) calloc (1, sizeof (disk_info_t));
 wipe_config_t *wipeConfig = (wipe_config_t *) calloc (1, sizeof (wipe_config_t));
 wipe_stats_t *wipeStats = (wipe_stats_t *) calloc (1, sizeof (wipe_stats_t));

 if (!device || !extendedAnalysis || !diskInfo || !wipeConfig || !wipeStats)
 {
  LOG_ERROR ("Memory allocation failed");
  free (device);
  free (extendedAnalysis);
  free (diskInfo);
  free (wipeConfig);
  free (wipeStats);
  return 1;
 }

 bool useExtended = config->analyzeWriteTest || config->quickWpCheck;
 error_code_t wipeError = ERR_OK;

 bool analysisInitialized = false;
 bool extendedAnalysisInitialized = false;

 if (diskScannerGetInfo (config->devicePath, diskInfo) == ERR_OK)
 {
  diskScannerPrintDetail (diskInfo);

  if (!config->destroyPartitionTable && (diskInfo->isSystem || diskInfo->isBoot))
  {
   fprintf (stderr, "\n*** CRITICAL: System disk - operation aborted.\n");
   free (device);
   free (extendedAnalysis);
   free (diskInfo);
   free (wipeConfig);
   free (wipeStats);
   return 1;
  }
 }

 bool needWriteAccess = useExtended || config->destroyPartitionTable;

 if (deviceOpen (device, config->devicePath, !needWriteAccess) != ERR_OK)
 {
  LOG_ERROR ("Failed to open device");
  goto cleanup;
 }

 if (isDeviceMounted (config->devicePath) && !config->destroyPartitionTable)
 {
  LOG_ERROR ("Device %s is currently mounted. Unmount it first.", config->devicePath);
  deviceClose (device);
  goto cleanup;
 }

 if (config->destroyPartitionTable)
 {
  printf ("\n--- Destroying partition table (MBR/GPT) with zeros ---\n");
  error_code_t errorCode = wiperZeroPartitionTable (device);
  deviceClose (device);

  if (errorCode == ERR_OK)
   printf ("Partition table destroyed successfully.\n");
  else
   printf ("Failed to destroy partition table.\n");

  goto cleanup;
 }

 if (config->quickWpCheck)
 {
  printf ("\n--- Quick write-protection check ---\n");
  uint64_t firstWpSector = 0;

  if (analyzerQuickWpCheck (device, 16384, &firstWpSector))
  {
   printf ("Write-protection detected at sector %llu (%.2f MB)\n", (unsigned long long) firstWpSector,
           (double) (firstWpSector * SECTOR_SIZE) / (1024.0 * 1024.0));
  }
  else
  {
   printf ("No write-protection detected in first 8MB.\n");
  }

  if (config->analyzeOnly && !config->analyzeWriteTest)
  {
   deviceClose (device);
   goto cleanup;
  }
 }

 if (useExtended)
 {
  if (analyzerInitExtendedResult (extendedAnalysis) != ERR_OK)
  {
   LOG_ERROR ("Init extended analysis failed");
   deviceClose (device);
   goto cleanup;
  }
  extendedAnalysisInitialized = true;
 }
 else
 {
  if (analyzerInitResult (analysis) != ERR_OK)
  {
   LOG_ERROR ("Init analysis failed");
   deviceClose (device);
   goto cleanup;
  }
  analysisInitialized = true;
 }

 printf ("\n--- PHASE 1: %s ---\n", useExtended ? "EXTENDED ANALYSIS" : "DEVICE ANALYSIS");

 if (config->analyzeWriteTest)
 {
  printf ("*** Write test mode - data will be modified! ***\n");
  analyze_flags_t analysisFlags = ANALYZE_WRITE_TEST | ANALYZE_DETECT_WP;

  if (analyzerScanDeviceExtended (device, extendedAnalysis, analysisFlags, progressHandler) != ERR_OK)
  {
   LOG_ERROR ("Extended analysis failed");
   deviceClose (device);
   goto cleanup;
  }

  analyzerPrintExtendedReport (extendedAnalysis);
  memcpy (analysis, &extendedAnalysis->base, sizeof (analysis_result_t));
 }
 else
 {
  if (analyzerScanDevice (device, analysis, progressHandler) != ERR_OK)
  {
   LOG_ERROR ("Device analysis failed");
   deviceClose (device);
   goto cleanup;
  }
  analyzerPrintReport (analysis);
 }

 if (config->analyzeOnly)
 {
  printf ("Analysis complete. Exiting.\n");
  deviceClose (device);
  goto cleanup;
 }

 deviceClose (device);

 uint32_t actualPasses;
 if (config->method == WIPE_METHOD_RANDOM)
  actualPasses = config->passes;
 else
  actualPasses = wiperMethodPasses (config->method);

 if (!config->autoConfirm && !confirmWipe (config->devicePath, analysis->totalBytes, config->method, actualPasses))
 {
  printf ("Operation cancelled.\n");
  goto cleanup;
 }

 if (deviceOpen (device, config->devicePath, false) != ERR_OK)
 {
  LOG_ERROR ("Failed to open device for writing");
  goto cleanup;
 }

 printf ("\n--- PHASE 2: Destroy filesystem metadata ---\n");
 wiperPrepareDevice (device, progressHandler);

 printf ("\n--- PHASE 3: Secure wipe ---\n");
 memset (wipeConfig, 0, sizeof (wipe_config_t));
 wipeConfig->method = config->method;
 wipeConfig->passes = config->passes;
 wipeConfig->skipBadSectors = true;
 wipeConfig->verifyAfterWipe = config->verify;
 wipeConfig->badSectors = analysis;
 wipeConfig->progress = progressHandler;

 time_t startTime = time (NULL);
 wipeError = wiperExecute (device, wipeConfig, wipeStats);
 time_t elapsedTime = time (NULL) - startTime;

 if (wipeError == ERR_OK)
 {
  char timeString[64];
  formatTime (elapsedTime, timeString, sizeof (timeString));
  printf ("\n--- Wipe complete ---\nTime: %s\nSectors wiped: %llu\nPasses: %llu\n", timeString, (unsigned long long) wipeStats->sectorsWiped,
          (unsigned long long) wipeStats->totalPasses);
 }
 else
 {
  LOG_ERROR ("Wipe failed");
 }

 if (config->verify && wipeError == ERR_OK)
 {
  printf ("\n--- PHASE 4: Verification ---\n");
  uint64_t verificationErrors = analyzerVerifyWipe (device, progressHandler);

  if (verificationErrors == 0)
   printf ("Verification PASSED.\n");
  else if (verificationErrors == UINT64_MAX)
   printf ("Verification FAILED.\n");
  else
   printf ("Verification completed with %llu errors.\n", (unsigned long long) verificationErrors);
 }

 deviceClose (device);

cleanup:
 if (useExtended && extendedAnalysisInitialized)
  analyzerFreeExtendedResult (extendedAnalysis);
 else if (analysisInitialized)
  analyzerFreeResult (analysis);

 free (device);
 free (extendedAnalysis);
 free (diskInfo);
 free (wipeConfig);
 free (wipeStats);
 return (wipeError == ERR_OK) ? 0 : 1;
}