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

 if (configuration.emergencyMode)
 {
  int exitCode = emergencyWipe (&configuration);
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
     if (strlen(gLogFilePath) == 0)
     {
         const char* temporaryDirectory = NULL;
#ifdef _WIN32
         char tempPathBuffer[MAX_PATH_LEN];
         temporaryDirectory = getenv("TEMP");
         if (!temporaryDirectory)
         {
             DWORD length = GetTempPathA(sizeof(tempPathBuffer), tempPathBuffer);
             if (length > 0 && length < sizeof(tempPathBuffer))
                 temporaryDirectory = tempPathBuffer;
             else
             {
                 char systemDrive[4] = "C:";
                 GetEnvironmentVariableA("SystemDrive", systemDrive, sizeof(systemDrive));
                 snprintf(tempPathBuffer, sizeof(tempPathBuffer), "%s\\Windows\\Temp", systemDrive);
                 temporaryDirectory = tempPathBuffer;
             }
         }
#else
         temporaryDirectory = "/tmp";
#endif
         time_t currentTime = time(NULL);
         struct tm* timeInfo = localtime(&currentTime);
         char timestamp[32];
         strftime(timestamp, sizeof(timestamp), "%Y%m%d_%H%M%S", timeInfo);
         snprintf(gLogFilePath, sizeof(gLogFilePath), "%s/securewipe_%s.log", temporaryDirectory, timestamp);
     }
     logInit(gLogFilePath, configuration.verbose);
     LOG_INFO("Secure Wipe started on %s", configuration.devicePath);
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