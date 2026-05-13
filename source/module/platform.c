#include "platform.h"
#include <locale.h>

void platformInit (void)
{
#ifdef _WIN32
 SetConsoleOutputCP (CP_UTF8);
 SetConsoleCP (CP_UTF8);
#else
 setlocale (LC_ALL, "C.UTF-8");
#endif
}

void platformCleanup (void)
{
}

bool checkAdminPrivileges (void)
{
#ifdef _WIN32
 BOOL isAdmin = FALSE;
 PSID adminGroup = NULL;
 SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;
 if (AllocateAndInitializeSid (&ntAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID, DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &adminGroup))
 {
  CheckTokenMembership (NULL, adminGroup, &isAdmin);
  FreeSid (adminGroup);
 }
 return isAdmin == TRUE;
#else
 return (geteuid () == 0);
#endif
}

void *alignedAlloc (size_t size)
{
#ifdef _WIN32
 return _aligned_malloc (size, SECTOR_SIZE);
#else
 return aligned_alloc (SECTOR_SIZE, size);
#endif
}

void alignedFree (void *pointer)
{
#ifdef _WIN32
 _aligned_free (pointer);
#else
 free (pointer);
#endif
}

#ifdef _WIN32
static bool isPhysicalDriveMounted (int diskNumber)
{
 HANDLE volumeHandle = FindFirstVolumeW (NULL, 0);
 if (volumeHandle == INVALID_HANDLE_VALUE)
  return false;
 wchar_t volumeName[MAX_PATH];
 DWORD bytesReturned = 0;
 bool found = false;
 while (FindNextVolumeW (volumeHandle, volumeName, MAX_PATH))
 {
  wchar_t volumePath[MAX_PATH];
  if (GetVolumePathNamesForVolumeNameW (volumeName, volumePath, MAX_PATH, &bytesReturned))
  {
   HANDLE currentVolume = CreateFileW (volumeName, 0, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL, OPEN_EXISTING, 0, NULL);
   if (currentVolume != INVALID_HANDLE_VALUE)
   {
	VOLUME_DISK_EXTENTS extents;
	bytesReturned = 0;
	if (DeviceIoControl (currentVolume, IOCTL_VOLUME_GET_VOLUME_DISK_EXTENTS, NULL, 0, &extents, sizeof (extents), &bytesReturned, NULL))
	{
	 for (DWORD extentIndex = 0; extentIndex < extents.NumberOfDiskExtents; extentIndex++)
	 {
	  if ((int) extents.Extents[extentIndex].DiskNumber == diskNumber)
	  {
	   found = true;
	   break;
	  }
	 }
	}
	CloseHandle (currentVolume);
	if (found)
	 break;
   }
  }
 }
 FindVolumeClose (volumeHandle);
 return found;
}
#endif

bool isDeviceMounted (const char *devicePath)
{
#ifdef _WIN32
 int diskNumber = -1;
 if (sscanf (devicePath, "\\\\.\\PhysicalDrive%d", &diskNumber) == 1)
  return isPhysicalDriveMounted (diskNumber);
 return false;
#else
 FILE *mountsFile = fopen ("/proc/mounts", "r");
 if (!mountsFile)
  return false;
 char line[512];
 bool found = false;
 while (fgets (line, sizeof (line), mountsFile))
 {
  char mountDevice[256], mountPoint[256];
  if (sscanf (line, "%255s %255s", mountDevice, mountPoint) == 2)
  {
   if (strcmp (mountDevice, devicePath) == 0)
   {
	LOG_WARN ("Device %s is mounted at %s", devicePath, mountPoint);
	found = true;
	break;
   }
  }
 }
 fclose (mountsFile);
 return found;
#endif
}