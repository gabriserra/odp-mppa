NETDEVDIR := $(dir $(realpath $(lastword $(MAKEFILE_LIST))))
SRCDIRS   += $(NETDEVDIR)

HDRFILES  += $(TOP_SRCDIR)/mppaeth/mppa_pcie_netdev.h
_CFLAGS   += -I$(TOP_SRCDIR)/mppaeth
_LDFLAGS  += -lpcie_queue
