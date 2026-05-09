#include "metadata.h"
#include "common.h"
#include "wiper.h"
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#ifdef _WIN32
#define strcasecmp _stricmp
#endif

#define SECURE_WIPE_NAME        "Secure Wipe"
#define SECURE_WIPE_VERSION     "2.0.0.0"
#define SECURE_WIPE_DESCRIPTION "Data Destruction Tool"
#define SECURE_WIPE_AUTHOR      "Bogachenko Vyacheslav"
#define SECURE_WIPE_CONTACT     "bogachenkove@outlook.com"
#define SECURE_WIPE_HOMEPAGE    "https://github.com/bogachenkove/secure_wipe"
#define SECURE_WIPE_LICENSE     "MIT License"
#define SECURE_WIPE_LICENSE_FILE "docs/LICENSE.txt"

static size_t parseSizeWithUnit(const char* arg)
{
    char* endptr;
    unsigned long long value = strtoull(arg, &endptr, 10);
    if (endptr == arg) return 0;
    while (*endptr == ' ') endptr++;
    size_t multiplier = 1;
    if (strcasecmp(endptr, "B") == 0 || *endptr == '\0')
        multiplier = 1;
    else if (strcasecmp(endptr, "KB") == 0)
        multiplier = 1024;
    else if (strcasecmp(endptr, "MB") == 0)
        multiplier = 1024 * 1024;
    else
        return 0;
    if (value > SIZE_MAX / multiplier) return 0;
    size_t result = (size_t)(value * multiplier);
    if (result > MAX_BUFFER_SIZE) return 0;
    return result;
}

void printMethods(void)
{
    printf("Wipe methods:\n");
    printf("  Zero     : 1 pass of zeros\n");
    printf("  Random   : N passes of random data\n");
    printf("  DoD short: 3 passes: 0x00, 0xFF, random\n");
    printf("  DoD full : 7 passes extended DoD\n");
    printf("  Schneier : 7 passes (0x00, 0xFF, 5x random)\n");
    printf("  Gutmann  : 35 passes (MFM/RLL patterns + random)\n");
}

void printUsage(const char* programName)
{
    printf("Usage: %s [options] <device>\n", programName);
    printf("       %s --list [-A]\n", programName);
    printf("       %s --select\n\n", programName);
    printf("Information flags:\n");
    printf("  --version            Show version information\n");
    printf("  --about              Show information about the program\n");
    printf("  --license            Show license information\n");
    printf("  --support            Show support/donation information\n");
    printf("  -h, --help           Show this help message\n");
    printf("\nDisk Discovery:\n");
    printf("  -L, --list           List disks\n");
    printf("  -S, --select         Interactive disk selection\n");
    printf("  -A                   Show all disks (including system)\n");
    printf("\nWipe Methods:\n");
    printf("  -z, --zero           Zero fill (1 pass)\n");
    printf("  -r N, --random N     Random data (N passes)\n");
    printf("  -d, --dod            DoD 5220.22-M short (3 passes) [default]\n");
    printf("  -D, --dod-full       DoD 5220.22-M ECE (7 passes)\n");
    printf("  -s, --schneier       Schneier method (7 passes)\n");
    printf("  -g, --gutmann        Gutmann method (35 passes)\n");
    printf("\nAnalysis Options:\n");
    printf("  -a, --analyze        Read-only analysis\n");
    printf("  --analyze-write      Analysis with write test (destroys data!)\n");
    printf("  --wp-check           Quick write-protection check\n");
    printf("  --destroy-partition-table   Destroy MBR/GPT only (zero out, no data wipe)\n");
    printf("\nOther Options:\n");
    printf("  -b, --buffer SIZE    I/O buffer size (e.g. 1MB, 512KB)\n");
    printf("  -v, --verify         Verify after wipe (default)\n");
    printf("  -n, --no-verify      Skip verification\n");
    printf("  -l FILE              Log file (default: secure_wipe.log)\n");
    printf("  -y, --yes            Auto-confirm (dangerous)\n");
    printf("  -q, --quiet          Quiet mode\n");
    printf("  --methods            List wipe methods\n");
    printf("\nNote: --destroy-partition-table does not require confirmation and does not overwrite data.\n");
}

void printVersion(void)
{
    printf(SECURE_WIPE_NAME " " SECURE_WIPE_VERSION "\n");
}

