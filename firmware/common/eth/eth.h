#ifndef __FIRMWARE__IOETH__ETH__H__
#define __FIRMWARE__IOETH__ETH__H__

#include <mppa_noc.h>

#define ETH_BASE_TX 4
#define ETH_DEFAULT_CTX 0
#define ETH_MATCHALL_TABLE_ID 0
#define ETH_MATCHALL_RULE_ID 0

#ifdef K1B_EXPLORER
#define N_ETH_LANE 1
#else
#define N_ETH_LANE 4
#endif

odp_rpc_cmd_ack_t eth_open(unsigned remoteClus, odp_rpc_t * msg, uint8_t *payload);
odp_rpc_cmd_ack_t eth_close(unsigned remoteClus, odp_rpc_t * msg);

int ethtool_setup_eth2clus(unsigned remoteClus, int eth_if,
			   int nocIf, int externalAddress,
			   int min_rx, int max_rx);
int ethtool_setup_clus2eth(unsigned remoteClus, int eth_if, int nocIf);
int ethtool_init_lane(unsigned eth_if, int loopback);
void ethtool_cleanup_cluster(unsigned remoteClus, unsigned eth_if);
int ethtool_enable_cluster(unsigned remoteClus, unsigned eth_if);
int ethtool_disable_cluster(unsigned remoteClus, unsigned eth_if);

typedef enum {
	ETH_CLUS_STATUS_OFF,
	ETH_CLUS_STATUS_ON,
	ETH_CLUS_STATUS_40G
} eth_cluster_lane_status_t;

typedef struct {
	int nocIf;
	int txId;
	int min_rx;
	int max_rx;
	int rx_tag;

	eth_cluster_lane_status_t opened;
	struct {
		int enabled : 1;
		int rx_enabled : 1; /* Pktio wants to receive packets from ethernet */
		int tx_enabled : 1; /* Pktio wants to send packets to ethernet */
		int jumbo : 1; /* Jumbo supported */
	};
} eth_cluster_status_t;

typedef struct {
	enum {
		ETH_LANE_OFF,
		ETH_LANE_ON,
		ETH_LANE_ON_40G,
		ETH_LANE_LOOPBACK,
		ETH_LANE_LOOPBACK_40G
	} initialized;
	eth_cluster_status_t cluster[BSP_NB_CLUSTER_MAX];
	int enabled_refcount;
} eth_status_t;

static inline void _eth_cluster_status_init(eth_cluster_status_t * cluster)
{
	cluster->nocIf = -1;
	cluster->txId = -1;
	cluster->min_rx = 0;
	cluster->max_rx = -1;
	cluster->rx_tag = -1;
	cluster->opened = ETH_CLUS_STATUS_OFF;
	cluster->enabled = 0;
	cluster->rx_enabled = 0;
	cluster->tx_enabled = 0;
	cluster->jumbo = 0;
}

static inline void _eth_status_init(eth_status_t * status)
{
	status->initialized = 0;
	status->enabled_refcount = 0;

	for (int i = 0; i < BSP_NB_CLUSTER_MAX; ++i)
		_eth_cluster_status_init(&status->cluster[i]);
}

extern eth_status_t status[N_ETH_LANE];

#endif /* __FIRMWARE__IOETH__ETH__H__ */
