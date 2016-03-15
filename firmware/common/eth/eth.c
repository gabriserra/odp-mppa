#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <assert.h>
#include <HAL/hal/hal.h>

#include <mppa_eth_io_utils.h>

#include <odp_rpc_internal.h>
#include <mppa_eth_core.h>
#include <mppa_eth_loadbalancer_core.h>
#include <mppa_eth_phy.h>
#include <mppa_eth_mac.h>
#include <mppa_routing.h>
#include <mppa_noc.h>

#include "rpc-server.h"
#include "eth.h"

eth_status_t status[N_ETH_LANE];

static inline int get_eth_dma_id(unsigned cluster_id){
	unsigned offset = cluster_id / 4;
#ifdef K1B_EXPLORER
	offset = 0;
#endif

	switch(__k1_get_cluster_id()){
#ifndef K1B_EXPLORER
	case 128:
	case 160:
		return offset + 4;
#endif
	case 192:
	case 224:
		return offset + 4;
	default:
		return -1;
	}
}

static uint32_t registered_clusters[N_ETH_LANE] = {0};

static inline pkt_rule_entry_t
mppabeth_lb_get_rule(void __iomem *lb_addr,
	unsigned int rule_id,
	unsigned int entry_id) {
	pkt_rule_entry_t entry = {0};
	entry.offset = mppabeth_lb_get_rule_offset(lb_addr, rule_id, entry_id);
	entry.cmp_mask = mppabeth_lb_get_rule_cmp_mask(lb_addr, rule_id, entry_id);
	entry.cmp_value = mppabeth_lb_get_rule_expected_value(lb_addr, rule_id, entry_id);
	entry.hash_mask = mppabeth_lb_get_rule_hashmask(lb_addr, rule_id, entry_id);
	return entry;
}

static int compare_rule_entries(pkt_rule_entry_t entry1, pkt_rule_entry_t entry2) {
	return entry1.offset != entry2.offset ||
		entry1.cmp_mask != entry2.cmp_mask ||
		entry1.cmp_value != entry2.cmp_value ||
		entry1.hash_mask != entry2.hash_mask;
}

static int check_rules_identical(const pkt_rule_t *rules, int nb_rules) {
	for ( int rule_id = 0; rule_id < nb_rules; ++rule_id ) {
		for ( int entry_id = 0; entry_id < rules[rule_id].nb_entries; ++entry_id ) {
			pkt_rule_entry_t entry = mppabeth_lb_get_rule((void *) &(mppa_ethernet[0]->lb), rule_id, entry_id);
			if ( compare_rule_entries(rules[rule_id].entries[entry_id], entry ) ) {
				fprintf(stderr, "Rule[%d] entry[%d] differs from already set rule\n", rule_id, entry_id);
				return 1;
			}
		}
	}
	return 0;
}

