Secure Wipe v2.0.4.0
Data Destruction Tool

DESCRIPTION
  Secure Wipe is a cross-platform (Windows/Linux/macOS) utility for irreversibly
  erasing data on storage devices using multiple overwriting methods.
  It supports analysis, bad sector detection, write‑protection testing,
  partition table destruction, and verification.

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

OPTIONS
  Information:
    --version, --about, --license, --support, --help, --methods

  Disk discovery:
    -L, --list-storage               List storage devices
    -S, --select-storage             Interactive storage device selection
    -A, --show-system-storage        Show all devices (including system disks)

  Wipe methods:
    --zero                           Zero fill (1 pass)
    --random N                       Random data (N passes, 1‑100)
    --dod-short                      DoD 5220.22-M short (3 passes) [default]
    --dod-full                       DoD 5220.22-M ECE (7 passes)
    --schneier                       Schneier (7 passes: 0xFF, 0x00, 5×random)
    --gutmann                        Gutmann (35 passes)
    --afssi-5020                     AFSSI‑5020 (3 passes: 0x00, 0xFF, random)
    --nist-clear                     NIST Clear (1 pass: zeros)
    --nist-purge                     NIST Purge (3 passes: random, zero, random)

  Analysis:
    --analyze                        Read‑only analysis (bad sectors, readability)
    --analyze-write                  Write test analysis (destroys data!)
    --wp-check                       Quick write‑protection check (first 8 MB)
    --destroy-partition-table        Zero MBR/GPT only (no data wipe)

  Emergency mode (zero‑write only, no full wipe):
    --emergency                      Enable emergency zero‑write mode
    --sector N                       Number of sectors to overwrite (must be >0)
    --block N                        Number of blocks to overwrite (block size = --buffer)
    (Requires --buffer, and either --sector or --block)

  Logging:
    --log FILE                       Write log to specified file (default: timestamped file in TEMP)
    --no-log                         Disable log file creation

  Other:
    -b, --buffer SIZE                I/O buffer size (e.g. 1MB, 512KB)
    -v, --verify                     Verify after wipe (default)
    -n, --no-verify                  Skip verification
    -y, --yes                        Auto‑confirm (dangerous)
    -q, --quiet                      Quiet mode (less verbose)

EXAMPLES
  securewipe -L --show-system-storage          # list all disks including system
  securewipe --select-storage                  # interactive device selection
  securewipe --analyze /dev/sdb                # read‑only analysis
  securewipe --dod-short -y \\.\PhysicalDrive2 # wipe disk 2 with DoD short
  securewipe --nist-purge /dev/sdc             # NIST Purge wipe
  securewipe --destroy-partition-table \\.\PhysicalDrive0   # wipe MBR/GPT only
  securewipe --log ./custom.log /dev/sdb       # write log to custom file
  securewipe --no-log /dev/sdb                 # disable logging
  securewipe --emergency --buffer 1M --sector 1000 /dev/sdc   # emergency zero‑write first 1000 sectors

NOTES
  - Always backup important data before wiping.
  - Wiped data cannot be recovered.
  - System disks are protected by default.
  - Write‑protected devices will cause operation failure.
  - On Windows use \\.\PhysicalDriveN (N = disk number).
  - On Linux use /dev/sdX, /dev/nvmeXnY, etc.
  - Verification reads entire device after wipe to check for errors.
  - Emergency mode is intended for quick partial overwrite (e.g., wiping partition table or boot area).

LICENSE
  MIT License – see docs/LICENSE.txt