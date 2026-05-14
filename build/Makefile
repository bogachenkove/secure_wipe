# =============================================================================
# Makefile Build Configuration for SecureWipe Utility
# =============================================================================
# This Makefile provides an alternative build system to CMake for Unix-like
# environments (Linux, macOS) and Windows (using MSVC cl.exe or GCC/MinGW).
# It handles compiler detection, metadata extraction, platform‑specific
# compilation flags, link-time optimization (LTO), and UAC manifest
# requirements on Windows.
# =============================================================================

# -----------------------------------------------------------------------------
# Build Type Selection
# -----------------------------------------------------------------------------
# User can override BUILD from command line: make BUILD=debug
# Default is release build (optimized, stripped).
BUILD ?= release

# Final executable name (without extension for Unix, with .exe on Windows)
TARGET := securewipe

# Convert symbolic build type to internal mode string.
# 'debug' -> Debug, anything else (e.g., 'release', 'fast') -> Release.
# This matches the CMake conditional: if(NOT CMAKE_BUILD_TYPE) set to Release.
ifeq ($(BUILD),debug)
    BUILD_MODE := Debug
else
    BUILD_MODE := Release
endif

# -----------------------------------------------------------------------------
# Directory Structure
# -----------------------------------------------------------------------------
# Source code root directory (contains all .c files recursively).
SRC_DIR  := ../source
# Object file output directory – mimics CMake's directory structure to avoid
# cluttering the source tree. All .o files go under ./MakeFiles/securewipe.dir.
OBJ_DIR  := ./MakeFiles/$(TARGET).dir

# -----------------------------------------------------------------------------
# Host Operating System Detection (Cross‑Platform)
# -----------------------------------------------------------------------------
# Detect Windows by checking the OS environment variable (set by cmd.exe).
# Equivalent to CMake's WIN32 variable.
ifeq ($(OS),Windows_NT)
    IS_WIN := 1
    EXE_EXT := .exe
else
    # For non‑Windows: use uname -s to distinguish Linux, Darwin (macOS), etc.
    UNAME_S := $(shell uname -s)
    ifeq ($(UNAME_S),Darwin)
        IS_MAC := 1          # macOS specific flags will be applied later
    endif
    EXE_EXT :=               # No extension on Unix-like systems
endif

# Final executable name with correct platform extension.
FINAL_TARGET := $(TARGET)$(EXE_EXT)

# -----------------------------------------------------------------------------
# Compiler Selection
# -----------------------------------------------------------------------------
# This block overrides the default CC (usually 'cc') with a platform‑suitable
# compiler if CC was not explicitly set by the user (e.g., via environment).
# Matches CMake's auto‑detection of MSVC on Windows.
ifeq ($(origin CC),default)
    ifeq ($(IS_WIN),1)
        # On Windows, default to Microsoft C compiler (cl.exe) – this matches
        # the behavior of Visual Studio Developer Command Prompt.
        CC := cl
    else
        # On Unix/Linux/macOS, default to GCC (GNU Compiler Collection).
        # Clang users can override with `make CC=clang`.
        CC := gcc
    endif
endif

# Detect if we are using MSVC (Microsoft Visual C++) based on the compiler
# executable name containing 'cl' (case‑insensitive substring search).
# This is needed because MSVC has completely different flag syntax (/Ox vs -O3).
# Analogous to CMake's if(MSVC) condition.
ifneq ($(findstring cl,$(notdir $(CC))),)
    MSVC := 1
endif

# -----------------------------------------------------------------------------
# Metadata Extraction from C Header (Shared with CMake)
# -----------------------------------------------------------------------------
# The metadata.c file contains #define constants for product information.
# This file is included in the source tree; the build system extracts those
# macros to pass them as compiler definitions (ensures consistency between
# the build script and the actual code).
# This replicates CMake's file(READ) + string(REGEX MATCH) logic.
VERSION_H := $(SRC_DIR)/module/metadata.c

# Define a helper function 'get_macro' that:
# 1. Greps the header for a line like '#define MACRO_NAME "value"'
# 2. Uses sed to extract the quoted string content.
# The $(shell ...) runs these commands at makefile parse time (once).
# Equivalent to CMake's get_version_macro function using regex matching.
define get_macro
$(shell grep -E "#define[[:space:]]+$(1)[[:space:]]+\".*\"" $(VERSION_H) | sed -E 's/.*"([^"]*)".*/\1/')
endef

# Extract each field – these will be used as preprocessor definitions.
# These values are passed as -D (or /D) flags, matching CMake's
# target_compile_definitions.
PRODUCT_NAME     := $(call get_macro,SECURE_WIPE_NAME)
FILE_DESCRIPTION := $(call get_macro,SECURE_WIPE_DESCRIPTION)
PRODUCT_VERSION  := $(call get_macro,SECURE_WIPE_VERSION)
LICENSE          := $(call get_macro,SECURE_WIPE_LICENSE)
COPYRIGHT        := $(call get_macro,SECURE_WIPE_COPYRIGHT)
AUTHOR           := $(call get_macro,SECURE_WIPE_AUTHOR)
CONTACT          := $(call get_macro,SECURE_WIPE_CONTACT)
HOMEPAGE         := $(call get_macro,SECURE_WIPE_HOMEPAGE)

