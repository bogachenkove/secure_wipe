# =============================================================================
# CMake Build Configuration for SecureWipe Utility
# =============================================================================
# This file configures the cross‑platform build process for a secure data
# wiping utility. It handles compiler selection, version metadata extraction,
# optimization flags (including LTO and architecture‑specific instructions),
# and OS‑specific system library linking (Windows: bcrypt, setupapi, etc.;
# Linux/Unix: libm).
# =============================================================================

# Require CMake 3.15 or higher.
# This ensures support for modern features like target_compile_definitions
# with generator expressions, and LTO (Link Time Optimization) flags.
cmake_minimum_required(VERSION 3.15)

# Define the project as "securewipe" using only the C language.
# The project name influences the default output executable name.
project(securewipe LANGUAGES C)

# Enforce strict C17 standard conformance.
# C17 is the latest stable revision of the ISO C standard (2018).
set(CMAKE_C_STANDARD 17)
set(CMAKE_C_STANDARD_REQUIRED ON)   # Fail if compiler does not support C17
set(CMAKE_C_EXTENSIONS OFF)         # Disable compiler‑specific extensions
                                    # (e.g., GNU extensions) for maximum
                                    # portability across compilers.

# If the user did not specify a build type, default to "Release".
# This avoids undefined optimization behavior on single‑configuration
# generators (like Makefiles). The FORCE option overwrites any cache value
# that might have been auto‑detected incorrectly.
if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
endif()

# On Windows, if no C compiler is explicitly selected, attempt to auto‑detect
# Microsoft Visual C++ (MSVC) 'cl.exe'. This ensures the build uses the
# expected Windows SDK environment rather than falling back to MinGW or Clang.
if(WIN32 AND NOT CMAKE_C_COMPILER)
    # Search the system PATH for MSVC compiler executable.
    find_program(MSVC_COMPILER cl)
    if(MSVC_COMPILER)
        # Cache the discovered compiler path for subsequent runs.
        set(CMAKE_C_COMPILER "${MSVC_COMPILER}" CACHE PATH "C compiler" FORCE)
        message(STATUS "Selected MSVC compiler: cl")
    endif()
endif()

# Define the source directory relative to this CMakeLists.txt file.
# All .c files will be searched under this directory tree.
set(SOURCE_DIR "${CMAKE_CURRENT_LIST_DIR}/../source")

# Path to the metadata.c file that contains #define constants for version,
# copyright, license, etc. This file is shared between the build system
# and the C source (so that version strings remain consistent).
set(VERSION_H "${SOURCE_DIR}/module/metadata.c")

# Extract product metadata from metadata.c by reading the file as a string
# and parsing #define directives using regular expressions. This eliminates
# the need to duplicate version numbers in the CMake cache.
if(EXISTS "${VERSION_H}")
    # Read entire metadata.c into a single CMake variable.
    file(READ "${VERSION_H}" VERSION_CONTENT)

    # Helper function: retrieve the string value of a #define macro.
    # Input: name   - macro name (e.g., "SECURE_WIPE_VERSION")
    #        output - variable name to store the extracted value.
    function(get_version_macro name output)
        # Regex matches: #define <name> "<value>"
        # The value is captured in CMAKE_MATCH_1 (quoted string content).
        string(REGEX MATCH "#define[ \t]+${name}[ \t]+\"([^\"]*)\"" match "${VERSION_CONTENT}")
        if(CMAKE_MATCH_1)
            # Propagate the matched value to the parent scope.
            set(${output} "${CMAKE_MATCH_1}" PARENT_SCOPE)
        else()
            # If macro not found, return empty string.
            set(${output} "" PARENT_SCOPE)
        endif()
    endfunction()

    # Extract each metadata field from the C header.
    # These values will be passed as compile definitions to the C compiler.
    get_version_macro("SECURE_WIPE_NAME"         PRODUCT_NAME)
    get_version_macro("SECURE_WIPE_DESCRIPTION"  FILE_DESCRIPTION)
    get_version_macro("SECURE_WIPE_VERSION"      PRODUCT_VERSION)
    get_version_macro("SECURE_WIPE_LICENSE"      LICENSE)
    get_version_macro("SECURE_WIPE_COPYRIGHT"    COPYRIGHT)
    get_version_macro("SECURE_WIPE_AUTHOR"       AUTHOR)
    get_version_macro("SECURE_WIPE_CONTACT"      CONTACT)
    get_version_macro("SECURE_WIPE_HOMEPAGE"     HOMEPAGE)
