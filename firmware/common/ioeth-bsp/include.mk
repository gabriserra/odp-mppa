ifndef __IOETH_BSP_INCLUDED__
__IOETH_BSP_INCLUDED__ := 1

# Add magic for BSP here
IOETHBSP_DIR := $(dir $(realpath $(lastword $(MAKEFILE_LIST))))
SRCDIRS  += $(IOETHBSP_DIR)

_LDFLAGS +=-T$(IOETHBSP_DIR)/platform.ld -L$(IOETHBSP_DIR)  -Wl,--defsym=K1_BOOT_ADDRESS=0x00200000 -Wl,--defsym=K1_EXCEPTION_ADDRESS=0x00200400  -Wl,--defsym=K1_INTERRUPT_ADDRESS=0x00200800 -Wl,--defsym=K1_SYSCALL_ADDRESS=0x00200c00 -Wl,--allow-multiple-definition

endif
