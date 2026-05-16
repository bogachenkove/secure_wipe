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

bool promptAtaErase (const ata_security_info_t *info, const char *devicePath)
{
 printf ("\n=== ATA SECURITY FEATURES DETECTED ===\n");
 printf ("Device: %s\n", devicePath);
 printf ("Model:  %s\n", info->model);
 printf ("Serial: %s\n", info->serial);
 printf ("Firmware: %s\n", info->firmware);
 printf ("\nThis device supports ATA Secure Erase");
 if (info->enhancedSupported)
  printf (" and Enhanced Secure Erase");
 printf (".\n");
 printf ("ATA Secure Erase is a built-in hardware command that\n");
 printf ("can completely erase the drive in seconds/minutes,\n");
 printf ("often faster and more thoroughly than software overwrite.\n");
 printf ("\nDo you want to use ATA Secure Erase instead of the selected software method?\n");

 if (info->enhancedSupported)
 {
  printf ("Choose option:\n");
  printf ("  1) ATA Enhanced Secure Erase (most secure, if supported)\n");
  printf ("  2) ATA Normal Secure Erase\n");
  printf ("  3) Skip ATA Erase, use software method\n");
  printf ("Enter choice (1-3) [3]: ");
  fflush (stdout);
  char input[16];
  if (!fgets (input, sizeof (input), stdin))
   return false;
  int choice = atoi (input);
  if (choice == 1)
  {
   printf ("\n--- ATA ENHANCED SECURE ERASE ---\n");
   printf ("This will completely erase all data on %s.\n", devicePath);
   printf ("Operation cannot be stopped once started.\n");
   printf ("Type 'YES' (all caps) to confirm: ");
   fflush (stdout);
   char confirm[16];
   if (!fgets (confirm, sizeof (confirm), stdin))
	return false;
   if (strcmp (confirm, "YES\n") != 0)
   {
	printf ("Operation cancelled.\n");
	return false;
   }
   return true;
  }
  else if (choice == 2)
  {
   printf ("\n--- ATA NORMAL SECURE ERASE ---\n");
   printf ("This will completely erase all data on %s.\n", devicePath);
   printf ("Operation cannot be stopped once started.\n");
   printf ("Type 'YES' (all caps) to confirm: ");
   fflush (stdout);
   char confirm[16];
   if (!fgets (confirm, sizeof (confirm), stdin))
	return false;
   if (strcmp (confirm, "YES\n") != 0)
   {
	printf ("Operation cancelled.\n");
	return false;
   }
   return true;
  }
  else
  {
   printf ("Skipping ATA Erase.\n");
   return false;
  }
 }
 else
 {
  printf ("Do you want to perform ATA Secure Erase? (y/n): ");
  fflush (stdout);
  char input[16];
  if (!fgets (input, sizeof (input), stdin))
   return false;
  if (input[0] == 'y' || input[0] == 'Y')
  {
   printf ("\n--- ATA SECURE ERASE ---\n");
   printf ("This will completely erase all data on %s.\n", devicePath);
   printf ("Operation cannot be stopped once started.\n");
   printf ("Type 'YES' (all caps) to confirm: ");
   fflush (stdout);
   char confirm[16];
   if (!fgets (confirm, sizeof (confirm), stdin))
	return false;
   if (strcmp (confirm, "YES\n") != 0)
   {
	printf ("Operation cancelled.\n");
	return false;
   }
   return true;
  }
  else
  {
   printf ("Skipping ATA Erase.\n");
   return false;
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
 for (int index = 0; index < scanResult->count; index++)
 {
  if (diskScannerIsSafeToWipe (&scanResult->disks[index]))
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
 printf ("  2. Random (custom passes)\n");
 printf ("Enter choice (1-2) [2]: ");
 fflush (stdout);
 if (!fgets (inputBuffer, sizeof (inputBuffer), stdin))
  inputBuffer[0] = '2';
 int methodChoice = atoi (inputBuffer);
 if (methodChoice == 1)
 {
  config->method = WIPE_METHOD_ZERO;
  config->passes = 1;
 }
 else
 {
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
 }

 printf ("\nNumber of full wipe cycles (1-100) [1]: ");
 fflush (stdout);
 char cycleBuffer[16];
 if (fgets (cycleBuffer, sizeof (cycleBuffer), stdin))
 {
  int cyclesValue = atoi (cycleBuffer);
  if (cyclesValue >= 1 && cyclesValue <= 100)
   config->cycles = (uint32_t) cyclesValue;
  else
   config->cycles = 1;
 }
 else
 {
  config->cycles = 1;
 }

 snprintf (config->devicePath, sizeof (config->devicePath), "%s", selectedDisk->devicePath);
 free (scanResult);
 return true;
}