#-*- mode: makefile -*-

CONFIGS :=
HOST_CONFIGS :=
K1_CONFIGS :=
_CONFIGS :=
BOARDS :=
_BOARDS :=
OSs :=
_OSs :=
_FIRMWARE_BOARDS :=

$(foreach FIRM, $(FIRMWARES), $(eval $(FIRM)_CONFIGS := ))
$(foreach FIRM, $(FIRMWARES), $(eval _$(FIRM)_CONFIGS := ))

CCACHE := $(shell which ccache 2>/dev/null)
#
#
#
# K1B Definitions
#
#
#
k1b-kalray-nodeos_hw_CONF_ENV    := CC="$(CCACHE) k1-nodeos-gcc"  CXX="$(CCACHE) k1-nodeos-g++" STRIP="k1-strip --strip-debug"
k1b-kalray-nodeos_hw_CONF_OPTS   :=
k1b-kalray-nodeos_hw_CFLAGS      := -march=k1b -DK1_NODEOS
k1b-kalray-nodeos_hw_LDFLAGS     :=
k1b-kalray-nodeos_hw_PLATFORM    := mppa
k1b-kalray-nodeos_hw_INC_DIR     := k1-nodeos
k1b-kalray-nodeos_hw_MAKE_VALID  := -j1
k1b-kalray-nodeos_hw_INSTALL     := true
k1b-kalray-nodeos_hw_VALID_TYPE  := jtag
K1_CONFIGS += k1b-kalray-nodeos_hw

k1b-kalray-nodeos_explorer_CONF_ENV    := CC="$(CCACHE) k1-nodeos-gcc"  CXX="$(CCACHE) k1-nodeos-g++" STRIP="k1-strip --strip-debug"
k1b-kalray-nodeos_explorer_CONF_OPTS   := 
k1b-kalray-nodeos_explorer_CFLAGS      := -march=k1b -DK1_NODEOS -mboard=explorer -DK1B_EXPLORER
k1b-kalray-nodeos_explorer_LDFLAGS     :=
k1b-kalray-nodeos_explorer_PLATFORM    := mppa
k1b-kalray-nodeos_explorer_INC_DIR     := k1-nodeos
k1b-kalray-nodeos_explorer_MAKE_VALID  := -j1
k1b-kalray-nodeos_explorer_INSTALL     := true
k1b-kalray-nodeos_explorer_VALID_TYPE  := jtag
_CONFIGS += k1b-kalray-nodeos_explorer

k1b-kalray-nodeos_simu_CONF_ENV    := CC="$(CCACHE) k1-nodeos-gcc"  CXX="$(CCACHE) k1-nodeos-g++" STRIP="k1-strip --strip-debug"
k1b-kalray-nodeos_simu_CONF_OPTS   :=
k1b-kalray-nodeos_simu_CFLAGS      := -march=k1b -DK1_NODEOS -DMAGIC_SCALL
k1b-kalray-nodeos_simu_LDFLAGS     :=
k1b-kalray-nodeos_simu_PLATFORM    := mppa
k1b-kalray-nodeos_simu_INC_DIR     := k1-nodeos
k1b-kalray-nodeos_simu_MAKE_VALID  :=
k1b-kalray-nodeos_simu_INSTALL     := true
k1b-kalray-nodeos_simu_VALID_TYPE  := simu
K1_CONFIGS += k1b-kalray-nodeos_simu

k1b-kalray-mos_hw_CONF_ENV    := CC="$(CCACHE) k1-gcc"  CXX="$(CCACHE) k1-g++" STRIP="k1-strip --strip-debug"
k1b-kalray-mos_hw_CONF_OPTS   :=
k1b-kalray-mos_hw_CFLAGS      := -march=k1b -mhypervisor -DK1_MOS
k1b-kalray-mos_hw_LDFLAGS     := -mhypervisor -lvbsp -lutask -Wl,--defsym=USER_STACK_SIZE=5120 \
				   -Wl,--undefined=DEBUGGER_HAVE_USER_MODE_SUPPORT
k1b-kalray-mos_hw_PLATFORM    := mppa
k1b-kalray-mos_hw_INC_DIR     := k1-elf
k1b-kalray-mos_hw_MAKE_VALID  := -j1
k1b-kalray-mos_hw_INSTALL     := true
k1b-kalray-mos_hw_INSTALL_DOC := true
k1b-kalray-mos_hw_VALID_TYPE  := jtag
K1_CONFIGS += k1b-kalray-mos_hw

