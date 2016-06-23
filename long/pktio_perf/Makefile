DIR := $(dir $(lastword $(MAKEFILE_LIST)))
TEST_NAME := $(lastword $(subst /, ,$(DIR)))

include $(DIR)/../Makefile 

VALID_TYPE=hw

ifeq ($(board),$(BOARD_explorer))


else
	$(TEST_NAME)-hw-timeout := 500
	$(TEST_NAME)-hw-cmd := $(DIR)/../../perf/run_single $(DIR)/../../install/local/k1tools/share/odp/tests/$(OS_BOARD_NAME)/$(OS_PLAT_NAME)/test/performance/odp_pktio_perf.kelf --perf-file $(PERF_DIR)/$(OS_BOARD_NAME)/$(OS_PLAT_NAME)/$(TEST_NAME).perf --regex-file $(DIR)/../../long/pktio_perf/regex --args duration [1] poll [Y]
	$(TEST_NAME)-hw-no-remote := 1

endif
$(TEST_NAME)-labels += jtag

NO_INSTALL_BINARY := 1
host-tests += $(TEST_NAME)

include $(K1_TOOLCHAIN_DIR)/share/make/Makefile.kalray
