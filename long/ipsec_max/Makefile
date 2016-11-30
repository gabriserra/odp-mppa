APP_DIR := $(dir $(lastword $(MAKEFILE_LIST)))

include $(APP_DIR)/../Makefile

cflags += . -std=gnu11 -O3 -g0

# use genuine apps/ipsec sources along with custom odp_ipsec.c
$(TEST_NAME)-srcs := $(wildcard $(APP_DIR)/ipsec/odp_ipsec_*.[ch]) $(APP_DIR)/odp_ipsec.c
$(TEST_NAME)-nameinmpk := odp_ipsec.kelf
cluster-cflags += -I$(APP_DIR)/ipsec

# at 500Mhz test should take ~75 seconds for 15M packets
TIMEOUT=150 # seconds
TARGET_PERF=0.30 # *1e6 packets per second / cluster (if freq != 500MHz it is scaled accordingly)
TARGET_COUNT=15 # *1e6 packets

ifeq ($(board),$(BOARD_konic80))
# see parameters in firmware/main.c
$(TEST_NAME)-hw-cmd := k1-jtag-runner --multibinary=$($(TEST_NAME)-multibinary) \
	--exec-multibin=IODDR0:firmware.kelf \
	--exec-multibin=IODDR1:firmware.kelf -- $(TARGET_PERF) $(TARGET_COUNT)
$(TEST_NAME)-labels := jtag
$(TEST_NAME)-hw-timeout := $(TIMEOUT)
$(TEST_NAME)-hw-no-remote := 1
host-tests += $(TEST_NAME)
endif

include $(K1_TOOLCHAIN_DIR)/share/make/Makefile.kalray
