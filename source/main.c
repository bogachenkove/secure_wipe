#include "module/metadata.h"
#include "module/platform.h"
#include "module/common.h"
#include "module/config.h"
#include "module/ui.h"
#include "module/executor.h"
#include "module/random_gen.h"
#include "module/wiper.h"
#include "module/disk_scanner.h"

int main (int argc, char *argv[])
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

 program_config_t config;
 configDefault (&config);

 if (!parseArguments (argc, argv, &config))
 {
  printUsage (argv[0]);
  platformCleanup ();
  return 1;
 }

 if (config.listDisks)
 {
  disk_scan_result_t *scanResult = (disk_scan_result_t *) malloc (sizeof (disk_scan_result_t));
  if (!scanResult)
  {
   fprintf (stderr, "Memory allocation failed\n");
   platformCleanup ();
   return 1;
  }
  if (diskScannerScan (scanResult) == ERR_OK)
   diskScannerPrintList (scanResult, config.showAllDisks);
  else
   fprintf (stderr, "Failed to scan disks\n");
  free (scanResult);
  platformCleanup ();
  return 0;
 }

 if (config.selectDisk || strlen (config.devicePath) == 0)
 {
  if (!interactiveSelectDisk (&config))
  {
   platformCleanup ();
   return 1;
  }
 }

 logInit (config.logPath, config.verbose);
 LOG_INFO ("Secure Wipe started on %s", config.devicePath);

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

 analysis_result_t analysis;
 int exitCode = runWipe (&config, &analysis);

 wiperCleanup ();
 randomCleanup ();
 LOG_INFO ("Secure Wipe finished with code %d", exitCode);
 logClose ();
 platformCleanup ();
 return exitCode;
}