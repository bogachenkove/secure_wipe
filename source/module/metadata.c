#include "metadata.h"
#include "platform.h"
#include "common.h"
#include "wiper.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#define SECURE_WIPE_NAME "Secure Wipe"
#define SECURE_WIPE_VERSION "2.0.4.0"
#define SECURE_WIPE_DESCRIPTION "Data Destruction Tool"
#define SECURE_WIPE_AUTHOR "Bogachenko Vyacheslav"
#define SECURE_WIPE_CONTACT "bogachenkove@outlook.com"
#define SECURE_WIPE_HOMEPAGE "https:"
#define SECURE_WIPE_LICENSE "MIT License"
#define SECURE_WIPE_LICENSE_FILE "docs/LICENSE.txt"

static size_t parseSizeWithUnit (const char *argument)
{
 char *endPointer;
 unsigned long long value = strtoull (argument, &endPointer, 10);
 if (endPointer == argument)
  return 0;
 while (*endPointer == ' ')
  endPointer++;
 size_t multiplier = 1;
 if (strcasecmp (endPointer, "B") == 0 || *endPointer == '\0')
  multiplier = 1;
 else if (strcasecmp (endPointer, "KB") == 0)
  multiplier = 1024;
 else if (strcasecmp (endPointer, "MB") == 0)
  multiplier = 1024 * 1024;
 else
  return 0;
 if (value > SIZE_MAX / multiplier)
  return 0;
 size_t result = (size_t) (value * multiplier);
 if (result > MAX_BUFFER_SIZE)
  return 0;
 return result;
}

void printMethods (void)
{
 printf ("Available wipe methods:\n\n");
 printf ("  Zero        : 1 pass of zeros (0x00)\n");
 printf ("  Random      : N passes of cryptographically secure random data\n");
 printf ("  DoD short   : DoD 5220.22-M (3 passes: 0x00, 0xFF, random)\n");
 printf ("  DoD full    : DoD 5220.22-M ECE (7 passes extended)\n");
 printf ("  Schneier    : Bruce Schneier Algorithm (7 passes: 0xFF, 0x00, 5x random)\n");
 printf ("  Gutmann     : Peter Gutmann Method (35 passes, MFM/RLL patterns + random)\n");
 printf ("  AFSSI-5020  : U.S. Air Force AFSSI-5020 (3 passes: 0x00, 0xFF, random)\n");
 printf ("  NIST Clear  : NIST SP-800-88 Rev. 1 Clear (1 pass of zeros)\n");
 printf ("  NIST Purge  : NIST SP-800-88 Rev. 1 Purge (3 passes: random, 0x00, random)\n");
 printf ("\nNotes:\n");
 printf ("  - NIST Clear is suitable for media reuse within organization\n");
 printf ("  - NIST Purge provides stronger sanitization for external release\n");
 printf ("  - AFSSI-5020 is equivalent to DoD short method\n");
 printf ("  - Gutmann method is designed for older MFM/RLL drives\n");
}

void printUsage (const char *programName)
{
 printf ("Usage: %s [options] <device>\n", programName);
 printf ("       %s --list [-A]\n", programName);
 printf ("       %s --select\n\n", programName);
 printf ("Information flags:\n");
 printf ("  --version             Show version information\n");
 printf ("  --about               Show information about the program\n");
 printf ("  --license             Show license information\n");
 printf ("  --support             Show support information\n");
 printf ("  --help                Show this help message\n");
 printf ("\nDisk Discovery:\n");
 printf ("  -L, --list            List disks\n");
 printf ("  -S, --select          Interactive disk selection\n");
 printf ("  -A                    Show all disks (including system)\n");
 printf ("\nWipe Methods:\n");
 printf ("  --zero                Zero fill (1 pass)\n");
 printf ("  --random N            Random data (N passes)\n");
 printf ("  --dod-short           DoD 5220.22-M short (3 passes) [default]\n");
 printf ("  --dod-full            DoD 5220.22-M ECE (7 passes)\n");
 printf ("  --schneier            Bruce Schneier Algorithm (7 passes)\n");
 printf ("  --gutmann             Gutmann method (35 passes)\n");
 printf ("  --afssi               AFSSI-5020 (3 passes)\n");
 printf ("  --nist-clear          NIST SP-800-88 Clear (1 pass)\n");
 printf ("  --nist-purge          NIST SP-800-88 Purge (3 passes)\n");
 printf ("\nAnalysis Options:\n");
 printf ("  --analyze             Read-only analysis (no wiping)\n");
 printf ("  --skip-analysis       Skip analysis and proceed directly to wipe\n");
 printf ("  --analyze-write       Analysis with write test (destroys data!)\n");
 printf ("  --wp-check            Quick write-protection check\n");
 printf ("  --destroy-partition-table   Destroy MBR/GPT only (zero out, no data wipe)\n");
 printf ("\nLogging Options:\n");
 printf ("  --log FILE            Write log to specified file\n");
 printf ("  --no-log              Disable log file creation\n");
 printf ("\nOther Options:\n");
 printf ("  -b, --buffer SIZE     I/O buffer size (e.g. 1MB, 512KB)\n");
 printf ("  -v, --verify          Verify after wipe (default)\n");
 printf ("  -n, --no-verify       Skip verification\n");
 printf ("  -y, --yes             Auto-confirm (dangerous)\n");
 printf ("  -q, --quiet           Quiet mode\n");
 printf ("  --methods             List wipe methods with descriptions\n");
 printf ("\nEmergency Mode (write zeros to first N sectors or N blocks):\n");
 printf ("  --emergency           Enable emergency zero-write mode (requires --buffer and either --sector or --block)\n");
 printf ("  --sector N            Number of sectors to overwrite (must be >0)\n");
 printf ("  --block N             Number of blocks to overwrite (block size = --buffer)\n");
}