k1b-kalray-mos_explorer_CONF_ENV    := CC="$(CCACHE) k1-gcc"  CXX="$(CCACHE) k1-g++" STRIP="k1-strip --strip-debug"
k1b-kalray-mos_explorer_CONF_OPTS   :=
k1b-kalray-mos_explorer_CFLAGS      := -march=k1b -mhypervisor -DK1_MOS -mboard=explorer -DK1B_EXPLORER
k1b-kalray-mos_explorer_LDFLAGS     := -mhypervisor -lvbsp -lutask -Wl,--defsym=USER_STACK_SIZE=5120 \
				   -Wl,--undefined=DEBUGGER_HAVE_USER_MODE_SUPPORT
k1b-kalray-mos_explorer_PLATFORM    := mppa
k1b-kalray-mos_explorer_INC_DIR     := k1-elf
k1b-kalray-mos_explorer_MAKE_VALID  := -j1
k1b-kalray-mos_explorer_INSTALL     := true
k1b-kalray-mos_explorer_VALID_TYPE  := jtag
_CONFIGS += k1b-kalray-mos_explorer

k1b-kalray-mos_simu_CONF_ENV    := CC="$(CCACHE) k1-gcc"  CXX="$(CCACHE) k1-g++" STRIP="k1-strip --strip-debug"
k1b-kalray-mos_simu_CONF_OPTS   :=
k1b-kalray-mos_simu_CFLAGS      := -march=k1b -mhypervisor -DK1_MOS -DMAGIC_SCALL
k1b-kalray-mos_simu_LDFLAGS     := -mhypervisor -lvbsp -lutask -Wl,--defsym=USER_STACK_SIZE=5120 \
				   -Wl,--undefined=DEBUGGER_HAVE_USER_MODE_SUPPORT

k1b-kalray-mos_simu_PLATFORM    := mppa
k1b-kalray-mos_simu_INC_DIR     := k1-elf
k1b-kalray-mos_simu_MAKE_VALID  :=
k1b-kalray-mos_simu_INSTALL     := true
k1b-kalray-mos_simu_VALID_TYPE  := simu
K1_CONFIGS += k1b-kalray-mos_simu

#
#
#
# Debug configs
#
#
#
define DEBUG_CONFIG_RULE
#$(1) config name
$(eval $(1)-debug_CONF_ENV    := $($(1)_CONF_ENV) CFLAGS="-g3 -Og")
$(eval $(1)-debug_CONF_OPTS   := $($(1)_CONF_OPTS) $(DEBUG_CONF_FLAGS) $(_DEBUG_CONF_FLAGS))
$(eval $(1)-debug_CFLAGS      := $($(1)_CFLAGS))
$(eval $(1)-debug_LDFLAGS     := $($(1)_LDFLAGS))
$(eval $(1)-debug_PLATFORM    := $($(1)_PLATFORM))
$(eval $(1)-debug_INC_DIR     := $($(1)_INC_DIR))
$(eval $(1)-debug_MAKE_VALID  := $($(1)_MAKE_VALID))
$(eval $(1)-debug_INSTALL     := $($(1)_INSTALL))
$(eval $(1)-debug_VALID_TYPE  := $($(1)_VALID_TYPE))
endef

$(foreach config,$(_CONFIGS), \
  $(eval $(call DEBUG_CONFIG_RULE,$(config))) \
  $(eval _CONFIGS += $(config)-debug) \
)

$(foreach config,$(K1_CONFIGS), \
  $(eval $(call DEBUG_CONFIG_RULE,$(config))) \
  $(eval _CONFIGS += $(config)-debug) \
)

CONFIGS += $(K1_CONFIGS)

#
#
#
# x86 Definitions
#
#
#
x86_64-unknown-linux-gnu_CONF_ENV    :=
x86_64-unknown-linux-gnu_CONF_OPTS   :=
x86_64-unknown-linux-gnu_CFLAGS      :=
x86_64-unknown-linux-gnu_LDFLAGS     :=
x86_64-unknown-linux-gnu_PLATFORM    := linux-generic
x86_64-unknown-linux-gnu_INC_DIR     :=
x86_64-unknown-linux-gnu_MAKE_VALID  :=
x86_64-unknown-linux-gnu_INSTALL     := false
HOST_CONFIGS += x86_64-unknown-linux-gnu

