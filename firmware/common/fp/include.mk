ifndef __FP_INCLUDED__
__FP_INCLUDED__ := 1

SRCDIRS  += $(dir $(realpath $(lastword $(MAKEFILE_LIST))))
_CFLAGS  += -I$(TOP_SRCDIR)/include
_LDFLAGS += -Wl,--undefined=__fp_rpc_constructor

endif
