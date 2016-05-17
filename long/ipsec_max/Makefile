DIR := $(dir $(lastword $(MAKEFILE_LIST)))
TEST_NAME := $(lastword $(subst /, ,$(DIR)))

include $(DIR)/../Makefile

pre-build-hooks := build-firmware
clean-hooks := clean-firmware

FIRMWARE_ELF := $(DIR)/firmware-$(OS_BOARD_NAME)/firmware.kelf
$(FIRMWARE_ELF)-nameinmpk := firmware.kelf
FIRMWARE_NAME := firmware.kelf

build-firmware:
	+mkdir -p $(DIR)/firmware-$(OS_BOARD_NAME) && cd $(DIR)/firmware-$(OS_BOARD_NAME) && \
	$(MAKE) -f $(DIR)/firmware/Makefile ODP_FIRMWARE_CONFIG="k1b-kalray_$(OS_BOARD_NAME)" \
	ODP_TOOLCHAIN_DIR="$(ODP_TOOLCHAIN_DIR)" all

clean-firmware:
	if [ -d $(DIR)/firmware-$(OS_BOARD_NAME) ]; then cd $(DIR)/firmware-$(OS_BOARD_NAME) && \
	$(MAKE) -f $(DIR)/firmware/Makefile ODP_FIRMWARE_CONFIG="k1b-kalray_$(OS_BOARD_NAME)" \
	ODP_TOOLCHAIN_DIR="$(ODP_TOOLCHAIN_DIR)" clean; else true; fi


cflags += . -std=gnu11 -O3

# use genuine apps/ipsec sources along with custom odp_ipsec.c
$(TEST_NAME)-srcs := $(wildcard $(DIR)/ipsec/odp_ipsec_*.[ch]) $(DIR)/odp_ipsec.c
$(TEST_NAME)-nameinmpk := odp_ipsec.kelf
cluster-cflags += -I$(DIR)/ipsec
cluster-bin += $(TEST_NAME)

$(TEST_NAME)_multibin-objs := $(cluster-bin) $(FIRMWARE_ELF)
mppa-bin += $(TEST_NAME)_multibin
$(TEST_NAME)-multibinary := $(TEST_NAME)_multibin.mpk

# @ 500MHz, perf is ~500Kpps / cluster
# => in 200 seconds, each will process max 100M packets
TIMEOUT=400 # seconds
TARGET_PERF=0.50 # *1e6 packets per second / cluster (if freq != 500MHz it is scaled accordingly)
TARGET_COUNT=100 # *1e6 packets

ifeq ($(board),$(BOARD_konic80))
# see parameters in firmware/main.c
$(TEST_NAME)-hw-cmd := k1-jtag-runner --multibinary=$($(TEST_NAME)-multibinary) --exec-multibin=IODDR0:$(FIRMWARE_NAME) \
	--exec-multibin=IODDR1:$(FIRMWARE_NAME) -- $(TARGET_PERF) $(TARGET_COUNT)
$(TEST_NAME)-labels := jtag
$(TEST_NAME)-hw-timeout := $(TIMEOUT)
$(TEST_NAME)-hw-no-remote := 1
host-tests += $(TEST_NAME)
endif
include $(K1_TOOLCHAIN_DIR)/share/make/Makefile.kalray
