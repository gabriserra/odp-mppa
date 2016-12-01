APP_DIR := $(dir $(lastword $(MAKEFILE_LIST)))
TEST_STD_FIRMWARE = $(IOUNIFIED_ELF)

include $(APP_DIR)/../Makefile

$(TEST_NAME)-srcs := $(APP_DIR)/cluster.c

$(TEST_NAME)-hw-cmd := k1-jtag-runner --multibinary=$($(TEST_NAME)-multibinary) --exec-multibin=IODDR0:iounified \
	--exec-multibin=IODDR1:$(IOUNIFIED_NAME) -- -c $(TEST_NAME) -c $(TEST_NAME)
$(TEST_NAME)-labels += jtag
$(TEST_NAME)-hw-no-remote := 1
host-tests += $(TEST_NAME)

include $(K1_TOOLCHAIN_DIR)/share/make/Makefile.kalray
