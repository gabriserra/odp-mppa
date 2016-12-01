APP_DIR := $(dir $(lastword $(MAKEFILE_LIST)))

include $(APP_DIR)/../Makefile

$(TEST_NAME)-srcs := $(APP_DIR)/odp_pktio_ddr.c

$(TEST_NAME)-multibinary := $(TEST_MULTIBIN_NAME).mpk

ifeq ($(board),$(BOARD_konic80))
$(TEST_NAME)-hw-cmd := k1-jtag-runner --multibinary=$($(TEST_NAME)-multibinary) --exec-multibin=IODDR0:firmware.kelf
$(TEST_NAME)-labels += jtag
$(TEST_NAME)-hw-no-remote := 1
host-tests += $(TEST_NAME)
endif

include $(K1_TOOLCHAIN_DIR)/share/make/Makefile.kalray
