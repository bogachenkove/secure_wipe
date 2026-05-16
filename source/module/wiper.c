#include "wiper.h"
#include "random_gen.h"
#include "platform.h"

static uint8_t *gWipeBuffer = NULL;

typedef enum
{
 PATTERN_ZERO,
 PATTERN_RANDOM
} pattern_type_t;

static void fillBufferWithPattern (uint8_t *buffer, size_t bufferSize, pattern_type_t patternType, const uint8_t *patternData, size_t patternLength)
{
 (void) patternData;
 (void) patternLength;
 switch (patternType)
 {
 case PATTERN_ZERO:
  memset (buffer, 0x00, bufferSize);
  break;
 case PATTERN_RANDOM:
  randomFill (buffer, bufferSize);
  break;
 default:
  break;
 }
}

static error_code_t singlePass (device_t *device, pattern_type_t patternType, const uint8_t *patternData, size_t patternLength,
								const analysis_result_t *badSectors, progress_callback_t progress, int passNumber, int totalPasses,
								const char *description)
{
 if (!device || !device->isOpen || !gWipeBuffer)
  return ERR_INVALID_ARG;

 const device_io_ops_t *ioOperations = deviceIoGetOps ();
 uint64_t currentSector = 0;
 int lastPercent = -1;
 uint64_t writeErrors = 0;
 char descriptionBuffer[128];

 snprintf (descriptionBuffer, sizeof (descriptionBuffer), "Pass %d/%d: %s", passNumber, totalPasses, description);
 LOG_INFO ("Starting %s", descriptionBuffer);

 while (currentSector < device->sectorCount)
 {
  uint32_t sectorsToWrite = (uint32_t) gBufferSectors;
  if (currentSector + sectorsToWrite > device->sectorCount)
   sectorsToWrite = (uint32_t) (device->sectorCount - currentSector);

  size_t bufferSize = (size_t) sectorsToWrite * device->sectorSize;

  if (patternType == PATTERN_RANDOM)
  {
   if (randomFill (gWipeBuffer, bufferSize) != ERR_OK)
   {
	LOG_ERROR ("Random fill failed at sector %llu", (unsigned long long) currentSector);
	return ERR_RANDOM_GEN;
   }
  }
  else
  {
   fillBufferWithPattern (gWipeBuffer, bufferSize, patternType, patternData, patternLength);
  }

  if (ioOperations->writeSectors (device, currentSector, sectorsToWrite, gWipeBuffer) != ERR_OK)
  {
   LOG_WARN ("Block write failed at sector %llu, switching to sector-by-sector", (unsigned long long) currentSector);

   uint64_t consecutiveErrors = 0;
   const uint64_t MAX_CONSECUTIVE_ERRORS = 100;

   for (uint32_t sectorOffset = 0; sectorOffset < sectorsToWrite; sectorOffset++)
   {
	uint64_t targetSector = currentSector + sectorOffset;
	if (badSectors && analyzerIsBadSector (badSectors, targetSector))
	 continue;

	if (patternType == PATTERN_RANDOM)
	 randomFill (gWipeBuffer, device->sectorSize);
	else
	 fillBufferWithPattern (gWipeBuffer, device->sectorSize, patternType, patternData, patternLength);

	if (ioOperations->writeSectors (device, targetSector, 1, gWipeBuffer) != ERR_OK)
	{
	 writeErrors++;
	 consecutiveErrors++;
	 if (consecutiveErrors <= 10)
	  logBadSector (targetSector, "write");
	 else if (consecutiveErrors == 11)
	  LOG_WARN ("Too many consecutive write errors, further errors will not be logged individually");
	}
	else
	{
	 consecutiveErrors = 0;
	}

	if (consecutiveErrors >= MAX_CONSECUTIVE_ERRORS)
	{
	 LOG_ERROR ("Aborting: %llu consecutive write errors (device may be write-protected)", (unsigned long long) consecutiveErrors);
	 return ERR_WRITE_DEVICE;
	}
   }
  }

  currentSector += sectorsToWrite;
  if (progress)
  {
   int currentPercent = (int) ((currentSector * 100) / device->sectorCount);
   if (currentPercent != lastPercent)
   {
	progress (currentSector, device->sectorCount, passNumber, descriptionBuffer);
	lastPercent = currentPercent;
   }
  }
 }

 ioOperations->flush (device);
 if (writeErrors > 0)
  LOG_WARN ("%s - completed with %llu write errors", descriptionBuffer, (unsigned long long) writeErrors);
 else
  LOG_INFO ("%s - completed successfully", descriptionBuffer);
 return ERR_OK;
}

