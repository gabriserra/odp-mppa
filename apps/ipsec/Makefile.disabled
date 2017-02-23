APP_DIR   := $(dir $(realpath $(lastword $(MAKEFILE_LIST))))
TEST_NAME := $(lastword $(subst /, ,$(APP_DIR)))

ODP_TOOLCHAIN_DIR ?= $(K1_TOOLCHAIN_DIR)
ODP_ARCH := k1b-kalray-mos_konic80-crypto
include $(ODP_TOOLCHAIN_DIR)/share/odp/build/apps/Makefile.apps

cflags += -I$(APP_DIR)

fast-ipsec-srcs := $(wildcard $(APP_DIR)/odp_ipsec*.[ch])
fast-ipsec-nameinmpk := odp_ipsec.kelf
cluster-bin += fast-ipsec

generator-srcs := $(wildcard $(APP_DIR)/odp_generator*.[ch])
generator-nameinmpk := odp_generator.kelf
cluster-bin += generator

$(TEST_NAME)-objs += fast-ipsec generator
mppa-bin += $(TEST_NAME)

$(TEST_NAME)-sim-cmd := $(IOPCIE_NAME) -c $(TEST_NAME) -c $(TEST_NAME) -c $(TEST_NAME) -c $(TEST_NAME)
$(TEST_NAME)-multibinary := $(TEST_NAME).mpk
mppa-tests += $(TEST_NAME)
$(TEST_NAME)-scripts := run.sh install-deps.sh ipsec.png ipsec.py konic80.png start.sh test.sh

include $(K1_TOOLCHAIN_DIR)/share/make/Makefile.kalray
