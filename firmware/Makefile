FIRMDIR    := $(shell basename $(SRCDIR))
BUILDDIR   := $(shell pwd)
MAKEFILE   := $(lastword $(MAKEFILE_LIST))
FIRM_TOPDIR:= $(realpath $(dir $(MAKEFILE)))
TOP_SRCDIR := $(realpath $(dir $(MAKEFILE))/..)
SRCFILES   += $(shell find $(SRCDIRS) -name "[^.]*.c")
ASMFILES   += $(shell find $(SRCDIRS) -name "[^.]*.S")
HDRFILES   += $(shell find $(SRCDIRS) -name "[^.]*.h")
OBJFILES   += $(patsubst %.c, $(BUILDDIR)/%.o, $(SRCFILES))
ASMOBJFILES+= $(patsubst %.S, $(BUILDDIR)/%.o, $(ASMFILES))
_EXTRA_INST:= $(patsubst $(BUILDDIR)/%,$(INSTDIR)/%,$(EXTRA_BINS))

ODP_TOOLCHAIN_DIR ?= $(K1_TOOLCHAIN_DIR)
include $(ODP_TOOLCHAIN_DIR)/share/odp/build/mk/platforms.inc
ODP_FIRMWARE_CONFIG ?= k1b-kalray_konic80

# Inherit flags from platforms.inc
CC            = $(firmware_$(ODP_FIRMWARE_CONFIG)_CC)
PLAT_CFLAGS   = $(firmware_$(ODP_FIRMWARE_CONFIG)_CFLAGS)
PLAT_LDFLAGS  = $(firmware_$(ODP_FIRMWARE_CONFIG)_CFLAGS)

# Add extra include to get ODP definitions
ODP_INCDIR  = $(ODP_TOOLCHAIN_DIR)/k1-elf/include
_CFLAGS    += -I$(ODP_INCDIR) -I$(FIRM_TOPDIR)/common
_CFLAGS    += $(CFLAGS) -g3 -Wall -Wextra -Werror -std=gnu99 $(patsubst %, -I%, $(SRCDIRS))
_LDFLAGS   += $(LDFLAGS)

ifdef DEBUG
_CFLAGS    += -O0
else
_CFLAGS    += -O2
endif
ifdef VERBOSE
_CFLAGS += -DVERBOSE
endif
ifdef NETDEV_VERBOSE
_CFLAGS += -DNETDEV_VERBOSE
endif

ifndef FIRMWARE_VERSION
include $(FIRM_TOPDIR)/../firmware-version.mk
endif
_CFLAGS += -DFIRMWARE_VERSION='"$(FIRMWARE_VERSION)"'

ifndef INCLUDING_FIRMWARE

ifdef BUILD_LIBRARY
LIB      := $(BUILDDIR)/lib$(FIRMDIR).a
INSTBIN  := $(INSTDIR)/lib$(FIRMDIR).a
else
BIN      := $(BUILDDIR)/$(FIRMDIR).kelf
INSTBIN  := $(INSTDIR)/$(FIRMDIR).kelf
endif
all: $(BIN) $(LIB) $(EXTRA_BINS)

$(BIN): $(OBJFILES) $(ASMOBJFILES)
	$(CC) -o $@ $^ $(PLAT_CFLAGS) $(_CFLAGS) $(PLAT_LDFLAGS) $(_LDFLAGS)
$(LIB): $(OBJFILES) $(ASMOBJFILES)
	ar rc $@ $^

$(OBJFILES): $(BUILDDIR)/%.o: %.c $(HDRFILES)
	@mkdir -p $$(dirname $@) || true
	$(CC) -o $@ -c $< $(PLAT_CFLAGS) $(_CFLAGS)

$(ASMOBJFILES): $(BUILDDIR)/%.o: %.S $(HDRFILES)
	@mkdir -p $$(dirname $@) || true
	$(CC) -o $@ -c $< $(PLAT_CFLAGS) $(_CFLAGS)

install: $(INSTBIN) $(INSTLIB) $(_EXTRA_INST) $(EXTRA_INST)
$(INSTBIN): $(BIN)
	if [ "$(INSTDIR)" == "" ]; then echo "ERROR: INSTDIR not set" && exit 1; fi
	install -D $< $@
$(INSTLIB): $(LIB)
	if [ "$(INSTDIR)" == "" ]; then echo "ERROR: INSTDIR not set" && exit 1; fi
	install -D $< $@

$(_EXTRA_INST):$(INSTDIR)/%:$(BUILDDIR)/%
	if [ "$(INSTDIR)" == "" ]; then echo "ERROR: INSTDIR not set" && exit 1; fi
	install -D $< $@


valid:
long:

clean:
	rm -f $(OBJFILES) $(ASMOBJFILES) $(BIN)
endif