endif()

# Recursively collect all .c source files under SOURCE_DIR.
# GLOB_RECURSE follows subdirectories (e.g., module/, platform/, etc.).
# This simplifies adding new files without updating CMakeLists.txt.
file(GLOB_RECURSE SOURCES "${SOURCE_DIR}/*.c")
# Sort the list alphabetically to ensure deterministic build order
# (avoids spurious rebuilds due to file system iteration order).
list(SORT SOURCES)

# Create the final executable target from all collected sources.
# The executable name defaults to the project name ("securewipe").
add_executable(${PROJECT_NAME} ${SOURCES})

# Pass the metadata macros as preprocessor definitions to every source file.
# PRIVATE scope means these definitions are only used when compiling this
# target, not by dependents. Each definition becomes a -D flag (or /D on MSVC).
target_compile_definitions(${PROJECT_NAME} PRIVATE
    PRODUCT_NAME="${PRODUCT_NAME}"
    FILE_DESCRIPTION="${FILE_DESCRIPTION}"
    PRODUCT_VERSION="${PRODUCT_VERSION}"
    LICENSE="${LICENSE}"
    COPYRIGHT="${COPYRIGHT}"
    AUTHOR="${AUTHOR}"
    CONTACT="${CONTACT}"
    HOMEPAGE="${HOMEPAGE}"
)

# For Linux/Unix: define _GNU_SOURCE to expose O_DIRECT and other extensions
# (required because CMAKE_C_EXTENSIONS is set to OFF).
if(NOT WIN32)
    target_compile_definitions(${PROJECT_NAME} PRIVATE _GNU_SOURCE)
endif()

# Add include directories for the compiler to search when resolving #include.
# The 'module' subdirectory is kept separate to mirror the C source layout.
target_include_directories(${PROJECT_NAME} PRIVATE
    ${SOURCE_DIR}            # Allows #include "module/metadata.h"
    ${SOURCE_DIR}/module     # Allows #include "metadata.h" directly
)

# =============================================================================
# Compiler and Linker Flags – Platform‑Specific Configuration
# =============================================================================

if(MSVC)
    # MSVC (Windows) specific compile options:
    # /nologo     - Suppress copyright banner
    # /TC         - Force all source files to be compiled as C (not C++)
    # /utf-8      - Set source and execution character sets to UTF-8
    target_compile_options(${PROJECT_NAME} PRIVATE /nologo /TC /utf-8)

    # Linker options for Windows:
    # /MANIFESTUAC:level='requireAdministrator' – Request UAC elevation to
    #   administrator rights. Essential because secure wiping requires
    #   direct disk access (raw device handles) which is restricted by
    #   Windows security. Without this, the program would fail with
    #   "access denied" on physical drives.
    # uiAccess='false' – Disable UI automation access (standard application).
    target_link_options(${PROJECT_NAME} PRIVATE "/MANIFESTUAC:level='requireAdministrator' uiAccess='false'")
else()
    # For GCC, Clang, and other Unix‑like compilers:
    # -finput-charset=UTF-8 – Interpret source files as UTF-8
    # -fexec-charset=UTF-8  – Generate UTF-8 encoded string literals
    target_compile_options(${PROJECT_NAME} PRIVATE -finput-charset=UTF-8 -fexec-charset=UTF-8)
endif()

# Link required system libraries (varies by OS).
if(WIN32)
    # Windows: link against cryptographic (bcrypt), device setup (setupapi),
    # configuration manager (cfgmgr32), advanced Windows APIs (advapi32),
    # and kernel32 (core system calls).
    # - bcrypt:   Cryptographic primitives for RNG and secure erase patterns.
    # - setupapi: Enumerating physical drives and volume management.
    # - cfgmgr32: Query device properties (disk size, serial number, etc.).
    # - advapi32: Registry access, privilege escalation (SeTakeOwnershipPrivilege).
    # - kernel32: Memory management, file I/O, handle manipulation.
    target_link_libraries(${PROJECT_NAME} PRIVATE bcrypt setupapi cfgmgr32 advapi32 kernel32)
