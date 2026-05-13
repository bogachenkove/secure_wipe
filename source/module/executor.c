#include "executor.h"
#include "device_io.h"
#include "analyzer.h"
#include "wiper.h"
#include "disk_scanner.h"
#include "platform.h"
#include "ui.h"

static error_code_t runSingleCycle (const program_config_t *config, analysis_result_t *analysis, int cycleNum)
{
 device_t *device = NULL;
 extended_analysis_result_t *extendedAnalysis = NULL;
 disk_info_t *diskInfo = NULL;
 wipe_config_t *wipeConfig = NULL;
 wipe_stats_t *wipeStats = NULL;
 error_code_t wipeError = ERR_OK;
 error_code_t result = ERR_OK;
 bool useExtended = config->analyzeWriteTest || config->quickWpCheck;
 bool analysisInitialized = false;
 bool extendedAnalysisInitialized = false;

 device = (device_t *) calloc (1, sizeof (device_t));
 extendedAnalysis = (extended_analysis_result_t *) calloc (1, sizeof (extended_analysis_result_t));
 diskInfo = (disk_info_t *) calloc (1, sizeof (disk_info_t));
 wipeConfig = (wipe_config_t *) calloc (1, sizeof (wipe_config_t));
 wipeStats = (wipe_stats_t *) calloc (1, sizeof (wipe_stats_t));

 if (!device || !extendedAnalysis || !diskInfo || !wipeConfig || !wipeStats)
 {
  LOG_ERROR ("Memory allocation failed");
  result = ERR_MEMORY;
  goto cleanup;
 }

 if (diskScannerGetInfo (config->devicePath, diskInfo) == ERR_OK)
 {
  if (cycleNum == 1)
   diskScannerPrintDetail (diskInfo);
  if (!config->destroyPartitionTable && (diskInfo->isSystem || diskInfo->isBoot))
  {
   fprintf (stderr, "\n*** CRITICAL: System disk - operation aborted.\n");
   result = ERR_PERMISSION;
   goto cleanup;
  }
 }

 bool needWriteAccess = useExtended || config->destroyPartitionTable;
 if (deviceOpen (device, config->devicePath, !needWriteAccess) != ERR_OK)
 {
  LOG_ERROR ("Failed to open device");
  result = ERR_OPEN_DEVICE;
  goto cleanup;
 }

 if (isDeviceMounted (config->devicePath) && !config->destroyPartitionTable)
 {
  LOG_ERROR ("Device %s is currently mounted. Unmount it first.", config->devicePath);
  deviceClose (device);
  result = ERR_PERMISSION;
  goto cleanup;
 }

 if (config->destroyPartitionTable)
 {
  printf ("\n--- Destroying partition table (MBR/GPT) with zeros ---\n");
  disk_info_t localDiskInfo;
  if (diskInfo->isSystem == false && diskInfo->isBoot == false && diskInfo->devicePath[0] == '\0')
  {
   if (diskScannerGetInfo (config->devicePath, &localDiskInfo) == ERR_OK)
   {
	memcpy (diskInfo, &localDiskInfo, sizeof (disk_info_t));
   }
  }
  if (diskInfo->isSystem || diskInfo->isBoot)
  {
   printf ("*** WARNING: This is a SYSTEM/BOOT disk! Wiping will make system unbootable! ***\n\n");
  }
  else
  {
   printf ("This operation will erase the MBR/GPT partition table (first 34 and last 33 sectors).\n");
   printf ("Data on the disk will become inaccessible, but the content itself will remain.\n");
  }
  printf ("Type 'YES' (all caps) to confirm: ");
  fflush (stdout);
  char confirm[16] = {0};
  if (!fgets (confirm, sizeof (confirm), stdin) || strcmp (confirm, "YES\n") != 0)
  {
   printf ("Operation cancelled.\n");
   deviceClose (device);
   result = ERR_PERMISSION;
   goto cleanup;
  }
  error_code_t errorCode = wiperZeroPartitionTable (device);
  deviceClose (device);
  if (errorCode == ERR_OK)
   printf ("Partition table destroyed successfully.\n");
  else
   printf ("Failed to destroy partition table (error: %s).\n", errorToString (errorCode));
  result = (errorCode == ERR_OK) ? ERR_OK : errorCode;
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
   goto after_analysis;
  }
 }

 if (config->skipAnalysis && !config->analyzeOnly)
 {
  LOG_INFO ("Skipping device analysis (--skip-analysis)");
  deviceClose (device);
  goto after_analysis;
 }

 if (useExtended)
 {
  if (analyzerInitExtendedResult (extendedAnalysis) != ERR_OK)
  {
   LOG_ERROR ("Init extended analysis failed");
   deviceClose (device);
   result = ERR_MEMORY;
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
   result = ERR_MEMORY;
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
   result = ERR_READ_DEVICE;
   goto cleanup;
  }
  analyzerPrintExtendedReport (extendedAnalysis);
  memcpy (analysis, &extendedAnalysis->base, sizeof (analysis_result_t));
 }
 else if (!config->skipAnalysis)
 {
  if (analyzerScanDevice (device, analysis, progressHandler) != ERR_OK)
  {
   LOG_ERROR ("Device analysis failed");
   deviceClose (device);
   result = ERR_READ_DEVICE;
   goto cleanup;
  }
  analyzerPrintReport (analysis);
 }

 if (config->analyzeOnly)
 {
  printf ("Analysis complete. Exiting.\n");
  deviceClose (device);
  goto after_analysis;
 }

 deviceClose (device);

