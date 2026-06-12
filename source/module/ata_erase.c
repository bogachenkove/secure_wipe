#include "ata_erase.h"
#include "platform.h"
#include <string.h>
#include <stdio.h>
#include <time.h>

#ifdef _WIN32
#include <ntddscsi.h>
#include <winioctl.h>

typedef struct
{
 BYTE command;
 BYTE commandSpecific;
 BYTE features;
 BYTE sectors;
 BYTE sectorNumber;
 BYTE cylinderLow;
 BYTE cylinderHigh;
 BYTE driveHead;
} ATA_COMMAND_REGISTER;

typedef struct
{
 BYTE command;
 BYTE features;
 BYTE sectorCount;
 BYTE sectorNumber;
 BYTE cylinderLow;
 BYTE cylinderHigh;
 BYTE driveHead;
 BYTE reserved[3];
} ATA_COMMAND_REGISTER_48;

static error_code_t ataPassThroughWindows (device_t *device, BYTE command, BYTE features, BYTE sectorCount, BYTE sectorNumber, BYTE cylinderLow,
										   BYTE cylinderHigh, BYTE driveHead, void *dataBuffer, DWORD bufferSize, BOOL isWrite)
{
 if (!device || !device->isOpen)
  return ERR_INVALID_ARG;

 DWORD totalBufferSize = sizeof (ATA_PASS_THROUGH_EX) + bufferSize;
 BYTE *fullBuffer = (BYTE *) malloc (totalBufferSize);
 if (!fullBuffer)
  return ERR_MEMORY;

 memset (fullBuffer, 0, totalBufferSize);

 ATA_PASS_THROUGH_EX *pte = (ATA_PASS_THROUGH_EX *) fullBuffer;
 pte->Length = sizeof (ATA_PASS_THROUGH_EX);
 pte->TimeOutValue = 30;
 pte->DataBufferOffset = sizeof (ATA_PASS_THROUGH_EX);
 pte->DataTransferLength = bufferSize;
 pte->AtaFlags = isWrite ? ATA_FLAGS_DATA_OUT : ATA_FLAGS_DATA_IN;

 if (command == 0xF3 || command == 0xF4 || command == 0xF5 || command == 0xF6)
  pte->AtaFlags = ATA_FLAGS_48BIT_COMMAND;

 pte->CurrentTaskFile[0] = command;
 pte->CurrentTaskFile[1] = features;
 pte->CurrentTaskFile[2] = sectorCount;
 pte->CurrentTaskFile[3] = sectorNumber;
 pte->CurrentTaskFile[4] = cylinderLow;
 pte->CurrentTaskFile[5] = cylinderHigh;
 pte->CurrentTaskFile[6] = driveHead;

 if (dataBuffer && bufferSize > 0 && isWrite)
 {
  if (pte->DataBufferOffset + bufferSize > totalBufferSize)
  {
   free (fullBuffer);
   return ERR_INVALID_ARG;
  }
  memcpy (fullBuffer + pte->DataBufferOffset, dataBuffer, bufferSize);
 }

 DWORD bytesReturned;
 BOOL success =
	 DeviceIoControl (device->handle, IOCTL_ATA_PASS_THROUGH, fullBuffer, totalBufferSize, fullBuffer, totalBufferSize, &bytesReturned, NULL);

 if (success)
 {
  if (pte->CurrentTaskFile[0] & 0x01)
  {
   free (fullBuffer);
   return ERR_READ_DEVICE;
  }
  if (dataBuffer && !isWrite && bufferSize > 0)
  {
   if (pte->DataBufferOffset + bufferSize > totalBufferSize)
   {
	free (fullBuffer);
	return ERR_INVALID_ARG;
   }
   memcpy (dataBuffer, fullBuffer + pte->DataBufferOffset, bufferSize);
  }
  free (fullBuffer);
  return ERR_OK;
 }

 free (fullBuffer);
 return ERR_READ_DEVICE;
}

static error_code_t ataIdentifyWindows (device_t *device, uint16_t *identifyData)
{
 if (!device || !identifyData)
  return ERR_INVALID_ARG;
 return ataPassThroughWindows (device, 0xEC, 0x00, 0x00, 0x00, 0x00, 0x00, 0xA0, identifyData, 512, FALSE);
}

#else

#include <linux/hdreg.h>
#include <linux/fs.h>
#include <sys/ioctl.h>
#include <limits.h>
#include <scsi/sg.h>
#include <stdbool.h>

