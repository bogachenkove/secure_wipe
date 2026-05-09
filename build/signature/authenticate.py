#!/usr/bin/env python

"""
    Script name: authenticate.py
    Script description: Authenticate checksum files by verifying GPG signatures.
                        This script imports a public key and verifies detached signatures for SHA256 and BLAKE2b checksum files.
    Author: Bogachenko Vyacheslav <bogachenkove@outlook.com>
    License: MIT license <https://raw.githubusercontent.com/bogachenkove/secure_wipe/stable/docs/LICENSE.txt>
    Last update: May 2026
"""
import subprocess
from pathlib import Path
import sys

# Directory where this script resides.
SCRIPT_DIR = Path(__file__).resolve().parent

# Default public key file path.
DEFAULT_PUBKEY = SCRIPT_DIR / "publickey.asc"

# Directory for SHA256-related files.
OUTPUT_DIR_SHA256 = SCRIPT_DIR / "SHA256"
SHA256_SUMS = OUTPUT_DIR_SHA256 / "SHA256SUMS"
SHA256_ASC = OUTPUT_DIR_SHA256 / "SHA256SUMS.asc"

# Directory for BLAKE2b-related files.
OUTPUT_DIR_BLAKE2B = SCRIPT_DIR / "BLAKE2b"
BLAKE2B_SUMS = OUTPUT_DIR_BLAKE2B / "BLAKE2BSUMS"
BLAKE2B_ASC = OUTPUT_DIR_BLAKE2B / "BLAKE2BSUMS.asc"


def get_public_key_path() -> Path:
    """
    Retrieve public key path.
    If the default publickey.asc exists, use it.
    Otherwise, prompt the user to enter a valid file path repeatedly until a file is found.
    """
    # Use default key if present.
    if DEFAULT_PUBKEY.exists():
        print(f"[INFO] Using default public key: {DEFAULT_PUBKEY}")
        return DEFAULT_PUBKEY

    # Notify user default is missing and request custom path.
    print(f"[WARNING] Default public key not found: {DEFAULT_PUBKEY}")
    while True:
        user_input = input("Please enter the full path to your public key file (publickey.asc): ").strip()
        if not user_input:
            continue  # Skip empty input.
        path = Path(user_input).expanduser().resolve()
        if path.exists():
            return path
        print(f"[ERROR] File does not exist: {path}")


def import_key(key_path: Path) -> bool:
    """
    Import public key into GPG keyring.
    Executes gpg --import on the provided file. Returns True if import succeeds, False on failure.
    """
    try:
        # Run GPG import silently, capturing output.
        subprocess.run(["gpg", "--import", str(key_path)], check=True, capture_output=True, text=True)
        print(f"[OK] Public key imported from: {key_path}")
        return True
    except subprocess.CalledProcessError as e:
        # Show GPG's error message for diagnosis.
        print(f"[ERROR] Failed to import public key:\n{e.stderr}")
        return False


def verify_signature(asc_file: Path, data_file: Path) -> bool:
    """
    Verify detached GPG signature.
    Checks that both files exist, then runs gpg --verify. Returns True for valid signature.
    """
    # Verify prerequisite files exist.
    if not asc_file.exists():
        print(f"[ERROR] Signature file not found: {asc_file}")
        return False
    if not data_file.exists():
        print(f"[ERROR] Data file not found: {data_file}")
        return False

    try:
        # Execute GPG verification with signature first, then data.
        subprocess.run(
            ["gpg", "--verify", str(asc_file), str(data_file)],
            check=True,
            capture_output=True,
            text=True
        )
        print(f"[OK] Valid signature for {data_file.name}")
        return True
    except subprocess.CalledProcessError as e:
        # Output GPG error details.
        print(f"[ERROR] Signature verification failed for {data_file.name}:\n{e.stderr}")
        return False


def main():
    """
    Orchestrate signature verification.
    Steps: obtain and import public key, verify SHA256 and BLAKE2b signatures, report overall status.
    """
    # Step 1: Get and import public key.
    pubkey_path = get_public_key_path()
    if not import_key(pubkey_path):
        sys.exit(1)  # Exit if key import fails.

    # Step 2: Verify both signature files.
    results = []
    results.append(verify_signature(SHA256_ASC, SHA256_SUMS))
    results.append(verify_signature(BLAKE2B_ASC, BLAKE2B_SUMS))

    # Step 3: Report final outcome.
    if all(results):
        print("\n[SUCCESS] All signatures are valid.")
    else:
        print("\n[FAILURE] One or more signatures are invalid.")
        sys.exit(1)


# Execute main function when script is run directly.
if __name__ == "__main__":
    main()