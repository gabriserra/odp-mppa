APP_DIR := $(dir $(lastword $(MAKEFILE_LIST)))
TEST_STD_FIRMWARE = $(IOUNIFIED_ELF)

include $(APP_DIR)/../Makefile

$(TEST_NAME)-srcs := $(APP_DIR)/cluster.c $(CUNIT_COMMON_SRC)
$(TEST_NAME)-jtag-cmd := $(IOUNIFIED_NAME) -c $(TEST_NAME)
$(TEST_NAME)-multibinary := $(TEST_MULTIBIN_NAME).mpk
mppa-tests += $(TEST_NAME)

include $(K1_TOOLCHAIN_DIR)/share/make/Makefile.kalray
