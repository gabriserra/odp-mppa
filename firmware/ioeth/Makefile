SRCDIR   := $(dir $(realpath $(lastword $(MAKEFILE_LIST))))
SRCDIRS  := $(SRCDIR)

_LDFLAGS = -lmppapower -lmppanoc -lmpparouting
EXTRA_BINS += $(BUILDDIR)/$(FIRMDIR).bin

include $(SRCDIR)/../common/rpc/include.mk
include $(SRCDIR)/../common/c2c/include.mk
include $(SRCDIR)/../common/eth/include.mk
include $(SRCDIR)/../common/rnd/include.mk
include $(SRCDIR)/../common/ioeth-bsp/include.mk
include $(SRCDIR)/../Makefile

$(BUILDDIR)/$(FIRMDIR).bin: $(BUILDDIR)/$(FIRMDIR).kelf
	k1-objcopy -Obinary $< $@
