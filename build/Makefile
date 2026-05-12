BUILD ?= release
ifeq ($(BUILD),debug)
BUILD_MODE := Debug
else ifeq ($(BUILD),release)
BUILD_MODE := Release
else
$(error BUILD must be either 'debug' or 'release' (default: release))
endif
TARGET = securewipe
SRC_DIR = source
OBJ_DIR = ./MakeFiles/$(TARGET).dir
SOURCES := $(shell LC_ALL=C find $(SRC_DIR) -name "*.c" | LC_ALL=C sort)
OBJECTS := $(addprefix $(OBJ_DIR)/,$(patsubst $(SRC_DIR)/%,%,$(SOURCES:.c=.c.o)))
ifeq ($(origin CC),default)
CC := $(shell command -v gcc 2>/dev/null || command -v clang 2>/dev/null || echo "none")
ifeq ($(CC),none)
$(error Neither gcc nor clang found. Please install a C compiler.)
endif
endif
CFLAGS = -std=c11
LDFLAGS =
LIBS =
PKG_CONFIG := $(shell command -v pkg-config 2>/dev/null)
OS := $(shell uname -s)
PLATFORM_LIBS_FOUND := 0
ifeq ($(OS),Windows_NT)
TARGET := securewipe.exe
PLATFORM_LIBS_NAMES := -lbcrypt -lsetupapi -lcfgmgr32 -ladvapi32 -lkernel32
PLATFORM_LIBS_FOUND := 1
else
ifdef PKG_CONFIG
PLATFORM_CFLAGS := $(shell $(PKG_CONFIG) --cflags glib-2.0 2>/dev/null)
PLATFORM_LIBS   := $(shell $(PKG_CONFIG) --libs   glib-2.0 2>/dev/null)
ifneq ($(PLATFORM_LIBS),)
CFLAGS += $(PLATFORM_CFLAGS)
PLATFORM_LIBS_NAMES := $(PLATFORM_LIBS)
PLATFORM_LIBS_FOUND := 1
endif
endif
ifndef PLATFORM_LIBS_NAMES
PLATFORM_LIBS_NAMES := 
endif
PLATFORM_LIBS_FOUND := 1
endif
ifeq ($(PLATFORM_LIBS_FOUND),0)
$(error Platform libraries not found.)
endif
LIBS += $(PLATFORM_LIBS_NAMES)
CFLAGS += -Wall -Wextra -pedantic -finput-charset=UTF-8 -fexec-charset=UTF-8
ifeq ($(BUILD),debug)
SRC_ABS := $(abspath $(SRC_DIR))
CUR_ABS := $(CURDIR)
CFLAGS += -O0 -g \
-fdebug-prefix-map=$(SRC_ABS)=. \
-fdebug-prefix-map=$(SRC_DIR)=. \
-fdebug-prefix-map=$(CUR_ABS)=. \
-fno-ident \
-gno-record-gcc-switches
else ifeq ($(BUILD),release)
CFLAGS += -O2 -DNDEBUG -g0
LDFLAGS += -s -Wl,--build-id=none
endif
VERSION_H = $(SRC_DIR)/module/metadata.c
escape_quotes = $(subst ",\",$(1))
PRODUCT_NAME     := $(shell grep -E "#define[[:space:]]+SECURE_WIPE_NAME[[:space:]]+\".*\"" $(VERSION_H) | sed -E 's/#define[[:space:]]+SECURE_WIPE_NAME[[:space:]]+\"([^\"]*)\".*/\1/')
PRODUCT_VERSION  := $(shell grep -E "#define[[:space:]]+SECURE_WIPE_VERSION[[:space:]]+\".*\"" $(VERSION_H) | sed -E 's/#define[[:space:]]+SECURE_WIPE_VERSION[[:space:]]+\"([^\"]*)\".*/\1/')
AUTHOR           := $(shell grep -E "#define[[:space:]]+SECURE_WIPE_AUTHOR[[:space:]]+\".*\"" $(VERSION_H) | sed -E 's/#define[[:space:]]+SECURE_WIPE_AUTHOR[[:space:]]+\"([^\"]*)\".*/\1/')
CFLAGS += \
-DPRODUCT_NAME="\"$(call escape_quotes,$(PRODUCT_NAME))\"" \
-DPRODUCT_VERSION="\"$(call escape_quotes,$(PRODUCT_VERSION))\"" \
-DAUTHOR="\"$(call escape_quotes,$(AUTHOR))\""
vpath %.c $(SRC_DIR)
vpath %.c $(SRC_DIR)/module
$(shell mkdir -p $(OBJ_DIR))
all: $(TARGET)
$(TARGET): $(OBJECTS)
	$(CC) -o $@ $^ $(LIBS) $(LDFLAGS)
$(OBJ_DIR)/%.c.o: %.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -c $< -o $@
clean:
	rm -rf $(OBJ_DIR) $(TARGET)
rebuild: clean all
run: all
	./$(TARGET)
.PHONY: all clean rebuild run