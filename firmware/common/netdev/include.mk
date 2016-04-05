ifndef __NETDEV_INCLUDED__
__NETDEV_INCLUDED__ := 1

NETDEVDIR := $(dir $(realpath $(lastword $(MAKEFILE_LIST))))
SRCDIRS   += $(NETDEVDIR)

_CFLAGS   += -I$(realpath $(K1_TOOLCHAIN_DIR))/../..//src/k1-mppapcie-dkms-2.2.0/mppapcie_netdev/
_CFLAGS   += -I$(TOP_SRCDIR)/mppaeth
_LDFLAGS  += -lpcie_queue

endif