else()
    # Unix (Linux, macOS, BSD): link only libm (math library) – used for
    # optional numerical patterns or random distribution functions.
    # No explicit kernel32 or device enumeration libs needed because
    # POSIX APIs (open, ioctl, read, write) are in libc.
    target_link_libraries(${PROJECT_NAME} PRIVATE m)
endif()

# =============================================================================
# Optimization & Debug Flags Based on Build Type
# =============================================================================

if(CMAKE_BUILD_TYPE STREQUAL "Release")
    # Release mode: aggressive optimization, small binary size, no debug symbols.
    if(MSVC)
        # MSVC Release flags:
        # /W0           – Disable all warnings (to reduce compile noise).
        # /Ox           – Maximum optimizations (favor speed).
        # /GL           – Whole Program Optimization (enables cross‑module inlining).
        # /arch:AVX2    – Use AVX2 instruction set for faster memory operations.
        # /DNDEBUG      – Remove assertions (assert() becomes no‑op).
        target_compile_options(${PROJECT_NAME} PRIVATE /W0 /Ox /GL /arch:AVX2 /DNDEBUG)
        # Linker flags:
        # /LTCG         – Link Time Code Generation (exploits /GL for cross‑module opt).
        # /OPT:REF      – Eliminate unreferenced functions/data (reduces size).
        # /OPT:ICF      – Combine identical COMDAT folding (reduces size further).
        # /DEBUG:NONE   – Strip all debug information from the final executable.
        target_link_options(${PROJECT_NAME} PRIVATE /LTCG /OPT:REF /OPT:ICF /DEBUG:NONE)
    else()
        # GCC/Clang Release flags:
        # -w            – Suppress all warnings (minimize noise).
        # -O3           – Highest optimization level (aggressive inlining, loop unrolling).
        # -flto         – Link Time Optimization (global analysis across translation units).
        # -march=native – Tune for the host CPU (enables SSE, AVX, etc. detected at build time).
        # -DNDEBUG      – Disable assert().
        target_compile_options(${PROJECT_NAME} PRIVATE -w -O3 -flto -march=native -DNDEBUG)
        # Linker flags:
        # -s            – Strip symbol table and debug info.
        # -flto         – Enable LTO also at link stage (required with compile‑time -flto).
        # -Wl,--build-id=none – Disable build‑ID generation (reduces binary size).
        target_link_options(${PROJECT_NAME} PRIVATE -s -flto -Wl,--build-id=none)
        if(APPLE)
            # macOS additional linker flag:
            # -Wl,-dead_strip – Remove unused sections (similar to /OPT:REF on Windows).
            target_link_options(${PROJECT_NAME} PRIVATE -Wl,-dead_strip)
        endif()
    endif()
elseif(CMAKE_BUILD_TYPE STREQUAL "Debug")
    # Debug build: no optimizations, full debugging information, runtime checks.
    if(MSVC)
        # MSVC Debug:
        # /W4           – High warning level (catches many bugs).
        # /Od           – Disable optimizations (clear call stacks, predictable variables).
        # /Zi           – Generate Program Database (PDB) for debugger.
        # /RTC1         – Runtime error checks (stack frame, uninitialized variables).
        target_compile_options(${PROJECT_NAME} PRIVATE /W4 /Od /Zi /RTC1)
        # /DEBUG        – Instruct linker to generate full debugging data.
        target_link_options(${PROJECT_NAME} PRIVATE /DEBUG)
    else()
        # GCC/Clang Debug:
        # -Wall -Wextra -pedantic – Enable almost all warnings, plus ISO C conformance checks.
        # -O0           – No optimization (variables not eliminated, easy stepping).
        # -ggdb3        – Produce GDB‑compatible debug info with macro definitions.
        target_compile_options(${PROJECT_NAME} PRIVATE -Wall -Wextra -pedantic -O0 -ggdb3)
    endif()
endif()