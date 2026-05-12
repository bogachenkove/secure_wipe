#include "analyzer.h"
#include "platform.h"

error_code_t analyzerInitResult (analysis_result_t *result)
{
 if (!result)
  return ERR_INVALID_ARG;
 memset (result, 0, sizeof (analysis_result_t));
 result->badSectorsCapacity = 1024;
 result->badSectors = (uint64_t *) malloc (result->badSectorsCapacity * sizeof (uint64_t));
 return result->badSectors ? ERR_OK : ERR_MEMORY;
}

void analyzerFreeResult (analysis_result_t *result)
{
 if (result)
 {
  free (result->badSectors);
  result->badSectors = NULL;
  result->badSectorsCapacity = 0;
  result->badSectorCount = 0;
 }
}

error_code_t analyzerAddBadSector (analysis_result_t *result, uint64_t sector)
{
 if (!result)
  return ERR_INVALID_ARG;
 if (result->badSectorCount >= result->badSectorsCapacity)
 {
  size_t newCapacity = result->badSectorsCapacity * 2;
  if (newCapacity > MAX_BAD_SECTORS)
   newCapacity = MAX_BAD_SECTORS;
  if (result->badSectorCount >= MAX_BAD_SECTORS)
  {
   LOG_WARN ("Maximum bad sector count reached");
   return ERR_MEMORY;
  }
  uint64_t *newArray = (uint64_t *) realloc (result->badSectors, newCapacity * sizeof (uint64_t));
  if (!newArray)
   return ERR_MEMORY;
  result->badSectors = newArray;
  result->badSectorsCapacity = newCapacity;
 }
 result->badSectors[result->badSectorCount++] = sector;
 logBadSector (sector, "scan");
 return ERR_OK;
}

bool analyzerIsBadSector (const analysis_result_t *result, uint64_t sector)
{
 if (!result || !result->badSectors)
  return false;
 for (uint64_t sectorIndex = 0; sectorIndex < result->badSectorCount; sectorIndex++)
 {
  if (result->badSectors[sectorIndex] == sector)
   return true;
 }
 return false;
}

error_code_t analyzerScanDevice (device_t *device, analysis_result_t *result, progress_callback_t progress)
{
 if (!device || !device->isOpen || !result)
  return ERR_INVALID_ARG;
 LOG_INFO ("Starting device analysis...");
 LOG_INFO ("Total sectors to scan: %llu", (unsigned long long) device->sectorCount);
 result->totalSectors = device->sectorCount;
 result->totalBytes = device->sizeBytes;
 result->readableSectors = 0;

 uint8_t *readBuffer = (uint8_t *) alignedAlloc (gBufferSize);
 if (!readBuffer)
  return ERR_MEMORY;
 const device_io_ops_t *ioOps = deviceIoGetOps ();
 uint64_t currentSector = 0;
 uint64_t lastProgressPercent = 0;

 while (currentSector < device->sectorCount)
 {
  uint32_t sectorsToRead = (uint32_t) gBufferSectors;
  if (currentSector + sectorsToRead > device->sectorCount)
   sectorsToRead = (uint32_t) (device->sectorCount - currentSector);
  error_code_t readError = ioOps->readSectors (device, currentSector, sectorsToRead, readBuffer);
  if (readError == ERR_OK)
  {
   result->readableSectors += sectorsToRead;
  }
  else
  {
   for (uint32_t offset = 0; offset < sectorsToRead; offset++)
   {
    uint64_t sectorToCheck = currentSector + offset;
    if (ioOps->readSectors (device, sectorToCheck, 1, readBuffer) == ERR_OK)
     result->readableSectors++;
    else
     analyzerAddBadSector (result, sectorToCheck);
   }
  }
  currentSector += sectorsToRead;
  if (progress)
  {
   uint64_t currentPercent = (currentSector * 100) / device->sectorCount;
   if (currentPercent > lastProgressPercent)
   {
    progress (currentSector, device->sectorCount, 0, "Analyzing");
    lastProgressPercent = currentPercent;
   }
  }
 }
 alignedFree (readBuffer);
 LOG_INFO ("Analysis complete - Readable: %llu, Bad: %llu", (unsigned long long) result->readableSectors,
           (unsigned long long) result->badSectorCount);
 return ERR_OK;
}

