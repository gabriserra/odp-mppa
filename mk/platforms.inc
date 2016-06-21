CONFIGS :=
K1_CONFIGS :=
_CONFIGS :=

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
k1b-kalray-nodeos_developer_CONF_ENV    := CC="$(CCACHE) k1-nodeos-gcc"  CXX="$(CCACHE) k1-nodeos-g++" STRIP="k1-strip --strip-debug"
k1b-kalray-nodeos_developer_CONF_OPTS   :=
k1b-kalray-nodeos_developer_CFLAGS      := -march=k1b -DK1_NODEOS
k1b-kalray-nodeos_developer_LDFLAGS     :=
k1b-kalray-nodeos_developer_PLATFORM    := mppa
k1b-kalray-nodeos_developer_INC_DIR     := k1-nodeos
k1b-kalray-nodeos_developer_MAKE_VALID  := -j1
k1b-kalray-nodeos_developer_INSTALL     := true
K1_CONFIGS += k1b-kalray-nodeos_developer

k1b-kalray-nodeos_explorer_CONF_ENV    := CC="$(CCACHE) k1-nodeos-gcc"  CXX="$(CCACHE) k1-nodeos-g++" STRIP="k1-strip --strip-debug"
k1b-kalray-nodeos_explorer_CONF_OPTS   :=
k1b-kalray-nodeos_explorer_CFLAGS      := -march=k1b -DK1_NODEOS -mboard=explorer -DK1B_EXPLORER
k1b-kalray-nodeos_explorer_LDFLAGS     :=
k1b-kalray-nodeos_explorer_PLATFORM    := mppa
k1b-kalray-nodeos_explorer_INC_DIR     := k1-nodeos
k1b-kalray-nodeos_explorer_MAKE_VALID  := -j1
k1b-kalray-nodeos_explorer_INSTALL     := true
_CONFIGS += k1b-kalray-nodeos_explorer

k1b-kalray-nodeos_simu_CONF_ENV    := CC="$(CCACHE) k1-nodeos-gcc"  CXX="$(CCACHE) k1-nodeos-g++" STRIP="k1-strip --strip-debug"
k1b-kalray-nodeos_simu_CONF_OPTS   :=
k1b-kalray-nodeos_simu_CFLAGS      := -march=k1b -DK1_NODEOS -DMAGIC_SCALL
k1b-kalray-nodeos_simu_LDFLAGS     :=
k1b-kalray-nodeos_simu_PLATFORM    := mppa
k1b-kalray-nodeos_simu_INC_DIR     := k1-nodeos
k1b-kalray-nodeos_simu_MAKE_VALID  :=
k1b-kalray-nodeos_simu_INSTALL     := true
K1_CONFIGS += k1b-kalray-nodeos_simu

k1b-kalray-nodeos_konic80_CONF_ENV    := CC="$(CCACHE) k1-nodeos-gcc"  CXX="$(CCACHE) k1-nodeos-g++" STRIP="k1-strip --strip-debug"
k1b-kalray-nodeos_konic80_CONF_OPTS   :=
k1b-kalray-nodeos_konic80_CFLAGS      := -march=k1b -DK1_NODEOS -mboard=konic80
k1b-kalray-nodeos_konic80_LDFLAGS     :=
k1b-kalray-nodeos_konic80_PLATFORM    := mppa
k1b-kalray-nodeos_konic80_INC_DIR     := k1-nodeos
k1b-kalray-nodeos_konic80_MAKE_VALID  := -j1
k1b-kalray-nodeos_konic80_INSTALL     := true
K1_CONFIGS += k1b-kalray-nodeos_konic80

k1b-kalray-nodeos_emb01_CONF_ENV    := CC="$(CCACHE) k1-nodeos-gcc"  CXX="$(CCACHE) k1-nodeos-g++" STRIP="k1-strip --strip-debug"
k1b-kalray-nodeos_emb01_CONF_OPTS   :=
k1b-kalray-nodeos_emb01_CFLAGS      := -march=k1b -DK1_NODEOS -mboard=emb01
k1b-kalray-nodeos_emb01_LDFLAGS     :=
k1b-kalray-nodeos_emb01_PLATFORM    := mppa
k1b-kalray-nodeos_emb01_INC_DIR     := k1-nodeos
k1b-kalray-nodeos_emb01_MAKE_VALID  := -j1
k1b-kalray-nodeos_emb01_INSTALL     := true
K1_CONFIGS += k1b-kalray-nodeos_emb01

