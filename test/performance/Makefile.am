include $(top_srcdir)/test/Makefile.inc

TESTS_ENVIRONMENT += TEST_DIR=${builddir}

COMPILE_ONLY = odp_l2fwd$(EXEEXT) \
				odp_scheduling$(EXEEXT) \
				odp_autoreply$(EXEEXT)
TESTSCRIPTS  =

if TARGET_IS_HW
EXECUTABLES = odp_atomic$(EXEEXT) odp_pktio_perf$(EXEEXT)
if ! TARGET_OS_MOS
TESTSCRIPTS += odp_scheduling_run
endif
else
if TARGET_IS_SIMU
COMPILE_ONLY += odp_atomic$(EXEEXT) odp_pktio_perf$(EXEEXT)
else
EXECUTABLES = odp_atomic$(EXEEXT) odp_pktio_perf$(EXEEXT)
TESTSCRIPTS += odp_l2fwd_run
if ! TARGET_OS_MOS
TESTSCRIPTS += odp_scheduling_run
endif
endif
endif

if test_perf
TESTS = $(EXECUTABLES) $(TESTSCRIPTS)
endif

bin_PROGRAMS = $(EXECUTABLES) $(COMPILE_ONLY)

odp_atomic_LDFLAGS = $(AM_LDFLAGS) -static
odp_atomic_CFLAGS = $(AM_CFLAGS) -I${top_srcdir}/test
odp_autoreply_LDFLAGS = $(AM_LDFLAGS) -static
odp_autoreply_CFLAGS = $(AM_CFLAGS) -I${top_srcdir}/test
odp_scheduling_LDFLAGS = $(AM_LDFLAGS) -static
odp_scheduling_CFLAGS = $(AM_CFLAGS) -I${top_srcdir}/test

noinst_HEADERS = \
		  $(top_srcdir)/test/test_debug.h

dist_odp_atomic_SOURCES = odp_atomic.c
dist_odp_scheduling_SOURCES = odp_scheduling.c
dist_odp_pktio_perf_SOURCES = odp_pktio_perf.c
dist_odp_autoreply_SOURCES  = odp_autoreply.c

dist_bin_SCRIPTS = $(TESTSCRIPTS)
