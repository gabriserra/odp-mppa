/* Copyright (c) 2013, Linaro Limited
 * All rights reserved.
 *
 * SPDX-License-Identifier:     BSD-3-Clause
 */


/**
 * @file
 *
 * ODP packet IO - implementation internal
 */

#ifndef ODP_PACKET_IO_INTERNAL_H_
#define ODP_PACKET_IO_INTERNAL_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <odp/api/spinlock.h>
#include <odp/api/rwlock.h>
#include <odp_classification_datamodel.h>
#include <odp_align_internal.h>
#include <odp_debug_internal.h>
#include <odp_buffer_inlines.h>
#include <odp_rx_internal.h>
#include <odp/api/hints.h>

#define PKTIO_NAME_LEN 256
#define PKTIO_MAX_QUEUES 16

#define PKTIN_INVALID  ((odp_pktin_queue_t) {ODP_PKTIO_INVALID, 0})
#define PKTOUT_INVALID ((odp_pktout_queue_t) {ODP_PKTIO_INVALID, 0})

#ifndef ETH_ALEN
#define ETH_ALEN 6
#endif

/* Forward declaration */
struct pktio_if_ops;

typedef struct {
	mppa_dnoc_header_t header;
	mppa_dnoc_channel_config_t config;

	struct {
		uint8_t nofree : 1;
	};
} pkt_tx_uc_config;

typedef struct {
	int cnoc_rx;
	int min_rx;
	int max_rx;
	int n_credits;
	int pkt_count;
} pkt_c2c_cfg_t;

typedef struct {
	int clus_id;			/**< Cluster ID */
	odp_pool_t pool; 		/**< pool to alloc packets from */
	odp_bool_t promisc;		/**< promiscuous mode state */

	pkt_c2c_cfg_t local;
	pkt_c2c_cfg_t remote;

	int mtu;

	odp_spinlock_t wlock;
	rx_config_t rx_config;
	pkt_tx_uc_config tx_config;

	mppa_cnoc_config_t config;
	mppa_cnoc_header_t header;
} pkt_cluster_t;

typedef struct {
	int fd;				/**< magic syscall eth interface file descriptor */
	odp_pool_t pool; 		/**< pool to alloc packets from */
	size_t max_frame_len; 		/**< max frame len = buf_size - sizeof(pkt_hdr) */
	size_t buf_size; 		/**< size of buffer payload in 'pool' */
} pkt_magic_t;

typedef struct {
	odp_queue_t loopq;		/**< loopback queue for "loop" device */
	odp_bool_t promisc;		/**< promiscuous mode state */
} pkt_loop_t;

typedef struct {
	odp_pool_t pool;                /**< pool to alloc packets from */
	uint8_t mac_addr[ETH_ALEN];     /**< Interface Mac address */
	uint16_t mtu;                   /**< Interface MTU */
	struct {
		uint8_t loopback : 1;
		uint8_t jumbo : 1;
		uint8_t verbose : 1;
		uint8_t min_payload: 6;
		uint8_t max_payload: 6;
		uint8_t no_wait_link : 1;
	};

	/* Rx Data */
	rx_config_t rx_config;
	int promisc;

	uint8_t slot_id;                /**< IO Eth Id */
	uint8_t port_id;                /**< Eth Port id. 4 for 40G */

	/* Tx data */
	uint16_t tx_if;                 /**< Remote DMA interface to forward
					 *   to Eth Egress */
	uint16_t tx_tag;                /**< Remote DMA tag to forward to
					 *   Eth Egress */

	uint64_t lb_ts_off;             /** offset between lb timestamp and dsu timestamp */
	pkt_tx_uc_config tx_config;
} pkt_eth_t;


typedef struct {
	odp_pool_t pool;                /**< pool to alloc packets from */
	uint8_t mac_addr[ETH_ALEN];     /**< Interface Mac address */
	uint16_t mtu;                   /**< Interface MTU */

	/* Rx Data */
	rx_config_t rx_config;
	int promisc;
	int log2_fragments;

	uint8_t slot_id;                /**< IO Eth Id */

	mppa_cnoc_config_t config;
	mppa_cnoc_header_t header;
	uint64_t pkt_count;

	pkt_tx_uc_config tx_config;
} pkt_ioddr_t;