after_analysis:
 uint32_t actualPasses;
 if (config->method == WIPE_METHOD_RANDOM)
  actualPasses = config->passes;
 else
  actualPasses = wiperMethodPasses (config->method);

 if (cycleNum == 1 && !config->autoConfirm && !confirmWipe (config->devicePath, analysis->totalBytes, config->method, actualPasses))
 {
  printf ("Operation cancelled.\n");
  result = ERR_PERMISSION;
  goto cleanup;
 }

 if (deviceOpen (device, config->devicePath, false) != ERR_OK)
 {
  LOG_ERROR ("Failed to open device for writing");
  result = ERR_OPEN_DEVICE;
  goto cleanup;
 }

 uint8_t *testBuffer = (uint8_t *) alignedAlloc (SECTOR_SIZE);
 if (!testBuffer)
 {
  LOG_ERROR ("Cannot allocate test buffer");
  deviceClose (device);
  result = ERR_MEMORY;
  goto cleanup;
 }
 memset (testBuffer, 0xAA, SECTOR_SIZE);
 if (deviceWriteSectors (device, 0, 1, testBuffer) != ERR_OK)
 {
  LOG_ERROR ("Device write test failed. Device is write-protected or inaccessible.");
  alignedFree (testBuffer);
  deviceClose (device);
  result = ERR_WRITE_DEVICE;
  goto cleanup;
 }
 alignedFree (testBuffer);

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
  printf ("\n--- Wipe complete (cycle %d) ---\nTime: %s\nSectors wiped: %llu\nPasses: %llu\n", cycleNum, timeString,
		  (unsigned long long) wipeStats->sectorsWiped, (unsigned long long) wipeStats->totalPasses);
 }
 else
 {
  LOG_ERROR ("Wipe failed");
  result = wipeError;
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
 result = ERR_OK;

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

 return (result == ERR_OK) ? ERR_OK : ERR_UNKNOWN;
}

int runWipe (const program_config_t *config, analysis_result_t *analysis)
{
 error_code_t lastError = ERR_OK;
 int finalExitCode = 0;

 for (uint32_t cycle = 1; cycle <= config->cycles; cycle++)
 {
  if (config->cycles > 1)
  {
   printf ("\n========== CYCLE %u / %u ==========\n", cycle, config->cycles);
   LOG_INFO ("Starting wipe cycle %u of %u", cycle, config->cycles);
  }

  lastError = runSingleCycle (config, analysis, (int) cycle);
  if (lastError != ERR_OK)
  {
   finalExitCode = 1;
   break;
  }

  if (config->cycles > 1 && cycle < config->cycles)
  {
   printf ("\n=== Cycle %u completed successfully. Starting next cycle... ===\n", cycle);
   memset (analysis, 0, sizeof (analysis_result_t));
  }
 }

 return (finalExitCode == 0 && lastError == ERR_OK) ? 0 : 1;
}

int emergencyWipe (const program_config_t *config)
{
 device_t device;
 error_code_t openStatus = deviceOpen (&device, config->devicePath, false);
 if (openStatus != ERR_OK)
 {
  fprintf (stderr, "Failed to open device %s: %s\n", config->devicePath, errorToString (openStatus));
  return 1;
 }

 uint64_t sectorsToWrite = config->emergencySectors;
 if (sectorsToWrite > device.sectorCount)
 {
  fprintf (stderr, "Requested %llu sectors, but device has only %llu sectors\n", (unsigned long long) sectorsToWrite,
		   (unsigned long long) device.sectorCount);
  deviceClose (&device);
  return 1;
 }

 size_t bufferBytes = config->bufferSize;
 if (bufferBytes % SECTOR_SIZE != 0)
 {
  fprintf (stderr, "Buffer size (%zu) must be multiple of sector size (%d)\n", bufferBytes, SECTOR_SIZE);
  deviceClose (&device);
  return 1;
 }

 uint32_t sectorsPerBuffer = (uint32_t) (bufferBytes / SECTOR_SIZE);
 uint8_t *zeroBuffer = (uint8_t *) alignedAlloc (bufferBytes);
 if (!zeroBuffer)
 {
  fprintf (stderr, "Failed to allocate %zu bytes for buffer\n", bufferBytes);
  deviceClose (&device);
  return 1;
 }
 memset (zeroBuffer, 0, bufferBytes);

 uint64_t writtenSectors = 0;
 printf ("Emergency zero-write: %llu sectors, buffer %zu bytes\n", (unsigned long long) sectorsToWrite, bufferBytes);

 while (writtenSectors < sectorsToWrite)
 {
  uint32_t chunkSectors = sectorsPerBuffer;
  if (writtenSectors + chunkSectors > sectorsToWrite)
   chunkSectors = (uint32_t) (sectorsToWrite - writtenSectors);

  error_code_t writeStatus = deviceWriteSectors (&device, writtenSectors, chunkSectors, zeroBuffer);
  if (writeStatus != ERR_OK)
  {
   fprintf (stderr, "\nWrite error at sector %llu: %s\n", (unsigned long long) writtenSectors, errorToString (writeStatus));
   alignedFree (zeroBuffer);
   deviceClose (&device);
   return 1;
  }

  writtenSectors += chunkSectors;
  printf ("\rProgress: %llu / %llu sectors (%.1f%%)", (unsigned long long) writtenSectors, (unsigned long long) sectorsToWrite,
		  100.0 * writtenSectors / sectorsToWrite);
  fflush (stdout);
 }

 printf ("\nEmergency wipe completed successfully.\n");
 alignedFree (zeroBuffer);
 deviceClose (&device);
 return 0;
}