void printVersion (void)
{
 printf (SECURE_WIPE_NAME " " SECURE_WIPE_VERSION "\n");
}

void printAbout (void)
{
 printf (SECURE_WIPE_NAME " " SECURE_WIPE_VERSION " - " SECURE_WIPE_DESCRIPTION "\n");
 printf ("Copyright (c) 2026 " SECURE_WIPE_AUTHOR "\n\n");
 printf ("Author:   " SECURE_WIPE_AUTHOR "\n");
 printf ("Contact:  " SECURE_WIPE_CONTACT "\n");
 printf ("Homepage: " SECURE_WIPE_HOMEPAGE "\n");
}

void printLicense (void)
{
 printf (SECURE_WIPE_NAME " " SECURE_WIPE_VERSION " - " SECURE_WIPE_DESCRIPTION "\n");
 printf ("Copyright (c) 2026 " SECURE_WIPE_AUTHOR "\n\n");
 printf ("This software is released under the " SECURE_WIPE_LICENSE ".\n");
 printf ("You are free to use, modify, and distribute it in accordance with the license terms.\n\n");
 FILE *licenseFile = fopen (SECURE_WIPE_LICENSE_FILE, "r");
 if (licenseFile)
 {
  char lineBuffer[1024];
  while (fgets (lineBuffer, sizeof (lineBuffer), licenseFile))
   printf ("%s", lineBuffer);
  fclose (licenseFile);
 }
 else
 {
  printf ("License file not found locally.\n");
  printf ("Please read the license agreement online:\n");
  printf ("https:");
 }
}

void printSupport (void)
{
 printf (SECURE_WIPE_NAME " " SECURE_WIPE_VERSION " - " SECURE_WIPE_DESCRIPTION "\n");
 printf ("Copyright (c) 2026 " SECURE_WIPE_AUTHOR "\n\n");
 printf ("Donating is an act of generosity.\n");
 printf ("Your support, however modest it might be, is necessary and you can provide it, ");
 printf ("because you love the " SECURE_WIPE_NAME " project and enjoy it.\n");
 printf ("Your donations help to continue to support and improve this project!\n\n");
 printf ("At the same time, the " SECURE_WIPE_NAME " project remains free to use ");
 printf ("and is distributed under the " SECURE_WIPE_LICENSE ", so making a donation is completely optional.\n");
 printf ("You can continue enjoying and using it without paying anything.\n");
 printf ("Contributions are a voluntary way to show appreciation and help the project grow, ");
 printf ("but there is absolutely no obligation to donate.\n\n\n");
 printf ("Bitcoin: 18NTjAZhiiwioSN1w6JBkvPQDkDqPiDt5T\n");
 printf ("Litecoin: LMuEHujV3cCYMzjYxKXdVHadrpB4YtwgeF\n");
 printf ("Ethereum: 0xEC2feBbA54050801E57946a1a7bfF66AF222d330\n");
 printf ("Tron: TUmv3VLiPS8RjLEFywUfn4XGVoL4B3m8jE\n");
 printf ("Solana: 6Ap7RzP8y8HNNuFUhuMKNoMuVqBzMmA3YpkxBsbY8ctY\n");
 printf ("Zcash: t1XNSbWCx9N6S6bHEMpoeMmo8iai6j5NSz7\n");
 printf ("Ripple: rw8nx6k6MD5jiR9cWtWfMeXGW6pJ2QVuYG\n");
 printf ("Dash: XbYWcSL76G1rBKwGmfGXx5B9RFYmYqhdtm\n\n");
}