CONFIGS += $(HOST_CONFIGS)

#
#
#
# Firmware Definitions
#
#
#
firmware_k1b-kalray_developer_CC          := $(CCACHE) k1-gcc
firmware_k1b-kalray_developer_CFLAGS      := -march=k1b -mos=bare -mcore=k1bio -mcluster=ioddr -mprocessing=single
firmware_k1b-kalray_developer_LDFLAGS     :=
firmware_k1b-kalray_developer_INSTDIR      = $(K1ST_DIR)/share/odp/firmware/developer/k1b
_FIRMWARE_BOARDS += k1b-kalray_developer

firmware_k1b-kalray_konic80_CC            := $(CCACHE) k1-gcc
firmware_k1b-kalray_konic80_CFLAGS        := -march=k1b -mos=bare -mcore=k1bio -mcluster=ioddr -mprocessing=single -mboard=konic80
firmware_k1b-kalray_konic80_LDFLAGS       :=
firmware_k1b-kalray_konic80_INSTDIR        = $(K1ST_DIR)/share/odp/firmware/konic80/k1b
_FIRMWARE_BOARDS += k1b-kalray_konic80

firmware_k1b-kalray_ab04_CC            := $(CCACHE) k1-gcc
firmware_k1b-kalray_ab04_CFLAGS        := -march=k1b -mos=bare -mcore=k1bio -mcluster=ioddr -mprocessing=single -mboard=ab04
firmware_k1b-kalray_ab04_LDFLAGS       :=
firmware_k1b-kalray_ab04_INSTDIR        = $(K1ST_DIR)/share/odp/firmware/ab04/k1b
_FIRMWARE_BOARDS += k1b-kalray_ab04

firmware_k1b-kalray_emb01_CC              := $(CCACHE) k1-gcc
firmware_k1b-kalray_emb01_CFLAGS          := -march=k1b -mos=bare -mcore=k1bio -mcluster=ioddr -mprocessing=single -mboard=emb01
firmware_k1b-kalray_emb01_LDFLAGS         :=
firmware_k1b-kalray_emb01_INSTDIR          = $(K1ST_DIR)/share/odp/firmware/emb01/k1b
_FIRMWARE_BOARDS += k1b-kalray_emb01

firmware_k1b-kalray_simu_CC               := $(CCACHE) k1-gcc
firmware_k1b-kalray_simu_CFLAGS           := -march=k1b -mos=bare -mcore=k1bio -mcluster=ioddr -mprocessing=single -DMAGIC_SCALL
firmware_k1b-kalray_simu_LDFLAGS          :=
firmware_k1b-kalray_simu_INSTDIR           = $(K1ST_DIR)/share/odp/firmware/simu/k1b
_FIRMWARE_BOARDS += k1b-kalray_simu

firmware_k1b-kalray_explorer_CC           := $(CCACHE) k1-gcc
firmware_k1b-kalray_explorer_CFLAGS       := -march=k1b -mos=bare -mcore=k1bio -mcluster=ioddr \
					     -mprocessing=single -mboard=explorer -DK1B_EXPLORER
firmware_k1b-kalray_explorer_LDFLAGS      :=
firmware_k1b-kalray_explorer_INSTDIR       = $(K1ST_DIR)/share/odp/firmware/explorer/k1b
_FIRMWARE_BOARDS += k1b-kalray_explorer

#
#
#
# Debug Firmware Definitions
#
#
#
define FIRMWARE_CONFIG_RULE
#$(1) config name
firmware_$(1)-debug_CC          := $(firmware_$(1)_CC)
firmware_$(1)-debug_CFLAGS      := $(firmware_$(1)_CFLAGS) -DVERBOSE -DDEBUG
firmware_$(1)-debug_LDFLAGS     := $(firmware_$(1)_LDFLAGS)
firmware_$(1)-debug_INSTDIR     := $(shell echo "$(firmware_$(1)_INSTDIR)" | sed -e 'sL\(/firmware/[^/]*\)/L\1-debug/L')
endef