error_code_t wiperInit (void)
{
 if (gWipeBuffer)
  return ERR_OK;
 gWipeBuffer = (uint8_t *) alignedAlloc (gBufferSize);
 if (!gWipeBuffer)
  return ERR_MEMORY;
 LOG_INFO ("Wiper module initialized (buffer: %zu bytes, %zu sectors)", gBufferSize, gBufferSectors);
 return ERR_OK;
}

void wiperCleanup (void)
{
 if (gWipeBuffer)
 {
  memset (gWipeBuffer, 0, gBufferSize);
  alignedFree (gWipeBuffer);
  gWipeBuffer = NULL;
  LOG_INFO ("Wiper module cleaned up");
 }
}

const char *wiperMethodName (wipe_method_t method)
{
 switch (method)
 {
 case WIPE_METHOD_ZERO:
  return "Zero Fill";
 case WIPE_METHOD_RANDOM:
  return "Random Fill";
 default:
  return "Unknown";
 }
}

int wiperMethodPasses (wipe_method_t method)
{
 switch (method)
 {
 case WIPE_METHOD_ZERO:
  return 1;
 case WIPE_METHOD_RANDOM:
  return 1;
 default:
  return 0;
 }
}

error_code_t wiperExecute (device_t *device, const wipe_config_t *config, wipe_stats_t *stats)
{
 if (!device || !device->isOpen || !config)
  return ERR_INVALID_ARG;
 if (!gWipeBuffer)
 {
  LOG_ERROR ("Wiper not initialized");
  return ERR_MEMORY;
 }
 if (stats)
 {
  memset (stats, 0, sizeof (wipe_stats_t));
  stats->startTime = time (NULL);
  stats->totalPasses = config->passes;
 }
 LOG_INFO ("Starting secure wipe: %s on %s", wiperMethodName (config->method), device->path);

 error_code_t resultCode = ERR_OK;
 wiper_method_func methodFunction = NULL;
 switch (config->method)
 {
 case WIPE_METHOD_ZERO:
  methodFunction = wiperMethodZero;
  break;
 case WIPE_METHOD_RANDOM:
  methodFunction = wiperMethodRandom;
  break;
 default:
  return ERR_INVALID_ARG;
 }
 resultCode = methodFunction (device, config->passes, config->progress);
 if (stats)
 {
  stats->endTime = time (NULL);
  if (resultCode == ERR_OK)
   stats->sectorsWiped = device->sectorCount * stats->totalPasses;
 }
 if (resultCode == ERR_OK)
  LOG_INFO ("Wipe completed successfully");
 else
  LOG_ERROR ("Wipe failed: %s", errorToString (resultCode));
 return resultCode;
}

error_code_t wiperMethodZero (device_t *device, uint32_t passes, progress_callback_t progress)
{
 (void) passes;
 uint8_t zeroByte = 0x00;
 return singlePass (device, PATTERN_ZERO, &zeroByte, 1, NULL, progress, 1, 1, "Zero fill (0x00)");
}

error_code_t wiperMethodRandom (device_t *device, uint32_t passes, progress_callback_t progress)
{
 for (uint32_t passIndex = 1; passIndex <= passes; passIndex++)
 {
  char description[64];
  snprintf (description, sizeof (description), "Random fill (pass %u/%u)", passIndex, passes);
  error_code_t errorCode = singlePass (device, PATTERN_RANDOM, NULL, 0, NULL, progress, (int) passIndex, (int) passes, description);
  if (errorCode != ERR_OK)
   return errorCode;
 }
 return ERR_OK;
}

static void overwriteSectorRange (device_t *device, uint64_t startSector, uint32_t sectorCount, uint8_t fillByte)
{
 if (!device || !device->isOpen || device->readOnly)
  return;
 const device_io_ops_t *ioOperations = deviceIoGetOps ();
 size_t bufferSize = (size_t) sectorCount * SECTOR_SIZE;
 uint8_t *buffer = (uint8_t *) alignedAlloc (bufferSize);
 if (!buffer)
 {
  LOG_ERROR ("Failed to allocate buffer for partition table destruction");
  return;
 }
 memset (buffer, fillByte, bufferSize);
 if (ioOperations->writeSectors (device, startSector, sectorCount, buffer) != ERR_OK)
 {
  LOG_WARN ("Block write failed, trying sector-by-sector");
  for (uint32_t offset = 0; offset < sectorCount; offset++)
  {
   memset (buffer, fillByte, SECTOR_SIZE);
   ioOperations->writeSectors (device, startSector + offset, 1, buffer);
  }
 }
 alignedFree (buffer);
}