# -----------------------------------------------------------------------------
# Compiler and Linker Flags – Branch by MSVC vs. GCC/Clang
# -----------------------------------------------------------------------------
# This section exactly mirrors the platform‑specific flag logic from CMake.
ifeq ($(MSVC),1)
    # ========== Microsoft Visual C++ (Windows) ==========
    # Base compile flags (common to both Debug and Release):
    # /nologo   – Suppress compiler copyright banner.
    # /TC       – Force all files to be compiled as C (not C++).
    # /utf-8    – Set source character set and execution character set to UTF-8.
    CFLAGS  := /nologo /TC /utf-8

    # Preprocessor definitions: each -D flag passes a quoted string macro.
    # The extra backslashes escape the quotes within the make variable expansion.
    # This results in: -DPRODUCT_NAME="\"SecureWipe\"".
    DEFS    := /DPRODUCT_NAME="\"$(PRODUCT_NAME)\"" /DFILE_DESCRIPTION="\"$(FILE_DESCRIPTION)\"" \
               /DPRODUCT_VERSION="\"$(PRODUCT_VERSION)\"" /DLICENSE="\"$(LICENSE)\"" \
               /DCOPYRIGHT="\"$(COPYRIGHT)\"" /DAUTHOR="\"$(AUTHOR)\"" \
               /DCONTACT="\"$(CONTACT)\"" /DHOMEPAGE="\"$(HOMEPAGE)\""

    # Windows system libraries required:
    # bcrypt.lib     – Cryptographic primitives (RNG for secure wipe patterns).
    # setupapi.lib   – Device enumeration (physical drives, volumes).
    # cfgmgr32.lib   – Configuration manager (disk properties, device IDs).
    # advapi32.lib   – Registry access, privilege manipulation.
    # kernel32.lib   – Core memory/file/process API.
    LIBS    := bcrypt.lib setupapi.lib cfgmgr32.lib advapi32.lib kernel32.lib

    # Additional flags based on build type:
    ifeq ($(BUILD_MODE),Release)
        # Release mode (optimized, small, no debug info):
        # /W0           – Disable all warnings (reduces noise in final build).
        # /Ox           – Maximum optimizations (favor speed).
        # /GL           – Whole Program Optimization (enables cross‑object inlining).
        # /arch:AVX2    – Generate AVX2 instructions (requires Intel Haswell or later).
        # /DNDEBUG      – Remove assert() macros (no runtime overhead).
        CFLAGS  += /W0 /Ox /GL /arch:AVX2 /DNDEBUG
        # Linker flags:
        # /LTCG         – Link Time Code Generation (exploits /GL for cross‑module opt).
        # /OPT:REF      – Eliminate unreferenced functions/data (reduces size).
        # /OPT:ICF      – Combine identical COMDAT folding (reduces size further).
        # /DEBUG:NONE   – Strip all debug information from the final executable.
        # /MANIFESTUAC  – Request administrator privilege via UAC manifest.
        #   level='requireAdministrator' – Forces elevation on Windows Vista+.
        #   uiAccess='false' – No UI automation bypass (standard app).
        LDFLAGS := /LTCG /OPT:REF /OPT:ICF /DEBUG:NONE \
                   /MANIFESTUAC:"level='requireAdministrator' uiAccess='false'"
    else
        # Debug mode (full debug info, runtime checks, no optimizations):
        # /W4           – Enable warning level 4 (catches many bugs).
        # /Od           – Disable all optimizations.
        # /Zi           – Generate Program Database (PDB) for source‑level debugging.
        # /RTC1         – Runtime error checks (stack frame, uninitialized vars).
        CFLAGS  += /W4 /Od /Zi /RTC1
        # /DEBUG        – Instruct linker to generate full debugging data in PDB.
        LDFLAGS := /DEBUG
    endif