typedef struct {
	odp_pool_t pool;                /**< pool to alloc packets from */
	odp_spinlock_t wlock;           /**< Tx lock */
	uint8_t mac_addr[ETH_ALEN];     /**< Interface Mac address */
	uint16_t mtu;                   /**< Interface MTU */

	/* Rx Data */
	rx_config_t rx_config;

	uint8_t slot_id;                /**< IO Eth Id */
	uint8_t pcie_eth_if_id;         /**< PCIe ethernet interface */

	/* Tx data */
	uint16_t tx_if;                 /**< Remote DMA interface to forward
					 *   to Eth Egress */
	uint16_t min_tx_tag;            /**< Remote DMA first tag to forward to
					 *   Eth Egress */
	uint16_t max_tx_tag;            /**< Remote DMA last tag to forward to
					 *   Eth Egress */
	uint8_t  nb_tx_tags;            /**< Remote DMA number of tag to forward
					 *   to Eth Egress */
	uint16_t cnoc_rx;               /**< Cnoc RX port for cluster -> host
					 *   flow control */
	uint64_t pkt_count;             /**< Flow control credit for cluster
					 *   -> host */

	pkt_tx_uc_config tx_config;
} pkt_pcie_t;

struct pktio_entry {
	const struct pktio_if_ops *ops; /**< Implementation specific methods */
	odp_ticketlock_t rxl;		/**< RX ticketlock */
	odp_ticketlock_t txl;		/**< TX ticketlock */
	int cls_enabled;		/**< is classifier enabled */
	odp_pktio_t handle;		/**< pktio handle */

	enum {
		/* Not allocated */
		PKTIO_STATE_FREE = 0,
		/* Close pending on scheduler response. Next state after this
		 * is PKTIO_STATE_FREE. */
		PKTIO_STATE_CLOSE_PENDING,
		/* Open in progress.
		   Marker for all active states following under. */
		PKTIO_STATE_ACTIVE,
		/* Open completed */
		PKTIO_STATE_OPENED,
		/* Start completed */
		PKTIO_STATE_STARTED,
		/* Stop pending on scheduler response */
		PKTIO_STATE_STOP_PENDING,
		/* Stop completed */
		PKTIO_STATE_STOPPED
	} state;
	odp_pktio_config_t config;	/**< Device configuration */
	classifier_t cls;		/**< classifier linked with this pktio*/
	odp_pktio_stats_t stats;	/**< statistic counters for pktio */
	enum {
		STATS_SYSFS = 0,
		STATS_ETHTOOL,
		STATS_UNSUPPORTED
	} stats_type;
	char name[PKTIO_NAME_LEN];      /**< name of pktio provided to
					     pktio_open() */


	union {
		pkt_magic_t pkt_magic;
		pkt_loop_t pkt_loop;
		pkt_cluster_t pkt_cluster;
		pkt_eth_t pkt_eth;
		pkt_pcie_t pkt_pcie;
		pkt_ioddr_t pkt_ioddr;
	};

	odp_pool_t pool;
	odp_pktio_param_t param;
	/* Storage for queue handles
	 * Multi-queue support is pktio driver specific */
	unsigned num_in_queue;
	unsigned num_out_queue;

	struct {
		odp_queue_t        queue;
		odp_pktin_queue_t  pktin;
	} in_queue[PKTIO_MAX_QUEUES];

	struct {
		odp_queue_t        queue;
		odp_pktout_queue_t pktout;
	} out_queue[PKTIO_MAX_QUEUES];
};

typedef union {
	struct pktio_entry s;
	uint8_t pad[ODP_CACHE_LINE_SIZE_ROUNDUP(sizeof(struct pktio_entry))];
} pktio_entry_t;

typedef struct {
	odp_spinlock_t lock;
	pktio_entry_t entries[ODP_CONFIG_PKTIO_ENTRIES];
} pktio_table_t;