error_code_t wiperZeroPartitionTable (device_t *device)
{
 if (!device || !device->isOpen)
  return ERR_INVALID_ARG;
 if (device->readOnly)
 {
  LOG_ERROR ("Device opened read-only, cannot destroy partition table");
  return ERR_PERMISSION;
 }
 LOG_INFO ("Destroying partition table (MBR+GPT) with zeros");
 uint32_t sectorsToWrite = 34;
 if (sectorsToWrite > device->sectorCount)
  sectorsToWrite = (uint32_t) device->sectorCount;
 overwriteSectorRange (device, 0, sectorsToWrite, 0x00);
 if (device->sectorCount > 33)
 {
  uint64_t backupStart = device->sectorCount - 33;
  LOG_INFO ("Overwriting backup GPT (sectors %llu-%llu) with zeros", (unsigned long long) backupStart,
			(unsigned long long) (device->sectorCount - 1));
  overwriteSectorRange (device, backupStart, 33, 0x00);
 }
 deviceIoGetOps ()->flush (device);
 LOG_INFO ("Partition table destruction complete");
 return ERR_OK;
}

static void overwriteMetadataRange (device_t *device, uint64_t startSector, uint64_t endSector, int passIndex, progress_callback_t progress,
									const char *phasePrefix)
{
 const device_io_ops_t *ioOperations = deviceIoGetOps ();
 uint8_t *buffer = (uint8_t *) alignedAlloc (gBufferSize);
 if (!buffer)
  return;
 uint64_t sectorIndex = startSector;
 while (sectorIndex < endSector)
 {
  uint32_t sectorsToWrite = (uint32_t) gBufferSectors;
  if (sectorIndex + sectorsToWrite > endSector)
   sectorsToWrite = (uint32_t) (endSector - sectorIndex);
  randomFill (buffer, (size_t) sectorsToWrite * SECTOR_SIZE);
  if (ioOperations->writeSectors (device, sectorIndex, sectorsToWrite, buffer) != ERR_OK)
  {
   for (uint32_t offset = 0; offset < sectorsToWrite; offset++)
   {
	randomFill (buffer, SECTOR_SIZE);
	ioOperations->writeSectors (device, sectorIndex + offset, 1, buffer);
   }
  }
  sectorIndex += sectorsToWrite;
  if (progress)
   progress (sectorIndex, endSector, passIndex, phasePrefix);
 }
 alignedFree (buffer);
}

static error_code_t wiperDestroyFilesystemMetadata (device_t *device, int passes, progress_callback_t progress)
{
 if (!device || !device->isOpen || device->readOnly || passes <= 0)
  return ERR_INVALID_ARG;
 LOG_INFO ("Destroying filesystem metadata (%d passes)", passes);
 uint64_t metadataSectors = 8192;
 if (metadataSectors > device->sectorCount / 2)
  metadataSectors = device->sectorCount / 2;
 const device_io_ops_t *ioOperations = deviceIoGetOps ();
 for (int passIndex = 1; passIndex <= passes; passIndex++)
 {
  overwriteMetadataRange (device, 0, metadataSectors, passIndex, progress, "Destroying metadata (start)");
  if (device->sectorCount > metadataSectors)
  {
   uint64_t endStart = device->sectorCount - metadataSectors;
   overwriteMetadataRange (device, endStart, device->sectorCount, passIndex, progress, "Destroying metadata (end)");
  }
  ioOperations->flush (device);
 }
 LOG_INFO ("Filesystem metadata destruction complete");
 return ERR_OK;
}

