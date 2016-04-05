ifndef __RPC_INCLUDED__
__RPC_INCLUDED__ := 1

SRCDIRS  += $(dir $(realpath $(lastword $(MAKEFILE_LIST))))
_CFLAGS  += -DRPC_FIRMWARE
_LDFLAGS += -Wl,--undefined=__bas_rpc_constructor

endif
