APP_DIR := $(dir $(lastword $(MAKEFILE_LIST)))
LONG_NO_DEFAULT_RULES := 1

include $(APP_DIR)/../Makefile

$(TEST_NAME)-srcs := $(APP_DIR)/main.c $(APP_DIR)/odp_rpc.c
$(TEST_NAME)-cflags += -DRPC_FIRMWARE -std=gnu99 -I$(ODP_TOOLCHAIN_DIR)/k1-elf/include
$(TEST_NAME)-lflags += -DRPC_FIRMWARE -std=gnu99 -lmppanoc  -lmpparouting

io-system := bare
io-bin += $(TEST_NAME)

$(TEST_MULTIBIN_NAME)-objs := $(IOUNIFIED_ELF) $(TEST_NAME)
mppa-bin += $(TEST_MULTIBIN_NAME)

$(TEST_NAME)-multibinary := $(TEST_MULTIBIN_NAME).mpk

$(TEST_NAME)-hw-cmd := k1-jtag-runner --multibinary=$($(TEST_NAME)-multibinary) \
	--exec-multibin=IODDR0:$(TEST_NAME) \
	--exec-multibin=IODDR1:$(IOUNIFIED_NAME)
$(TEST_NAME)-labels += jtag
$(TEST_NAME)-hw-no-remote := 1
host-tests += $(TEST_NAME)

include $(K1_TOOLCHAIN_DIR)/share/make/Makefile.kalray
