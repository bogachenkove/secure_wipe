#include "disk_scanner.h"

#ifdef _WIN32
#include <windows.h>
#include <ntddscsi.h>
#include <cfgmgr32.h>
#pragma comment(lib, "cfgmgr32.lib")
#endif

const char *diskTypeToString (disk_type_t type)
{
 switch (type)
 {
 case DISK_TYPE_HDD:
  return "HDD";
 case DISK_TYPE_SSD:
  return "SSD";
 case DISK_TYPE_USB:
  return "USB";
 case DISK_TYPE_NVME:
  return "NVMe";
 case DISK_TYPE_SD_CARD:
  return "SD Card";
 case DISK_TYPE_OPTICAL:
  return "Optical";
 case DISK_TYPE_VIRTUAL:
  return "Virtual";
 default:
  return "Unknown";
 }
}

bool diskScannerIsSafeToWipe (const disk_info_t *info)
{
 if (!info)
  return false;
 return !info->isSystem && !info->isBoot;
}

const disk_info_t *diskScannerGetByIndex (const disk_scan_result_t *result, int index)
{
 if (!result || index < 1 || index > result->count)
  return NULL;
 return &result->disks[index - 1];
}

#ifdef _WIN32

static disk_type_t getDiskTypeFromBus (STORAGE_BUS_TYPE busType, bool isRemovable)
{
 switch (busType)
 {
 case BusTypeUsb:
  return DISK_TYPE_USB;
 case BusTypeNvme:
  return DISK_TYPE_NVME;
 case BusTypeSata:
 case BusTypeAta:
  return isRemovable ? DISK_TYPE_USB : DISK_TYPE_HDD;
 case BusTypeSd:
 case BusTypeMmc:
  return DISK_TYPE_SD_CARD;
 case BusTypeVirtual:
 case BusTypeFileBackedVirtual:
  return DISK_TYPE_VIRTUAL;
 case BusTypeAtapi:
  return DISK_TYPE_OPTICAL;
 default:
  return DISK_TYPE_UNKNOWN;
 }
}

