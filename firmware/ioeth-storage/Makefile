SRCDIR   := $(dir $(realpath $(lastword $(MAKEFILE_LIST))))
SRCDIRS  := $(SRCDIR)

EXTRA_BINS += $(BUILDDIR)/$(FIRMDIR).bin

include $(SRCDIR)/../common/rpc/include.mk
include $(SRCDIR)/../common/pcie/include.mk
include $(SRCDIR)/../common/c2c/include.mk
include $(SRCDIR)/../common/eth/include.mk
include $(SRCDIR)/../common/rnd/include.mk
include $(SRCDIR)/../common/fp/include.mk
include $(SRCDIR)/../common/ioeth-bsp-storage/include.mk
include $(SRCDIR)/../Makefile

$(BUILDDIR)/$(FIRMDIR).bin: $(BUILDDIR)/$(FIRMDIR).kelf
	cp $< $<.tmp
	dd if=$(K1_TOOLCHAIN_DIR)/lib/firmware/kalray/dtb/k1b/$(lastword $(subst _, ,$(ODP_FIRMWARE_CONFIG)))/default.dtb bs=1  of=$<.tmp bs=1 seek=$$(echo "ibase=16; $$(k1-readelf --sections $< | grep .dtb | awk '{print 0x toupper($$5)}')" | bc) conv=notrunc
	k1-objcopy -Obinary $<.tmp $@
