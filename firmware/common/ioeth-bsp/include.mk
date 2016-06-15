ifndef __IOETH_BSP_INCLUDED__
__IOETH_BSP_INCLUDED__ := 1

# Add magic for BSP here
IOETHBSP_DIR := $(dir $(realpath $(lastword $(MAKEFILE_LIST))))
SRCDIRS  += $(IOETHBSP_DIR)

_CFLAGS  += -DLINUX_FIRMWARE -DUNUSABLE_TX=4 -DUNUSABLE_RX=128
_LDFLAGS +=-T$(IOETHBSP_DIR)/platform.ld -L$(IOETHBSP_DIR)  -Wl,--defsym=K1_BOOT_ADDRESS=0xF0000000 -Wl,--defsym=K1_EXCEPTION_ADDRESS=0xF0000400  -Wl,--defsym=K1_INTERRUPT_ADDRESS=0xF0000800 -Wl,--defsym=K1_SYSCALL_ADDRESS=0xF0000c00 -Wl,--allow-multiple-definition

endif
