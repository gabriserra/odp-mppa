# Uncomment this if you need to change the CUSTOM_STR string
export CUSTOM_STR=https://github.com/kalray/odp-mppa.git

include $(top_srcdir)/platform/Makefile.inc

AM_CFLAGS +=  -I$(srcdir)/include
AM_CFLAGS +=  -I$(top_srcdir)/include
AM_CFLAGS +=  -I$(top_srcdir)/helper/include

include_HEADERS = \
		  $(top_srcdir)/include/odp.h

odpincludedir= $(includedir)/odp
odpinclude_HEADERS = \
		  $(srcdir)/include/odp/align.h \
		  $(srcdir)/include/odp/atomic.h \
		  $(srcdir)/include/odp/barrier.h \
		  $(srcdir)/include/odp/buffer.h \
		  $(srcdir)/include/odp/byteorder.h \
		  $(srcdir)/include/odp/classification.h \
		  $(srcdir)/include/odp/compiler.h \
		  $(srcdir)/include/odp/config.h \
		  $(srcdir)/include/odp/cpu.h \
		  $(srcdir)/include/odp/cpumask.h \
		  $(srcdir)/include/odp/crypto.h \
		  $(srcdir)/include/odp/debug.h \
		  $(srcdir)/include/odp/errno.h \
		  $(srcdir)/include/odp/event.h \
		  $(srcdir)/include/odp/hash.h \
		  $(srcdir)/include/odp/hints.h \
		  $(srcdir)/include/odp/init.h \
		  $(srcdir)/include/odp/packet_flags.h \
		  $(srcdir)/include/odp/packet.h \
		  $(srcdir)/include/odp/packet_io.h \
		  $(srcdir)/include/odp/pool.h \
		  $(srcdir)/include/odp/queue.h \
		  $(srcdir)/include/odp/random.h \
		  $(srcdir)/include/odp/rwlock.h \
		  $(srcdir)/include/odp/rwlock_recursive.h \
		  $(srcdir)/include/odp/schedule.h \
		  $(srcdir)/include/odp/schedule_types.h \
		  $(srcdir)/include/odp/shared_memory.h \
		  $(srcdir)/include/odp/spinlock.h \
		  $(srcdir)/include/odp/spinlock_recursive.h \
		  $(srcdir)/include/odp/std_clib.h \
		  $(srcdir)/include/odp/std_types.h \
		  $(srcdir)/include/odp/sync.h \
		  $(srcdir)/include/odp/system_info.h \
		  $(srcdir)/include/odp/thread.h \
		  $(srcdir)/include/odp/thrmask.h \
		  $(srcdir)/include/odp/ticketlock.h \
		  $(srcdir)/include/odp/time.h \
		  $(srcdir)/include/odp/timer.h \
		  $(srcdir)/include/odp/version.h

odpplatincludedir= $(includedir)/odp/plat
odpplatinclude_HEADERS = \
		  $(srcdir)/include/odp/plat/atomic_types.h \
		  $(srcdir)/include/odp/plat/barrier_types.h \
		  $(srcdir)/include/odp/plat/buffer_types.h \
		  $(srcdir)/include/odp/plat/byteorder_types.h \
		  $(srcdir)/include/odp/plat/classification_types.h \
		  $(srcdir)/include/odp/plat/cpumask_types.h \
		  $(srcdir)/include/odp/plat/crypto_types.h \
		  $(srcdir)/include/odp/plat/endian.h \
		  $(srcdir)/include/odp/plat/event_types.h \
		  $(srcdir)/include/odp/plat/init_types.h \
		  $(srcdir)/include/odp/plat/packet_types.h \
		  $(srcdir)/include/odp/plat/packet_io_types.h \
		  $(srcdir)/include/odp/plat/pool_types.h \
		  $(srcdir)/include/odp/plat/queue_types.h \
		  $(srcdir)/include/odp/plat/rwlock_types.h \
		  $(srcdir)/include/odp/plat/rwlock_recursive_types.h \
		  $(srcdir)/include/odp/plat/schedule_types.h \
		  $(srcdir)/include/odp/plat/shared_memory_types.h \
		  $(srcdir)/include/odp/plat/spinlock_types.h \
		  $(srcdir)/include/odp/plat/spinlock_recursive_types.h \
		  $(srcdir)/include/odp/plat/strong_types.h \
		  $(srcdir)/include/odp/plat/thread_types.h \
		  $(srcdir)/include/odp/plat/thrmask_types.h \
		  $(srcdir)/include/odp/plat/ticketlock_types.h \
		  $(srcdir)/include/odp/plat/time_types.h \
		  $(srcdir)/include/odp/plat/timer_types.h \
		  $(srcdir)/include/odp/plat/version_types.h

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
		  $(srcdir)/include/odp_schedule_internal.h \
		  $(srcdir)/include/odp_spin_internal.h \
		  $(srcdir)/include/odp_timer_internal.h \
		  $(srcdir)/include/odp_timer_types_internal.h \
		  $(srcdir)/include/odp_tx_uc_internal.h

noinst_HEADERS = \
		  $(srcdir)/Makefile.inc


__LIB__libodp_la_LIBADD = -lmppanoc -lmpparouting

__LIB__libodp_la_SOURCES = \
			   ../linux-generic/odp_barrier.c \
			   odp_buffer.c \
			   odp_buffer_ring.c \
			   ../linux-generic/odp_classification.c \
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
			   ../linux-generic/odp_version.c \
			   ../linux-generic/odp_weak.c \
			   ucode_fw/ucode_eth_v2.c \
			   ucode_fw/ucode_pcie_v2.c \
			   ucode_fw/odp_ucode_linear.c


if crypto
__LIB__libodp_la_SOURCES += odp_crypto.c
endif

