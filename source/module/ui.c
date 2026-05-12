#include "ui.h"
#include "common.h"
#include "disk_scanner.h"
#include "wiper.h"
#include <stdio.h>

bool confirmWipe (const char *devicePath, uint64_t sizeBytes, wipe_method_t method, uint32_t actualPasses)
{
 char sizeString[32];
 formatBytes (sizeBytes, sizeString, sizeof (sizeString));
 printf ("\n--- WARNING ---\n");
 printf ("Device: %s\nSize:   %s\nMethod: %s\nPasses: %u\n", devicePath, sizeString, wiperMethodName (method), actualPasses);
 printf ("This operation CANNOT be undone!\n");
 printf ("Type 'YES' (all caps) to confirm: ");
 fflush (stdout);
 char response[16] = {0};
 if (!fgets (response, sizeof (response), stdin))
  return false;
 size_t responseLength = strlen (response);
 if (responseLength && response[responseLength - 1] == '\n')
  response[responseLength - 1] = '\0';
 return strcmp (response, "YES") == 0;
}

void progressHandler (uint64_t current, uint64_t total, int pass, const char *phase)
{
 (void) pass;
 static int lastPercent = -1;
 int currentPercent = (int) ((current * 100) / total);
 if (currentPercent != lastPercent)
 {
  int barWidth = 40;
  int filledWidth = (currentPercent * barWidth) / 100;
  printf ("\r[");
  for (int barIndex = 0; barIndex < barWidth; barIndex++)
  {
   if (barIndex < filledWidth)
    printf ("#");
   else
    printf (" ");
  }
  printf ("] %3d%% - %s", currentPercent, phase);
  fflush (stdout);
  lastPercent = currentPercent;
  if (currentPercent == 100)
  {
   printf ("\n");
   lastPercent = -1;
  }
 }
}

bool interactiveSelectDisk (program_config_t *config)
{
 disk_scan_result_t *scanResult = (disk_scan_result_t *) malloc (sizeof (disk_scan_result_t));
 if (!scanResult)
 {
  fprintf (stderr, "Memory allocation failed\n");
  return false;
 }
 printf ("Scanning for available disks...\n\n");
 if (diskScannerScan (scanResult) != ERR_OK)
 {
  fprintf (stderr, "Failed to scan disks\n");
  free (scanResult);
  return false;
 }
 if (scanResult->count == 0)
 {
  fprintf (stderr, "No disks found\n");
  free (scanResult);
  return false;
 }
 diskScannerPrintList (scanResult, config->showAllDisks);
 int safeCount = 0;
 for (int diskIndex = 0; diskIndex < scanResult->count; diskIndex++)
 {
  if (diskScannerIsSafeToWipe (&scanResult->disks[diskIndex]))
   safeCount++;
 }
 if (safeCount == 0)
 {
  fprintf (stderr, "No safe disks available.\n");
  free (scanResult);
  return false;
 }
 printf ("Enter disk number (1-%d) or 'q': ", scanResult->count);
 fflush (stdout);
 char inputBuffer[16];
 if (!fgets (inputBuffer, sizeof (inputBuffer), stdin))
 {
  free (scanResult);
  return false;
 }
 if (inputBuffer[0] == 'q' || inputBuffer[0] == 'Q')
 {
  free (scanResult);
  return false;
 }
 int selection = atoi (inputBuffer);
 const disk_info_t *selectedDisk = diskScannerGetByIndex (scanResult, selection);
 if (!selectedDisk)
 {
  fprintf (stderr, "Invalid selection\n");
  free (scanResult);
  return false;
 }
 if (!diskScannerIsSafeToWipe (selectedDisk))
 {
  fprintf (stderr, "System disk selected - not allowed.\n");
  free (scanResult);
  return false;
 }
 diskScannerPrintDetail (selectedDisk);
 printf ("Confirm this disk? (y/n): ");
 fflush (stdout);
 if (!fgets (inputBuffer, sizeof (inputBuffer), stdin))
 {
  free (scanResult);
  return false;
 }
 if (inputBuffer[0] != 'y' && inputBuffer[0] != 'Y')
 {
  free (scanResult);
  return false;
 }
 printf ("\nSelect wipe method:\n");
 printf ("  1. Zero Fill (1 pass)\n");
 printf ("  2. DoD 5220.22-M (3 passes) [default]\n");
 printf ("  3. DoD 5220.22-M ECE (7 passes)\n");
 printf ("  4. Bruce Schneier Algorithm (7 passes)\n");
 printf ("  5. Gutmann Method (35 passes)\n");
 printf ("  6. AFSSI-5020 (3 passes)\n");
 printf ("  7. NIST SP-800-88 Clear (1 pass)\n");
 printf ("  8. NIST SP-800-88 Purge (3 passes)\n");
 printf ("  9. Random (custom passes)\n");
 printf ("Enter choice (1-9) [2]: ");
 fflush (stdout);
 if (!fgets (inputBuffer, sizeof (inputBuffer), stdin))
  inputBuffer[0] = '2';
 int methodChoice = atoi (inputBuffer);
 if (methodChoice == 0)
  methodChoice = 2;
 switch (methodChoice)
 {
 case 1:
  config->method = WIPE_METHOD_ZERO;
  config->passes = 1;
  break;
 case 2:
  config->method = WIPE_METHOD_DOD_SHORT;
  config->passes = 3;
  break;
 case 3:
  config->method = WIPE_METHOD_DOD_FULL;
  config->passes = 7;
  break;
 case 4:
  config->method = WIPE_METHOD_SCHNEIER;
  config->passes = 7;
  break;
 case 5:
  config->method = WIPE_METHOD_GUTMANN;
  config->passes = 35;
  break;
 case 6:
  config->method = WIPE_METHOD_AFSSI_5020;
  config->passes = 3;
  break;
 case 7:
  config->method = WIPE_METHOD_NIST_CLEAR;
  config->passes = 1;
  break;
 case 8:
  config->method = WIPE_METHOD_NIST_PURGE;
  config->passes = 3;
  break;
 case 9:
  config->method = WIPE_METHOD_RANDOM;
  printf ("Number of random passes (1-100) [3]: ");
  fflush (stdout);
  if (fgets (inputBuffer, sizeof (inputBuffer), stdin))
  {
   int passesValue = atoi (inputBuffer);
   config->passes = (passesValue > 0 && passesValue <= 100) ? (uint32_t) passesValue : 3;
  }
  else
  {
   config->passes = 3;
  }
  break;
 default:
  config->method = WIPE_METHOD_DOD_SHORT;
  config->passes = 3;
  break;
 }
 snprintf (config->devicePath, sizeof (config->devicePath), "%s", selectedDisk->devicePath);
 free (scanResult);
 return true;
}