void analyzerPrintReport (const analysis_result_t *result)
{
 if (!result)
  return;
 char totalString[32], readableString[32];
 formatBytes (result->totalBytes, totalString, sizeof (totalString));
 formatBytes (result->readableSectors * SECTOR_SIZE, readableString, sizeof (readableString));
 printf ("\n========== ANALYSIS REPORT ==========\n");
 printf ("Total capacity:     %s\n", totalString);
 printf ("Total sectors:      %llu\n", (unsigned long long) result->totalSectors);
 printf ("Readable sectors:   %llu\n", (unsigned long long) result->readableSectors);
 printf ("Readable data:      %s\n", readableString);
 printf ("Bad sectors:        %llu\n", (unsigned long long) result->badSectorCount);
 if (result->badSectorCount > 0)
 {
  printf ("\nBad sector addresses (first 20):\n");
  uint64_t showCount = result->badSectorCount > 20 ? 20 : result->badSectorCount;
  for (uint64_t sectorIndex = 0; sectorIndex < showCount; sectorIndex++)
  {
   printf ("  Sector %llu (offset 0x%llX)\n", (unsigned long long) result->badSectors[sectorIndex],
           (unsigned long long) (result->badSectors[sectorIndex] * SECTOR_SIZE));
  }
  if (result->badSectorCount > 20)
   printf ("  ... and %llu more\n", (unsigned long long) (result->badSectorCount - 20));
 }
 printf ("======================================\n\n");
}

uint64_t analyzerVerifyWipe (device_t *device, progress_callback_t progress)
{
 if (!device || !device->isOpen)
  return UINT64_MAX;
 LOG_INFO ("Starting wipe verification...");
 uint8_t *readBuffer = (uint8_t *) alignedAlloc (gBufferSize);
 if (!readBuffer)
  return UINT64_MAX;
 const device_io_ops_t *ioOps = deviceIoGetOps ();
 uint64_t errorCount = 0;
 uint64_t currentSector = 0;
 uint64_t lastProgressPercent = 0;

 while (currentSector < device->sectorCount)
 {
  uint32_t sectorsToRead = (uint32_t) gBufferSectors;
  if (currentSector + sectorsToRead > device->sectorCount)
   sectorsToRead = (uint32_t) (device->sectorCount - currentSector);
  if (ioOps->readSectors (device, currentSector, sectorsToRead, readBuffer) != ERR_OK)
  {
   for (uint32_t offset = 0; offset < sectorsToRead; offset++)
   {
    if (ioOps->readSectors (device, currentSector + offset, 1, readBuffer) != ERR_OK)
    {
     errorCount++;
     logBadSector (currentSector + offset, "verification");
    }
   }
  }
  currentSector += sectorsToRead;
  if (progress)
  {
   uint64_t currentPercent = (currentSector * 100) / device->sectorCount;
   if (currentPercent > lastProgressPercent)
   {
    progress (currentSector, device->sectorCount, 0, "Verifying");
    lastProgressPercent = currentPercent;
   }
  }
 }
 alignedFree (readBuffer);
 LOG_INFO ("Verification complete. Errors: %llu", (unsigned long long) errorCount);
 return errorCount;
}

error_code_t analyzerInitExtendedResult (extended_analysis_result_t *result)
{
 if (!result)
  return ERR_INVALID_ARG;
 memset (result, 0, sizeof (extended_analysis_result_t));
 error_code_t baseError = analyzerInitResult (&result->base);
 if (baseError != ERR_OK)
  return baseError;
 result->wpCapacity = 1024;
 result->writeProtected = (uint64_t *) malloc (result->wpCapacity * sizeof (uint64_t));
 if (!result->writeProtected)
 {
  analyzerFreeResult (&result->base);
  return ERR_MEMORY;
 }
 result->firstWpSector = UINT64_MAX;
 result->lastWpSector = 0;
 return ERR_OK;
}

void analyzerFreeExtendedResult (extended_analysis_result_t *result)
{
 if (result)
 {
  analyzerFreeResult (&result->base);
  free (result->writeProtected);
  result->writeProtected = NULL;
  result->wpCapacity = 0;
  result->wpCount = 0;
 }
}

static error_code_t addWpSector (extended_analysis_result_t *result, uint64_t sector)
{
 if (!result)
  return ERR_INVALID_ARG;
 if (result->wpCount >= result->wpCapacity)
 {
  size_t newCapacity = result->wpCapacity * 2;
  if (newCapacity > MAX_BAD_SECTORS)
   newCapacity = MAX_BAD_SECTORS;
  uint64_t *newArray = (uint64_t *) realloc (result->writeProtected, newCapacity * sizeof (uint64_t));
  if (!newArray)
   return ERR_MEMORY;
  result->writeProtected = newArray;
  result->wpCapacity = newCapacity;
 }
 result->writeProtected[result->wpCount++] = sector;
 if (sector < result->firstWpSector)
  result->firstWpSector = sector;
 if (sector > result->lastWpSector)
  result->lastWpSector = sector;
 result->hasWpRegions = true;
 return ERR_OK;
}

