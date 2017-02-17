# Uncomment this if you need to change the CUSTOM_STR string
export CUSTOM_STR=https://github.com/kalray/odp-mppa.git

include $(top_srcdir)/platform/Makefile.inc


pkgconfig_DATA = $(top_builddir)/pkgconfig/libodp-mppa.pc

.PHONY: pkgconfig/libodp-mppa.pc

lib_LTLIBRARIES = $(LIB)/libodp-mppa.la

AM_CFLAGS +=  -I$(srcdir)/include
AM_CFLAGS +=  -I$(top_srcdir)/include
AM_CFLAGS +=  -I$(top_srcdir)/helper/include

include_HEADERS = \
		  $(top_srcdir)/include/odp.h \
		  $(top_srcdir)/include/odp_api.h


odpapiincludedir= $(includedir)/odp/api
odpapiinclude_HEADERS = \
		  $(srcdir)/include/odp/api/align.h \
		  $(srcdir)/include/odp/api/atomic.h \
		  $(srcdir)/include/odp/api/barrier.h \
		  $(srcdir)/include/odp/api/buffer.h \
		  $(srcdir)/include/odp/api/byteorder.h \
		  $(srcdir)/include/odp/api/classification.h \
		  $(srcdir)/include/odp/api/compiler.h \
		  $(srcdir)/include/odp/api/cpu.h \
		  $(srcdir)/include/odp/api/cpumask.h \
		  $(srcdir)/include/odp/api/crypto.h \
		  $(srcdir)/include/odp/api/debug.h \
		  $(srcdir)/include/odp/api/errno.h \
		  $(srcdir)/include/odp/api/event.h \
		  $(srcdir)/include/odp/api/hash.h \
		  $(srcdir)/include/odp/api/hints.h \
		  $(srcdir)/include/odp/api/init.h \
		  $(srcdir)/include/odp/api/packet_flags.h \
		  $(srcdir)/include/odp/api/packet.h \
		  $(srcdir)/include/odp/api/packet_io.h \
		  $(srcdir)/include/odp/api/packet_io_stats.h \
		  $(srcdir)/include/odp/api/pool.h \
		  $(srcdir)/include/odp/api/queue.h \
		  $(srcdir)/include/odp/api/random.h \
		  $(srcdir)/include/odp/api/rwlock.h \
		  $(srcdir)/include/odp/api/rwlock_recursive.h \
		  $(srcdir)/include/odp/api/schedule.h \
		  $(srcdir)/include/odp/api/schedule_types.h \
		  $(srcdir)/include/odp/api/shared_memory.h \
		  $(srcdir)/include/odp/api/spinlock.h \
		  $(srcdir)/include/odp/api/spinlock_recursive.h \
		  $(srcdir)/include/odp/api/std_clib.h \
		  $(srcdir)/include/odp/api/std_types.h \
		  $(srcdir)/include/odp/api/sync.h \
		  $(srcdir)/include/odp/api/system_info.h \
		  $(srcdir)/include/odp/api/thread.h \
		  $(srcdir)/include/odp/api/thrmask.h \
		  $(srcdir)/include/odp/api/ticketlock.h \
		  $(srcdir)/include/odp/api/time.h \
		  $(srcdir)/include/odp/api/timer.h \
		  $(srcdir)/include/odp/api/traffic_mngr.h \
		  $(srcdir)/include/odp/api/version.h \
		  $(srcdir)/include/odp/api/visibility_begin.h \
		  $(srcdir)/include/odp/api/visibility_end.h

odpapiplatincludedir= $(includedir)/odp/api/plat
odpapiplatinclude_HEADERS = \
		  $(srcdir)/include/odp/api/plat/atomic_types.h \
		  $(srcdir)/include/odp/api/plat/barrier_types.h \
		  $(srcdir)/include/odp/api/plat/buffer_types.h \
		  $(srcdir)/include/odp/api/plat/byteorder_types.h \
		  $(srcdir)/include/odp/api/plat/classification_types.h \
		  $(srcdir)/include/odp/api/plat/cpumask_types.h \
		  $(srcdir)/include/odp/api/plat/crypto_types.h \
		  $(srcdir)/include/odp/api/plat/endian.h \
		  $(srcdir)/include/odp/api/plat/event_types.h \
		  $(srcdir)/include/odp/api/plat/init_types.h \
		  $(srcdir)/include/odp/api/plat/packet_types.h \
		  $(srcdir)/include/odp/api/plat/packet_io_types.h \
		  $(srcdir)/include/odp/api/plat/pool_types.h \
		  $(srcdir)/include/odp/api/plat/queue_types.h \
		  $(srcdir)/include/odp/api/plat/rwlock_types.h \
		  $(srcdir)/include/odp/api/plat/rwlock_recursive_types.h \
		  $(srcdir)/include/odp/api/plat/schedule_types.h \
		  $(srcdir)/include/odp/api/plat/shared_memory_types.h \
		  $(srcdir)/include/odp/api/plat/spinlock_types.h \
		  $(srcdir)/include/odp/api/plat/spinlock_recursive_types.h \
		  $(srcdir)/include/odp/api/plat/strong_types.h \
		  $(srcdir)/include/odp/api/plat/thread_types.h \
		  $(srcdir)/include/odp/api/plat/thrmask_types.h \
		  $(srcdir)/include/odp/api/plat/ticketlock_types.h \
		  $(srcdir)/include/odp/api/plat/time_types.h \
		  $(srcdir)/include/odp/api/plat/timer_types.h \
		  $(srcdir)/include/odp/api/plat/traffic_mngr_types.h \
		  $(srcdir)/include/odp/api/plat/version_types.h

