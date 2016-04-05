NETDEVDIR := $(dir $(realpath $(lastword $(MAKEFILE_LIST))))
SRCDIRS   += $(NETDEVDIR)

_CFLAGS   += -I$(TOP_SRCDIR)/mppaeth
_LDFLAGS  += -lpcie_queue