void printAbout(void)
{
    printf(SECURE_WIPE_NAME " " SECURE_WIPE_VERSION " - " SECURE_WIPE_DESCRIPTION "\n");
    printf("Copyright (c) 2026 " SECURE_WIPE_AUTHOR "\n\n");
    printf("Author:   " SECURE_WIPE_AUTHOR "\n");
    printf("Contact:  " SECURE_WIPE_CONTACT "\n");
    printf("Homepage: " SECURE_WIPE_HOMEPAGE "\n");
}

void printLicense(void)
{
    printf(SECURE_WIPE_NAME " " SECURE_WIPE_VERSION " - " SECURE_WIPE_DESCRIPTION "\n");
    printf("Copyright (c) 2026 " SECURE_WIPE_AUTHOR "\n\n");
    printf("This software is released under the " SECURE_WIPE_LICENSE ".\n");
    printf("You are free to use, modify, and distribute it in accordance with the license terms.\n\n");
    FILE* licenseFile = fopen(SECURE_WIPE_LICENSE_FILE, "r");
    if (licenseFile)
    {
        char buffer[1024];
        while (fgets(buffer, sizeof(buffer), licenseFile))
            printf("%s", buffer);
        fclose(licenseFile);
    }
    else
    {
        printf("License file not found locally.\n");
        printf("Please read the license agreement online:\n");
        printf("https://raw.githubusercontent.com/bogachenkove/secure_wipe/stable/%s\n", SECURE_WIPE_LICENSE_FILE);
    }
}

void printSupport(void)
{
    printf(SECURE_WIPE_NAME " " SECURE_WIPE_VERSION " - " SECURE_WIPE_DESCRIPTION "\n");
    printf("Copyright (c) 2026 " SECURE_WIPE_AUTHOR "\n\n");
    printf("Donating is an act of generosity.\nYour support, however modest it might be, is necessary and you can provide it, because you love the " SECURE_WIPE_NAME " project and enjoy it.\nYour donations help to continue to support and improve this project!\n\n");
    printf("At the same time, the " SECURE_WIPE_NAME " project remains free to use and is distributed under the " SECURE_WIPE_LICENSE ", so making a donation is completely optional.\nYou can continue enjoying and using it without paying anything.\n");
    printf("Contributions are a voluntary way to show appreciation and help the project grow, but there is absolutely no obligation to donate.\n\n\n");
    printf("Bitcoin: 18NTjAZhiiwioSN1w6JBkvPQDkDqPiDt5T\n");
    printf("Litecoin: LMuEHujV3cCYMzjYxKXdVHadrpB4YtwgeF\n");
    printf("Ethereum: 0xEC2feBbA54050801E57946a1a7bfF66AF222d330\n");
    printf("Tron: TUmv3VLiPS8RjLEFywUfn4XGVoL4B3m8jE\n");
    printf("Solana: 6Ap7RzP8y8HNNuFUhuMKNoMuVqBzMmA3YpkxBsbY8ctY\n");
    printf("Zcash: t1XNSbWCx9N6S6bHEMpoeMmo8iai6j5NSz7\n");
    printf("Ripple: rw8nx6k6MD5jiR9cWtWfMeXGW6pJ2QVuYG\n");
    printf("Dash: XbYWcSL76G1rBKwGmfGXx5B9RFYmYqhdtm\n\n");
}

