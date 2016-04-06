DIR := $(dir $(lastword $(MAKEFILE_LIST)))
TEST_NAME := $(lastword $(subst /, ,$(DIR)))

include $(DIR)/../Makefile
$(TEST_NAME)-srcs := $(DIR)/odp_pktio_perf.c
cluster-cflags += -I$(DIR)
cluster-bin += $(TEST_NAME)

$(TEST_NAME)_multibin-objs := $(IOUNIFIED_ELF) $(TEST_NAME)
mppa-bin += $(TEST_NAME)_multibin
$(TEST_NAME)-multibinary := $(TEST_NAME)_multibin.mpk

ifeq ($(board),$(BOARD_konic80))
$(TEST_NAME)-hw-cmd := k1-jtag-runner --multibinary=$($(TEST_NAME)-multibinary) --exec-multibin=IODDR0:iounified \
	--exec-multibin=IODDR1:$(IOUNIFIED_NAME) -- -c $(TEST_NAME) -a -i -a e0,e1:tags=60 -a -p
$(TEST_NAME)-labels += jtag
$(TEST_NAME)-hw-no-remote := 1
host-tests += $(TEST_NAME)
endif
include $(K1_TOOLCHAIN_DIR)/share/make/Makefile.kalray
