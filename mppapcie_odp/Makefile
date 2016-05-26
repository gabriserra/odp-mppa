obj-m += mppapcie_odp.o

TOOLCHAIN_DIR ?= $(K1_TOOLCHAIN_DIR)
KERNELBUILD_DIR ?= /lib/modules/$(shell uname -r)/build
K1_TOOLCHAIN_DIR ?= /usr/local/k1tools

# 3 choices to fetch the PCIe driver
# * Custom build
# * KalrayEnv
# * Installed DKMS

ifneq ($(MPPAPCIE_DIR),)
# Set by user
MPPAPCIE_INCLUDE_DIR := $(shell readlink -f $(MPPAPCIE_DIR)/include)
MPPA_EXTRA_SYMBOLS   := $(shell find $(MPPAPCIE_DIR)/ -name Module.symvers)
else
# Check for DKMS first
MPPAPCIE_DIR ?= $(shell readlink -e /usr/src/k1-mppapcie-dkms*)

ifneq ($(MPPAPCIE_DIR),)
# Use the found DKMS
MPPAPCIE_INCLUDE_DIR := $(MPPAPCIE_DIR)/include
MPPAPCIE_VERSION     := $(shell basename $$(readlink -e /usr/src/k1-mppapcie-dkms*) | sed -e 's/k1-mppapcie-dkms-//')
MPPA_EXTRA_SYMBOLS   := $(shell readlink -e /var/lib/dkms/k1-mppapcie-dkms/$(MPPAPCIE_VERSION)/$$(uname -r)/$$(uname -m)/module/Module.symvers)
else
# Use the driver from KENV
MPPAPCIE_INCLUDE_DIR ?= $(shell readlink -f $$(readlink -m $$(readlink -m $(K1_TOOLCHAIN_DIR))/../../src/k1-mppapcie-dkms*/include))
MPPA_EXTRA_SYMBOLS = $(shell find $(MPPAPCIE_INCLUDE_DIR)/.. -name Module.symvers)
endif
endif

EXTRA_CFLAGS += -I$(PWD) -I$(MPPAPCIE_INCLUDE_DIR) -DCONFIG_K1_PCIE_ETH_MAX_IF=$(CONFIG_K1_PCIE_ETH_MAX_IF) -Wall -Werror

DEBUG ?= 0
ifeq ($(DEBUG), 1)
	EXTRA_CFLAGS += -DDEBUG
endif

ifeq ($(CONFIG_MPPA_TRACE), 1)
	EXTRA_CFLAGS += -fno-inline
endif

mppapcie_odp-objs += odp.o odp_rx.o odp_tx.o

all:
	if [ "$(MPPAPCIE_INCLUDE_DIR)" == "" ]; then echo "Could not find MPPAPCIE include. Please set MPPAPCIE_DIR"; exit 1; fi
	if [ "$(MPPA_EXTRA_SYMBOLS)" == "" ]; then echo "Could not find MPPAPCIE Module.symvers include. Please set MPPAPCIE_DIR and build the mppapcie module"; exit 1; fi
	@echo "---------------------------------------------------------------------"
ifeq ($(DEBUG), 1)
	@echo "		   DEBUG is enabled, GCOV support enabled"
else
	@echo "		DEBUG is not enabled (make DEBUG=1 to enable)"
endif
	@echo "---------------------------------------------------------------------"
	$(MAKE) -C $(KERNELBUILD_DIR) M=$(PWD) modules

clean:
	$(MAKE) -C $(KERNELBUILD_DIR) M=$(PWD) clean
