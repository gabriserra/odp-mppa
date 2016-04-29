ifndef __NETDEV_INCLUDED__
__NETDEV_INCLUDED__ := 1

NETDEVDIR := $(dir $(realpath $(lastword $(MAKEFILE_LIST))))
SRCDIRS   += $(NETDEVDIR)

ifdef MPPAPCIE_DIR
_CFLAGS  += -I$(MPPAPCIE_DIR)/include/
else
_CFLAGS  += -I$($(realpath $(K1_TOOLCHAIN_DIR))/../..//src/k1-mppapcie-dkms-2.2.0/inclue)
endif
_CFLAGS   += -I$(TOP_SRCDIR)/mppaeth
_LDFLAGS  += -lpcie_queue

endif