static error_code_t getDiskInfoWindows (int diskNumber, disk_info_t *info)
{
 char devicePath[64];
 snprintf (devicePath, sizeof (devicePath), "\\\\.\\PhysicalDrive%d", diskNumber);
 HANDLE deviceHandle = CreateFileA (devicePath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
 if (deviceHandle == INVALID_HANDLE_VALUE)
  return ERR_OPEN_DEVICE;

 memset (info, 0, sizeof (disk_info_t));
 snprintf (info->devicePath, sizeof (info->devicePath), "%s", devicePath);
 info->diskNumber = diskNumber;
 info->sectorSize = SECTOR_SIZE;

 DWORD bytesReturned = 0;
 DISK_GEOMETRY_EX geometry = {0};
 if (DeviceIoControl (deviceHandle, IOCTL_DISK_GET_DRIVE_GEOMETRY_EX, NULL, 0, &geometry, sizeof (geometry), &bytesReturned, NULL))
 {
  info->sizeBytes = geometry.DiskSize.QuadPart;
  info->sectorSize = geometry.Geometry.BytesPerSector;
 }

 STORAGE_PROPERTY_QUERY query;
 memset (&query, 0, sizeof (query));
 query.PropertyId = StorageDeviceProperty;
 query.QueryType = PropertyStandardQuery;

 uint8_t propertyBuffer[4096] = {0};
 bytesReturned = 0;
 if (DeviceIoControl (deviceHandle, IOCTL_STORAGE_QUERY_PROPERTY, &query, sizeof (query), propertyBuffer, sizeof (propertyBuffer), &bytesReturned,
					  NULL))
 {
  STORAGE_DEVICE_DESCRIPTOR *descriptor = (STORAGE_DEVICE_DESCRIPTOR *) propertyBuffer;
  info->isRemovable = descriptor->RemovableMedia;
  info->type = getDiskTypeFromBus (descriptor->BusType, info->isRemovable);

  if (descriptor->VendorIdOffset > 0)
  {
   char *vendor = (char *) propertyBuffer + descriptor->VendorIdOffset;
   snprintf (info->vendor, sizeof (info->vendor), "%s", vendor);
   size_t vendorLength = strlen (info->vendor);
   while (vendorLength > 0 && (info->vendor[vendorLength - 1] == ' ' || info->vendor[vendorLength - 1] == '\0'))
	info->vendor[--vendorLength] = '\0';
  }
  if (descriptor->ProductIdOffset > 0)
  {
   char *product = (char *) propertyBuffer + descriptor->ProductIdOffset;
   snprintf (info->model, sizeof (info->model), "%s", product);
   size_t modelLength = strlen (info->model);
   while (modelLength > 0 && (info->model[modelLength - 1] == ' ' || info->model[modelLength - 1] == '\0'))
	info->model[--modelLength] = '\0';
  }

  switch (descriptor->BusType)
  {
  case BusTypeUsb:
   snprintf (info->busType, sizeof (info->busType), "USB");
   break;
  case BusTypeSata:
   snprintf (info->busType, sizeof (info->busType), "SATA");
   break;
  case BusTypeNvme:
   snprintf (info->busType, sizeof (info->busType), "NVMe");
   break;
  case BusTypeAta:
   snprintf (info->busType, sizeof (info->busType), "ATA");
   break;
  case BusTypeSd:
   snprintf (info->busType, sizeof (info->busType), "SD");
   break;
  case BusTypeScsi:
   snprintf (info->busType, sizeof (info->busType), "SCSI");
   break;
  default:
   snprintf (info->busType, sizeof (info->busType), "Unknown");
   break;
  }
 }

 char systemPath[MAX_PATH];
 if (GetSystemDirectoryA (systemPath, sizeof (systemPath)))
 {
  char systemDrive[4] = {systemPath[0], ':', '\\', '\0'};
  char volumePath[64];
  snprintf (volumePath, sizeof (volumePath), "\\\\.\\%s", systemDrive);
  HANDLE volumeHandle = CreateFileA (volumePath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
  if (volumeHandle != INVALID_HANDLE_VALUE)
  {
   bytesReturned = 0;
   VOLUME_DISK_EXTENTS extents = {0};
   if (DeviceIoControl (volumeHandle, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, NULL, 0, &extents, sizeof (extents), &bytesReturned, NULL))
   {
	if (extents.NumberOfDiskExtents > 0 && (int) extents.Extents[0].DiskNumber == diskNumber)
	{
	 info->isSystem = true;
	 info->isBoot = true;
	}
   }
   CloseHandle (volumeHandle);
  }
 }
 else
 {
  LOG_WARN ("GetSystemDirectoryA failed, falling back to default C:");
  char systemDrive[4] = "C:";
  char volumePath[64];
  snprintf (volumePath, sizeof (volumePath), "\\\\.\\%s", systemDrive);
  HANDLE volumeHandle = CreateFileA (volumePath, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
  if (volumeHandle != INVALID_HANDLE_VALUE)
  {
   bytesReturned = 0;
   VOLUME_DISK_EXTENTS extents = {0};
   if (DeviceIoControl (volumeHandle, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, NULL, 0, &extents, sizeof (extents), &bytesReturned, NULL))
   {
	if (extents.NumberOfDiskExtents > 0 && (int) extents.Extents[0].DiskNumber == diskNumber)
	{
	 info->isSystem = true;
	 info->isBoot = true;
	}
   }
   CloseHandle (volumeHandle);
  }
 }

 CloseHandle (deviceHandle);
 return ERR_OK;
}

error_code_t diskScannerScan (disk_scan_result_t *result)
{
 if (!result)
  return ERR_INVALID_ARG;
 memset (result, 0, sizeof (disk_scan_result_t));
 for (int diskIndex = 0; diskIndex < MAX_DISKS; diskIndex++)
 {
  disk_info_t info;
  if (getDiskInfoWindows (diskIndex, &info) == ERR_OK && info.sizeBytes > 0)
   result->disks[result->count++] = info;
 }
 return (result->count > 0) ? ERR_OK : ERR_OPEN_DEVICE;
}

error_code_t diskScannerGetInfo (const char *devicePath, disk_info_t *info)
{
 if (!devicePath || !info)
  return ERR_INVALID_ARG;
 int diskNumber = -1;
 if (sscanf (devicePath, "\\\\.\\PhysicalDrive%d", &diskNumber) == 1)
  return getDiskInfoWindows (diskNumber, info);
 return ERR_INVALID_ARG;
}

#else

#include <dirent.h>
#include <sys/sysmacros.h>
#include <mntent.h>

static bool readSysfsString (const char *filePath, char *outputBuffer, size_t bufferSize)
{
 FILE *file = fopen (filePath, "r");
 if (!file)
  return false;
 if (fgets (outputBuffer, (int) bufferSize, file) != NULL)
 {
  size_t length = strlen (outputBuffer);
  if (length && outputBuffer[length - 1] == '\n')
   outputBuffer[length - 1] = '\0';
  fclose (file);
  return true;
 }
 fclose (file);
 return false;
}

static uint64_t readSysfsUint64 (const char *filePath)
{
 char valueBuffer[64];
 if (readSysfsString (filePath, valueBuffer, sizeof (valueBuffer)))
  return strtoull (valueBuffer, NULL, 10);
 return 0;
}

static disk_type_t detectDiskTypeLinux (const char *deviceName)
{
 char sysPath[512];
 char tempBuffer[256];

 if (strncmp (deviceName, "nvme", 4) == 0)
  return DISK_TYPE_NVME;
 if (strncmp (deviceName, "mmcblk", 6) == 0)
  return DISK_TYPE_SD_CARD;
 if (strncmp (deviceName, "sr", 2) == 0)
  return DISK_TYPE_OPTICAL;

 snprintf (sysPath, sizeof (sysPath), "/sys/block/%s/removable", deviceName);
 if (readSysfsString (sysPath, tempBuffer, sizeof (tempBuffer)) && tempBuffer[0] == '1')
  return DISK_TYPE_USB;

 snprintf (sysPath, sizeof (sysPath), "/sys/block/%s/device/../../driver", deviceName);
 char resolved[PATH_MAX];
 if (realpath (sysPath, resolved) && strstr (resolved, "usb"))
  return DISK_TYPE_USB;

 snprintf (sysPath, sizeof (sysPath), "/sys/block/%s/queue/rotational", deviceName);
 if (readSysfsString (sysPath, tempBuffer, sizeof (tempBuffer)))
  return (tempBuffer[0] == '0') ? DISK_TYPE_SSD : DISK_TYPE_HDD;

 return DISK_TYPE_UNKNOWN;
}

static bool isRealDisk (const char *name)
{
 if (strncmp (name, "loop", 4) == 0)
  return false;
 if (strncmp (name, "ram", 3) == 0)
  return false;
 if (strncmp (name, "dm-", 3) == 0)
  return false;
 if (strncmp (name, "zram", 4) == 0)
  return false;
 return true;
}

static void checkSystemDisk (disk_info_t *info)
{
 FILE *mountsFile = setmntent ("/proc/mounts", "r");
 if (!mountsFile)
  return;
 struct mntent *mountEntry;
 char deviceBase[64];
 snprintf (deviceBase, sizeof (deviceBase), "%s", info->devicePath);
 while ((mountEntry = getmntent (mountsFile)) != NULL)
 {
  if ((strcmp (mountEntry->mnt_dir, "/") == 0 || strcmp (mountEntry->mnt_dir, "/boot") == 0) && strstr (mountEntry->mnt_fsname, deviceBase) != NULL)
  {
   info->isSystem = true;
   info->isBoot = true;
   break;
  }
 }
 endmntent (mountsFile);
}

error_code_t diskScannerScan (disk_scan_result_t *result)
{
 if (!result)
  return ERR_INVALID_ARG;
 memset (result, 0, sizeof (disk_scan_result_t));
 DIR *sysBlockDir = opendir ("/sys/block");
 if (!sysBlockDir)
  return ERR_OPEN_DEVICE;

 struct dirent *entry;
 while ((entry = readdir (sysBlockDir)) != NULL && result->count < MAX_DISKS)
 {
  if (entry->d_name[0] == '.')
   continue;
  if (!isRealDisk (entry->d_name))
   continue;

  disk_info_t *info = &result->disks[result->count];
  memset (info, 0, sizeof (disk_info_t));
  snprintf (info->devicePath, sizeof (info->devicePath), "/dev/%s", entry->d_name);

  char sysPath[512];
  snprintf (sysPath, sizeof (sysPath), "/sys/block/%s/size", entry->d_name);
  uint64_t sectorCount = readSysfsUint64 (sysPath);
  info->sizeBytes = sectorCount * 512;
  if (info->sizeBytes == 0)
   continue;

  snprintf (sysPath, sizeof (sysPath), "/sys/block/%s/queue/hw_sector_size", entry->d_name);
  info->sectorSize = (uint32_t) readSysfsUint64 (sysPath);
  if (info->sectorSize == 0)
   info->sectorSize = 512;

  info->type = detectDiskTypeLinux (entry->d_name);

  snprintf (sysPath, sizeof (sysPath), "/sys/block/%s/removable", entry->d_name);
  char removableBuffer[8];
  if (readSysfsString (sysPath, removableBuffer, sizeof (removableBuffer)))
   info->isRemovable = (removableBuffer[0] == '1');

  snprintf (sysPath, sizeof (sysPath), "/sys/block/%s/device/vendor", entry->d_name);
  readSysfsString (sysPath, info->vendor, sizeof (info->vendor));
  snprintf (sysPath, sizeof (sysPath), "/sys/block/%s/device/model", entry->d_name);
  readSysfsString (sysPath, info->model, sizeof (info->model));

  if (info->type == DISK_TYPE_USB)
   snprintf (info->busType, sizeof (info->busType), "USB");
  else if (info->type == DISK_TYPE_NVME)
   snprintf (info->busType, sizeof (info->busType), "NVMe");
  else if (info->type == DISK_TYPE_SD_CARD)
   snprintf (info->busType, sizeof (info->busType), "SD/MMC");
  else
   snprintf (info->busType, sizeof (info->busType), "SATA/ATA");

  info->diskNumber = result->count;
  checkSystemDisk (info);
  result->count++;
 }
 closedir (sysBlockDir);
 return (result->count > 0) ? ERR_OK : ERR_OPEN_DEVICE;
}

error_code_t diskScannerGetInfo (const char *devicePath, disk_info_t *info)
{
 if (!devicePath || !info)
  return ERR_INVALID_ARG;
 disk_scan_result_t scanResult;
 error_code_t scanError = diskScannerScan (&scanResult);
 if (scanError != ERR_OK)
  return scanError;
 for (int index = 0; index < scanResult.count; index++)
 {
  if (strcmp (scanResult.disks[index].devicePath, devicePath) == 0)
  {
   *info = scanResult.disks[index];
   return ERR_OK;
  }
 }
 return ERR_OPEN_DEVICE;
}

#endif

void diskScannerPrintList (const disk_scan_result_t *result, bool showAll)
{
 if (!result || result->count == 0)
 {
  printf ("No disks found.\n");
  return;
 }
 printf ("\n--- Available disks ---\n");
 printf ("#  Device                  Size       Type     Bus       Status\n");
 int displayedCount = 0;
 for (int index = 0; index < result->count; index++)
 {
  const disk_info_t *disk = &result->disks[index];
  if (!showAll && disk->isSystem)
   continue;
  displayedCount++;
  char sizeString[16];
  formatBytes (disk->sizeBytes, sizeString, sizeof (sizeString));
  const char *statusText;
  if (disk->isSystem || disk->isBoot)
   statusText = "SYSTEM";
  else if (disk->isRemovable)
   statusText = "Removable";
  else
   statusText = "Fixed";

  char deviceDisplay[25];
  if (strlen (disk->devicePath) > 24)
   snprintf (deviceDisplay, sizeof (deviceDisplay), "..%s", disk->devicePath + strlen (disk->devicePath) - 22);
  else
   snprintf (deviceDisplay, sizeof (deviceDisplay), "%s", disk->devicePath);

  printf ("%2d  %-24s %10s %-8s %-8s %s\n", index + 1, deviceDisplay, sizeString, diskTypeToString (disk->type), disk->busType, statusText);
 }
 printf ("\nTotal: %d disk(s) found", result->count);
 if (!showAll && displayedCount < result->count)
  printf (" (%d hidden system disk(s), use -A to show all)", result->count - displayedCount);
 printf ("\n\n");
}

void diskScannerPrintDetail (const disk_info_t *info)
{
 if (!info)
  return;
 char sizeString[32];
 formatBytes (info->sizeBytes, sizeString, sizeof (sizeString));
 printf ("\n--- Disk Information ---\n");
 printf ("Device Path:   %s\n", info->devicePath);
 printf ("Vendor:        %s\n", strlen (info->vendor) ? info->vendor : "(unknown)");
 printf ("Model:         %s\n", strlen (info->model) ? info->model : "(unknown)");
 printf ("Size:          %s\n", sizeString);
 printf ("Sector Size:   %u\n", info->sectorSize);
 printf ("Type:          %s\n", diskTypeToString (info->type));
 printf ("Bus Type:      %s\n", info->busType);
 printf ("Removable:     %s\n", info->isRemovable ? "Yes" : "No");
 printf ("System Disk:   %s\n", info->isSystem ? "YES - DO NOT WIPE!" : "No");
 printf ("Boot Disk:     %s\n", info->isBoot ? "YES - DO NOT WIPE!" : "No");
 printf ("\n");
 if (info->isSystem || info->isBoot)
  printf ("*** WARNING: This is a SYSTEM/BOOT disk! Wiping will make system unbootable! ***\n\n");
}