static error_code_t ataPassThroughLinux (device_t *device, uint8_t command, uint8_t features, uint8_t sectorCount, uint64_t logicalBlockAddress,
										 void *dataBuffer, size_t bufferSize, bool isWrite)
{
 if (!device || !device->isOpen)
  return ERR_INVALID_ARG;

 if (bufferSize > UINT_MAX)
  return ERR_INVALID_ARG;

 struct sg_io_hdr sgHeader;
 uint8_t cdb[16];
 uint8_t sense[32];
 uint8_t *data = NULL;

 memset (&sgHeader, 0, sizeof (sgHeader));
 memset (cdb, 0, sizeof (cdb));
 memset (sense, 0, sizeof (sense));

 if (logicalBlockAddress > 0x0FFFFFFF)
 {
  cdb[0] = isWrite ? 0x8F : 0x8E;
  cdb[1] = 0x00;
  cdb[2] = (logicalBlockAddress >> 40) & 0xFF;
  cdb[3] = (logicalBlockAddress >> 32) & 0xFF;
  cdb[4] = (logicalBlockAddress >> 24) & 0xFF;
  cdb[5] = (logicalBlockAddress >> 16) & 0xFF;
  cdb[6] = (logicalBlockAddress >> 8) & 0xFF;
  cdb[7] = logicalBlockAddress & 0xFF;
  cdb[8] = sectorCount;
  cdb[9] = features;
  cdb[10] = command;
  cdb[11] = 0x00;
  cdb[12] = 0x00;
  cdb[13] = 0x00;
  cdb[14] = 0x00;
  cdb[15] = 0x00;
 }
 else
 {
  cdb[0] = isWrite ? 0x8B : 0x8A;
  cdb[1] = 0x00;
  cdb[2] = (logicalBlockAddress >> 24) & 0xFF;
  cdb[3] = (logicalBlockAddress >> 16) & 0xFF;
  cdb[4] = (logicalBlockAddress >> 8) & 0xFF;
  cdb[5] = logicalBlockAddress & 0xFF;
  cdb[6] = sectorCount;
  cdb[7] = features;
  cdb[8] = command;
  cdb[9] = 0x00;
  cdb[10] = 0x00;
  cdb[11] = 0x00;
  cdb[12] = 0x00;
  cdb[13] = 0x00;
  cdb[14] = 0x00;
  cdb[15] = 0x00;
 }

 if (dataBuffer && bufferSize > 0)
 {
  data = (uint8_t *) malloc (bufferSize);
  if (!data)
   return ERR_MEMORY;
  if (isWrite)
   memcpy (data, dataBuffer, bufferSize);
 }

 sgHeader.interface_id = 'S';
 sgHeader.dxfer_direction = isWrite ? SG_DXFER_TO_DEV : SG_DXFER_FROM_DEV;
 sgHeader.cmd_len = sizeof (cdb);
 sgHeader.cmdp = cdb;
 sgHeader.dxferp = data ? data : dataBuffer;
 sgHeader.dxfer_len = (unsigned int) bufferSize;
 sgHeader.sbp = sense;
 sgHeader.mx_sb_len = sizeof (sense);
 sgHeader.timeout = 30000;

 int ioctlResult = ioctl (device->handle, SG_IO, &sgHeader);
 error_code_t result = ERR_OK;

 if (ioctlResult < 0 || (sgHeader.info & SG_INFO_CHECK))
  result = ERR_READ_DEVICE;
 else if (data && !isWrite && bufferSize > 0)
  memcpy (dataBuffer, data, bufferSize);

 if (data)
  free (data);

 return result;
}

static error_code_t ataIdentifyLinux (device_t *device, uint16_t *identifyData)
{
 if (!device || !identifyData)
  return ERR_INVALID_ARG;
 return ataPassThroughLinux (device, 0xEC, 0x00, 0x01, 0, identifyData, 512, false);
}

#endif