static int testSectorWrite (device_t *device, const device_io_ops_t *ioOps, uint64_t sector, uint8_t *writeBuffer, uint8_t *readBuffer,
                            uint8_t *backupBuffer)
{
 if (backupBuffer)
  ioOps->readSectors (device, sector, 1, backupBuffer);
 for (int byteIndex = 0; byteIndex < SECTOR_SIZE; byteIndex++)
  writeBuffer[byteIndex] = (uint8_t) ((sector ^ 0xAA ^ byteIndex) & 0xFF);
 if (ioOps->writeSectors (device, sector, 1, writeBuffer) != ERR_OK)
 {
  if (backupBuffer)
   ioOps->writeSectors (device, sector, 1, backupBuffer);
  return 1;
 }
 if (ioOps->readSectors (device, sector, 1, readBuffer) != ERR_OK)
 {
  if (backupBuffer)
   ioOps->writeSectors (device, sector, 1, backupBuffer);
  return 2;
 }
 if (memcmp (writeBuffer, readBuffer, SECTOR_SIZE) != 0)
 {
  if (backupBuffer)
   ioOps->writeSectors (device, sector, 1, backupBuffer);
  return 3;
 }
 if (backupBuffer)
  ioOps->writeSectors (device, sector, 1, backupBuffer);
 return 0;
}

error_code_t analyzerScanDeviceExtended (device_t *device, extended_analysis_result_t *result, analyze_flags_t flags, progress_callback_t progress)
{
 if (!device || !device->isOpen || !result)
  return ERR_INVALID_ARG;
 bool doWriteTest = (flags & ANALYZE_WRITE_TEST) != 0;
 bool detectWp = (flags & ANALYZE_DETECT_WP) != 0 || doWriteTest;
 if (doWriteTest)
  LOG_WARN ("=== WRITE TEST MODE - DATA WILL BE DESTROYED! ===");
 LOG_INFO ("Starting extended analysis... Sectors: %llu", (unsigned long long) device->sectorCount);
 result->base.totalSectors = device->sectorCount;
 result->base.totalBytes = device->sizeBytes;
 result->base.readableSectors = 0;

 uint8_t *readBuffer = (uint8_t *) alignedAlloc (gBufferSize);
 uint8_t *writeBuffer = NULL, *verifyBuffer = NULL, *backupBuffer = NULL;
 if (!readBuffer)
  return ERR_MEMORY;
 if (doWriteTest || detectWp)
 {
  writeBuffer = alignedAlloc (SECTOR_SIZE);
  verifyBuffer = alignedAlloc (SECTOR_SIZE);
  if (detectWp && !doWriteTest)
   backupBuffer = alignedAlloc (SECTOR_SIZE);
  if (!writeBuffer || !verifyBuffer)
  {
   alignedFree (readBuffer);
   if (writeBuffer)
    alignedFree (writeBuffer);
   if (verifyBuffer)
    alignedFree (verifyBuffer);
   if (backupBuffer)
    alignedFree (backupBuffer);
   return ERR_MEMORY;
  }
 }

 const device_io_ops_t *ioOps = deviceIoGetOps ();
 uint64_t currentSector = 0;
 uint64_t lastProgressPercent = 0;

 while (currentSector < device->sectorCount)
 {
  uint32_t sectorsToProcess = (uint32_t) gBufferSectors;
  if (currentSector + sectorsToProcess > device->sectorCount)
   sectorsToProcess = (uint32_t) (device->sectorCount - currentSector);
  error_code_t readError = ioOps->readSectors (device, currentSector, sectorsToProcess, readBuffer);
  if (readError == ERR_OK)
  {
   result->base.readableSectors += sectorsToProcess;
   if (doWriteTest || detectWp)
   {
    uint32_t step = doWriteTest ? 1 : 64;
    for (uint32_t offset = 0; offset < sectorsToProcess; offset += step)
    {
     uint64_t testSector = currentSector + offset;
     int writeResult = testSectorWrite (device, ioOps, testSector, writeBuffer, verifyBuffer, backupBuffer);
     switch (writeResult)
     {
     case 1:
      result->writeErrors++;
      addWpSector (result, testSector);
      break;
     case 2:
      result->readErrors++;
      analyzerAddBadSector (&result->base, testSector);
      break;
     case 3:
      result->verifyErrors++;
      break;
     }
    }
   }
  }
  else
  {
   for (uint32_t offset = 0; offset < sectorsToProcess; offset++)
   {
    uint64_t testSector = currentSector + offset;
    if (ioOps->readSectors (device, testSector, 1, readBuffer) == ERR_OK)
    {
     result->base.readableSectors++;
     if (doWriteTest || detectWp)
     {
      int writeResult = testSectorWrite (device, ioOps, testSector, writeBuffer, verifyBuffer, backupBuffer);
      if (writeResult == 1)
      {
       result->writeErrors++;
       addWpSector (result, testSector);
      }
      else if (writeResult == 2)
      {
       result->readErrors++;
      }
      else if (writeResult == 3)
      {
       result->verifyErrors++;
      }
     }
    }
    else
    {
     result->readErrors++;
     analyzerAddBadSector (&result->base, testSector);
    }
   }
  }
  currentSector += sectorsToProcess;
  if (progress)
  {
   uint64_t currentPercent = (currentSector * 100) / device->sectorCount;
   if (currentPercent > lastProgressPercent)
   {
    progress (currentSector, device->sectorCount, 0, doWriteTest ? "Write testing" : "Analyzing");
    lastProgressPercent = currentPercent;
   }
  }
 }
 alignedFree (readBuffer);
 if (writeBuffer)
  alignedFree (writeBuffer);
 if (verifyBuffer)
  alignedFree (verifyBuffer);
 if (backupBuffer)
  alignedFree (backupBuffer);
 LOG_INFO ("Extended analysis complete - Readable: %llu, Read errors: %llu, Write errors: %llu, Verify errors: %llu",
           (unsigned long long) result->base.readableSectors, (unsigned long long) result->readErrors, (unsigned long long) result->writeErrors,
           (unsigned long long) result->verifyErrors);
 return ERR_OK;
}