k1b-kalray-mos_developer_CONF_ENV    := CC="$(CCACHE) k1-gcc"  CXX="$(CCACHE) k1-g++" STRIP="k1-strip --strip-debug"
k1b-kalray-mos_developer_CONF_OPTS   :=
k1b-kalray-mos_developer_CFLAGS      := -march=k1b -mhypervisor -DK1_MOS
k1b-kalray-mos_developer_LDFLAGS     := -mhypervisor -lvbsp -lutask -Wl,--defsym=USER_STACK_SIZE=5120 \
				   -Wl,--undefined=DEBUGGER_HAVE_USER_MODE_SUPPORT
k1b-kalray-mos_developer_PLATFORM    := mppa
k1b-kalray-mos_developer_INC_DIR     := k1-elf
k1b-kalray-mos_developer_MAKE_VALID  := -j1
k1b-kalray-mos_developer_INSTALL     := true
k1b-kalray-mos_developer_INSTALL_DOC := true
K1_CONFIGS += k1b-kalray-mos_developer

k1b-kalray-mos_explorer_CONF_ENV    := CC="$(CCACHE) k1-gcc"  CXX="$(CCACHE) k1-g++" STRIP="k1-strip --strip-debug"
k1b-kalray-mos_explorer_CONF_OPTS   :=
k1b-kalray-mos_explorer_CFLAGS      := -march=k1b -mhypervisor -DK1_MOS -mboard=explorer -DK1B_EXPLORER
k1b-kalray-mos_explorer_LDFLAGS     := -mhypervisor -lvbsp -lutask -Wl,--defsym=USER_STACK_SIZE=5120 \
				   -Wl,--undefined=DEBUGGER_HAVE_USER_MODE_SUPPORT
k1b-kalray-mos_explorer_PLATFORM    := mppa
k1b-kalray-mos_explorer_INC_DIR     := k1-elf
k1b-kalray-mos_explorer_MAKE_VALID  := -j1
k1b-kalray-mos_explorer_INSTALL     := true
_CONFIGS += k1b-kalray-mos_explorer

k1b-kalray-mos_konic80_CONF_ENV    := CC="$(CCACHE) k1-gcc"  CXX="$(CCACHE) k1-g++" STRIP="k1-strip --strip-debug"
k1b-kalray-mos_konic80_CONF_OPTS   :=
k1b-kalray-mos_konic80_CFLAGS      := -march=k1b -mhypervisor -DK1_MOS -mboard=konic80
k1b-kalray-mos_konic80_LDFLAGS     := -mhypervisor -lvbsp -lutask -Wl,--defsym=USER_STACK_SIZE=5120 \
				   -Wl,--undefined=DEBUGGER_HAVE_USER_MODE_SUPPORT
k1b-kalray-mos_konic80_PLATFORM    := mppa
k1b-kalray-mos_konic80_INC_DIR     := k1-elf
k1b-kalray-mos_konic80_MAKE_VALID  := -j1
k1b-kalray-mos_konic80_INSTALL     := true
K1_CONFIGS += k1b-kalray-mos_konic80

k1b-kalray-mos_emb01_CONF_ENV    := CC="$(CCACHE) k1-gcc"  CXX="$(CCACHE) k1-g++" STRIP="k1-strip --strip-debug"
k1b-kalray-mos_emb01_CONF_OPTS   :=
k1b-kalray-mos_emb01_CFLAGS      := -march=k1b -mhypervisor -DK1_MOS -mboard=emb01
k1b-kalray-mos_emb01_LDFLAGS     := -mhypervisor -lvbsp -lutask -Wl,--defsym=USER_STACK_SIZE=5120 \
				   -Wl,--undefined=DEBUGGER_HAVE_USER_MODE_SUPPORT
k1b-kalray-mos_emb01_PLATFORM    := mppa
k1b-kalray-mos_emb01_INC_DIR     := k1-elf
k1b-kalray-mos_emb01_MAKE_VALID  := -j1
k1b-kalray-mos_emb01_INSTALL     := true
K1_CONFIGS += k1b-kalray-mos_emb01

