#ifndef PCIE_INTERNAL__H
#define PCIE_INTERNAL__H

#include <string.h>
#include <odp/rpc/rpc.h>
#include <odp/rpc/pcie.h>
#include "pcie.h"
#include "rpc-server.h"
#include "internal/rpc-server.h"
#include "ring.h"
#include "mppapcie_odp.h"
#include "netdev.h"
#include "internal/netdev.h"

#define MPPA_PCIE_USABLE_DNOC_IF	4

#define MPPA_PCIE_NOC_RX_NB 16
#define MPPA_PCIE_PKT_BURSTINESS 32

/**
 * PKT size
 */
#define MPPA_PCIE_MULTIBUF_PKT_SIZE	(MPODP_MAX_MTU + sizeof(struct mpodp_pkt_hdr) + 8)

/**
 * Packets per multi buffer
 */
#define MPPA_PCIE_MULTIBUF_PKT_COUNT	8
#define MPPA_PCIE_MULTIBUF_SIZE		(MPPA_PCIE_MULTIBUF_PKT_COUNT * MPPA_PCIE_MULTIBUF_PKT_SIZE)

#define MPPA_PCIE_MULTIBUF_BURST        16

#define RX_RM_STACK_SIZE	(0x2000 / (sizeof(uint64_t)))

#include "noc2pci.h"

extern buffer_ring_t g_free_buf_pool;
extern buffer_ring_t g_full_buf_pool[MPODP_MAX_IF_COUNT][MPODP_MAX_RX_QUEUES];

struct mppa_pcie_eth_dnoc_tx_cfg {
	int opened;
	unsigned int cluster;
	unsigned int mtu;
	volatile void *fifo_addr;
	unsigned int pcie_eth_if;
	unsigned int h2c_q;
};

typedef struct mppa_pcie_link_cluster_status {
	struct {
		uint8_t opened : 1;
		uint8_t rx_enabled : 1;
		uint8_t tx_enabled : 1;
	};
	int p2c_nocIf;
	int p2c_txId;
	int p2c_min_rx;
	int p2c_max_rx;
	int p2c_q;

	int c2p_nocIf;
	int c2p_min_rx;
	int c2p_max_rx;
	int c2p_next_tag;
	uint64_t c2p_credit;
	int c2p_remote_cnoc_rx;
	int c2p_q;

	mppa_cnoc_config_t c2p_config;
	mppa_cnoc_header_t c2p_header;

} mppa_pcie_link_cluster_status_t;

typedef struct {
	int enabled;
	int cnoc_tx;
	mppa_pcie_link_cluster_status_t cluster[RPC_MAX_CLIENTS];
} mppa_pcie_link_status;

typedef struct {
	mppa_pcie_link_status link[MPODP_MAX_IF_COUNT];
} mppa_pcie_status_t;

static inline void _init_mppa_pcie_link_cluster_status(mppa_pcie_link_cluster_status_t *status)
{
	status->opened = 0;
	status->tx_enabled = 0;
	status->rx_enabled = 0;

	status->p2c_nocIf = -1;
	status->p2c_txId = -1;
	status->p2c_min_rx = status->p2c_max_rx = -1;

	status->c2p_nocIf = -1;
	status->c2p_min_rx = status->c2p_max_rx = -1;
}

static inline void _init_mppa_pcie_link_status(mppa_pcie_link_status *status)
{
	status->enabled = 0;
	status->cnoc_tx = -1;
	for (int i = 0; i < RPC_MAX_CLIENTS; ++i) {
		_init_mppa_pcie_link_cluster_status(&status->cluster[i]);
	}
}

static inline void _init_mppa_pcie_status(mppa_pcie_status_t *status)
{
	for (int i = 0; i < MPODP_MAX_IF_COUNT; ++i) {
		_init_mppa_pcie_link_status(&status->link[i]);
	}
}
extern mppa_pcie_status_t pcie_status;
void
pcie_start_rx_rm();

void
pcie_start_tx_rm();


static inline unsigned pcie_cluster_to_h2c_q(unsigned pcie_eth_if_id,
					     unsigned remoteClus)
{
	return remoteClus % eth_ctrl->configs[pcie_eth_if_id].n_txqs;
}

static inline unsigned pcie_cluster_to_c2h_q(unsigned pcie_eth_if_id,
					     unsigned remoteClus)
{
	return remoteClus % eth_ctrl->configs[pcie_eth_if_id].n_rxqs;
}
int pcietool_open_cluster(unsigned remoteClus, int if_id,
			  mppa_rpc_odp_answer_t *answer);

int pcietool_setup_pcie2clus(unsigned remoteClus, int if_id,
			     int nocIf, int externalAddress,
			     int min_rx, int max_rx,
			     mppa_rpc_odp_answer_t *answer);

int pcietool_setup_clus2pcie(unsigned remoteClus, int if_id, int nocIf,
			     int remote_cnoc_rx, int verbose,
			     mppa_rpc_odp_answer_t *answer);

int pcietool_enable_cluster(unsigned remoteClus, unsigned if_id,
			    mppa_rpc_odp_answer_t *answer);

static inline
int no_printf(__attribute__((unused)) const char *fmt , ...)
{
	return 0;
}

#define __FILENAME__ (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)

#    define err_printf(fmt, args...) \
	printf("[ERR] %s:%d: " fmt,  __FILENAME__, __LINE__, ## args)

#if defined VERBOSE
#    define dbg_printf(fmt, args...) \
	printf("[DBG] %s:%d: " fmt,  __FILENAME__, __LINE__, ## args)
#else
#    define dbg_printf(fmt, args...)	no_printf(fmt, ## args)
#endif

#define PCIE_RPC_ERR_MSG(answer, x...)			\
	do {						\
		RPC_ERROR(answer, "[PCIE]", ##x);	\
	} while(0)
#endif
