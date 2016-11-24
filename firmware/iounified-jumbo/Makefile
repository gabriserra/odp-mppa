SRCDIR   := $(dir $(realpath $(lastword $(MAKEFILE_LIST))))
SRCDIRS  := $(SRCDIR)

_LDFLAGS = -lmppapower -lmppanoc -lmpparouting

include $(SRCDIR)/../common/rpc/include.mk
include $(SRCDIR)/../common/pcie/include.mk
include $(SRCDIR)/../common/eth/include.mk
include $(SRCDIR)/../common/rnd/include.mk
include $(SRCDIR)/../common/c2c/include.mk
include $(SRCDIR)/../common/boot/include.mk
include $(SRCDIR)/../Makefile
