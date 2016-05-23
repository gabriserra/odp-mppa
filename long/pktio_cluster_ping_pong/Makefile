DIR := $(dir $(lastword $(MAKEFILE_LIST)))
TEST_NAME := $(lastword $(subst /, ,$(DIR)))

include $(DIR)/../Makefile 
$(TEST_NAME)-srcs := $(DIR)/cluster_ping_pong.c
cluster-bin += $(TEST_NAME)

$(TEST_NAME)_multibin-objs := $(IOPCIE_ELF) $(TEST_NAME)
mppa-bin += $(TEST_NAME)_multibin

# Unimplemented MPPA_DNOC_DECR_NOTIF_RELOAD_ETH mode
#$(TEST_NAME)-sim-cmd := $(IOPCIE_NAME) -c $(TEST_NAME) -c $(TEST_NAME) -c $(TEST_NAME) -c $(TEST_NAME)
ifeq ($(board),$(BOARD_explorer))

else
	$(TEST_NAME)-jtag-cmd := $(IOPCIE_NAME) -c $(TEST_NAME) -c $(TEST_NAME) -c $(TEST_NAME) -c $(TEST_NAME) -c $(TEST_NAME) -c $(TEST_NAME) -c $(TEST_NAME) -c $(TEST_NAME) -c $(TEST_NAME) -c $(TEST_NAME) -c $(TEST_NAME) -c $(TEST_NAME) -c $(TEST_NAME) -c $(TEST_NAME) -c $(TEST_NAME) -c $(TEST_NAME)
endif
$(TEST_NAME)-multibinary := $(TEST_NAME)_multibin.mpk
mppa-tests += $(TEST_NAME)
include $(K1_TOOLCHAIN_DIR)/share/make/Makefile.kalray
