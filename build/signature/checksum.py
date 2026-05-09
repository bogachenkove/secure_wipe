#!/usr/bin/env python

"""
    Script name: checksum.py
    Script description: Generate SHA256 and BLAKE2b checksums for source files.
                        This script walks through the source directory, computes hashes for all .cpp and .hpp files,
                        and writes them to separate checksum files in standard format.
    Author: Bogachenko Vyacheslav <bogachenkove@outlook.com>
    License: MIT license <https://raw.githubusercontent.com/bogachenkove/secure_wipe/stable/docs/LICENSE.txt>
    Last update: May 2026
"""

import hashlib
from pathlib import Path
import sys

# Directory where this script resides.
SCRIPT_DIR = Path(__file__).resolve().parent

# Project root is two levels up.
PROJECT_ROOT = SCRIPT_DIR.parent.parent

# Source code directory.
SOURCE_DIR = PROJECT_ROOT / "source"

# Directory for SHA256-related files.
OUTPUT_DIR_SHA256 = SCRIPT_DIR / "SHA256"
OUTPUT_FILE_SHA256 = OUTPUT_DIR_SHA256 / "SHA256SUMS"

# Directory for BLAKE2b-related files.
OUTPUT_DIR_BLAKE2B = SCRIPT_DIR / "BLAKE2b"
OUTPUT_FILE_BLAKE2B = OUTPUT_DIR_BLAKE2B / "BLAKE2BSUMS"


def calculate_sha256(file_path: Path) -> str:
    """
    Compute SHA-256 hash.
    Reads file in binary mode and returns hexadecimal digest.
    """
    data = file_path.read_bytes()
    return hashlib.sha256(data).hexdigest()


def calculate_blake2b(file_path: Path) -> str:
    """
    Compute BLAKE2b hash.
    Reads file in binary mode and returns hexadecimal digest.
    """
    data = file_path.read_bytes()
    return hashlib.blake2b(data).hexdigest()


def main():
    """
    Generate checksum files.
    Steps: validate source directory, create output directories, collect all .cpp/.hpp files,
    sort them, compute hashes, and write to SHA256SUMS and BLAKE2BSUMS.
    """
    # Ensure source directory exists.
    if not SOURCE_DIR.exists():
        print(f"[ERROR] Source directory not found: {SOURCE_DIR}")
        sys.exit(1)

    # Create output directories if they don't exist.
    OUTPUT_DIR_SHA256.mkdir(parents=True, exist_ok=True)
    OUTPUT_DIR_BLAKE2B.mkdir(parents=True, exist_ok=True)

    # Collect all .cpp and .hpp files recursively.
    files = []
    for file_path in SOURCE_DIR.rglob("*"):
        if file_path.suffix.lower() in {".cpp", ".hpp"}:
            files.append(file_path)

    # Sort files by relative path for deterministic order.
    files.sort(key=lambda p: p.relative_to(SOURCE_DIR).as_posix())

    # Write both checksum files simultaneously.
    with OUTPUT_FILE_SHA256.open("w", encoding="utf-8") as sha_file, \
         OUTPUT_FILE_BLAKE2B.open("w", encoding="utf-8") as blake_file:

        for file_path in files:
            # Compute relative path for output.
            rel_path = file_path.relative_to(SOURCE_DIR).as_posix()
            sha256_hash = calculate_sha256(file_path)
            blake2b_hash = calculate_blake2b(file_path)

            # Write in standard format: "<hash>  <path>"
            sha_file.write(f"{sha256_hash}  {rel_path}\n")
            blake_file.write(f"{blake2b_hash}  {rel_path}\n")

    print(f"[OK] SHA256 sums written to: {OUTPUT_FILE_SHA256}")
    print(f"[OK] BLAKE2b sums written to: {OUTPUT_FILE_BLAKE2B}")


# Execute main function when script is run directly.
if __name__ == "__main__":
    main()