typedef struct pktio_if_ops {
	const char *name;
	void (*print)(pktio_entry_t *pktio_entry);
	int (*init)(void);
	int (*term)(void);
	int (*open)(odp_pktio_t pktio, pktio_entry_t *pktio_entry,
		    const char *devname, odp_pool_t pool);
	int (*close)(pktio_entry_t *pktio_entry);
	int (*start)(pktio_entry_t *pktio_entry);
	int (*stop)(pktio_entry_t *pktio_entry);
	int (*stats)(pktio_entry_t *pktio_entry, odp_pktio_stats_t *stats);
	int (*stats_reset)(pktio_entry_t *pktio_entry);
	uint64_t (*pktin_ts_res)(pktio_entry_t *pktio_entry);
	odp_time_t (*pktin_ts_from_ns)(pktio_entry_t *pktio_entry, uint64_t ns);
	int (*recv)(pktio_entry_t *pktio_entry, int index,
		    odp_packet_t pkt_table[], unsigned num);
	int (*send)(pktio_entry_t *pktio_entry, int index,
		    const odp_packet_t pkt_table[], unsigned num);
	uint32_t (*mtu_get)(pktio_entry_t *pktio_entry);
	int (*promisc_mode_set)(pktio_entry_t *pktio_entry,  int enable);
	int (*promisc_mode_get)(pktio_entry_t *pktio_entry);
	int (*mac_get)(pktio_entry_t *pktio_entry, void *mac_addr);
	int (*link_status)(pktio_entry_t *pktio_entry);
	int (*capability)(pktio_entry_t *pktio_entry,
			  odp_pktio_capability_t *capa);
	int (*config)(pktio_entry_t *pktio_entry,
		      const odp_pktio_config_t *config);
	int (*input_queues_config)(pktio_entry_t *pktio_entry,
				   const odp_pktin_queue_param_t *param);
	int (*output_queues_config)(pktio_entry_t *pktio_entry,
				    const odp_pktout_queue_param_t *p);
} pktio_if_ops_t;

extern pktio_table_t pktio_tbl;

static inline pktio_entry_t *get_pktio_entry(odp_pktio_t pktio)
{
	if (odp_unlikely(pktio == ODP_PKTIO_INVALID))
		return NULL;

	return (pktio_entry_t *)pktio;
}

static inline int pktio_to_id(odp_pktio_t pktio)
{
	pktio_entry_t * entry = get_pktio_entry(pktio);
	return entry - pktio_tbl.entries;
}

static inline int pktio_cls_enabled(pktio_entry_t *entry)
{
	return entry->s.cls_enabled;
}

static inline void pktio_cls_enabled_set(pktio_entry_t *entry, int ena)
{
	entry->s.cls_enabled = ena;
}

/*
 * Dummy single queue implementations of multi-queue API
 */
int single_capability(odp_pktio_capability_t *capa);
int single_input_queues_config(pktio_entry_t *entry,
			       const odp_pktin_queue_param_t *param);
int single_output_queues_config(pktio_entry_t *entry,
				const odp_pktout_queue_param_t *param);
int single_recv_queue(pktio_entry_t *entry, int index, odp_packet_t packets[],
		      int num);
int single_send_queue(pktio_entry_t *entry, int index,
		      const odp_packet_t packets[], int num);

extern const pktio_if_ops_t loopback_pktio_ops;
extern const pktio_if_ops_t magic_pktio_ops;
extern const pktio_if_ops_t cluster_pktio_ops;
extern const pktio_if_ops_t eth_pktio_ops;
extern const pktio_if_ops_t pcie_pktio_ops;
extern const pktio_if_ops_t drop_pktio_ops;
extern const pktio_if_ops_t ioddr_pktio_ops;
extern const pktio_if_ops_t * const pktio_if_ops[];

typedef struct _odp_pkt_iovec {
	void    *iov_base;
	uint32_t iov_len;
} odp_pkt_iovec_t;

static inline
uint32_t _tx_pkt_to_iovec(odp_packet_t pkt,
			  odp_pkt_iovec_t *iovecs)
{
	uint32_t seglen;
	iovecs[0].iov_base = odp_packet_offset(pkt, 0, &seglen, NULL);
	iovecs[0].iov_len = seglen;

	return 1;
}

static inline
uint32_t _rx_pkt_to_iovec(odp_packet_t pkt,
			  odp_pkt_iovec_t *iovecs)
{
	odp_packet_hdr_t *pkt_hdr = odp_packet_hdr(pkt);
	uint32_t seglen;
	uint8_t *ptr = packet_map(pkt_hdr, 0, &seglen);

	if (ptr) {
		iovecs[0].iov_base = ptr;
		iovecs[0].iov_len = seglen;
	}
	return 1;
}

struct pkt_rule;
const char* parse_hashpolicy(const char* pptr, int *nb_rules,
			     struct pkt_rule *rules, int max_rules);

int _odp_pktio_classify(pktio_entry_t *const pktio_entry,
			int index ODP_UNUSED,
			odp_packet_t pkt_table[], unsigned len);
#ifdef __cplusplus
}
#endif

#endif
