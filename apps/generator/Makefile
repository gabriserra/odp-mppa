APP_DIR   := $(dir $(realpath $(lastword $(MAKEFILE_LIST))))

####################################
# Configure your project name here #
####################################
TEST_NAME := generator

####################################
# Select one of these platforms    #
####################################
#ODP_ARCH := k1b-kalray-mos_developer
#ODP_ARCH := k1b-kalray-nodeos_developer
ODP_ARCH := k1b-kalray-mos_konic80
#ODP_ARCH := k1b-kalray-nodeos_konic80
#ODP_ARCH := k1b-kalray-mos_simu
#ODP_ARCH := k1b-kalray-nodeos_simu

ODP_TOOLCHAIN_DIR ?= $(K1_TOOLCHAIN_DIR)
include $(ODP_TOOLCHAIN_DIR)/share/odp/build/apps/Makefile.apps

####################################
# Configure your flags here        #
####################################
cflags += -I$(APP_DIR)
ldflags +=

####################################
# Create cluster binaries and pick #
# the source and header it uses    #
####################################
odp_generator-srcs := $(APP_DIR)/odp_generator.c
odp_generator-nameinmpk := odp_generator.kelf
cluster-bin += odp_generator

####################################
#  GENERIC RULES. DO NOT EDIT !!   #
####################################
$(TEST_NAME)-objs += $(cluster-bin)
mppa-bin += $(TEST_NAME)
$(TEST_NAME)-multibinary := $(TEST_NAME).mpk
mppa-tests += $(TEST_NAME)
$(TEST_NAME)-scripts := run.sh

include $(K1_TOOLCHAIN_DIR)/share/make/Makefile.kalray
