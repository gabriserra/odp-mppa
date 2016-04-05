ifndef __BOOT_INCLUDED__
__BOOT_INCLUDED__ := 1

SRCDIRS  += $(dir $(realpath $(lastword $(MAKEFILE_LIST))))
_CFLAGS  += -I$(TOP_SRCDIR)/include
_LDFLAGS += -lmppapower -lmppanoc -lmpparouting \
	-Wl,--undefined=__bsync_rpc_constructor

endif
