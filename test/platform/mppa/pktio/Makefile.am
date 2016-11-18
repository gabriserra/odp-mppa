include $(top_srcdir)/test/validation/Makefile.inc

dist_bin_SCRIPTS = pktio_run \
	pktio_env

bin_PROGRAMS = pktio$(EXEEXT)
dist_pktio_SOURCES = pktio.c
pktio_LDADD  = $(top_builddir)/test/validation/pktio/libtestpktio.la $(LIBCUNIT_COMMON) $(LIBODP)
pktio_CFLAGS = $(AM_CFLAGS) -I$(top_srcdir)/test/validation/pktio/

EXTRA_DIST = $(top_srcdir)/test/validation/pktio/pktio.h
