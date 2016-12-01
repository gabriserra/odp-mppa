O ?= ./output

project-name := odp

LONG_DIR := $(dir $(lastword $(MAKEFILE_LIST)))
ODP_SRC_DIR ?= $(LONG_DIR)/..
ODP_TOOLCHAIN_DIR ?= $(K1_TOOLCHAIN_DIR)
ODP_MAKEFILE_NO_RULES := 1

# If we selected a STD firware, auto use STD firmware rule
ifdef TEST_STD_FIRMWARE
ODP_MAKEFILE_STD_FIRMWARE := 1
endif
ifdef LONG_NO_DEFAULT_RULES
ODP_MAKEFILE_NO_FIRMWARE := 1
endif

TEST_NAME ?= $(lastword $(subst /, ,$(APP_DIR)))
TEST_MULTIBIN_NAME ?= $(TEST_NAME)_multibin

include $(ODP_TOOLCHAIN_DIR)/share/odp/build/apps/Makefile.apps

CUNIT_COMMON_SRC := $(ODP_SRC_DIR)/test/validation/common/odp_cunit_common.c
CUNIT_INSTALL_DIR = $(ODP_TOOLCHAIN_DIR)/kalray_internal/cunit/$(REAL_ODP_ARCH)

IOPCIE_ELF := $(ODP_TOOLCHAIN_DIR)/share/odp/firmware/$(OS_BOARD_NAME)/$(CORE_ARCH)/iopcie.kelf
$(IOPCIE_ELF)-nameinmpk = iopcie
IOPCIE_NAME := iopcie

cluster-cflags += -I$(CUNIT_INSTALL_DIR)/include -I$(ODP_SRC_DIR)/test/validation/common/ \
		 -I$(LONG_DIR)/include -I$(APP_DIR) -g3 -O2
cluster-lflags += -L$(CUNIT_INSTALL_DIR)/lib/ -lcunit



ifneq ($(BOARD_NAME),explorer)
IOUNIFIED_ELF := $(ODP_TOOLCHAIN_DIR)/share/odp/firmware/$(OS_BOARD_NAME)/$(CORE_ARCH)/iounified.kelf
$(IOUNIFIED_ELF)-nameinmpk = iounified
IOUNIFIED_NAME := iounified
endif

io-bin += $(IOPCIE_ELF) $(IOUNIFIED_ELF)

build: all

install: all
	@if [ "$(LONT_DIR)" == "" ]; then echo "ERROR: LONT_DIR" not set; exit 1; fi
	mkdir -p $(LONT_DIR)/$(OS_BOARD_NAME)/$(OS_PLAT_NAME)/$(TEST_NAME)
	if [ "$(NO_INSTALL_BINARY)" != "1" ]; then \
		install $(addprefix output/bin/, $($(TEST_NAME)-multibinary)) $(LONT_DIR)/$(OS_BOARD_NAME)/$(OS_PLAT_NAME)/$(TEST_NAME); \
		k1-multibin-util -f $(LONT_DIR)/$(OS_BOARD_NAME)/$(OS_PLAT_NAME)/$(TEST_NAME)/$($(TEST_NAME)-multibinary) -a --foreach="k1-strip --strip-debug {}"; \
	fi
	if [ "$($(TEST_NAME)-scripts)$($(TEST_NAME)-extras)" != "" ]; then \
		install $(addprefix $(APP_DIR)/,$($(TEST_NAME)-scripts)) $($(TEST_NAME)-extras) $(LONT_DIR)/$(OS_BOARD_NAME)/$(OS_PLAT_NAME)/$(TEST_NAME); \
	fi
	if [ -e output/bin/CTestTestfile.cmake ]; then \
		install $(addprefix output/bin/, CTestTestfile.cmake DartConfiguration.tcl) $($(TEST_NAME)-extras) $(LONT_DIR)/$(OS_BOARD_NAME)/$(OS_PLAT_NAME)/$(TEST_NAME); \
	else \
		echo "WARNING: No ctest file"; \
	fi


ifndef LONG_NO_DEFAULT_RULES
cluster-bin += $(TEST_NAME)
$(TEST_MULTIBIN_NAME)-objs += $(TEST_STD_FIRMWARE) $(TEST_NAME)
mppa-bin += $(TEST_MULTIBIN_NAME)
$(TEST_NAME)-multibinary := $(TEST_MULTIBIN_NAME).mpk
endif
