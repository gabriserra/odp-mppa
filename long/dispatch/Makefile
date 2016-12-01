APP_DIR := $(dir $(lastword $(MAKEFILE_LIST)))
TEST_STD_FIRMWARE = $(IOUNIFIED_ELF)

include $(APP_DIR)/../Makefile

$(TEST_NAME)-srcs := $(APP_DIR)/dispatch_simple.c
$(TEST_NAME)-jtag-cmd := $(IOUNIFIED_NAME) -c $(TEST_NAME)
mppa-tests += $(TEST_NAME)

include $(K1_TOOLCHAIN_DIR)/share/make/Makefile.kalray
