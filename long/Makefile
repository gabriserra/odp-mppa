O ?= ./output

project-name := odp

ODP_SRC_DIR ?= $(CURDIR)/..
ODP_ARCH ?= k1b-kalray-nodeos

include $(ODP_SRC_DIR)/mk/platforms.inc

# Kalray internal system name
SYSTEM_NAME = $(subst -kalray,,$(ODP_ARCH))
CORE_ARCH = $(firstword $(subst -, ,$(ODP_ARCH)))
OS_BOARD_NAME = $(lastword $(subst -, ,$(ODP_ARCH)))

# -mos option according to real os name
OS_mos := bare
OS_nodeos := nodeos
OS_rtems := rtems
BOARD_simu := developer
BOARD_explorer := explorer
BOARD_konic80 := konic80

# extract OS name and board name
REAL_OS_NAME = $(firstword $(subst _, ,$(OS_BOARD_NAME)))
OS_NAME = $(OS_$(REAL_OS_NAME))
BOARD_NAME = $(if $(findstring _,$(OS_BOARD_NAME)),$(BOARD_$(lastword $(subst _, ,$(OS_BOARD_NAME)))),developer)

cluster-system := $(OS_NAME)
arch = $(CORE_ARCH)
board = $(BOARD_NAME)

cflags := -Wall -Werror -Wextra
BOARD_EXT := $(if $(findstring explorer,$(BOARD_NAME)),_explorer,)$(if $(findstring simu,$(OS_BOARD_NAME)),_simu,)$(if $(findstring _konic80,$(OS_BOARD_NAME)),_konic80,)
ODP_FILTERED_ARCH := $(subst _konic80,,$(ODP_ARCH))
INCLUDE_DIR=$($(ODP_FILTERED_ARCH)_INC_DIR)

ifeq ($(MAKELEVEL),0)
$(info --- Building for arch $(arch), os $(OS_NAME), board $(BOARD_NAME) ---)
endif

CUNIT_COMMON_SRC := $(ODP_SRC_DIR)/test/validation/common/odp_cunit_common.c
LIB_NOC_LFLAGS :=  -lmppapower -lmpparouting -lmppanoc
ifeq ($(USE_PACKAGES),1)
  ODP_INSTALL_DIR = $(K1_TOOLCHAIN_DIR)
  CUNIT_INSTALL_DIR = $(K1_TOOLCHAIN_DIR)/kalray_internal/cunit/$(ODP_FILTERED_ARCH)
  IOPCIE_ELF := $(ODP_INSTALL_DIR)/share/odp/firmware/$(CORE_ARCH)$(BOARD_EXT)/iopcie.kelf
else
  ODP_INSTALL_DIR = $(ODP_SRC_DIR)/install/local/k1tools/
  CUNIT_INSTALL_DIR = $(ODP_SRC_DIR)/install/local/k1tools/kalray_internal/cunit/$(ODP_FILTERED_ARCH)/

  # Build the ioddr boot firmware to avoid additionnal build/install steps...
  INCLUDING_FIRMWARE := 1
  include $(ODP_SRC_DIR)/firmware/iopcie/Makefile
  iopcie-srcs := $(SRCFILES)
  iopcie-system := bare
  iopcie-lflags := $($(CORE_ARCH)-kalray-ioddr$(BOARD_EXT)_LDFLAGS) $(_LDFLAGS)
  iopcie-cflags := $($(CORE_ARCH)-kalray-ioddr$(BOARD_EXT)_CFLAGS) $(_CFLAGS)
  IOPCIE_ELF := iopcie
endif

cluster-cflags := -I$(ODP_INSTALL_DIR)/$(INCLUDE_DIR)/include -I$(CUNIT_INSTALL_DIR)/include
cluster-lflags := -L$(ODP_INSTALL_DIR)/lib/$(ODP_FILTERED_ARCH)/ -L$(CUNIT_INSTALL_DIR)/lib/ 

cluster-cflags += -I$(ODP_SRC_DIR)/test/validation/common/ -I$(CURDIR)/include $($(ODP_FILTERED_ARCH)_CFLAGS) -g3
cluster-lflags += -lodphelper -lodp -lcunit -lcrypto  $(LIB_NOC_LFLAGS) $($(ODP_FILTERED_ARCH)_LDFLAGS) -Wl,--gc-sections

$(IOPCIE_ELF)-nameinmpk = iopcie
IOPCIE_NAME := iopcie

# Empty direct assignement to allows += to work in included Makefiles
io-bin := $(IOPCIE_ELF)
mppa-bin :=
cluster-bin :=
cluster-tests :=
mppa-tests :=
sim-runner-opts := --mboard=developer --march=bostan

# Subdirectories
SUB_DIRS := $(wildcard */Makefile) 
include $(SUB_DIRS)

include $(CURDIR)/../mk/Makefile.kalray