Secure Wipe v2.0.11.0
Data Destruction Tool

DESCRIPTION
  Secure Wipe is a cross-platform (Windows/Linux/macOS) utility for irreversibly
  erasing data on storage devices (disks, SSDs, USB drives) and files/directories.
  It supports disk analysis, bad sector detection, write‑protection testing,
  partition table destruction, ATA Secure Erase, and secure file wiping.

REQUIREMENTS
  - Windows: Administrator privileges
  - Linux/macOS: root privileges

BUILD
  The project supports multiple platforms (Windows, Linux, macOS) via two
  comprehensive build systems. Both automatically handle compiler detection,
  metadata extraction, and hardware optimization (LTO, AVX2, Native arch).

  Using CMake:
    mkdir build && cd build
    cmake .. -DCMAKE_BUILD_TYPE=Release # or Debug
    cmake --build . --config Release # or Debug

  Using Make:
    make BUILD=release # or debug

  Build System Features:
    - Metadata Sync: Version info is pulled directly from source/module/metadata.c.
    - Security: Embeds UAC manifest (Windows) to ensure Administrator access.
    - Performance: Enables Link Time Optimization (LTO) and hardware acceleration (AVX2/Native).
    - Standards: Enforces strict C17 compliance without compiler-specific extensions.
    - Quality: High-level diagnostics in Debug mode (Wall/W4) and silent Release builds.
    - Dependencies: Links OS-specific libs (bcrypt, setupapi on Win; libm on Unix).

USAGE
  securewipe [options] <device>
  securewipe -L, --list-storage [-A, --show-system-storage]
  securewipe -S, --select-storage
  securewipe --wipe-file <path>
  securewipe --wipe-dir <path>

OPTIONS
  Information:
    --version, --about, --license, --support, --help, --methods

  Disk discovery:
    -L, --list-storage			List storage devices
    -S, --select-storage		Interactive storage device selection
    -A, --all-storage			Show all devices (including system disks)

  Wipe methods (software overwrite):
    --zero				Zero fill (1 pass)
    --random N				Random data (N passes, 1‑100)

  ATA Security Commands (hardware erase):
    --ata-erase				Perform ATA Secure Erase (normal)
    --ata-enhanced-erase		Perform ATA Enhanced Secure Erase
    Note: Requires a device path, incompatible with other wipe methods

  File and Directory Wiping:
    --wipe-file <path>			Securely wipe a single file
    --wipe-dir <path>			Securely wipe a directory recursively
    --rename-count <N>			Number of renames before deletion (default 3, 0‑255)
    --no-rename				Disable renaming before deletion

  Analysis:
    --analyze				Read‑only analysis (bad sectors, readability)
    --analyze-write			Write test analysis (destroys data!)
    --wp-check				Quick write‑protection check (first 8 MB)
    --skip-analysis			Skip pre‑wipe analysis and proceed directly to wipe

  Partition table:
    --destroy-partition-table		Zero MBR/GPT only (first 34 and last 33 sectors, no data wipe)

  Emergency mode (zero‑write only, no full wipe):
    --emergency				Enable emergency zero‑write mode
    --sector N				Number of sectors to overwrite (must be >0)
    --block N				Number of blocks to overwrite (block size = --buffer)
    -b, --buffer SIZE			I/O buffer size (optional, default = 512KB)
    (If neither --sector nor --block is given, the entire device is zeroed)

  Logging:
    --log FILE				Write log to specified file (default: timestamped file in TEMP)
    --no-log				Disable log file creation

  Other:
    -b, --buffer SIZE			I/O buffer size (e.g. 1MB, 512KB)
    -v, --verify			Verify after wipe (default)
    -n, --no-verify			Skip verification
    -y, --yes				Auto‑confirm (dangerous)
    -q, --quiet				Quiet mode (less verbose)
    --cycle N				Repeat full wipe cycle N times (default 1, max 100)
    --methods				List all wipe methods with descriptions

EXAMPLES
  securewipe -L --show-system-storage				# list all disks including system
  securewipe --select-storage					# interactive device selection
  securewipe --analyze /dev/sdb					# read‑only analysis
  securewipe --skip-analysis --zero /dev/sdc			# skip analysis, write zeros immediately
  securewipe --zero -y \\.\PhysicalDrive2			# zero fill disk 2
  securewipe --random 5 /dev/sdc				# 5 passes of random data
  securewipe --ata-erase /dev/sda				# ATA Secure Erase (normal)
  securewipe --ata-enhanced-erase /dev/nvme0n1			# ATA Enhanced Secure Erase
  securewipe --destroy-partition-table \\.\PhysicalDrive0	# wipe MBR/GPT only
  securewipe --log ./custom.log /dev/sdb			# write log to custom file
  securewipe --no-log /dev/sdb					# disable logging
  securewipe --emergency --sector 1000 /dev/sdc			# emergency zero‑write first 1000 sectors
  securewipe --emergency --buffer 1M --block 10 /dev/sdc	# zero 10 blocks of 1MB each
  securewipe --emergency /dev/sdc				# zero entire device (full disk)
  securewipe --wipe-file secret.txt				# securely wipe a file
  securewipe --wipe-dir ./confidential				# recursively wipe directory
  securewipe --rename-count 5 --wipe-file data.bin		# rename 5 times before deletion
  securewipe --methods						# show all wipe methods

NOTES
  - Always backup important data before wiping.
  - Wiped data cannot be recovered.
  - System disks are protected by default.
  - Write‑protected devices will cause operation failure.
  - On Windows use \\.\PhysicalDriveN (N = disk number).
  - On Linux use /dev/sdX, /dev/nvmeXnY, etc.
  - Verification reads entire device after wipe to check for errors.
  - ATA Secure Erase is a hardware command, much faster than software overwrite,
    but may not work on USB‑connected drives (many USB bridges block ATA commands).
  - Emergency mode is intended for quick partial overwrite (e.g., wiping partition table or boot area).
    Without --sector/--block it erases the whole disk. Buffer size is optional (default 512KB).
  - Use --cycle to repeat the entire wipe process (analysis + wipe + verification) multiple times.
  - File wiping overwrites file content (zero or random) and optionally renames the file
    multiple times before deletion to complicate forensic recovery.
  - The program includes safety checks: overflow protection, strict argument validation,
    and a second mount check just before writing to prevent filesystem corruption.

LICENSE
  MIT License – see docs/LICENSE.txt