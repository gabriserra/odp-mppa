ifndef __PCIE_INCLUDED__
__PCIE_INCLUDED__ := 1

SRCDIRS  += $(dir $(realpath $(lastword $(MAKEFILE_LIST))))
_CFLAGS  += -I$(realpath $(K1_TOOLCHAIN_DIR))/../..//src/k1-mppapcie-dkms-2.2.0/mppapcie_netdev/
_LDFLAGS += -Wl,--undefined=__pcie_rpc_constructor

endif
