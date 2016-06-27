DIR := $(dir $(lastword $(MAKEFILE_LIST)))
TEST_NAME := $(lastword $(subst /, ,$(DIR)))

include $(DIR)/../Makefile 
$(TEST_NAME)-srcs := $(DIR)/odp_l2fwd.c
$(TEST_NAME)-cflags += -I$(DIR)
cluster-bin += $(TEST_NAME)

$(TEST_NAME)_multibin-objs := $(IOPCIE_ELF) $(TEST_NAME)
mppa-bin += $(TEST_NAME)_multibin

#$(TEST_NAME)-sim-cmd := $(IOPCIE_NAME) -c $(TEST_NAME)
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
$(TEST_NAME)-hw-timeout := 120
host-tests += $(TEST_NAME)
endif
include $(K1_TOOLCHAIN_DIR)/share/make/Makefile.kalray