static int eth_apply_rules(int lane_id, pkt_rule_t *rules, int nb_rules, int cluster_id) {
#ifdef VERBOSE
	printf("Applying %d rules for cluster %d\n", nb_rules, cluster_id);
#endif

	if ( registered_clusters[lane_id] & (1 << cluster_id) ) {
		fprintf(stderr, "cluster %d already registered on lane %d\n", cluster_id, lane_id);
		return -1;
	}
	registered_clusters[lane_id] |= 1 << cluster_id;

	// if first rule first entry is null, it means that it is the first clusters registering
	const pkt_rule_entry_t entry_nul = {0};
	if ( compare_rule_entries(mppabeth_lb_get_rule((void *) &(mppa_ethernet[0]->lb), 0, 0), entry_nul ) ) {
		if ( check_rules_identical(rules, nb_rules) ) {
			fprintf(stderr, "cluster %d registration failed\n", cluster_id);
			return -1;
		}
	}
	else {
		for ( int rule_id = 0; rule_id < nb_rules; ++rule_id) {
			for ( int entry_id = 0; entry_id < rules[rule_id].nb_entries; ++entry_id) {
#ifdef VERBOSE
				printf("Rule[%d] (P%d) Entry[%d]: offset %d cmp_mask 0x%02x cmp_value 0x%016llx hash_mask 0x%02x>\n",
				       rule_id,
				       rules[rule_id].priority,
				       entry_id,
				       rules[rule_id].entries[entry_id].offset,
				       rules[rule_id].entries[entry_id].cmp_mask,
				       rules[rule_id].entries[entry_id].cmp_value,
				       rules[rule_id].entries[entry_id].hash_mask);
#endif
				mppabeth_lb_cfg_rule((void *) &(mppa_ethernet[0]->lb),
						     rule_id, entry_id,
						     rules[rule_id].entries[entry_id].offset,
						     rules[rule_id].entries[entry_id].cmp_mask,
						     rules[rule_id].entries[entry_id].cmp_value,
						     rules[rule_id].entries[entry_id].hash_mask);
				mppabeth_lb_cfg_min_max_swap((void *) &(mppa_ethernet[0]->lb),
							     rule_id, (entry_id >> 1), 0);
			}
			mppabeth_lb_cfg_extract_table_mode((void *) &(mppa_ethernet[0]->lb),
							   rule_id,
							   rules[rule_id].priority,
							   MPPA_ETHERNET_DISPATCH_POLICY_HASH);
		}
	}

	// dispatch hash lut between registered clusters
	uint32_t clusters = registered_clusters[lane_id];
	int nb_registered = __k1_cbs(clusters);

	for ( int cluster_nb = 0; cluster_nb < nb_registered; ++ cluster_nb ) {
		int cluster_id = __k1_ctz(clusters);
		clusters &= ~(1 << cluster_id);
		int tx_id = status[lane_id].cluster[cluster_id].txId;
		int noc_if = status[lane_id].cluster[cluster_id].nocIf;
#ifdef VERBOSE
		printf("config lut[%02d, +=%02d] -> C%2d: %d %d %d %d\n",
			   cluster_nb, nb_registered, cluster_id,
			   lane_id, tx_id, ETH_DEFAULT_CTX, noc_if - ETH_BASE_TX);
#endif
		for ( int lut_id = cluster_nb; lut_id < MPPABETHLB_LUT_ARRAY_SIZE; lut_id+=nb_registered ) {
			mppabeth_lb_cfg_luts((void *) &(mppa_ethernet[0]->lb),
								 lane_id, lut_id, tx_id,
								 ETH_DEFAULT_CTX,
								 noc_if - ETH_BASE_TX);
		}
	}
#if 0
	// Extra verbose: display all LUT entries at each new config
	for ( int lut_id = 0; lut_id < MPPABETHLB_LUT_ARRAY_SIZE; lut_id++ ) {
		printf("[%3d] : dma %d tx %d ctx %d\n", lut_id,
		mppabeth_lb_get_luts_dma_id((void *) &(mppa_ethernet[0]->lb),
								 lane_id, lut_id),
		mppabeth_lb_get_luts_tx_id((void *) &(mppa_ethernet[0]->lb),
								 lane_id, lut_id),
		mppabeth_lb_get_luts_context((void *) &(mppa_ethernet[0]->lb),
								 lane_id, lut_id));
	}
#endif
	return 0;
}

odp_rpc_cmd_ack_t  eth_open(unsigned remoteClus, odp_rpc_t *msg, uint8_t *payload)
{
	odp_rpc_cmd_ack_t ack = { .status = 0};
	odp_rpc_cmd_eth_open_t data = { .inl_data = msg->inl_data };
	const int nocIf = get_eth_dma_id(remoteClus);
	const unsigned int eth_if = data.ifId % 4; /* 4 is actually 0 in 40G mode */

	if(nocIf < 0) {
		fprintf(stderr, "[ETH] Error: Invalid NoC interface (%d %d)\n", nocIf, remoteClus);
		goto err;
	}
	if(data.ifId > 4 || eth_if >= N_ETH_LANE) {
		fprintf(stderr, "[ETH] Error: Invalid Eth lane\n");
		goto err;
	}

	if (data.ifId == 4) {
		/* 40G port. We need to check all lanes */
		for (int i = 0; i < N_ETH_LANE; ++i) {
			if(status[i].cluster[remoteClus].opened != ETH_CLUS_STATUS_OFF) {
				fprintf(stderr, "[ETH] Error: Lane %d is already opened for cluster %d\n",
						i, remoteClus);
				goto err;
			}
		}
	} else {
		if (status[eth_if].cluster[remoteClus].opened != ETH_CLUS_STATUS_OFF) {
			fprintf(stderr, "[ETH] Error: Lane %d is already opened for cluster %d\n",
					eth_if, remoteClus);
			goto err;
		}
		if (data.jumbo) {
			fprintf(stderr,
				"[ETH] Error: Trying to enable Jumbo on 1/10G lane %d\n",
				eth_if);
			goto err;

		}
	}
	int externalAddress = __k1_get_cluster_id() + nocIf;
#ifdef K1B_EXPLORER
	externalAddress = __k1_get_cluster_id() + (nocIf % 4);
#endif

	status[eth_if].cluster[remoteClus].rx_enabled = data.rx_enabled;
	status[eth_if].cluster[remoteClus].tx_enabled = data.tx_enabled;
	status[eth_if].cluster[remoteClus].jumbo = data.jumbo;

	if (ethtool_setup_eth2clus(remoteClus, data.ifId, nocIf, externalAddress,
				   data.min_rx, data.max_rx))
		goto err;
	if (ethtool_setup_clus2eth(remoteClus, data.ifId, nocIf))
		goto err;
	if (ethtool_init_lane(data.ifId, data.loopback, data.verbose))
		goto err;
	if (ethtool_enable_cluster(remoteClus, data.ifId))
		goto err;

	if (data.ifId == 4) {
		for (int i = 0; i < N_ETH_LANE; ++i) {
			status[i].cluster[remoteClus].opened = ETH_CLUS_STATUS_40G;
		}
	} else {
		status[eth_if].cluster[remoteClus].opened = ETH_CLUS_STATUS_ON;
	}
	ack.cmd.eth_open.tx_if = externalAddress;
	ack.cmd.eth_open.tx_tag = status[eth_if].cluster[remoteClus].rx_tag;
	if (data.jumbo) {
		ack.cmd.eth_open.mtu = 9000;
	} else {
		ack.cmd.eth_open.mtu = 1600;
	}
	memset(ack.cmd.eth_open.mac, 0, ETH_ALEN);
	ack.cmd.eth_open.mac[ETH_ALEN-1] = 1 << eth_if;
	ack.cmd.eth_open.mac[ETH_ALEN-2] = __k1_get_cluster_id();

	if ( data.nb_rules > 0 ) {
		if ( eth_apply_rules(eth_if, (pkt_rule_t*)payload, data.nb_rules, remoteClus))
			goto err;
	}

	return ack;

 err:
	ethtool_cleanup_cluster(remoteClus, eth_if);
	ack.status = 1;
	return ack;
}