#ifdef _WIN32
static void dismountVolumesOnDisk (int diskNumber)
{
 wchar_t volumeName[MAX_PATH];
 HANDLE findHandle = FindFirstVolumeW (volumeName, MAX_PATH);
 if (findHandle == INVALID_HANDLE_VALUE)
  return;
 do
 {
  wchar_t devicePath[MAX_PATH];
  DWORD charsReturned;
  if (GetVolumePathNamesForVolumeNameW (volumeName, devicePath, MAX_PATH, &charsReturned))
  {
   HANDLE volume = CreateFileW (volumeName, GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
   if (volume != INVALID_HANDLE_VALUE)
   {
	VOLUME_DISK_EXTENTS extents = {0};
	DWORD bytes;
	if (DeviceIoControl (volume, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, NULL, 0, &extents, sizeof (extents), &bytes, NULL))
	{
	 for (DWORD extentIndex = 0; extentIndex < extents.NumberOfDiskExtents; extentIndex++)
	 {
	  if ((int) extents.Extents[extentIndex].DiskNumber == diskNumber)
	  {
	   DeviceIoControl (volume, FSCTL_DISMOUNT_VOLUME, NULL, 0, NULL, 0, &bytes, NULL);
	   DeviceIoControl (volume, FSCTL_LOCK_VOLUME, NULL, 0, NULL, 0, &bytes, NULL);
	   break;
	  }
	 }
	}
	CloseHandle (volume);
   }
  }
 } while (FindNextVolumeW (findHandle, volumeName, MAX_PATH));
 FindVolumeClose (findHandle);
}
#endif

bool wiperTryRemoveWriteProtection (device_t *device)
{
 if (!device || !device->isOpen)
  return false;
 LOG_INFO ("Attempting to remove write protection...");

#ifdef _WIN32
 int diskNumber = -1;
 if (sscanf (device->path, "\\\\.\\PhysicalDrive%d", &diskNumber) == 1)
  dismountVolumesOnDisk (diskNumber);

 DWORD bytesReturned;
 typedef struct
 {
  ULONG Version;
  BOOLEAN Persist;
  BYTE Reserved1[3];
  ULONGLONG Attributes;
  ULONGLONG AttributesMask;
  ULONG Reserved2[4];
 } SET_DISK_ATTRIBUTES;
#ifndef IOCTL_DISK_SET_DISK_ATTRIBUTES
#define IOCTL_DISK_SET_DISK_ATTRIBUTES 0x0007C0F4
#endif
#ifndef DISK_ATTRIBUTE_READ_ONLY
#define DISK_ATTRIBUTE_READ_ONLY 0x0000000000000002ULL
#endif
 SET_DISK_ATTRIBUTES attributes = {0};
 attributes.Version = sizeof (attributes);
 attributes.Persist = TRUE;
 attributes.AttributesMask = DISK_ATTRIBUTE_READ_ONLY;
 DeviceIoControl (device->handle, IOCTL_DISK_SET_DISK_ATTRIBUTES, &attributes, sizeof (attributes), NULL, 0, &bytesReturned, NULL);

 PREVENT_MEDIA_REMOVAL pmr = {FALSE};
 DeviceIoControl (device->handle, IOCTL_STORAGE_MEDIA_REMOVAL, &pmr, sizeof (pmr), NULL, 0, &bytesReturned, NULL);
 DeviceIoControl (device->handle, IOCTL_DISK_UPDATE_PROPERTIES, NULL, 0, NULL, 0, &bytesReturned, NULL);

 CloseHandle (device->handle);
 device->handle =
	 CreateFileA (device->path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_FLAG_NO_BUFFERING | FILE_FLAG_WRITE_THROUGH, NULL);
 if (device->handle != INVALID_HANDLE_VALUE)
 {
  LOG_INFO ("Device reopened with exclusive access");
  device->isOpen = true;
  return true;
 }
 else
 {
  LOG_WARN ("Failed to reopen device (error %lu)", GetLastError ());
  device->isOpen = false;
  return false;
 }
#else
 int readonlyFlag = 0;
 if (ioctl (device->handle, BLKROSET, &readonlyFlag) == 0)
 {
  LOG_INFO ("Write protection removed via BLKROSET");
  return true;
 }
 else
 {
  LOG_WARN ("BLKROSET failed: %s", strerror (errno));
  return false;
 }
#endif
}

error_code_t wiperPrepareDevice (device_t *device, progress_callback_t progress)
{
 if (!device || !device->isOpen)
  return ERR_INVALID_ARG;
 LOG_INFO ("Preparing device for secure wipe");
 if (device->readOnly)
  return ERR_PERMISSION;
 if (!wiperTryRemoveWriteProtection (device))
  LOG_WARN ("Could not remove write protection via system call, will attempt to write anyway");
 return wiperDestroyFilesystemMetadata (device, 3, progress);
}