static void parseIdentifyData (const uint16_t *data, ata_security_info_t *info)
{
 if (!data || !info)
  return;

 memset (info, 0, sizeof (ata_security_info_t));

 for (int wordIndex = 0; wordIndex < 20; wordIndex++)
 {
  uint16_t word = data[10 + wordIndex];
  info->model[wordIndex * 2] = (word >> 8) & 0xFF;
  info->model[wordIndex * 2 + 1] = word & 0xFF;
 }
 for (int charIndex = 19; charIndex >= 0 && info->model[charIndex] == ' '; charIndex--)
  info->model[charIndex] = '\0';

 for (int wordIndex = 0; wordIndex < 4; wordIndex++)
 {
  uint16_t word = data[23 + wordIndex];
  info->firmware[wordIndex * 2] = (word >> 8) & 0xFF;
  info->firmware[wordIndex * 2 + 1] = word & 0xFF;
 }
 for (int charIndex = 7; charIndex >= 0 && info->firmware[charIndex] == ' '; charIndex--)
  info->firmware[charIndex] = '\0';

 for (int wordIndex = 0; wordIndex < 10; wordIndex++)
 {
  uint16_t word = data[10 + wordIndex];
  info->serial[wordIndex * 2] = (word >> 8) & 0xFF;
  info->serial[wordIndex * 2 + 1] = word & 0xFF;
 }
 for (int charIndex = 19; charIndex >= 0 && info->serial[charIndex] == ' '; charIndex--)
  info->serial[charIndex] = '\0';

 if (data[82] & 0x0002)
  info->supported = true;
 if (data[82] & 0x0004)
  info->enhancedSupported = true;

 info->maxSectorsPerCommand = data[103];
 if (info->maxSectorsPerCommand == 0)
  info->maxSectorsPerCommand = 256;

 info->maxLba28 = data[60] | (data[61] << 16);
 info->maxLba48 = ((uint64_t) data[100] | ((uint64_t) data[101] << 16) | ((uint64_t) data[102] << 32) | ((uint64_t) data[103] << 48));
}

error_code_t ataGetSecurityInfo (device_t *device, ata_security_info_t *info)
{
 if (!device || !device->isOpen || !info)
  return ERR_INVALID_ARG;

 uint16_t identifyData[256];
 memset (identifyData, 0, sizeof (identifyData));

 error_code_t result;
#ifdef _WIN32
 result = ataIdentifyWindows (device, identifyData);
#else
 result = ataIdentifyLinux (device, identifyData);
#endif

 if (result != ERR_OK)
  return result;

 parseIdentifyData (identifyData, info);
 return ERR_OK;
}

static error_code_t ataSetPassword (device_t *device, const char *password, size_t passwordLength)
{
 if (!device || !password || passwordLength == 0 || passwordLength > 32)
  return ERR_INVALID_ARG;

 uint8_t securityBuffer[512];
 memset (securityBuffer, 0, sizeof (securityBuffer));

 securityBuffer[0] = 0x01;
 securityBuffer[2] = (uint8_t) passwordLength;
 memcpy (securityBuffer + 8, password, passwordLength);
 for (size_t fillIndex = passwordLength; fillIndex < 32; fillIndex++)
  securityBuffer[8 + fillIndex] = 0;

#ifdef _WIN32
 return ataPassThroughWindows (device, 0xF1, 0x01, 0x00, 0x00, 0x00, 0x00, 0xA0, securityBuffer, 512, true);
#else
 return ataPassThroughLinux (device, 0xF1, 0x01, 0x01, 0, securityBuffer, 512, true);
#endif
}

static error_code_t ataDisablePassword (device_t *device, const char *password, size_t passwordLength)
{
 if (!device || !password || passwordLength == 0 || passwordLength > 32)
  return ERR_INVALID_ARG;

 uint8_t securityBuffer[512];
 memset (securityBuffer, 0, sizeof (securityBuffer));

 securityBuffer[0] = 0x00;
 securityBuffer[2] = (uint8_t) passwordLength;
 memcpy (securityBuffer + 8, password, passwordLength);
 for (size_t fillIndex = passwordLength; fillIndex < 32; fillIndex++)
  securityBuffer[8 + fillIndex] = 0;

#ifdef _WIN32
 return ataPassThroughWindows (device, 0xF1, 0x00, 0x00, 0x00, 0x00, 0x00, 0xA0, securityBuffer, 512, true);
#else
 return ataPassThroughLinux (device, 0xF1, 0x00, 0x01, 0, securityBuffer, 512, true);
#endif
}

static error_code_t ataExecuteSecureErase (device_t *device, ata_erase_type_t eraseType, const char *password, size_t passwordLength)
{
 (void) password;
 (void) passwordLength;
 if (!device || !password || passwordLength == 0 || passwordLength > 32)
  return ERR_INVALID_ARG;

 uint8_t command = (eraseType == ATA_ERASE_ENHANCED) ? 0xF5 : 0xF4;
 uint8_t features = 0x00;

#ifdef _WIN32
 return ataPassThroughWindows (device, command, features, 0x00, 0x00, 0x00, 0x00, 0xA0, NULL, 0, false);
#else
 return ataPassThroughLinux (device, command, features, 0x01, 0, NULL, 0, false);
#endif
}