$(foreach firmware, $(_FIRMWARE_BOARDS), \
  $(eval $(call FIRMWARE_CONFIG_RULE,$(firmware))) \
  $(eval _FIRMWARE_BOARDS += $(firmware)-debug) \
)



#
# IOPCIE
#
iopcie_CONFIGS += k1b-kalray_developer
iopcie_CONFIGS += k1b-kalray_konic80
iopcie_CONFIGS += k1b-kalray_ab04
iopcie_CONFIGS += k1b-kalray_emb01
iopcie_CONFIGS += k1b-kalray_simu
_iopcie_CONFIGS += k1b-kalray_explorer

#
# IOPCIE-JUMBO
#
iopcie-jumbo_CONFIGS += k1b-kalray_developer
iopcie-jumbo_CONFIGS += k1b-kalray_konic80
iopcie-jumbo_CONFIGS += k1b-kalray_ab04
iopcie-jumbo_CONFIGS += k1b-kalray_emb01
iopcie-jumbo_CONFIGS += k1b-kalray_simu
_iopcie-jumbo_CONFIGS += k1b-kalray_explorer

#
# IOUNIFIED
#
iounified_CONFIGS += k1b-kalray_developer
iounified_CONFIGS += k1b-kalray_simu
iounified_CONFIGS += k1b-kalray_konic80
iounified_CONFIGS += k1b-kalray_ab04
iounified_CONFIGS += k1b-kalray_emb01

#
# IOUNIFIED-JUMBO
#
iounified-jumbo_CONFIGS += k1b-kalray_developer
iounified-jumbo_CONFIGS += k1b-kalray_simu
iounified-jumbo_CONFIGS += k1b-kalray_konic80
iounified-jumbo_CONFIGS += k1b-kalray_ab04
iounified-jumbo_CONFIGS += k1b-kalray_emb01


#
# IOETH
#
_ioeth_CONFIGS += k1b-kalray_developer
_ioeth_CONFIGS += k1b-kalray_konic80
_ioeth_CONFIGS += k1b-kalray_ab04
_ioeth_CONFIGS += k1b-kalray_emb01

#
# IOETH storage
#
_ioeth-storage_CONFIGS += k1b-kalray_konic80

$(foreach firmware,$(FIRMWARES), \
	$(foreach flavour,$($(firmware)_CONFIGS), \
		$(eval $(firmware)_CONFIGS += $(flavour)-debug) \
	) \
	$(foreach flavour,$(_$(firmware)_CONFIGS), \
		$(eval _$(firmware)_CONFIGS += $(flavour)-debug) \
	) \
)
#
#
#
# Board Definitions
#
#
#
BOARD_developer     := developer
BOARD_developer_VER := hw
BOARDS              += developer

BOARD_simu          := developer
BOARD_simu_VER      := simu
BOARDS              += simu

BOARD_explorer      := explorer
BOARD_explorer_VER  := explorer
_BOARDS              += explorer

BOARD_konic80       := konic80
BOARD_konic80_VER   := hw
BOARDS              += konic80

BOARD_emb01         := emb01
BOARD_emb01_VER     := hw
BOARDS              += emb01

BOARD_ab04          := ab04
BOARD_ab04_VER      := hw
BOARDS              += ab04

$(foreach B, $(_BOARDS), \
 $(eval BOARD_$(B)-debug     := $(BOARD_$(B))) \
 $(eval BOARD_$(B)-debug_VER := $(BOARD_$(B)_VER)-debug) \
 $(eval _BOARDS += $(B)-debug))
$(foreach B, $(BOARDS), \
 $(eval BOARD_$(B)-debug     := $(BOARD_$(B))) \
 $(eval BOARD_$(B)-debug_VER := $(BOARD_$(B)_VER)-debug) \
 $(eval BOARDS += $(B)-debug))
#
#
#
# OS Definitions
#
#
#
OS_mos := bare
OSs += mos

OS_nodeos := nodeos
OSs += nodeos

LONG_CONFIGS ?= $(foreach OS, $(OSs),\
					 $(foreach BOARD, $(BOARDS), k1b-kalray-$(OS)_$(BOARD)))
_LONG_CONFIGS ?= $(foreach OS, $(OSs) $(_OSs),\
					 $(foreach BOARD, $(BOARDS) $(_BOARDS), k1b-kalray-$(OS)_$(BOARD)))
