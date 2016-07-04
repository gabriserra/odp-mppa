#ifndef __FIRMWARE__IOETH__ETH__H__
#define __FIRMWARE__IOETH__ETH__H__

#include <mppa_noc.h>

#ifdef K1B_EXPLORER
#define ETH_BASE_TX 0
#define ETH_N_DMA_TX 1
#else
#define ETH_BASE_TX 4
#define ETH_N_DMA_TX 4
#endif

#define ETH_DEFAULT_CTX 0
#define ETH_MATCHALL_TABLE_ID 0
#define ETH_MATCHALL_RULE_ID 0

#ifdef K1B_EXPLORER
#define N_ETH_LANE 1
#else
#define N_ETH_LANE 4
#endif

#define ETH_RPC_ERR_MSG(answer, x...)			\
	do {						\
		RPC_ERROR(answer, "[ETH]", ##x);	\
	} while(0)

int ethtool_init_lane(int eth_if);

int ethtool_open_cluster(unsigned remoteClus, unsigned if_id,
			 mppa_rpc_odp_answer_t *answer);
int ethtool_close_cluster(unsigned remoteClus, unsigned if_id,
			  mppa_rpc_odp_answer_t *answer);

int ethtool_setup_eth2clus(unsigned remoteClus, int if_id,
			   int nocIf, int externalAddress,
			   int min_rx, int max_rx,
			   int min_payload, int max_payload,
			   mppa_rpc_odp_answer_t *answer);
int ethtool_setup_clus2eth(unsigned remoteClus, int if_id, int nocIf,
			   mppa_rpc_odp_answer_t *answer);
int ethtool_configure_policy(unsigned remoteClus, unsigned if_id,
			     int fallthrough,  int nb_rules,
			     const pkt_rule_t rules[nb_rules],
			     mppa_rpc_odp_answer_t *answer);

int ethtool_enable_cluster(unsigned remoteClus, unsigned if_id,
			   mppa_rpc_odp_answer_t *answer);
int ethtool_disable_cluster(unsigned remoteClus, unsigned if_id,
			    mppa_rpc_odp_answer_t *answer);

int ethtool_start_lane(unsigned if_id, int loopback, int verbose,
		       mppa_rpc_odp_answer_t *answer);
int ethtool_stop_lane(unsigned if_id,
		      mppa_rpc_odp_answer_t *answer);

int ethtool_poll_lane(unsigned if_id);
int ethtool_lane_stats(unsigned if_id,
		       mppa_rpc_odp_answer_t *answer);

int ethtool_set_dual_mac(int enabled,
			 mppa_rpc_odp_answer_t *answer);

typedef enum {
	ETH_CLUS_STATUS_OFF,
	ETH_CLUS_STATUS_ON,
	ETH_CLUS_STATUS_40G
} eth_cluster_lane_status_t;

typedef enum {
	ETH_CLUS_POLICY_FALLTHROUGH,
	ETH_CLUS_POLICY_HASH,
	ETH_CLUS_POLICY_MAC_MATCH,
	ETH_CLUS_POLICY_N_POLICY
} eth_cluster_policy_t;

typedef struct {
	int nocIf;
	int txId;
	int min_rx;
	int max_rx;
	int rx_tag;
	int eth_tx_fifo;

	eth_cluster_lane_status_t opened;
	struct {
		int enabled : 1;
		int rx_enabled : 1; /* Pktio wants to receive packets from ethernet */
		int tx_enabled : 1; /* Pktio wants to send packets to ethernet */
		int jumbo : 1;      /* Jumbo supported */
	};
	eth_cluster_policy_t policy;
} eth_cluster_status_t;

typedef struct {
	int policy[ETH_CLUS_POLICY_N_POLICY];
	int enabled;
	int opened;
} eth_refcounts_t;

typedef struct {
	enum {
		ETH_LANE_OFF,
		ETH_LANE_ON,
		ETH_LANE_ON_40G,
	} initialized;
	uint8_t mac_address[2][6];
	eth_cluster_status_t cluster[RPC_MAX_CLIENTS];

	eth_refcounts_t refcounts;
	eth_refcounts_t rx_refcounts;

} eth_status_t;

typedef struct {
	struct {
		int enabled : 1;
		int loopback;
		int dual_mac : 1;
		unsigned nb_rules : 3;
	};
	int opened_refcount;
	uint16_t tx_fifo[MPPA_ETHERNET_TX_FIFO_IF_NUMBER];
} eth_lb_status_t;

static inline void _eth_cluster_status_init(eth_cluster_status_t * cluster)
{
	cluster->nocIf = -1;
	cluster->txId = -1;
	cluster->min_rx = 0;
	cluster->max_rx = -1;
	cluster->rx_tag = -1;
	cluster->eth_tx_fifo = -1;
	cluster->opened = ETH_CLUS_STATUS_OFF;
	cluster->enabled = 0;
	cluster->rx_enabled = 0;
	cluster->tx_enabled = 0;
	cluster->jumbo = 0;
}

static inline void _eth_refcount_init(eth_refcounts_t *refcount)
{
	int i;

	for (i = 0; i < ETH_CLUS_POLICY_N_POLICY; ++i)
		refcount->policy[i] = 0;
	refcount->enabled = 0;
	refcount->opened = 0;
}
static inline void _eth_status_init(eth_status_t * status)
{
	int i;

	status->initialized = 0;
	_eth_refcount_init(&status->refcounts);
	_eth_refcount_init(&status->rx_refcounts);

	for (i = 0; i < RPC_MAX_CLIENTS; ++i)
		_eth_cluster_status_init(&status->cluster[i]);
}

static inline void _eth_lb_status_init(eth_lb_status_t * status)
{
	status->enabled = 0;
	status->dual_mac = 0;
	status->loopback = 0;
	status->opened_refcount = 0;
	for (int i = 0; i < MPPA_ETHERNET_TX_FIFO_IF_NUMBER; ++i){
		status->tx_fifo[i] = -1;
	}
}
extern eth_status_t status[N_ETH_LANE];
extern eth_lb_status_t lb_status;

#endif /* __FIRMWARE__IOETH__ETH__H__ */