bool ataIsFrozen (device_t *device)
{
 if (!device || !device->isOpen)
  return true;

#ifdef _WIN32
 uint16_t identifyData[256];
 memset (identifyData, 0, sizeof (identifyData));
 if (ataIdentifyWindows (device, identifyData) != ERR_OK)
  return true;
 return (identifyData[128] & 0x0008) ? true : false;
#else
 uint16_t identifyData[256];
 memset (identifyData, 0, sizeof (identifyData));
 if (ataIdentifyLinux (device, identifyData) != ERR_OK)
  return true;
 return (identifyData[128] & 0x0008) != 0;
#endif
}

error_code_t ataThawDevice (device_t *device)
{
 if (!device || !device->isOpen)
  return ERR_INVALID_ARG;

 LOG_INFO ("Attempting to thaw device from frozen state...");

 if (!ataIsFrozen (device))
 {
  LOG_INFO ("Device is not frozen, no thaw needed");
  return ERR_OK;
 }

 error_code_t result = ataSetPassword (device, "SECUREWIPE", 10);
 if (result != ERR_OK)
 {
  LOG_WARN ("Failed to set temporary password for thaw");
  return result;
 }

 result = ataDisablePassword (device, "SECUREWIPE", 10);
 if (result != ERR_OK)
 {
  LOG_WARN ("Failed to disable temporary password after thaw");
  return result;
 }

 LOG_INFO ("Device thawed successfully");
 return ERR_OK;
}

error_code_t ataSecureErase (device_t *device, ata_erase_type_t eraseType, progress_callback_t progress)
{
 if (!device || !device->isOpen)
  return ERR_INVALID_ARG;

 LOG_INFO ("Starting ATA Secure Erase (%s)", eraseType == ATA_ERASE_ENHANCED ? "ENHANCED" : "NORMAL");

 ata_security_info_t securityInfo;
 error_code_t result = ataGetSecurityInfo (device, &securityInfo);
 if (result != ERR_OK)
 {
  LOG_ERROR ("Failed to get device security info");
  return result;
 }

 if (!securityInfo.supported)
 {
  LOG_ERROR ("ATA Security not supported by this device");
  return ERR_PERMISSION;
 }

 if (eraseType == ATA_ERASE_ENHANCED && !securityInfo.enhancedSupported)
 {
  LOG_WARN ("Enhanced erase not supported, falling back to normal erase");
  eraseType = ATA_ERASE_NORMAL;
 }

 LOG_INFO ("Device: %s (FW: %s, SN: %s)", securityInfo.model, securityInfo.firmware, securityInfo.serial);
 LOG_INFO ("Security %s, Enhanced %s", securityInfo.supported ? "supported" : "not supported",
		   securityInfo.enhancedSupported ? "supported" : "not supported");

 if (ataIsFrozen (device))
 {
  LOG_WARN ("Device is frozen. Attempting to thaw...");
  result = ataThawDevice (device);
  if (result != ERR_OK)
  {
   LOG_ERROR ("Cannot proceed: device is frozen and thaw failed.");
   LOG_ERROR ("Try suspending and resuming system, or use --emergency mode.");
   return ERR_PERMISSION;
  }
 }

 const char *temporaryPassword = "SECUREWIPE_TEMP";
 size_t passwordLength = 15;

 if (progress)
  progress (0, 100, 0, "Setting security password");

 result = ataSetPassword (device, temporaryPassword, passwordLength);
 if (result != ERR_OK)
 {
  LOG_ERROR ("Failed to set security password");
  return result;
 }

 LOG_INFO ("Security password set, starting erase operation...");

 if (progress)
  progress (10, 100, 0, "Erasing (this may take several minutes)");

 result = ataExecuteSecureErase (device, eraseType, temporaryPassword, passwordLength);

 if (progress)
  progress (90, 100, 0, "Finalizing");

 if (result != ERR_OK)
 {
  LOG_ERROR ("Secure erase command failed");
  ataDisablePassword (device, temporaryPassword, passwordLength);
  return result;
 }

 LOG_INFO ("Secure erase completed, disabling security...");
 result = ataDisablePassword (device, temporaryPassword, passwordLength);
 if (result != ERR_OK)
  LOG_WARN ("Failed to disable security after erase (may need power cycle)");

 if (progress)
  progress (100, 100, 0, "Complete");

 LOG_INFO ("ATA Secure Erase completed successfully");
 return ERR_OK;
}