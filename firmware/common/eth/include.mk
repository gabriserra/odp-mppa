ifndef __ETH_INCLUDED__
__ETH_INCLUDED__ := 1

SRCDIRS  += $(dir $(realpath $(lastword $(MAKEFILE_LIST))))
_CFLAGS  += -I$(TOP_SRCDIR)/include
_LDFLAGS += -lmppapower -lmppanoc -lmpparouting -lmppaeth_utils -lmppaeth_88E1111 -li2c -lsysctl -lphy \
	 -Wl,--undefined=__eth_rpc_constructor

endif
