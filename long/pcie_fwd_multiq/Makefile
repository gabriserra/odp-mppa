DIR := $(dir $(lastword $(MAKEFILE_LIST)))
TEST_NAME := $(lastword $(subst /, ,$(DIR)))

include $(DIR)/../Makefile 
$(TEST_NAME)-srcs := $(DIR)/odp_l2fwd.c
$(TEST_NAME)-cflags += -I$(DIR)
cluster-bin += $(TEST_NAME)

pre-build-hooks := build-firmware
clean-hooks := clean-firmware

IOPCIE_MQ_ELF := $(DIR)/firmware-$(OS_BOARD_NAME)/firmware.kelf
$(IOPCIE_MQ_ELF)-nameinmpk := firmware
build-firmware:
	+mkdir -p $(DIR)/firmware-$(OS_BOARD_NAME) && cd $(DIR)/firmware-$(OS_BOARD_NAME) && \
	$(MAKE) -f $(DIR)/firmware/Makefile ODP_FIRMWARE_CONFIG="k1b-kalray_$(OS_BOARD_NAME)" \
	ODP_TOOLCHAIN_DIR="$(ODP_TOOLCHAIN_DIR)" all

clean-firmware:
	if [ -d $(DIR)/firmware-$(OS_BOARD_NAME) ]; then cd $(DIR)/firmware-$(OS_BOARD_NAME) && \
	$(MAKE) -f $(DIR)/firmware/Makefile ODP_FIRMWARE_CONFIG="k1b-kalray_$(OS_BOARD_NAME)" \
	ODP_TOOLCHAIN_DIR="$(ODP_TOOLCHAIN_DIR)" clean; else true; fi


$(TEST_NAME)_multibin-objs := $(IOPCIE_MQ_ELF) $(TEST_NAME)
mppa-bin += $(TEST_NAME)_multibin

ifeq ($(board),$(BOARD_explorer))
else
	$(TEST_NAME)-jtag-cmd := $(IOPCIE_NAME) -c $(TEST_NAME)
endif
$(TEST_NAME)-multibinary := $(TEST_NAME)_multibin.mpk
$(TEST_NAME)-scripts := run.sh

ifeq ($(board),$(BOARD_konic80))
# see parameters in firmware/main.c
$(TEST_NAME)-hw-cmd := run.sh
$(TEST_NAME)-labels := jtag
$(TEST_NAME)-hw-no-remote := 1
host-tests += $(TEST_NAME)
endif
include $(K1_TOOLCHAIN_DIR)/share/make/Makefile.kalray