bool parseArguments (int argc, char *argv[], program_config_t *config)
{
 configDefault (config);
 if (argc < 2)
 {
  config->selectDisk = true;
  return true;
 }
 for (int argIndex = 1; argIndex < argc; argIndex++)
 {
  if (strcmp (argv[argIndex], "--help") == 0)
  {
   printUsage (argv[0]);
   exit (0);
  }
  else if (strcmp (argv[argIndex], "--methods") == 0)
  {
   printMethods ();
   exit (0);
  }
  else if (strcmp (argv[argIndex], "--version") == 0)
  {
   printVersion ();
   exit (0);
  }
  else if (strcmp (argv[argIndex], "--about") == 0)
  {
   printAbout ();
   exit (0);
  }
  else if (strcmp (argv[argIndex], "--license") == 0)
  {
   printLicense ();
   exit (0);
  }
  else if (strcmp (argv[argIndex], "--support") == 0)
  {
   printSupport ();
   exit (0);
  }
  else if (strcmp (argv[argIndex], "-L") == 0 || strcmp (argv[argIndex], "--list-storage") == 0)
  {
   config->listDisks = true;
  }
  else if (strcmp (argv[argIndex], "-S") == 0 || strcmp (argv[argIndex], "--select-storage") == 0)
  {
   config->selectDisk = true;
  }
  else if (strcmp (argv[argIndex], "-A") == 0 || strcmp(argv[argIndex], "--show-system-storage") == 0)
  {
   config->showAllDisks = true;
  }
  else if (strcmp (argv[argIndex], "--zero") == 0)
  {
   config->method = WIPE_METHOD_ZERO;
   config->passes = 1;
  }
  else if (strcmp (argv[argIndex], "--random") == 0)
  {
   config->method = WIPE_METHOD_RANDOM;
   if (argIndex + 1 < argc && argv[argIndex + 1][0] != '-')
   {
    int nextArg = ++argIndex;
    config->passes = (uint32_t) atoi (argv[nextArg]);
    if (config->passes < 1 || config->passes > 100)
    {
     fprintf (stderr, "ERROR: --random requires a number between 1 and 100. See --help.\n");
     return false;
    }
   }
   else
   {
    fprintf (stderr, "ERROR: --random requires a number of passes (e.g., --random 3). See --help.\n");
    return false;
   }
  }
  else if (strcmp (argv[argIndex], "--dod-short") == 0)
  {
   config->method = WIPE_METHOD_DOD_SHORT;
   config->passes = 3;
  }
  else if (strcmp (argv[argIndex], "--dod-full") == 0)
  {
   config->method = WIPE_METHOD_DOD_FULL;
   config->passes = 7;
  }
  else if (strcmp (argv[argIndex], "--schneier") == 0)
  {
   config->method = WIPE_METHOD_SCHNEIER;
   config->passes = 7;
  }
  else if (strcmp (argv[argIndex], "--gutmann") == 0)
  {
   config->method = WIPE_METHOD_GUTMANN;
   config->passes = 35;
  }
  else if (strcmp (argv[argIndex], "--afssi-5020") == 0)
  {
   config->method = WIPE_METHOD_AFSSI_5020;
   config->passes = 3;
  }
  else if (strcmp (argv[argIndex], "--nist-clear") == 0)
  {
   config->method = WIPE_METHOD_NIST_CLEAR;
   config->passes = 1;
  }
  else if (strcmp (argv[argIndex], "--nist-purge") == 0)
  {
   config->method = WIPE_METHOD_NIST_PURGE;
   config->passes = 3;
  }
  else if (strcmp (argv[argIndex], "-b") == 0 || strcmp (argv[argIndex], "--buffer") == 0)
  {
   if (argIndex + 1 < argc)
   {
    size_t newSize = parseSizeWithUnit (argv[++argIndex]);
    if (newSize == 0 || bufferSetSize (newSize) != 0)
    {
     fprintf (stderr, "ERROR: Invalid buffer size. Use format like 1MB, 512KB. See --help.\n");
     return false;
    }
    config->bufferSize = newSize;
    config->bufferGiven = true;
   }
   else
   {
    fprintf (stderr, "ERROR: --buffer requires a size argument (e.g., --buffer 1M). See --help.\n");
    return false;
   }
  }
  else if (strcmp (argv[argIndex], "--analyze") == 0)
  {
   config->analyzeOnly = true;
  }
  else if (strcmp (argv[argIndex], "--analyze-write") == 0)
  {
   config->analyzeOnly = true;
   config->analyzeWriteTest = true;
  }
  else if (strcmp (argv[argIndex], "--wp-check") == 0)
  {
   config->analyzeOnly = true;
   config->quickWpCheck = true;
  }
  else if (strcmp (argv[argIndex], "--skip-analysis") == 0)
  {
   config->skipAnalysis = true;
  }
  else if (strcmp (argv[argIndex], "--destroy-partition-table") == 0)
  {
   config->destroyPartitionTable = true;
   config->analyzeOnly = true;
  }
  else if (strcmp (argv[argIndex], "--log") == 0)
  {
   if (argIndex + 1 < argc && argv[argIndex + 1][0] != '-')
   {
    snprintf (gLogFilePath, sizeof (gLogFilePath), "%s", argv[++argIndex]);
   }
   else
   {
    fprintf (stderr, "ERROR: --log requires a file path. See --help.\n");
    return false;
   }
  }
  else if (strcmp (argv[argIndex], "--no-log") == 0)
  {
   gNoLog = true;
   gLogFilePath[0] = '\0';
  }
  else if (strcmp (argv[argIndex], "-v") == 0 || strcmp (argv[argIndex], "--verify") == 0)
  {
   config->verify = true;
  }
  else if (strcmp (argv[argIndex], "-n") == 0 || strcmp (argv[argIndex], "--no-verify") == 0)
  {
   config->verify = false;
  }
  else if (strcmp (argv[argIndex], "-y") == 0 || strcmp (argv[argIndex], "--yes") == 0)
  {
   config->autoConfirm = true;
  }
  else if (strcmp (argv[argIndex], "-q") == 0 || strcmp (argv[argIndex], "--quiet") == 0)
  {
   config->verbose = false;
  }
  else if (strcmp (argv[argIndex], "--sector") == 0)
  {
   if (argIndex + 1 < argc)
   {
    config->emergencySectors = strtoull (argv[++argIndex], NULL, 10);
    if (config->emergencySectors == 0)
    {
     fprintf (stderr, "ERROR: --sector must be a positive number. See --help.\n");
     return false;
    }
   }
   else
   {
    fprintf (stderr, "ERROR: --sector requires a number. See --help.\n");
    return false;
   }
  }
  else if (strcmp (argv[argIndex], "--block") == 0)
  {
   if (argIndex + 1 < argc)
   {
    config->blockCount = strtoull (argv[++argIndex], NULL, 10);
    if (config->blockCount == 0)
    {
     fprintf (stderr, "ERROR: --block must be a positive number. See --help.\n");
     return false;
    }
    config->blockGiven = true;
   }
   else
   {
    fprintf (stderr, "ERROR: --block requires a number. See --help.\n");
    return false;
   }
  }
  else if (strcmp (argv[argIndex], "--emergency") == 0)
  {
   config->emergencyMode = true;
  }
  else if (argv[argIndex][0] != '-')
  {
   snprintf (config->devicePath, sizeof (config->devicePath), "%s", argv[argIndex]);
  }
  else
  {
   fprintf (stderr, "ERROR: Unknown option '%s'. See --help for usage.\n", argv[argIndex]);
   return false;
  }
 }

 if (config->emergencyMode)
 {
  if (!config->bufferGiven)
  {
   fprintf (stderr, "ERROR: --emergency requires --buffer SIZE (e.g., --buffer 1M). See --help.\n");
   return false;
  }

  if (config->blockGiven && config->emergencySectors != 0)
  {
   fprintf (stderr, "ERROR: --emergency cannot use both --sector and --block. Choose one. See --help.\n");
   return false;
  }

  if (!config->blockGiven && config->emergencySectors == 0)
  {
   fprintf (stderr, "ERROR: --emergency requires either --sector N or --block N. See --help.\n");
   return false;
  }

  if (config->blockGiven)
  {
   size_t bytesPerBlock = config->bufferSize;
   if (bytesPerBlock % SECTOR_SIZE != 0)
   {
    fprintf (stderr, "ERROR: buffer size (%zu) must be multiple of sector size (%d). See --help.\n", bytesPerBlock, SECTOR_SIZE);
    return false;
   }
   uint64_t sectorsPerBlock = bytesPerBlock / SECTOR_SIZE;
   config->emergencySectors = config->blockCount * sectorsPerBlock;
  }

  if (config->listDisks || config->selectDisk || config->analyzeOnly || config->destroyPartitionTable || config->quickWpCheck || config->skipAnalysis)
  {
   fprintf (stderr, "ERROR: --emergency is incompatible with other operation flags (--list, --select, --analyze, --destroy-partition-table, "
                    "--wp-check, --skip-analysis). See --help.\n");
   return false;
  }

  if (strlen (config->devicePath) == 0)
  {
   fprintf (stderr, "ERROR: --emergency requires a device path. See --help.\n");
   return false;
  }
 }

 return true;
}