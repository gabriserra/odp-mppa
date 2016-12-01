APP_DIR := $(dir $(lastword $(MAKEFILE_LIST)))
LONG_NO_DEFAULT_RULES := 1

include $(APP_DIR)/../Makefile

VALID_TYPE=hw
$(TEST_NAME)-scripts += ../../perf/run_single ../../perf/$(TEST_NAME)/regex
$(TEST_NAME)-extras  += $(ODP_TOOLCHAIN_DIR)/share/odp/tests/$(ODP_FLAVOUR)/$(OS_PLAT_NAME)/test/performance/odp_atomic.kelf

ifeq ($(board),$(BOARD_explorer))

else
	$(TEST_NAME)-hw-timeout := 500
	$(TEST_NAME)-hw-cmd := run_single odp_atomic.kelf --perf-file "\$$$$ENV{perf_files_path}"/$(OS_BOARD_NAME)/$(OS_PLAT_NAME)/$(TEST_NAME).perf --regex-file ./regex --args t [1]

	$(TEST_NAME)-hw-no-remote := 1

endif
$(TEST_NAME)-labels += jtag

NO_INSTALL_BINARY := 1
host-tests += $(TEST_NAME)

include $(K1_TOOLCHAIN_DIR)/share/make/Makefile.kalray