void analyzerPrintExtendedReport (const extended_analysis_result_t *result)
{
 if (!result)
  return;
 analyzerPrintReport (&result->base);
 printf ("========= EXTENDED ANALYSIS =========\n");
 printf ("Read errors:        %llu\n", (unsigned long long) result->readErrors);
 printf ("Write errors:       %llu\n", (unsigned long long) result->writeErrors);
 printf ("Verify errors:      %llu\n", (unsigned long long) result->verifyErrors);
 if (result->hasWpRegions)
 {
  printf ("\n*** WRITE-PROTECTED REGIONS DETECTED ***\n");
  printf ("WP sectors count:   %llu\n", (unsigned long long) result->wpCount);
  printf ("First WP sector:    %llu (offset 0x%llX, ~%.2f MB)\n", (unsigned long long) result->firstWpSector,
          (unsigned long long) (result->firstWpSector * SECTOR_SIZE), (double) (result->firstWpSector * SECTOR_SIZE) / (1024.0 * 1024.0));
  printf ("Last WP sector:     %llu (offset 0x%llX, ~%.2f MB)\n", (unsigned long long) result->lastWpSector,
          (unsigned long long) (result->lastWpSector * SECTOR_SIZE), (double) (result->lastWpSector * SECTOR_SIZE) / (1024.0 * 1024.0));
  if (result->wpCount > 0)
  {
   printf ("\nFirst 10 WP sectors:\n");
   uint64_t showCount = result->wpCount > 10 ? 10 : result->wpCount;
   for (uint64_t sectorIndex = 0; sectorIndex < showCount; sectorIndex++)
    printf ("  Sector %llu\n", (unsigned long long) result->writeProtected[sectorIndex]);
   if (result->wpCount > 10)
    printf ("  ... and %llu more\n", (unsigned long long) (result->wpCount - 10));
  }
 }
 else
 {
  printf ("\nNo write-protected regions detected.\n");
 }
 printf ("======================================\n\n");
}

bool analyzerQuickWpCheck (device_t *device, uint64_t testSectors, uint64_t *firstWpSector)
{
 if (!device || !device->isOpen)
  return false;
 if (testSectors == 0)
  testSectors = 8192;
 if (testSectors > device->sectorCount)
  testSectors = device->sectorCount;
 LOG_INFO ("Quick write-protection check (first %llu sectors)...", (unsigned long long) testSectors);
 uint8_t *writeBuffer = alignedAlloc (SECTOR_SIZE);
 uint8_t *readBuffer = alignedAlloc (SECTOR_SIZE);
 uint8_t *backupBuffer = alignedAlloc (SECTOR_SIZE);
 if (!writeBuffer || !readBuffer || !backupBuffer)
 {
  if (writeBuffer)
   alignedFree (writeBuffer);
  if (readBuffer)
   alignedFree (readBuffer);
  if (backupBuffer)
   alignedFree (backupBuffer);
  return false;
 }
 const device_io_ops_t *ioOps = deviceIoGetOps ();
 bool foundWp = false;
 for (uint64_t sector = 0; sector < testSectors && !foundWp; sector += 128)
 {
  int writeResult = testSectorWrite (device, ioOps, sector, writeBuffer, readBuffer, backupBuffer);
  if (writeResult == 1)
  {
   foundWp = true;
   if (firstWpSector)
    *firstWpSector = sector;
   LOG_WARN ("Write-protection detected at sector %llu", (unsigned long long) sector);
  }
 }
 alignedFree (writeBuffer);
 alignedFree (readBuffer);
 alignedFree (backupBuffer);
 if (!foundWp)
  LOG_INFO ("No write-protection detected in tested range");
 return foundWp;
}