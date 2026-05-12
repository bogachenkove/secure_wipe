Secure Wipe v2.0.3.0
Data Destruction Tool

DESCRIPTION
  Secure Wipe is a cross-platform (Windows/Linux) utility for irreversibly
  erasing data on storage devices using multiple overwriting methods.
  It supports analysis, bad sector detection, write‑protection testing,
  partition table destruction, and verification.

REQUIREMENTS
  - Windows: Administrator privileges
  - Linux: root privileges (sudo)

BUILD
  Compile all .c files from source/module/ and source/main.c.
  Link against platform libraries:
    Windows: bcrypt.lib, setupapi.lib
    Linux: no extra libs (uses POSIX + Linux ioctls)

USAGE
  securewipe [options] <device>
  securewipe --list [-A]
  securewipe --select

OPTIONS
  Information:
    --version, --about, --license, --support, -h/--help, --methods

  Disk discovery:
    -L/--list               List disks
    -S/--select             Interactive disk selection
    -A                      Show all disks (including system)

  Wipe methods:
    -z/--zero               Zero fill (1 pass)
    -r N/--random N         Random data (N passes, 1‑100)
    -d/--dod                DoD 5220.22-M short (3 passes)
    -D/--dod-full           DoD 5220.22-M ECE (7 passes)
    -s/--schneier           Schneier (7 passes: 0xFF, 0x00, 5×random)
    -g/--gutmann            Gutmann (35 passes)
    --afssi                 AFSSI‑5020 (3 passes: 0x00, 0xFF, random)
    --nist-clear            NIST Clear (1 pass: zeros)
    --nist-purge            NIST Purge (3 passes: random, zero, random)

  Analysis:
    -a/--analyze            Read‑only analysis (bad sectors, readability)
    --analyze-write         Write test analysis (destroys data!)
    --wp-check              Quick write‑protection check (first 8 MB)
    --destroy-partition-table  Zero MBR/GPT only (no data wipe)

  Logging:
    --log FILE              Write log to specified file (default: timestamped file in TEMP)
    --no-log                Disable log file creation

  Other:
    -b/--buffer SIZE        I/O buffer size (e.g. 1MB, 512KB)
    -v/--verify             Verify after wipe (default)
    -n/--no-verify          Skip verification
    -y/--yes                Auto‑confirm (dangerous)
    -q/--quiet              Quiet mode (less verbose)

EXAMPLES
  securewipe -L												# list disks
  securewipe --select										# interactive selection
  securewipe -a /dev/sdb									# read‑only analysis
  securewipe -d -y \\.\PhysicalDrive2						# wipe disk 2 with DoD short
  securewipe --nist-purge /dev/sdc							# NIST Purge wipe
  securewipe --destroy-partition-table \\.\PhysicalDrive0	# wipe MBR/GPT only (prompts)
  securewipe --log ./custom.log /dev/sdb					# write log to custom file
  securewipe --no-log /dev/sdb								# disable logging

NOTES
  - Always backup important data before wiping.
  - Wiped data cannot be recovered.
  - System disks are protected by default.
  - Write‑protected devices will cause operation failure.
  - On Windows use \\.\PhysicalDriveN (N = disk number).
  - On Linux use /dev/sdX, /dev/nvmeXnY, etc.
  - Verification reads entire device after wipe to check for errors.

LICENSE
  MIT License – see docs/LICENSE.txt