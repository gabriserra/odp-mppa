APP_DIR := $(dir $(lastword $(MAKEFILE_LIST)))
TEST_STD_FIRMWARE = $(IOUNIFIED_ELF)

include $(APP_DIR)/../Makefile

$(TEST_NAME)-srcs := $(APP_DIR)/odp_pktio_perf.c

ifeq ($(board),$(BOARD_konic80))
$(TEST_NAME)-hw-cmd := k1-jtag-runner --multibinary=$($(TEST_NAME)-multibinary) --exec-multibin=IODDR0:iounified \
	--exec-multibin=IODDR1:$(IOUNIFIED_NAME) -- -c $(TEST_NAME) -a -i -a e0,e1:tags=60 -a -p
$(TEST_NAME)-labels += jtag
$(TEST_NAME)-hw-no-remote := 1
host-tests += $(TEST_NAME)
endif

include $(K1_TOOLCHAIN_DIR)/share/make/Makefile.kalray
