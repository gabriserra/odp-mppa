obj-m += mppapcie_odp.o

TOOLCHAIN_DIR ?= $(K1_TOOLCHAIN_DIR)
KERNELBUILD_DIR ?= /lib/modules/$(shell uname -r)/build
MPPAPCIE_INCLUDE_DIR ?= $(shell readlink -f $$(readlink -f $${K1_TOOLCHAIN_DIR})/../../src/k1-mppapcie-dkms*/include)

ifneq ($(MPPAPCIE_INCLUDE_DIR),)
MPPA_EXTRA_SYMBOLS = $(shell find $(MPPAPCIE_INCLUDE_DIR)/.. -name Module.symvers)
endif
ifneq ($(MPPA_EXTRA_SYMBOLS),)
KBUILD_EXTRA_SYMBOLS += $(MPPA_EXTRA_SYMBOLS)
endif

EXTRA_CFLAGS += -I$(PWD) -I$(MPPAPCIE_INCLUDE_DIR) -DCONFIG_K1_PCIE_ETH_MAX_IF=$(CONFIG_K1_PCIE_ETH_MAX_IF) -Wall -Werror

DEBUG ?= 0
ifeq ($(DEBUG), 1)
	EXTRA_CFLAGS += -DDEBUG
endif

ifeq ($(CONFIG_MPPA_TRACE), 1)
	EXTRA_CFLAGS += -fno-inline
endif

mppapcie_odp-objs += odp.o

all:
	@if [ "$(MPPAPCIE_INCLUDE_DIR)" == "" ]; then echo "Could not find MPPAPCIE include. Please set MPPAPCIE_INCLUDE_DIR"; exit 1; fi
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
