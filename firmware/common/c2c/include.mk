ifndef __C2C_INCLUDED__
__C2C_INCLUDED__ := 1

SRCDIRS  += $(dir $(realpath $(lastword $(MAKEFILE_LIST))))
_CFLAGS  += -I$(TOP_SRCDIR)/include
_LDFLAGS += -Wl,--undefined=__c2c_rpc_constructor

endif