odp_rpc_cmd_ack_t  eth_close(unsigned remoteClus, odp_rpc_t *msg)
{
	odp_rpc_cmd_ack_t ack = { .status = 0 };
	odp_rpc_cmd_eth_clos_t data = { .inl_data = msg->inl_data };
	const unsigned int eth_if = data.ifId % 4; /* 4 is actually 0 in 40G mode */

	if (data.ifId == 4) {
		if(status[eth_if].cluster[remoteClus].opened != ETH_CLUS_STATUS_40G) {
			ack.status = -1;
			return ack;
		}
	} else {
		if(status[eth_if].cluster[remoteClus].opened != ETH_CLUS_STATUS_ON) {
			ack.status = -1;
			return ack;
		}
	}

	ethtool_disable_cluster(remoteClus, data.ifId);
	ethtool_cleanup_cluster(remoteClus, data.ifId);
	if (data.ifId == 4) {
		for (int i = 0; i < N_ETH_LANE; ++i) {
			_eth_cluster_status_init(&status[i].cluster[remoteClus]);
		}
	} else {
		_eth_cluster_status_init(&status[eth_if].cluster[remoteClus]);
	}

	return ack;
}

static void eth_init(void)
{
	/* "MATCH_ALL" Rule */
	mppabeth_lb_cfg_rule((void *)&(mppa_ethernet[0]->lb),
			     ETH_MATCHALL_TABLE_ID, ETH_MATCHALL_RULE_ID,
			     /* offset */ 0, /* Cmp Mask */0,
			     /* Espected Value */ 0, /* Hash. Unused */0);

	mppabeth_lb_cfg_extract_table_mode((void *)&(mppa_ethernet[0]->lb),
					   ETH_MATCHALL_TABLE_ID, /* Priority */ 0,
					   MPPABETHLB_DISPATCH_POLICY_RR);
	for (int eth_if = 0; eth_if < N_ETH_LANE; ++eth_if) {
		_eth_status_init(&status[eth_if]);

		mppabeth_lb_cfg_header_mode((void *)&(mppa_ethernet[0]->lb),
					    eth_if, MPPABETHLB_ADD_HEADER);

		mppabeth_lb_cfg_table_rr_dispatch_trigger((void *)&(mppa_ethernet[0]->lb),
							  ETH_MATCHALL_TABLE_ID,
							  eth_if, 1);

	}
}

static int eth_rpc_handler(unsigned remoteClus, odp_rpc_t *msg, uint8_t *payload)
{
	odp_rpc_cmd_ack_t ack = ODP_RPC_CMD_ACK_INITIALIZER;

	switch (msg->pkt_type){
	case ODP_RPC_CMD_ETH_OPEN:
		ack = eth_open(remoteClus, msg, payload);
		break;
	case ODP_RPC_CMD_ETH_CLOS:
		ack = eth_close(remoteClus, msg);
		break;
	default:
		return -1;
	}
	odp_rpc_server_ack(msg, ack);
	return 0;
}

void  __attribute__ ((constructor)) __eth_rpc_constructor()
{
#if defined(MAGIC_SCALL)
	return;
#endif

	eth_init();
	if(__n_rpc_handlers < MAX_RPC_HANDLERS) {
		__rpc_handlers[__n_rpc_handlers++] = eth_rpc_handler;
	} else {
		fprintf(stderr, "Failed to register ETH RPC handlers\n");
		exit(EXIT_FAILURE);
	}
}
