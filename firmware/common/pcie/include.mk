ifndef __PCIE_INCLUDED__
__PCIE_INCLUDED__ := 1

PCIEDIR := $(dir $(realpath $(lastword $(MAKEFILE_LIST))))

SRCDIRS  += $(PCIEDIR)
ifdef MPPAPCIE_DIR
_CFLAGS  += -I$(MPPAPCIE_DIR)/mppapcie_netdev/
else
_CFLAGS  += -I$(realpath $(K1_TOOLCHAIN_DIR))/../..//src/k1-mppapcie-dkms-2.2.0/mppapcie_netdev/
endif
_LDFLAGS += -Wl,--undefined=__pcie_rpc_constructor

include $(PCIEDIR)/../netdev/include.mk

endif
