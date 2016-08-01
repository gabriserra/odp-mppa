DIR := $(dir $(lastword $(MAKEFILE_LIST)))
TEST_NAME := $(lastword $(subst /, ,$(DIR)))

include $(DIR)/../Makefile 
$(TEST_NAME)-srcs := $(DIR)/odp_l2fwd.c
$(TEST_NAME)-cflags += -I$(DIR)
cluster-bin += $(TEST_NAME)

$(TEST_NAME)_multibin-objs := $(IOPCIE_ELF) $(TEST_NAME)
mppa-bin += $(TEST_NAME)_multibin

$(TEST_NAME)-multibinary := $(TEST_NAME)_multibin.mpk
$(TEST_NAME)-scripts := h2c_bw.py functions.sh parameters.sh pktgen_sample01_simple.sh

ifeq ($(board),$(BOARD_konic80))
# see parameters in firmware/main.c
tests-env := OS_PLAT_NAME=$(OS_PLAT_NAME)
$(TEST_NAME)-hw-cmd := h2c_bw.py --perf-file "\$$$$ENV{perf_files_path}"/$(OS_BOARD_NAME)/$(OS_PLAT_NAME)/$(TEST_NAME).perf
$(TEST_NAME)-labels := jtag
$(TEST_NAME)-hw-no-remote := 1
$(TEST_NAME)-hw-timeout := 90
host-tests += $(TEST_NAME)
endif
include $(K1_TOOLCHAIN_DIR)/share/make/Makefile.kalray
