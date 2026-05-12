BUILD ?= release
TARGET := securewipe

ifeq ($(BUILD),debug)
    BUILD_MODE := Debug
else
    BUILD_MODE := Release
endif

SRC_DIR  := source
OBJ_DIR  := ./MakeFiles/$(TARGET).dir

ifeq ($(OS),Windows_NT)
    IS_WIN := 1
    EXE_EXT := .exe
else
    UNAME_S := $(shell uname -s)
    ifeq ($(UNAME_S),Darwin)
        IS_MAC := 1
    endif
    EXE_EXT :=
endif

FINAL_TARGET := $(TARGET)$(EXE_EXT)

ifeq ($(origin CC),default)
    ifeq ($(IS_WIN),1)
        CC := cl
    else
        CC := gcc
    endif
endif

ifneq ($(findstring cl,$(notdir $(CC))),)
    MSVC := 1
endif

VERSION_H := $(SRC_DIR)/module/metadata.c

define get_macro
$(shell grep -E "#define[[:space:]]+$(1)[[:space:]]+\".*\"" $(VERSION_H) | sed -E 's/.*"([^"]*)".*/\1/')
endef

PRODUCT_NAME     := $(call get_macro,SECURE_WIPE_NAME)
FILE_DESCRIPTION := $(call get_macro,SECURE_WIPE_DESCRIPTION)
PRODUCT_VERSION  := $(call get_macro,SECURE_WIPE_VERSION)
LICENSE          := $(call get_macro,SECURE_WIPE_LICENSE)
COPYRIGHT        := $(call get_macro,SECURE_WIPE_COPYRIGHT)
AUTHOR           := $(call get_macro,SECURE_WIPE_AUTHOR)
CONTACT          := $(call get_macro,SECURE_WIPE_CONTACT)
HOMEPAGE         := $(call get_macro,SECURE_WIPE_HOMEPAGE)

ifeq ($(MSVC),1)
    CFLAGS  := /nologo /TC /utf-8
    DEFS    := /DPRODUCT_NAME="\"$(PRODUCT_NAME)\"" /DFILE_DESCRIPTION="\"$(FILE_DESCRIPTION)\"" \
               /DPRODUCT_VERSION="\"$(PRODUCT_VERSION)\"" /DLICENSE="\"$(LICENSE)\"" \
               /DCOPYRIGHT="\"$(COPYRIGHT)\"" /DAUTHOR="\"$(AUTHOR)\"" \
               /DCONTACT="\"$(CONTACT)\"" /DHOMEPAGE="\"$(HOMEPAGE)\""
    LIBS    := bcrypt.lib setupapi.lib cfgmgr32.lib advapi32.lib kernel32.lib

    ifeq ($(BUILD_MODE),Release)
        CFLAGS  += /W0 /Ox /GL /arch:AVX2 /DNDEBUG
        LDFLAGS := /LTCG /OPT:REF /OPT:ICF /DEBUG:NONE \
                   /MANIFESTUAC:"level='requireAdministrator' uiAccess='false'"
    else
        CFLAGS  += /W4 /Od /Zi /RTC1
        LDFLAGS := /DEBUG
    endif
else
    CFLAGS  := -std=c17 -finput-charset=UTF-8 -fexec-charset=UTF-8
    DEFS    := -DPRODUCT_NAME="\"$(PRODUCT_NAME)\"" -DFILE_DESCRIPTION="\"$(FILE_DESCRIPTION)\"" \
               -DPRODUCT_VERSION="\"$(PRODUCT_VERSION)\"" -DLICENSE="\"$(LICENSE)\"" \
               -DCOPYRIGHT="\"$(COPYRIGHT)\"" -DAUTHOR="\"$(AUTHOR)\"" \
               -DCONTACT="\"$(CONTACT)\"" -DHOMEPAGE="\"$(HOMEPAGE)\""

    ifeq ($(IS_WIN),1)
        LIBS := -lbcrypt -lsetupapi -lcfgmgr32 -ladvapi32 -lkernel32
    else
        LIBS := -lm
    endif

    ifeq ($(BUILD_MODE),Release)
        CFLAGS  += -w -O3 -flto -march=native -DNDEBUG
        LDFLAGS := -s -flto -Wl,--build-id=none
        ifeq ($(IS_MAC),1)
            LDFLAGS += -Wl,-dead_strip
        endif
    else
        CFLAGS  += -Wall -Wextra -pedantic -O0 -ggdb3
        LDFLAGS :=
    endif
endif

SOURCES := $(shell find $(SRC_DIR) -name "*.c")
OBJECTS := $(patsubst $(SRC_DIR)/%.c, $(OBJ_DIR)/%.o, $(SOURCES))

all: $(FINAL_TARGET)

$(FINAL_TARGET): $(OBJECTS)
ifeq ($(MSVC),1)
    link.exe $(LDFLAGS) /OUT:$@ $^ $(LIBS)
else
    $(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS) $(LIBS)
endif

$(OBJ_DIR)/%.o: $(SRC_DIR)/%.c
    @mkdir -p $(dir $@)
ifeq ($(MSVC),1)
    $(CC) $(CFLAGS) $(DEFS) /c $< /Fo$@
else
    $(CC) $(CFLAGS) $(DEFS) -c $< -o $@
endif

clean:
    rm -rf $(OBJ_DIR) $(FINAL_TARGET) *.pdb *.manifest

.PHONY: all clean