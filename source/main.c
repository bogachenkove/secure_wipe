#include "module/metadata.h"
#include "module/platform.h"
#include "module/common.h"
#include "module/config.h"
#include "module/ui.h"
#include "module/executor.h"
#include "module/random_gen.h"
#include "module/wiper.h"
#include "module/disk_scanner.h"
#include "module/device_io.h"
#include "module/ata_erase.h"
#include "module/file_wiper.h"

int main (int argumentCount, char *argumentVector[])
{
 platformInit ();

 if (!checkAdminPrivileges ())
 {
#ifdef _WIN32
  fprintf (stderr, "ERROR: This program requires Administrator privileges.\n");
  fprintf (stderr, "Please run as Administrator.\n");
#else
  fprintf (stderr, "ERROR: This program requires root privileges.\n");
  fprintf (stderr, "Please run with sudo.\n");
#endif
  platformCleanup ();
  return 1;
 }

 program_config_t configuration;
 configDefault (&configuration);

 if (!parseArguments (argumentCount, argumentVector, &configuration))
 {
  platformCleanup ();
  return 1;
 }

 if (configuration.wipeFileMode || configuration.wipeDirMode)
 {
  file_wipe_config_t fileConfig = {0};
  fileConfig.method = configuration.method;
  fileConfig.passes = configuration.passes;
  fileConfig.renameBeforeDelete = (gDefaultRenameCount > 0);
  fileConfig.renameCount = gDefaultRenameCount;
  fileConfig.preserveTimestamps = false;
  fileConfig.progress = progressHandler;

  error_code_t err = fileWipePath (configuration.wipeFilePath, &fileConfig);
  if (err != ERR_OK)
   fprintf (stderr, "Failed to wipe: %s\n", errorToString (err));
  else
   printf ("Wipe completed successfully.\n");

  platformCleanup ();
  return (err == ERR_OK) ? 0 : 1;
 }

 if (configuration.emergencyMode)
 {
  int exitCode = emergencyWipe (&configuration);
  platformCleanup ();
  return exitCode;
 }
 if (configuration.ataSecureErase)
 {
  device_t device;
  error_code_t openStatus = deviceOpen (&device, configuration.devicePath, false);
  if (openStatus != ERR_OK)
  {
   fprintf (stderr, "Failed to open device %s: %s\n", configuration.devicePath, errorToString (openStatus));
   platformCleanup ();
   return 1;
  }
  ata_security_info_t info;
  if (ataGetSecurityInfo (&device, &info) != ERR_OK)
  {
   fprintf (stderr, "ERROR: Cannot query ATA security features.\n");
   fprintf (stderr, "This may be because:\n");
   fprintf (stderr, "  - Device is connected via USB (many USB bridges block ATA commands)\n");
   fprintf (stderr, "  - Device is virtual or does not support ATA Secure Erase\n");
   fprintf (stderr, "  - Driver/antivirus is blocking low-level access\n");
   fprintf (stderr, "Try using standard wipe methods instead.\n");
   deviceClose (&device);
   platformCleanup ();
   return 1;
  }
  if (!info.supported)
  {
   fprintf (stderr, "ERROR: ATA Security not supported by this device.\n");
   deviceClose (&device);
   platformCleanup ();
   return 1;
  }
  ata_erase_type_t eraseType = configuration.ataEnhancedErase ? ATA_ERASE_ENHANCED : ATA_ERASE_NORMAL;
  int exitCode = ataSecureErase (&device, eraseType, progressHandler);
  deviceClose (&device);
  platformCleanup ();
  return exitCode;
 }

 if (configuration.listDisks)
 {
  disk_scan_result_t *scanResult = (disk_scan_result_t *) malloc (sizeof (disk_scan_result_t));
  if (!scanResult)
  {
   fprintf (stderr, "Memory allocation failed\n");
   platformCleanup ();
   return 1;
  }
  if (diskScannerScan (scanResult) == ERR_OK)
   diskScannerPrintList (scanResult, configuration.showAllDisks);
  else
   fprintf (stderr, "Failed to scan disks\n");
  free (scanResult);
  platformCleanup ();
  return 0;
 }

 if (configuration.selectDisk || strlen (configuration.devicePath) == 0)
 {
  if (!interactiveSelectDisk (&configuration))
  {
   platformCleanup ();
   return 1;
  }
 }

 if (!gNoLog)
 {
  if (strlen (gLogFilePath) == 0)
  {
   const char *temporaryDirectory = NULL;
#ifdef _WIN32
   char tempPathBuffer[MAX_PATH_LEN];
   temporaryDirectory = getenv ("TEMP");
   if (!temporaryDirectory)
   {
	DWORD length = GetTempPathA (sizeof (tempPathBuffer), tempPathBuffer);
	if (length > 0 && length < sizeof (tempPathBuffer))
	 temporaryDirectory = tempPathBuffer;
	else
	{
	 char systemDrive[4] = "C:";
	 GetEnvironmentVariableA ("SystemDrive", systemDrive, sizeof (systemDrive));
	 snprintf (tempPathBuffer, sizeof (tempPathBuffer), "%s\\Windows\\Temp", systemDrive);
	 temporaryDirectory = tempPathBuffer;
	}
   }
#else
   temporaryDirectory = "/tmp";
#endif
   time_t currentTime = time (NULL);
   struct tm *timeInfo = localtime (&currentTime);
   if (!timeInfo)
   {
	fprintf (stderr, "localtime failed\n");
	platformCleanup ();
	return 1;
   }
   char timestamp[32];
   strftime (timestamp, sizeof (timestamp), "%Y%m%d_%H%M%S", timeInfo);
   snprintf (gLogFilePath, sizeof (gLogFilePath), "%s/securewipe_%s.log", temporaryDirectory, timestamp);
  }
  logInit (gLogFilePath, configuration.verbose);
  LOG_INFO ("Secure Wipe started on %s", configuration.devicePath);
 }
 else
 {
  logInit (NULL, configuration.verbose);
  if (configuration.verbose)
   printf ("Logging to file disabled (--no-log).\n");
 }

 if (randomInit () != ERR_OK)
 {
  LOG_ERROR ("Random init failed");
  logClose ();
  platformCleanup ();
  return 1;
 }

 if (wiperInit () != ERR_OK)
 {
  LOG_ERROR ("Wiper init failed");
  randomCleanup ();
  logClose ();
  platformCleanup ();
  return 1;
 }

 analysis_result_t analysisResult;
 int finalExitCode = runWipe (&configuration, &analysisResult);

 wiperCleanup ();
 randomCleanup ();
 LOG_INFO ("Secure Wipe finished with code %d", finalExitCode);
 logClose ();
 platformCleanup ();
 return finalExitCode;
}