odprpcincludedir= $(includedir)/odp/rpc
odprpcinclude_HEADERS = \
		  $(srcdir)/include/odp/rpc/api.h \
		  $(srcdir)/include/odp/rpc/bas.h \
		  $(srcdir)/include/odp/rpc/c2c.h \
		  $(srcdir)/include/odp/rpc/defines.h \
		  $(srcdir)/include/odp/rpc/eth.h \
		  $(srcdir)/include/odp/rpc/helpers.h \
		  $(srcdir)/include/odp/rpc/pcie.h \
		  $(srcdir)/include/odp/rpc/rnd.h \
		  $(srcdir)/include/odp/rpc/fp.h \
		  $(srcdir)/include/odp/rpc/rpc.h


odpinternalincludedir= $(oldincludedir)/odp/internal
odpinternalinclude_HEADERS = \
		  $(srcdir)/include/odp_align_internal.h \
		  $(srcdir)/include/odp_atomic_internal.h \
		  $(srcdir)/include/odp_buffer_inlines.h \
		  $(srcdir)/include/odp_buffer_internal.h \
		  $(srcdir)/include/odp_buffer_ring_internal.h \
		  $(srcdir)/include/odp_classification_datamodel.h \
		  $(srcdir)/include/odp_classification_inlines.h \
		  $(srcdir)/include/odp_classification_internal.h \
		  $(srcdir)/include/odp_config_internal.h \
		  $(srcdir)/include/odp_crypto_internal.h \
		  $(srcdir)/include/odp_debug_internal.h \
		  $(srcdir)/include/odp_forward_typedefs_internal.h \
		  $(srcdir)/include/odp_internal.h \
		  $(srcdir)/include/odp_macros_internal.h \
		  $(srcdir)/include/odp_packet_internal.h \
		  $(srcdir)/include/odp_packet_io_internal.h \
		  $(srcdir)/include/odp_packet_io_queue.h \
		  $(srcdir)/include/odp_pool_internal.h \
		  $(srcdir)/include/odp_queue_internal.h \
		  $(srcdir)/include/odp_rx_internal.h \
		  $(srcdir)/include/odp_schedule_if.h \
		  $(srcdir)/include/odp_schedule_internal.h \
		  $(srcdir)/include/odp_timer_internal.h \
		  $(srcdir)/include/odp_timer_types_internal.h \
		  $(srcdir)/include/odp_tx_uc_internal.h \
		  ${srcdir}/include/protocols/eth.h \
		  ${srcdir}/include/protocols/ip.h \
		  ${srcdir}/include/protocols/ipsec.h \
		  ${srcdir}/include/protocols/tcp.h \
		  ${srcdir}/include/protocols/udp.h

noinst_HEADERS = \
		  $(srcdir)/Makefile.inc


__LIB__libodp_mppa_la_LIBADD = -lmppanoc -lmpparouting

__LIB__libodp_mppa_la_SOURCES = \
			   odp_atomic.c \
			   ../linux-generic/odp_barrier.c \
			   odp_buffer.c \
			   odp_buffer_ring.c \
			   odp_classification.c \
			   odp_cpu.c \
			   odp_cpumask.c \
			   odp_cpumask_task.c \
			   ../linux-generic/odp_errno.c \
			   odp_event.c \
			   ../linux-generic/odp_hash.c \
			   odp_init.c \
			   ../linux-generic/odp_impl.c \
			   odp_packet.c \
			   ../linux-generic/odp_packet_flags.c \
			   odp_packet_io.c \
			   pktio/io_ops.c \
			   pktio/eth.c \
			   pktio/pcie.c \
			   pktio/pcie_print.c \
			   pktio/ioddr.c \
			   pktio/cluster.c \
			   pktio/drop.c \
			   pktio/magic.c \
			   pktio/loop.c \
			   pktio/rx_thread.c \
			   pktio/tx_uc.c \
			   pktio/parse.c \
			   odp_pool.c \
			   odp_queue.c \
			   odp_rpc.c \
			   odp_rwlock.c \
			   odp_rwlock_recursive.c \
			   odp_schedule.c \
			   odp_schedule_if.c \
			   odp_schedule_ordered.c \
			   odp_shared_memory.c \
			   odp_sleep.c \
			   odp_spinlock.c \
			   odp_spinlock_recursive.c \
			   odp_system_info.c \
			   odp_thread.c \
			   ../linux-generic/odp_thrmask.c \
			   odp_ticketlock.c \
			   odp_time.c \
			   odp_timer.c \
			   os/@OS@/timer.c \
			   odp_tm.c \
			   ../linux-generic/odp_version.c \
			   ../linux-generic/odp_weak.c \
			   ucode_fw/ucode_eth_v2.c \
			   ucode_fw/ucode_pcie_v2.c \
			   ucode_fw/odp_ucode_linear.c


if crypto
__LIB__libodp_mppa_la_SOURCES += odp_crypto.c
endif

