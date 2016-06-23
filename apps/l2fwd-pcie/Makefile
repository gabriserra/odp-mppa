APP_DIR   := $(dir $(realpath $(lastword $(MAKEFILE_LIST))))
TEST_NAME := $(lastword $(subst /, ,$(APP_DIR)))

ODP_TOOLCHAIN_DIR ?= $(K1_TOOLCHAIN_DIR)
include $(ODP_TOOLCHAIN_DIR)/share/odp/build/apps/Makefile.apps

cflags += -I$(APP_DIR)

odp_l2fwd-srcs := $(wildcard $(APP_DIR)/odp_l2fwd*.[ch])
odp_l2fwd-nameinmpk := odp_l2fwd.kelf
cluster-bin += odp_l2fwd
$(TEST_NAME)-objs += odp_l2fwd
mppa-bin += $(TEST_NAME)

$(TEST_NAME)-multibinary := $(TEST_NAME).mpk
mppa-tests += $(TEST_NAME)
$(TEST_NAME)-scripts := run.sh

include $(K1_TOOLCHAIN_DIR)/share/make/Makefile.kalray
