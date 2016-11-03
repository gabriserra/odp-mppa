DIR := $(dir $(lastword $(MAKEFILE_LIST)))
TEST_NAME := $(lastword $(subst /, ,$(DIR)))

include $(DIR)/../Makefile 

VALID_TYPE=hw

ifeq ($(board),$(BOARD_explorer))


else
	$(TEST_NAME)-hw-timeout := 500

	 $(TEST_NAME)-hw-cmd := ./../../../../perf/run_single ./../../../../tests/$(ODP_FLAVOUR)/$(OS_PLAT_NAME)/test/performance/odp_atomic.kelf --perf-file "\$$$$ENV{perf_files_path}"/$(OS_BOARD_NAME)/$(OS_PLAT_NAME)/$(TEST_NAME).perf --regex-file ./../../../../perf/$(TEST_NAME)/regex --args t [1]

	$(TEST_NAME)-hw-no-remote := 1

endif
$(TEST_NAME)-labels += jtag

NO_INSTALL_BINARY := 1
host-tests += $(TEST_NAME)

include $(K1_TOOLCHAIN_DIR)/share/make/Makefile.kalray
