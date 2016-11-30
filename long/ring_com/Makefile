APP_DIR := $(dir $(lastword $(MAKEFILE_LIST)))
TEST_STD_FIRMWARE = $(IOPCIE_ELF)

include $(APP_DIR)/../Makefile 

$(TEST_NAME)-srcs := $(APP_DIR)/ring_com.c
$(TEST_NAME)-sim-cmd := $(TEST_MULTIBIN_NAME).mpk $(foreach i,$(shell seq 16),-c $(TEST_NAME))

include $(K1_TOOLCHAIN_DIR)/share/make/Makefile.kalray