bool parseArguments(int argc, char* argv[], program_config_t* cfg)
{
    configDefault(cfg);
    if (argc < 2)
    {
        cfg->selectDisk = true;
        return true;
    }
    for (int argIndex = 1; argIndex < argc; argIndex++)
    {
        if (strcmp(argv[argIndex], "-h") == 0 || strcmp(argv[argIndex], "--help") == 0)
        {
            printUsage(argv[0]);
            exit(0);
        }
        else if (strcmp(argv[argIndex], "--methods") == 0)
        {
            printMethods();
            exit(0);
        }
        else if (strcmp(argv[argIndex], "--version") == 0)
        {
            printVersion();
            exit(0);
        }
        else if (strcmp(argv[argIndex], "--about") == 0)
        {
            printAbout();
            exit(0);
        }
        else if (strcmp(argv[argIndex], "--license") == 0)
        {
            printLicense();
            exit(0);
        }
        else if (strcmp(argv[argIndex], "--support") == 0)
        {
            printSupport();
            exit(0);
        }
        else if (strcmp(argv[argIndex], "-L") == 0 || strcmp(argv[argIndex], "--list") == 0)
        {
            cfg->listDisks = true;
        }
        else if (strcmp(argv[argIndex], "-S") == 0 || strcmp(argv[argIndex], "--select") == 0)
        {
            cfg->selectDisk = true;
        }
        else if (strcmp(argv[argIndex], "-A") == 0)
        {
            cfg->showAllDisks = true;
        }
        else if (strcmp(argv[argIndex], "-z") == 0 || strcmp(argv[argIndex], "--zero") == 0)
        {
            cfg->method = WIPE_METHOD_ZERO;
            cfg->passes = 1;
        }
        else if (strcmp(argv[argIndex], "-r") == 0 || strcmp(argv[argIndex], "--random") == 0)
        {
            cfg->method = WIPE_METHOD_RANDOM;
            if (argIndex + 1 < argc && argv[argIndex + 1][0] != '-')
            {
                int nextArg = ++argIndex;
                cfg->passes = (uint32_t)atoi(argv[nextArg]);
                if (cfg->passes < 1 || cfg->passes > 100)
                    return false;
            }
            else
            {
                cfg->passes = 3;
            }
        }
        else if (strcmp(argv[argIndex], "-d") == 0 || strcmp(argv[argIndex], "--dod") == 0)
        {
            cfg->method = WIPE_METHOD_DOD_SHORT;
            cfg->passes = 3;
        }
        else if (strcmp(argv[argIndex], "-D") == 0 || strcmp(argv[argIndex], "--dod-full") == 0)
        {
            cfg->method = WIPE_METHOD_DOD_FULL;
            cfg->passes = 7;
        }
        else if (strcmp(argv[argIndex], "-s") == 0 || strcmp(argv[argIndex], "--schneier") == 0)
        {
            cfg->method = WIPE_METHOD_SCHNEIER;
            cfg->passes = 7;
        }
        else if (strcmp(argv[argIndex], "-g") == 0 || strcmp(argv[argIndex], "--gutmann") == 0)
        {
            cfg->method = WIPE_METHOD_GUTMANN;
            cfg->passes = 35;
        }
        else if (strcmp(argv[argIndex], "-b") == 0 || strcmp(argv[argIndex], "--buffer") == 0)
        {
            if (argIndex + 1 < argc)
            {
                size_t newSize = parseSizeWithUnit(argv[++argIndex]);
                if (newSize == 0 || bufferSetSize(newSize) != 0)
                {
                    fprintf(stderr, "Invalid buffer size. Use format like 1MB, 512KB.\n");
                    return false;
                }
                cfg->bufferSize = newSize;
            }
            else
            {
                return false;
            }
        }
        else if (strcmp(argv[argIndex], "-a") == 0 || strcmp(argv[argIndex], "--analyze") == 0)
        {
            cfg->analyzeOnly = true;
        }
        else if (strcmp(argv[argIndex], "--analyze-write") == 0)
        {
            cfg->analyzeOnly = true;
            cfg->analyzeWriteTest = true;
        }
        else if (strcmp(argv[argIndex], "--wp-check") == 0)
        {
            cfg->analyzeOnly = true;
            cfg->quickWpCheck = true;
        }
        else if (strcmp(argv[argIndex], "--destroy-partition-table") == 0)
        {
            cfg->destroyPartitionTable = true;
            cfg->analyzeOnly = true;
        }
        else if (strcmp(argv[argIndex], "-v") == 0 || strcmp(argv[argIndex], "--verify") == 0)
        {
            cfg->verify = true;
        }
        else if (strcmp(argv[argIndex], "-n") == 0 || strcmp(argv[argIndex], "--no-verify") == 0)
        {
            cfg->verify = false;
        }
        else if (strcmp(argv[argIndex], "-l") == 0)
        {
            if (argIndex + 1 < argc)
                snprintf(cfg->logPath, sizeof(cfg->logPath), "%s", argv[++argIndex]);
        }
        else if (strcmp(argv[argIndex], "-y") == 0 || strcmp(argv[argIndex], "--yes") == 0)
        {
            cfg->autoConfirm = true;
        }
        else if (strcmp(argv[argIndex], "-q") == 0 || strcmp(argv[argIndex], "--quiet") == 0)
        {
            cfg->verbose = false;
        }
        else if (argv[argIndex][0] != '-')
        {
            snprintf(cfg->devicePath, sizeof(cfg->devicePath), "%s", argv[argIndex]);
        }
        else
        {
            return false;
        }
    }
    return true;
}