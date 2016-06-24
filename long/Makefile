O ?= ./output

project-name := odp

LONG_DIR := $(dir $(lastword $(MAKEFILE_LIST)))
ODP_SRC_DIR ?= $(LONG_DIR)/..
ODP_ARCH ?= k1b-kalray-nodeos_developer
ODP_TOOLCHAIN_DIR ?= $(K1_TOOLCHAIN_DIR)
include $(ODP_TOOLCHAIN_DIR)/share/odp/build/mk/platforms.inc

INCLUDE_DIR=$($(ODP_ARCH)_INC_DIR)
# Kalray internal system name
SYSTEM_NAME = $(subst -kalray,,$(ODP_ARCH))
CORE_ARCH = $(firstword $(subst -, ,$(ODP_ARCH)))
OS_PLAT_NAME  = $(firstword $(subst _, ,$(ODP_ARCH)))
OS_BOARD_NAME = $(lastword $(subst _, ,$(ODP_ARCH)))

# -mos option according to real os name
OS_mos := bare
OS_nodeos := nodeos
OS_rtems := rtems
BOARD_developer := developer
BOARD_simu := developer
BOARD_explorer := explorer
BOARD_konic80 := konic80
BOARD_emb01 := emb01

# extract OS name and board name
REAL_OS_NAME = $(lastword $(subst -, ,$(OS_PLAT_NAME)))
OS_NAME = $(OS_$(REAL_OS_NAME))
BOARD_NAME = $(BOARD_$(lastword $(subst _, ,$(OS_BOARD_NAME))))

cluster-system := $(OS_NAME)
arch = $(CORE_ARCH)
board = $(BOARD_NAME)

cflags := -Wall -Werror -Wextra
LIB_NOC_LFLAGS :=  -lmppapower -lmpparouting -lmppanoc
VALID_TYPE := $(if $(findstring simu,$(OS_BOARD_NAME)),sim,jtag)

ifeq ($(MAKELEVEL),0)
$(info --- Building for arch $(arch), os $(OS_NAME), board $(BOARD_NAME) ---)
endif

CUNIT_COMMON_SRC := $(ODP_SRC_DIR)/test/validation/common/odp_cunit_common.c
CUNIT_INSTALL_DIR = $(ODP_TOOLCHAIN_DIR)/kalray_internal/cunit/$(ODP_ARCH)
IOPCIE_ELF := $(ODP_TOOLCHAIN_DIR)/share/odp/firmware/$(OS_BOARD_NAME)/$(CORE_ARCH)/iopcie.kelf
cluster-cflags := -I$(ODP_TOOLCHAIN_DIR)/$(INCLUDE_DIR)/include -I$(CUNIT_INSTALL_DIR)/include
cluster-lflags := -L$(ODP_TOOLCHAIN_DIR)/lib/odp/$(OS_BOARD_NAME)/$(OS_PLAT_NAME)/ -L$(CUNIT_INSTALL_DIR)/lib/

cluster-cflags += -I$(ODP_SRC_DIR)/test/validation/common/ -I$(LONG_DIR)/include $($(ODP_ARCH)_CFLAGS) -g3 -O2
cluster-lflags += -lodphelper -lodp -lcunit -lcrypto  $(LIB_NOC_LFLAGS) $($(ODP_ARCH)_LDFLAGS) -Wl,--gc-sections

$(IOPCIE_ELF)-nameinmpk = iopcie
IOPCIE_NAME := iopcie

ifneq ($(BOARD_NAME),explorer)
IOUNIFIED_ELF := $(ODP_TOOLCHAIN_DIR)/share/odp/firmware/$(OS_BOARD_NAME)/$(CORE_ARCH)/iounified.kelf
$(IOUNIFIED_ELF)-nameinmpk = iounified
IOUNIFIED_NAME := iounified
endif

# Empty direct assignement to allows += to work in included Makefiles
io-bin := $(IOPCIE_ELF) $(IOUNIFIED_ELF)
mppa-bin =
cluster-bin :=
cluster-tests :=
mppa-tests :=
sim-runner-opts := --mboard=developer 

build: all

install: all
		 @if [ "$(LONT_DIR)" == "" ]; then echo "ERROR: LONT_DIR" not set; exit 1; fi
		 mkdir -p $(LONT_DIR)/$(OS_BOARD_NAME)/$(OS_PLAT_NAME)/$(TEST_NAME)
		 @if [ "$(NO_INSTALL_BINARY)" != "1" ]; then \
		 install $(addprefix $(DIR)/,$($(TEST_NAME)-scripts)) $(addprefix output/bin/, $($(TEST_NAME)-multibinary)) $($(TEST_NAME)-extras) $(LONT_DIR)/$(OS_BOARD_NAME)/$(OS_PLAT_NAME)/$(TEST_NAME); \
		 k1-multibin-util -f $(LONT_DIR)/$(OS_BOARD_NAME)/$(OS_PLAT_NAME)/$(TEST_NAME)/$($(TEST_NAME)-multibinary) -a --foreach="k1-strip --strip-debug {}"; \
		 fi
		 if [ -e output/bin/CTestTestfile.cmake ]; then \
			install $(addprefix output/bin/, CTestTestfile.cmake DartConfiguration.tcl) $($(TEST_NAME)-extras) $(LONT_DIR)/$(OS_BOARD_NAME)/$(OS_PLAT_NAME)/$(TEST_NAME); \
		 else \
			echo "WARNING: No ctest file"; \
		 fi


valid: install
	cd output/bin && ctest -L $(VALID_TYPE)
