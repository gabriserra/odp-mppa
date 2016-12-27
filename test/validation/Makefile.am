include $(top_srcdir)/test/Makefile.inc

ODP_MODULES = atomic \
	      barrier \
	      buffer \
	      classification \
	      cpumask \
	      errno \
	      hash \
	      init \
	      lock \
	      queue \
	      packet \
	      pktio \
	      pool \
	      scheduler \
	      std_clib \
	      thread \
	      time \
	      timer \
	      traffic_mngr \
	      shmem \
	      system

if crypto
  ODP_MODULES  += crypto random
endif


SUBDIRS = common $(ODP_MODULES)

#The tests will need to retain the deprecated test implementation
AM_CFLAGS += -Wno-deprecated-declarations