k1b-kalray-mos_simu_CONF_ENV    := CC="$(CCACHE) k1-gcc"  CXX="$(CCACHE) k1-g++" STRIP="k1-strip --strip-debug"
k1b-kalray-mos_simu_CONF_OPTS   :=
k1b-kalray-mos_simu_CFLAGS      := -march=k1b -mhypervisor -DK1_MOS -DMAGIC_SCALL
k1b-kalray-mos_simu_LDFLAGS     := -mhypervisor -lvbsp -lutask -Wl,--defsym=USER_STACK_SIZE=5120 \
				   -Wl,--undefined=DEBUGGER_HAVE_USER_MODE_SUPPORT

k1b-kalray-mos_simu_PLATFORM    := mppa
k1b-kalray-mos_simu_INC_DIR     := k1-elf
k1b-kalray-mos_simu_MAKE_VALID  :=
k1b-kalray-mos_simu_INSTALL     := true
K1_CONFIGS += k1b-kalray-mos_simu

CONFIGS += $(K1_CONFIGS)

# Buid long for all BUT x86_64
LONG_CONFIGS ?= $(K1_CONFIGS)
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
CONFIGS += x86_64-unknown-linux-gnu

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

firmware_k1b-kalray_konic80_CC            := $(CCACHE) k1-gcc
firmware_k1b-kalray_konic80_CFLAGS        := -march=k1b -mos=bare -mcore=k1bio -mcluster=ioddr -mprocessing=single -mboard=konic80
firmware_k1b-kalray_konic80_LDFLAGS       :=
firmware_k1b-kalray_konic80_INSTDIR        = $(K1ST_DIR)/share/odp/firmware/konic80/k1b

firmware_k1b-kalray_emb01_CC              := $(CCACHE) k1-gcc
firmware_k1b-kalray_emb01_CFLAGS          := -march=k1b -mos=bare -mcore=k1bio -mcluster=ioddr -mprocessing=single -mboard=emb01
firmware_k1b-kalray_emb01_LDFLAGS         :=
firmware_k1b-kalray_emb01_INSTDIR          = $(K1ST_DIR)/share/odp/firmware/emb01/k1b

firmware_k1b-kalray_simu_CC               := $(CCACHE) k1-gcc
firmware_k1b-kalray_simu_CFLAGS           := -march=k1b -mos=bare -mcore=k1bio -mcluster=ioddr -mprocessing=single -DMAGIC_SCALL
firmware_k1b-kalray_simu_LDFLAGS          :=
firmware_k1b-kalray_simu_INSTDIR           = $(K1ST_DIR)/share/odp/firmware/simu/k1b

firmware_k1b-kalray_explorer_CC           := $(CCACHE) k1-gcc
firmware_k1b-kalray_explorer_CFLAGS       := -march=k1b -mos=bare -mcore=k1bio -mcluster=ioddr \
					     -mprocessing=single -mboard=explorer -DK1B_EXPLORER
firmware_k1b-kalray_explorer_LDFLAGS      :=
firmware_k1b-kalray_explorer_INSTDIR       = $(K1ST_DIR)/share/odp/firmware/explorer/k1b


#
# IOPCIE
#
iopcie_CONFIGS += k1b-kalray_developer
iopcie_CONFIGS += k1b-kalray_konic80
iopcie_CONFIGS += k1b-kalray_emb01
iopcie_CONFIGS += k1b-kalray_simu
_iopcie_CONFIGS += k1b-kalray_explorer

#
# IOUNIFIED
#
iounified_CONFIGS += k1b-kalray_developer
iounified_CONFIGS += k1b-kalray_simu
iounified_CONFIGS += k1b-kalray_konic80
iounified_CONFIGS += k1b-kalray_emb01


#
# IOETH
#
ioeth_CONFIGS += k1b-kalray_developer
ioeth_CONFIGS += k1b-kalray_simu
ioeth_CONFIGS += k1b-kalray_konic80
ioeth_CONFIGS += k1b-kalray_emb01

#
# Netdev loopback
#
_netdev-loopback_CONFIGS += k1b-kalray_developer
_netdev-loopback_CONFIGS += k1b-kalray_konic80
