#!/usr/bin/env python

"""
    Script name: signature.py
    Script description: Sign checksum files with GPG detached signatures.
                        This script creates ASCII-armored detached signatures (.asc) for the SHA256 and BLAKE2b checksum files.
    Author: Bogachenko Vyacheslav <bogachenkove@outlook.com>
    License: MIT license <https://raw.githubusercontent.com/bogachenkove/secure_wipe/stable/docs/LICENSE.txt>
    Last update: May 2026
"""

import subprocess
from pathlib import Path
import sys

# Directory where this script resides.
SCRIPT_DIR = Path(__file__).resolve().parent

# Directory for SHA256-related files.
OUTPUT_DIR_SHA256 = SCRIPT_DIR / "SHA256"
OUTPUT_FILE_SHA256 = OUTPUT_DIR_SHA256 / "SHA256SUMS"
OUTPUT_FILE_SHA256_ASC = OUTPUT_FILE_SHA256.with_suffix(".asc")

# Directory for BLAKE2b-related files.
OUTPUT_DIR_BLAKE2B = SCRIPT_DIR / "BLAKE2b"
OUTPUT_FILE_BLAKE2B = OUTPUT_DIR_BLAKE2B / "BLAKE2BSUMS"
OUTPUT_FILE_BLAKE2B_ASC = OUTPUT_FILE_BLAKE2B.with_suffix(".asc")


def sign_file(file_path: Path, key_id: str = None):
    """
    Create detached GPG signature for a file.
    Generates an ASCII-armored .asc file alongside the original. Optionally specify a key ID.
    Returns True on success, False on failure.
    """
    # Check that the file to sign exists.
    if not file_path.exists():
        print(f"[ERROR] File not found: {file_path}")
        return False

    # Build GPG command: armor, detached sign, optional key selection.
    cmd = ["gpg", "--armor", "--detach-sign"]
    if key_id:
        cmd.extend(["--local-user", key_id])
    cmd.append(str(file_path))

    try:
        # Execute GPG signing.
        subprocess.run(cmd, check=True)
        print(f"[OK] Signed: {file_path} -> {file_path}.asc")
        return True
    except subprocess.CalledProcessError as e:
        print(f"[ERROR] Failed to sign {file_path}: {e}")
        return False


def main():
    """
    Sign both checksum files.
    Invokes sign_file for SHA256 and BLAKE2b checksum files.
    """
    key_id = sys.argv[1] if len(sys.argv) > 1 else None
    success_sha = sign_file(OUTPUT_FILE_SHA256, key_id)
    success_blake = sign_file(OUTPUT_FILE_BLAKE2B, key_id)
    if not (success_sha and success_blake):
        sys.exit(1)


# Execute main function when script is run directly.
if __name__ == "__main__":
    main()