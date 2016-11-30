APP_DIR := $(dir $(lastword $(MAKEFILE_LIST)))
TEST_STD_FIRMWARE = $(IOPCIE_ELF)

include $(APP_DIR)/../Makefile
$(TEST_NAME)-srcs := $(APP_DIR)/odp_l2fwd.c
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
