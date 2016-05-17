SRCDIR   := $(dir $(realpath $(lastword $(MAKEFILE_LIST))))
SRCDIRS  := $(SRCDIR)
FIRMDIR := $(realpath $(ODP_TOOLCHAIN_DIR)/share/odp/build/firmware)

_LDFLAGS = -lmppapower -lmppanoc -lmpparouting

include $(FIRMDIR)/common/rpc/include.mk
include $(FIRMDIR)/common/eth/include.mk
include $(FIRMDIR)/common/boot/include.mk
include $(FIRMDIR)/common/rnd/include.mk
include $(FIRMDIR)/Makefile
