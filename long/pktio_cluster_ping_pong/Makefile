APP_DIR := $(dir $(lastword $(MAKEFILE_LIST)))
TEST_STD_FIRMWARE = $(IOPCIE_ELF)

include $(APP_DIR)/../Makefile

$(TEST_NAME)-srcs := $(APP_DIR)/cluster_ping_pong.c

# Unimplemented MPPA_DNOC_DECR_NOTIF_RELOAD_ETH mode
#$(TEST_NAME)-sim-cmd := $(IOPCIE_NAME) -c $(TEST_NAME) -c $(TEST_NAME) -c $(TEST_NAME) -c $(TEST_NAME)
ifeq ($(board),$(BOARD_explorer))
else
  $(TEST_NAME)-jtag-cmd := $(IOPCIE_NAME) -c $(TEST_NAME) -c $(TEST_NAME) -c $(TEST_NAME) -c $(TEST_NAME) -c $(TEST_NAME) -c $(TEST_NAME) -c $(TEST_NAME) -c $(TEST_NAME) -c $(TEST_NAME) -c $(TEST_NAME) -c $(TEST_NAME) -c $(TEST_NAME) -c $(TEST_NAME) -c $(TEST_NAME) -c $(TEST_NAME) -c $(TEST_NAME)
endif
$(TEST_NAME)-multibinary := $(TEST_MULTIBIN_NAME).mpk
mppa-tests += $(TEST_NAME)

include $(K1_TOOLCHAIN_DIR)/share/make/Makefile.kalray
