SRCDIR   := $(dir $(realpath $(lastword $(MAKEFILE_LIST))))
SRCDIRS  := $(SRCDIR)
FIRMDIR := $(realpath $(ODP_TOOLCHAIN_DIR)/share/odp/build/firmware)

####################################
# Choose the wanted RPC modules    #
# for the firmware                 #
####################################
include $(FIRMDIR)/common/rpc/include.mk
include $(FIRMDIR)/common/eth/include.mk
include $(FIRMDIR)/common/boot/include.mk
include $(FIRMDIR)/common/rnd/include.mk
include $(FIRMDIR)/common/pcie/include.mk
include $(FIRMDIR)/common/netdev/include.mk
include $(FIRMDIR)/Makefile