else
    # ========== GCC / Clang (Unix, Linux, macOS, MinGW) ==========
    # Base compile flags:
    # -std=c17              – Enforce C17 standard (no GNU extensions).
    # -finput-charset=UTF-8 – Interpret source files as UTF-8.
    # -fexec-charset=UTF-8  – Store string literals as UTF-8 in the binary.
    CFLAGS  := -std=c17 -finput-charset=UTF-8 -fexec-charset=UTF-8

    # Preprocessor definitions: -DNAME="value" (nested quotes handled similarly).
    DEFS    := -DPRODUCT_NAME="\"$(PRODUCT_NAME)\"" -DFILE_DESCRIPTION="\"$(FILE_DESCRIPTION)\"" \
               -DPRODUCT_VERSION="\"$(PRODUCT_VERSION)\"" -DLICENSE="\"$(LICENSE)\"" \
               -DCOPYRIGHT="\"$(COPYRIGHT)\"" -DAUTHOR="\"$(AUTHOR)\"" \
               -DCONTACT="\"$(CONTACT)\"" -DHOMEPAGE="\"$(HOMEPAGE)\""

    # Link libraries differ between Windows (MinGW) and Unix.
    ifeq ($(IS_WIN),1)
        # MinGW (GCC on Windows) uses the same libraries as MSVC but with
        # lowercase -l flags and .lib extension replaced by import libraries.
        LIBS := -lbcrypt -lsetupapi -lcfgmgr32 -ladvapi32 -lkernel32
    else
        # Unix/Linux/macOS: only libm (math library) is required.
        # POSIX disk I/O (open(), read(), write(), ioctl()) is in libc.
        LIBS := -lm
    endif

    # Build‑type specific flags:
    ifeq ($(BUILD_MODE),Release)
        # Release mode:
        # -w                – Suppress all warnings (minimize noise).
        # -O3               – Highest optimization level (aggressive inlining, loop unrolling).
        # -flto             – Link Time Optimization (global analysis across translation units).
        # -march=native     – Tune for the host CPU (enables SSE, AVX, etc. detected at build time).
        # -DNDEBUG          – Disable assert().
        CFLAGS  += -w -O3 -flto -march=native -DNDEBUG
        # Linker flags:
        # -s                – Strip symbol table and debug info.
        # -flto             – Enable LTO also at link stage (required with compile‑time -flto).
        # -Wl,--build-id=none – Disable build‑ID generation (reduces binary size).
        LDFLAGS := -s -flto -Wl,--build-id=none
        ifeq ($(IS_MAC),1)
            # macOS (Darwin) specific: -dead_strip removes unused sections.
            # Equivalent to /OPT:REF on Windows.
            LDFLAGS += -Wl,-dead_strip
        endif
    else
        # Debug mode:
        # -Wall -Wextra -pedantic – Enable extensive warnings and ISO C strictness.
        # -O0               – No optimizations (variables preserved, easy debugging).
        # -ggdb3            – Produce GDB‑compatible debug info including macro definitions.
        CFLAGS  += -Wall -Wextra -pedantic -O0 -ggdb3
        # No special linker flags for debug (default linking is fine).
        LDFLAGS :=
    endif
endif

# -----------------------------------------------------------------------------
# Source File Discovery and Object File Mapping
# -----------------------------------------------------------------------------
# Recursively find all C source files under SRC_DIR (including subdirectories
# like module/, platform/, etc.). The find command is executed at makefile
# expansion time, which automatically picks up new files without editing.
# This is analogous to CMake's file(GLOB_RECURSE SOURCES ...).
SOURCES := $(shell find $(SRC_DIR) -name "*.c")

# Transform each source path into an object file path under OBJ_DIR.
# Example: source/main.c -> ./MakeFiles/securewipe.dir/main.o
#          source/module/wipe.c -> ./MakeFiles/securewipe.dir/module/wipe.o
# This preserves the directory hierarchy to avoid filename collisions.
# Equivalent to CMake's object file generation without explicit sorting.
OBJECTS := $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(SOURCES))

# -----------------------------------------------------------------------------
# Build Rules
# -----------------------------------------------------------------------------
# Default target: build the final executable.
all: $(FINAL_TARGET)

# Rule to link the final executable from all object files.
# This matches CMake's add_executable and target_link_libraries steps.
$(FINAL_TARGET): $(OBJECTS)
ifeq ($(MSVC),1)
    # For MSVC: use the link.exe linker explicitly.
    # $^ expands to all dependencies (all object files).
    # $@ is the target name (e.g., securewipe.exe).
    # /OUT:$@ specifies output file.
    link.exe $(LDFLAGS) /OUT:$@ $^ $(LIBS)
else
    # For GCC/Clang: use CC as the linker driver (frontend).
    $(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LIBS)
endif

# Rule to compile a single .c file into a .o file.
# The directory for the object file is created automatically.
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
    # Create the target directory (e.g., ./MakeFiles/securewipe.dir/module)
    # before compiling. '@' suppresses echoing of the mkdir command.
    @mkdir -p $(dir $@)
ifeq ($(MSVC),1)
    # MSVC compile command:
    # /c          – Compile only, no link.
    # $<          – First dependency (the .c file).
    # /Fo$@       – Output object to the target path.
    $(CC) $(CFLAGS) $(DEFS) /c $< /Fo$@
else
    # GCC/Clang compile command:
    # -c          – Compile only, no link.
    # -o $@       – Output object file.
    $(CC) $(CFLAGS) $(DEFS) -c $< -o $@
endif

# -----------------------------------------------------------------------------
# Cleanup Rule
# -----------------------------------------------------------------------------
# Remove all generated files: object directories, final executable,
# Windows debug databases (.pdb), and manifest files (.manifest).
# Matches the behavior of CMake's clean target.
clean:
    rm -rf $(OBJ_DIR) $(FINAL_TARGET) *.pdb *.manifest

# -----------------------------------------------------------------------------
# Phony Targets (not actual files)
# -----------------------------------------------------------------------------
.PHONY: all clean