#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <assert.h>
#include <errno.h>

#include <odp/rpc/rpc.h>
#include <mppa_eth_core.h>
#include <mppa_eth_loadbalancer_core.h>
#include <mppa_eth_phy.h>
#include <mppa_eth_mac.h>
#include <mppa_routing.h>
#include <mppa_noc.h>
#include <mppa_eth_io_utils.h>
#include <mppa_eth_qsfp_utils.h>
#include <odp/rpc/helpers.h>
#include <HAL/hal/core/optimize.h>

#include "rpc-server.h"
#include "internal/rpc-server.h"
#include "internal/eth.h"

uint64_t lb_timestamp = 0xFFFFFFFFFFFFFFFULL;

enum mppa_eth_mac_ethernet_mode_e ethtool_get_mac_speed(unsigned if_id,
							mppa_rpc_odp_answer_t *answer)
{
	int eth_if = if_id % 4;
	enum mppa_eth_mac_ethernet_mode_e link_speed =
		mppa_eth_utils_mac_get_default_mode(eth_if);

	if ((int)link_speed == -1) {
		ETH_RPC_ERR_MSG(answer,
				"Unsupported lane or board\n");
		return -1;
	}

	if (!link_speed == MPPA_ETH_MAC_ETHMODE_40G && if_id == 4) {
		ETH_RPC_ERR_MSG(answer,
				"Cannot open 40G link on this board\n");
		return -1;
	} else if (link_speed == MPPA_ETH_MAC_ETHMODE_40G && if_id < 4) {
		/* Link could do 40G but we use only one lane */
		link_speed = MPPA_ETH_MAC_ETHMODE_10G_BASE_R;
	}
	return link_speed;
}
static int first_header = 0;
int ethtool_init_lane(int eth_if)
{
	mppabeth_lb_cfg_header_mode((void *)&(mppa_ethernet[0]->lb),
				    eth_if, MPPABETHLB_ADD_HEADER);
	if(first_header == 0){
		lb_timestamp = __k1_read_dsu_timestamp();
		first_header = 1;
	}

	mppabeth_lb_cfg_table_rr_dispatch_trigger((void *)&(mppa_ethernet[0]->lb),
						  ETH_MATCHALL_TABLE_ID,
						  eth_if, 1);
	mppabeth_lb_cfg_default_dispatch_policy((void *)&(mppa_ethernet[0]->lb),
						eth_if,
						MPPABETHLB_DISPATCH_DEFAULT_POLICY_DROP);
	return 0;
}

int ethtool_open_cluster(unsigned remoteClus, unsigned if_id,
			 mppa_rpc_odp_answer_t *answer)
{
	const int eth_if = if_id % 4;
	if (if_id == 4) {
		for (int i = 0; i < N_ETH_LANE; ++i)
			if (status[i].cluster[remoteClus].opened != ETH_CLUS_STATUS_OFF){
				ETH_RPC_ERR_MSG(answer,
						"Trying to open 40G lane but lane %d is already opened by this cluster\n",
						i);
				return -1;
			}
		for (int i = 0; i < N_ETH_LANE; ++i)
			status[i].cluster[remoteClus].opened = ETH_CLUS_STATUS_40G;
	} else {
		if (status[eth_if].cluster[remoteClus].opened != ETH_CLUS_STATUS_OFF){
			ETH_RPC_ERR_MSG(answer,
					"Trying to open 1/10G %d lane but it is already opened by this cluster\n",
					eth_if);
			return -1;
		}
		status[eth_if].cluster[remoteClus].opened = ETH_CLUS_STATUS_ON;
	}
	status[eth_if].refcounts.opened++;

	return 0;
}

int ethtool_setup_eth2clus(unsigned remoteClus, int if_id,
			   int nocIf, int externalAddress,
			   int min_rx, int max_rx,
			   int min_payload, int max_payload,
			   mppa_rpc_odp_answer_t *answer)
{
	int ret;

	mppa_dnoc_header_t header = { 0 };
	mppa_dnoc_channel_config_t config = { 0 };
	unsigned nocTx;
	int eth_if = if_id % 4;

	if (!status[eth_if].cluster[remoteClus].rx_enabled)
		return 0;

	ret = mppa_routing_get_dnoc_unicast_route(externalAddress,
						  mppa_rpc_odp_undensify_cluster_id(remoteClus),
						  &config, &header);
	if (ret != MPPA_ROUTING_RET_SUCCESS) {
		ETH_RPC_ERR_MSG(answer,
				"Failed to route to cluster %d\n", remoteClus);
		return -1;
	}

	ret = mppa_noc_dnoc_tx_alloc_auto(nocIf, &nocTx, MPPA_NOC_BLOCKING);
	if (ret != MPPA_NOC_RET_SUCCESS) {
		ETH_RPC_ERR_MSG(answer,
				"Failed to find an available Tx on DMA %d\n",
				nocIf);
		return -1;
	}

	config._.loopback_multicast = 0;
	config._.cfg_pe_en = 1;
	config._.cfg_user_en = 1;
	config._.write_pe_en = 1;
	config._.write_user_en = 1;
	config._.decounter_id = 0;
	config._.decounted = 0;
	config._.payload_min = min_payload ? : 1;
	config._.payload_max = max_payload ? : 32;
	config._.bw_current_credit = 0xff;
	config._.bw_max_credit     = 0xff;
	config._.bw_fast_delay     = 0x00;
	config._.bw_slow_delay     = 0x00;

	header._.tag = min_rx;
	header._.valid = 1;
	header._.multicast = 0;

	ret = mppa_noc_dnoc_tx_configure(nocIf, nocTx, header, config);
	if (ret != MPPA_NOC_RET_SUCCESS) {
		ETH_RPC_ERR_MSG(answer, "Failed to configure Tx\n");
		mppa_noc_dnoc_tx_free(nocIf, nocTx);
		return -1;
	}

	status[eth_if].cluster[remoteClus].nocIf = nocIf;
	status[eth_if].cluster[remoteClus].txId = nocTx;
	status[eth_if].cluster[remoteClus].min_rx = min_rx;
	status[eth_if].cluster[remoteClus].max_rx = max_rx;


	volatile mppa_dnoc_min_max_task_id_t *context =
		&mppa_dnoc[nocIf]->tx_chan_route[nocTx].
		min_max_task_id[ETH_DEFAULT_CTX];

	context->_.current_task_id = min_rx;
	context->_.min_task_id = min_rx;
	context->_.max_task_id = max_rx;
	context->_.min_max_task_id_en = 1;

	if (status[eth_if].cluster[remoteClus].jumbo) {
		mppabeth_lb_cfg_jumbo_mode((void *)&(mppa_ethernet[0]->lb),
					   eth_if, MPPABETHLB_JUMBO_ALLOWED);
	} else {
		mppabeth_lb_cfg_jumbo_mode((void *)&(mppa_ethernet[0]->lb),
					   eth_if, MPPABETHLB_JUMBO_DISABLED);
	}

	return 0;
}

extern int phy_status __attribute((weak));

int ethtool_setup_clus2eth(unsigned remoteClus, int if_id, int nocIf,
			   mppa_rpc_odp_answer_t *answer)
{
	int ret;
	unsigned rx_port;
	int eth_if = if_id % 4;

	if (!status[eth_if].cluster[remoteClus].tx_enabled)
		return 0;

	mppa_dnoc[nocIf]->rx_global.rx_ctrl._.alert_level = -1;
	mppa_dnoc[nocIf]->rx_global.rx_ctrl._.payload_slice = 2;
	ret = mppa_noc_dnoc_rx_alloc_auto(nocIf, &rx_port, MPPA_NOC_NON_BLOCKING);
	if(ret) {
		ETH_RPC_ERR_MSG(answer, "Failed to find an available Rx on DMA %d\n", nocIf);
		return -1;
	}
	status[eth_if].cluster[remoteClus].nocIf = nocIf;
	status[eth_if].cluster[remoteClus].rx_tag = rx_port;

	mppa_dnoc_queue_event_it_target_t it_targets = {
		.reg = 0
	};
	int fifo_id = -1;
	uint16_t fifo_mask;
	uint16_t avail_mask = lb_status.tx_fifo[nocIf - 4];

	while(avail_mask && fifo_id == -1){
		fifo_id = __builtin_k1_ctz(avail_mask);
		fifo_mask = if_id == 4 ? (0xf << fifo_id) : (1 << fifo_id);
		if ((fifo_mask & avail_mask) != fifo_mask) {
			/* Not enough contiguous bits */
			avail_mask &= ~(1 << fifo_id);
			fifo_id = -1;
			continue;
		}
	}

	if (fifo_id == -1) {
		ETH_RPC_ERR_MSG(answer, "No more Ethernet Tx fifo available on NoC interface %d\n",
				nocIf);
		return  -1;
	}

	status[eth_if].cluster[remoteClus].eth_tx_fifo = fifo_id;
	lb_status.tx_fifo[nocIf - 4] &= ~fifo_mask;


	/* If we are using 40G */
	if (if_id == 4) {
		mppa_ethernet[0]->tx.fifo_if[nocIf - ETH_BASE_TX].lane[eth_if].
			eth_fifo[fifo_id].eth_fifo_ctrl._.jumbo_mode = 1;
	}

	mppa_ethernet[0]->tx.fifo_if[nocIf - ETH_BASE_TX].lane[eth_if].
		eth_fifo[fifo_id].eth_fifo_ctrl._.drop_en = 1;
	mppa_noc_dnoc_rx_configuration_t conf = {
		.buffer_base = (unsigned long)(void*)
		&mppa_ethernet[0]->tx.fifo_if[nocIf - ETH_BASE_TX].lane[eth_if].
		eth_fifo[fifo_id].push_data,
		.buffer_size = 8,
		.current_offset = 0,
		.event_counter = 0,
		.item_counter = 1,
		.item_reload = 1,
		.reload_mode = MPPA_NOC_RX_RELOAD_MODE_DECR_NOTIF_RELOAD,
		.activation = MPPA_NOC_ACTIVATED | MPPA_NOC_FIFO_MODE,
		.counter_id = 0,
		.event_it_targets = &it_targets,
	};

	ret = mppa_noc_dnoc_rx_configure(nocIf, rx_port, conf);
	if(ret) {
		ETH_RPC_ERR_MSG(answer, "Failed to configure Rx\n");
		mppa_noc_dnoc_rx_free(nocIf, rx_port);
		return -1;
	}

	return 0;
}

int ethtool_start_lane(unsigned if_id, int loopback, int verbose,
		       mppa_rpc_odp_answer_t *answer)
{
	int ret;
	int eth_if = if_id % 4;
	if (lb_status.opened_refcount) {
		if (loopback && !lb_status.loopback) {
			ETH_RPC_ERR_MSG(answer,
					"One lane was enabled. Cannot set lane %d in loopback\n",
					eth_if);
			return -1;
		} else if (!loopback && lb_status.loopback) {
			ETH_RPC_ERR_MSG(answer,
					"Eth is in MAC loopback. Cannot enable lane %d\n",
					eth_if);
			return -1;
		}
	}

	switch (status[eth_if].initialized) {
	case ETH_LANE_OFF:
		if (if_id == 4) {
			for (int i = 0; i < N_ETH_LANE; ++i) {
				if (status[i].initialized != ETH_LANE_ON)
					continue;
				ETH_RPC_ERR_MSG(answer,
						"One lane was enabled in 1 or 10G. Cannot set lane %d in 40G\n",
						eth_if);
				return -1;
			}
		}
		if (loopback) {
			if (verbose)
				printf("[ETH] Initializing lane %d in loopback\n", eth_if);

			mppabeth_mac_enable_loopback_bypass((void *)&(mppa_ethernet[0]->mac));
			lb_status.loopback = 1;
		} else {
			enum mppa_eth_mac_ethernet_mode_e link_speed =
				ethtool_get_mac_speed(if_id, answer);
			if ((int)link_speed < 0)
				return link_speed;

			if (verbose)
				printf("[ETH] Initializing global MAC @ %d\n", link_speed);

			mppabeth_mac_cfg_mode((void*) &(mppa_ethernet[0]->mac), link_speed);

			/* Init MAC */
			if (verbose)
				printf("[ETH] Initializing MAC for lane %d\n", eth_if);

			ret = mppa_eth_utils_init_mac(eth_if, link_speed);
			switch(ret){
			case BAD_VENDOR:
				ETH_RPC_ERR_MSG(answer,
						"[ETH] Warning: QSFP coonector is not supported\n");
				return -1;
				break;
			case -EBUSY:
				/* lane is already configured. Ignore */
				/* FIXME: Reenable lane */
				break;
			case -ENETDOWN:
				/* No link yet but it's sort of expected */
			case 0:
				break;
			default:
				ETH_RPC_ERR_MSG(answer,
					"Internal error during initialization of lane %d (err code %d)\n",
						eth_if, ret);
				return -1;
			}

			mppabeth_mac_enable_rx_check_sfd((void*)
							 &(mppa_ethernet[0]->mac));
			mppabeth_mac_enable_rx_fcs_deletion((void*)
							    &(mppa_ethernet[0]->mac));
			mppabeth_mac_enable_tx_fcs_insertion((void*)
				&(mppa_ethernet[0]->mac));
			mppabeth_mac_enable_tx_add_padding((void*)
				&(mppa_ethernet[0]->mac));
			mppabeth_mac_enable_rx_check_preambule((void*)
				&(mppa_ethernet[0]->mac));

		}
		if (if_id == 4) {
			for (int i = 0; i < N_ETH_LANE; ++i)
				status[i].initialized = ETH_LANE_ON_40G;
		} else
			status[eth_if].initialized = ETH_LANE_ON;
		break;
	case ETH_LANE_ON:
		if (if_id == 4) {
			ETH_RPC_ERR_MSG(answer,
					"One lane was enabled in 1 or 10G. Cannot set lane %d in 40G\n",
					eth_if);
			return -1;
		}
		break;
	case ETH_LANE_ON_40G:
		if (if_id < 4) {
			ETH_RPC_ERR_MSG(answer,
					"Interface was enabled in 40G. Cannot set lane %d in 1 or 10G mode\n",
					eth_if);
			return -1;
		}
		break;
	default:
		ETH_RPC_ERR_MSG(answer, "Internal error\n");
		return -1;
	}

	lb_status.opened_refcount++;
	return 0;
}

int ethtool_stop_lane(unsigned if_id,
		      mppa_rpc_odp_answer_t *answer __attribute__((unused)))
{
	const int eth_if = if_id % 4;

	/* Close the lane ! */
	/* FIXME: Close the lane */
	/* mppa_ethernet[0]->mac.port_ctl._.rx_enable &= ~(1 << eth_if); */

	if (if_id == 4) {
		for (int i = 0; i < N_ETH_LANE; ++i)
			status[i].initialized = ETH_LANE_OFF;
	} else
		status[eth_if].initialized = ETH_LANE_OFF;

	lb_status.opened_refcount--;

	if (lb_status.loopback && !lb_status.opened_refcount){
		lb_status.loopback = 0;
		mppabeth_mac_disable_loopback_bypass((void *)&(mppa_ethernet[0]->mac));
	}
	return 0;
}

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

static int compare_rule_entries(const pkt_rule_entry_t entry1,
				const pkt_rule_entry_t entry2)
{
	return entry1.offset != entry2.offset ||
		entry1.cmp_mask != entry2.cmp_mask ||
		entry1.cmp_value != entry2.cmp_value ||
		entry1.hash_mask != entry2.hash_mask;
}

static int check_rules_identical(const pkt_rule_t *rules, int nb_rules,
				 mppa_rpc_odp_answer_t *answer)
{
	for ( int rule_id = 0; rule_id < nb_rules; ++rule_id ) {
		for ( int entry_id = 0; entry_id < rules[rule_id].nb_entries; ++entry_id ) {
			pkt_rule_entry_t entry =
				mppabeth_lb_get_rule((void *) &(mppa_ethernet[0]->lb),
						     rule_id, entry_id);

			if ( compare_rule_entries(rules[rule_id].entries[entry_id], entry ) ) {
				ETH_RPC_ERR_MSG(answer,
						"Rule[%d] entry[%d] differs from already set rule\n",
						rule_id, entry_id);
				return 1;
			}
		}
	}
	return 0;
}

// dispatch hash lut between registered clusters
static void update_lut(unsigned if_id)
{

	if (!lb_status.enabled)
		return;

	const int eth_if = if_id % 4;
	uint32_t clusters = 0;
	for (int i = 0; i < RPC_MAX_CLIENTS; ++i) {
		if (status[eth_if].cluster[i].opened != ETH_CLUS_STATUS_OFF &&
		    status[eth_if].cluster[i].policy == ETH_CLUS_POLICY_HASH &&
		    status[eth_if].cluster[i].rx_enabled)
			clusters |=  1 << i;
	}

	const int nb_registered = __k1_cbs(clusters);
	if (!nb_registered) {
		if (lb_status.opened_refcount > 1) {
			/* Set all the rules to DROP */
			for (int i = 0; i < lb_status.nb_rules; ++i){
				mppabeth_lb_cfg_extract_table_dispatch_mode((void *) &(mppa_ethernet[0]->lb),
									    i, MPPA_ETHERNET_DISPATCH_POLICY_DROP);
			}

		} else {
			/* HW has no support to stop hashing a subset of lanes.
			 * So let's assume this won't happen (Known Limitation ) */
		}


	}
	// dispatch hash lut between registered clusters

	for ( int cluster_nb = 0; cluster_nb < nb_registered; ++ cluster_nb ) {
		int cluster_id = __k1_ctz(clusters);
		clusters &= ~(1 << cluster_id);
		int tx_id = status[eth_if].cluster[cluster_id].txId;
		int noc_if = status[eth_if].cluster[cluster_id].nocIf;
#ifdef VERBOSE
		printf("config lut[%02d, +=%02d] -> C%2d: %d %d %d %d\n",
			   cluster_nb, nb_registered, cluster_id,
			   eth_if, tx_id, ETH_DEFAULT_CTX, noc_if - ETH_BASE_TX);
#endif
		for ( int lut_id = cluster_nb; lut_id < MPPABETHLB_LUT_ARRAY_SIZE; lut_id+=nb_registered ) {
			mppabeth_lb_cfg_luts((void *) &(mppa_ethernet[0]->lb),
								 eth_if, lut_id, tx_id,
								 ETH_DEFAULT_CTX,
								 noc_if - ETH_BASE_TX);
		}
	}
	for (int i = 0; i < lb_status.nb_rules; ++i){
		mppabeth_lb_cfg_extract_table_dispatch_mode((void *) &(mppa_ethernet[0]->lb),
							    i, MPPA_ETHERNET_DISPATCH_POLICY_HASH);
	}
}

static uint64_t h2n_order(uint64_t host_value, uint8_t cmp_mask)
{
	int upper_byte = 7 - (__k1_clz(cmp_mask) - ( 32 - 8 ));
	union {
		uint64_t d;
		uint8_t b[8];
	} original_value, reordered_value;
	reordered_value.d = 0ULL;
	original_value.d = host_value;
	for ( int src = upper_byte, dst = 0; src >= 0; --src, ++dst ) {
		reordered_value.b[dst] = original_value.b[src];
	}
	return reordered_value.d;
}

static void
ethtool_add_mac_match_entry(int hw_rule_id, uint64_t mac)
{
	/* Add a filter to make sure we match the target mac */
	unsigned entry_id = 9;

#ifdef VERBOSE
		printf("[ETH] Entry[%d] added to rule [%d] for Mac %llx added\n",
		       entry_id, hw_rule_id, (long long unsigned)mac);
#endif
	mppabeth_lb_cfg_rule((void *) &(mppa_ethernet[0]->lb),
			     hw_rule_id, entry_id,
			     0, 0x3f, h2n_order(mac, 0x3f), 0);
	mppabeth_lb_cfg_min_max_swap((void *) &(mppa_ethernet[0]->lb),
				     hw_rule_id, (entry_id >> 1), 0);
}

static void
ethtool_add_entry(int hw_rule_id, const pkt_rule_t *rule,
		  int rule_id __attribute__((unused)), int entry_id)
{
#ifdef VERBOSE
	printf("Rule[%d] => HWRule[%d] (P%d) Entry[%d]: offset %d cmp_mask 0x%02x cmp_value "
	       "0x%016llx hash_mask 0x%02x>\n",
	       rule_id, hw_rule_id,
	       rule->priority,
	       entry_id,
	       rule->entries[entry_id].offset,
	       rule->entries[entry_id].cmp_mask,
	       rule->entries[entry_id].cmp_value,
	       rule->entries[entry_id].hash_mask);
#endif
	mppabeth_lb_cfg_rule((void *) &(mppa_ethernet[0]->lb),
			     hw_rule_id, entry_id,
			     rule->entries[entry_id].offset,
			     rule->entries[entry_id].cmp_mask,
			     rule->entries[entry_id].cmp_value,
			     rule->entries[entry_id].hash_mask);
	mppabeth_lb_cfg_min_max_swap((void *) &(mppa_ethernet[0]->lb),
				     hw_rule_id, (entry_id >> 1), 0);
}

static void
ethtool_add_rule(int hw_rule_id, const pkt_rule_t *rule, int rule_id, uint64_t mac)
{
	for ( int entry_id = 0; entry_id < rule->nb_entries; ++entry_id) {
		ethtool_add_entry(hw_rule_id, rule, rule_id, entry_id);
	}
	if (lb_status.dual_mac) {
		ethtool_add_mac_match_entry(hw_rule_id, mac);
	}
}

static int
ethtool_configure_rules(int hw_rule_id, int nb_rules,
			const pkt_rule_t rules[nb_rules],
			uint64_t mac)
{
	for (int rule_id = 0; rule_id < nb_rules; ++rule_id, ++hw_rule_id) {
		ethtool_add_rule(hw_rule_id, &rules[rule_id], rule_id, mac);
		/* Set rule to DROP mode by default.
		 * It'll be enabled when a cluster is enabled */
		mppabeth_lb_cfg_extract_table_mode((void *) &(mppa_ethernet[0]->lb),
						   hw_rule_id,
						   rules[rule_id].priority,
						   MPPA_ETHERNET_DISPATCH_POLICY_DROP);
	}

	/* Note: in MAC_MATCH, we end up here but did nothing to the LB
	 * because nb_rules = 0. Rules will be created when a cluster first enables
	 * a lane with rule_id = lane_id */

	return hw_rule_id;
}

static inline uint64_t
ethtool_mac_to_64(unsigned eth_if) {
	uint64_t mac;
	memcpy(&mac, status[eth_if].mac_address[1], ETH_ALEN);
	return __builtin_bswap64(mac << 16);
}

int ethtool_configure_policy(unsigned remoteClus, unsigned if_id,
			     int fallthrough, int nb_rules,
			     const pkt_rule_t rules[nb_rules],
			     mppa_rpc_odp_answer_t *answer )
{
	const int eth_if = if_id % 4;
	eth_cluster_policy_t policy;
	if (fallthrough) {
		policy = ETH_CLUS_POLICY_FALLTHROUGH;
	} else if (!nb_rules) {
		if (lb_status.dual_mac)
			policy = ETH_CLUS_POLICY_MAC_MATCH;
		else {
			policy = ETH_CLUS_POLICY_FALLTHROUGH;
		}
	} else {
		policy = ETH_CLUS_POLICY_HASH;
	}

	status[eth_if].cluster[remoteClus].policy = policy;
	status[eth_if].refcounts.policy[policy]++;

	/* In fallthrough, the LB should not be touched */
	if (policy == ETH_CLUS_POLICY_FALLTHROUGH)
		return 0;

#ifdef VERBOSE
	printf("Applying %d rules for cluster %d\n", nb_rules, remoteClus);
#endif

	if (lb_status.enabled) {
		/* Some one already configure the LB.
		 * Just make sure rules are a match */
		if ( check_rules_identical(rules, nb_rules, answer) ) {
			ETH_RPC_ERR_MSG(answer, "Lane already opened with different rules\n");
			return -1;
		}
		return 0;
	}

	/* If we are in MAC MATCH mode, we have nothing to do here.
	 * Rule is created when ENABLE command is received */
	if (!nb_rules)
		return 0;
	/* No Hash rules yet. Make sure no one opened anything yet */
	for (int i =0; i < N_ETH_LANE; ++i){
		if (status[i].initialized == ETH_LANE_OFF)
			continue;

		/* In standard (not dual mac) mode, a cluster opened
		 * the interface without rules. This is not allowed */
		if(!lb_status.dual_mac)
			goto lane_opened_err;

		/* In dual mac mode, check for OPEN_DEF might have been executed
		 * this is allowed as they are in passthrough.
		 * We need to make sure no cluster opened the interface
		 * without rules (MAC_MATCH) */
		for(int j = 0; j < RPC_MAX_CLIENTS; ++j) {
			if(status[i].cluster[j].opened != ETH_CLUS_STATUS_OFF &&
			   status[i].cluster[j].policy == ETH_CLUS_POLICY_MAC_MATCH)
				goto lane_opened_err;
		}
	}

	/* Configure the LB */
	/* This is a little bit tricky.
	 * - In non dual mac mode, rules are the one the user provided.
	 * - In dual mac / 40G, we setup the rules once and add an entry that matches the 40G MAC
	 * address at the end.
	 * - In dual mac / 1-10G, we setup the rules 4 times each time adding
	 *   an entry that matches the mac of the N-th ethernet lane */
	int hw_rule_id = 0;
	for (int i = 0; i < ((lb_status.dual_mac && if_id < 4) ? 4 : 1); ++i) {
		uint64_t mac = ethtool_mac_to_64(i);

		/* Setup the rules */
		hw_rule_id = ethtool_configure_rules(hw_rule_id, nb_rules, rules, mac);
	}

	lb_status.enabled = 1;
	lb_status.nb_rules = hw_rule_id;

	return 0;

 lane_opened_err:
	ETH_RPC_ERR_MSG(answer, "Lane already opened without hashpolicy\n");
	return -1;

}

int ethtool_enable_cluster(unsigned remoteClus, unsigned if_id,
			   mppa_rpc_odp_answer_t *answer)
{
	const int eth_if = if_id % 4;
	const int noc_if = status[eth_if].cluster[remoteClus].nocIf;
	const int tx_id = status[eth_if].cluster[remoteClus].txId;
	const eth_cluster_policy_t policy = status[eth_if].cluster[remoteClus].policy;

	if(noc_if < 0 ||
	   status[eth_if].cluster[remoteClus].enabled) {
		ETH_RPC_ERR_MSG(answer, "Trying to enable lane %d which is closed or already enabled\n",
				eth_if);
		return -1;
	}

	/* Enable on single lane while 40G mode is one */
	if (if_id < 4 && status[eth_if].cluster[remoteClus].opened == ETH_CLUS_STATUS_40G){
		ETH_RPC_ERR_MSG(answer, "Trying to enable lane %d in 1/10G mode while lanes are open in 40G mode\n",
				eth_if);
		return -1;
	}
	/* Enable on all lanes while 1 or 10G mode is one */
	if (if_id == 4 && status[eth_if].cluster[remoteClus].opened == ETH_CLUS_STATUS_ON){
		ETH_RPC_ERR_MSG(answer, "Trying to enable lane %d in 40G while lane is open in 1/10G mode\n",
				eth_if);
		return -1;
	}

	/* Make sure link is up */
	if (!lb_status.loopback){
		enum mppa_eth_mac_ethernet_mode_e link_speed =
			ethtool_get_mac_speed(if_id, answer);

		if ((int)link_speed == -1)
			return -1;

		unsigned long long start = __k1_read_dsu_timestamp();
		int up = 0;
		int attempt = 0;
		int max_attempt = 10;
		do
		{
			while (__k1_read_dsu_timestamp() - start < 3ULL * __bsp_frequency) {
				if (mppa_eth_utils_mac_poll_state(eth_if, link_speed) == 0) {
					up = 1;
					break;
				}
			}
			if(up != 1) {
				if (&phy_status)
					phy_status = -1;
				mppa_eth_utils_init_mac(eth_if, link_speed);
				attempt++;
				printf("[ETH] Reinitializing lane %d (%d times over %d\n", eth_if,attempt, max_attempt);
			}
			start = __k1_read_dsu_timestamp();
		}while(!up && attempt <= max_attempt);

		if (!up) {
			ETH_RPC_ERR_MSG(answer, "No carrier on lane %d\n", eth_if);
			return -1;
		}

	}

	status[eth_if].cluster[remoteClus].enabled = 1;
	status[eth_if].refcounts.enabled++;

	if (!status[eth_if].cluster[remoteClus].rx_enabled)
		return 0;

	switch(policy){
	case ETH_CLUS_POLICY_HASH:
		/* Nothing to do in dual_mac mode, it was handled during LB rules configuration */
		if (!status[eth_if].rx_refcounts.policy[ETH_CLUS_POLICY_HASH]) {
			// TODO
			/* Change rule types from DROP to HASH but we don't actually
			 * drop FTM */
		}
		status[eth_if].rx_refcounts.policy[ETH_CLUS_POLICY_HASH]++;
		update_lut(if_id);
		break;
	case ETH_CLUS_POLICY_FALLTHROUGH:
		if (!status[eth_if].rx_refcounts.policy[ETH_CLUS_POLICY_FALLTHROUGH]) {
			mppabeth_lb_cfg_default_dispatch_policy((void *)&(mppa_ethernet[0]->lb),
								eth_if,
								MPPABETHLB_DISPATCH_DEFAULT_POLICY_RR);
		}
		status[eth_if].rx_refcounts.policy[ETH_CLUS_POLICY_FALLTHROUGH]++;
		mppabeth_lb_cfg_default_rr_dispatch_channel((void *)&(mppa_ethernet[0]->lb),
							    eth_if, noc_if - ETH_BASE_TX, tx_id,
					    (1 << ETH_DEFAULT_CTX));
		break;
	case ETH_CLUS_POLICY_MAC_MATCH:
		if (!status[eth_if].rx_refcounts.policy[ETH_CLUS_POLICY_MAC_MATCH]) {
			/* First enable on this lane. Configure a MAC match
			 * only rules in round robin to dispatch matching
			 * traffic to all MAC_MATCH clusters */
			ethtool_add_mac_match_entry(eth_if, ethtool_mac_to_64(eth_if));
			mppabeth_lb_cfg_extract_table_mode((void *)&(mppa_ethernet[0]->lb),
							   eth_if, /* Priority */ 0,
							   MPPABETHLB_DISPATCH_POLICY_RR);
		}
		status[eth_if].rx_refcounts.policy[ETH_CLUS_POLICY_MAC_MATCH]++;
		mppabeth_lb_cfg_table_rr_dispatch_channel((void *)&(mppa_ethernet[0]->lb),
							  eth_if, eth_if, noc_if - ETH_BASE_TX, tx_id,
							  (1 << ETH_DEFAULT_CTX));
		break;
	default:
		ETH_RPC_ERR_MSG(answer, "Internal error\n");
		return -1;
	}

	status[eth_if].rx_refcounts.enabled++;

	return 0;
}

int ethtool_disable_cluster(unsigned remoteClus, unsigned if_id,
			    mppa_rpc_odp_answer_t *answer)
{

	const int eth_if = if_id % 4;
	const int noc_if = status[eth_if].cluster[remoteClus].nocIf;
	const int tx_id = status[eth_if].cluster[remoteClus].txId;
	const eth_cluster_policy_t policy = status[eth_if].cluster[remoteClus].policy;

	/* Disable on single lane while 40G mode is one */
	if (if_id < 4 && status[eth_if].cluster[remoteClus].opened == ETH_CLUS_STATUS_40G){
		ETH_RPC_ERR_MSG(answer, "Trying to disable lane %d in 1/10G mode while lanes are open in 40G mode\n",
				eth_if);
		return -1;
	}
	/* Disable on all lanes while 1 or 10G mode is one */
	if (if_id == 4 && status[eth_if].cluster[remoteClus].opened == ETH_CLUS_STATUS_ON){
		ETH_RPC_ERR_MSG(answer, "Trying to disable lane %d in 40G while lane is open in 1/10G mode\n",
				eth_if);
		return -1;
	}

	if(status[eth_if].cluster[remoteClus].nocIf < 0 ||
	   status[eth_if].cluster[remoteClus].enabled == 0) {
		ETH_RPC_ERR_MSG(answer, "Trying to disable lane %d which is closed or already disabled\n",
				eth_if);
		return -1;
	}
	status[eth_if].cluster[remoteClus].enabled = 0;
	status[eth_if].refcounts.enabled--;

	if (!status[eth_if].cluster[remoteClus].rx_enabled)
		return 0;

	switch(policy){
	case ETH_CLUS_POLICY_HASH:
		status[eth_if].rx_refcounts.policy[ETH_CLUS_POLICY_HASH]--;
		update_lut(if_id);
		break;
	case ETH_CLUS_POLICY_FALLTHROUGH:
		status[eth_if].rx_refcounts.policy[ETH_CLUS_POLICY_FALLTHROUGH]--;
		if (!status[eth_if].rx_refcounts.policy[ETH_CLUS_POLICY_FALLTHROUGH]) {
			mppabeth_lb_cfg_default_dispatch_policy((void *)&(mppa_ethernet[0]->lb),
								eth_if,
								MPPABETHLB_DISPATCH_DEFAULT_POLICY_DROP);
		}
		/* Clear context from RR mask */
		mppabeth_lb_cfg_default_rr_dispatch_channel((void *)&(mppa_ethernet[0]->lb),
							    eth_if, noc_if - ETH_BASE_TX, tx_id, 0);
		break;
	case ETH_CLUS_POLICY_MAC_MATCH:
		status[eth_if].rx_refcounts.policy[ETH_CLUS_POLICY_MAC_MATCH]--;
		if (!status[eth_if].rx_refcounts.policy[ETH_CLUS_POLICY_MAC_MATCH]) {
			mppabeth_lb_cfg_extract_table_mode((void *)&(mppa_ethernet[0]->lb),
							   eth_if, /* Priority */ 0,
							   MPPABETHLB_DISPATCH_POLICY_DROP);
		}
		mppabeth_lb_cfg_table_rr_dispatch_channel((void *)&(mppa_ethernet[0]->lb),
							  eth_if, eth_if,
							  noc_if - ETH_BASE_TX, tx_id, 0);
		break;
	default:
		ETH_RPC_ERR_MSG(answer, "Internal error\n");
		return -1;
	}
	status[eth_if].rx_refcounts.enabled--;

	return 0;
}

int ethtool_close_cluster(unsigned remoteClus, unsigned if_id,
			  mppa_rpc_odp_answer_t *answer)
{
	int eth_if = if_id % 4;
	int noc_if = status[eth_if].cluster[remoteClus].nocIf;
	int tx_id = status[eth_if].cluster[remoteClus].txId;
	int rx_tag = status[eth_if].cluster[remoteClus].rx_tag;
	int fifo_id = status[eth_if].cluster[remoteClus].eth_tx_fifo;
	const eth_cluster_policy_t policy = status[eth_if].cluster[remoteClus].policy;

	if (if_id == 4) {
		for (int i = 0; i < N_ETH_LANE; ++i)
			if (status[i].cluster[remoteClus].enabled) {
				ETH_RPC_ERR_MSG(answer, "Trying to close 40G lane while lane %d is enabled\n",
						i);
				return -1;
			}
	} else {
		if (status[eth_if].cluster[remoteClus].enabled) {
			ETH_RPC_ERR_MSG(answer, "Trying to close lane %d while it is enabled\n",
					eth_if);
			return -1;
		}
	}

	if (rx_tag >= 0)
		mppa_noc_dnoc_rx_free(noc_if, rx_tag);

	if (fifo_id >= 0) {
		uint16_t mask = (if_id == 4) ? (0xff << fifo_id) : (0x1 << fifo_id);
		mppa_ethernet[0]->tx.fifo_if[noc_if - ETH_BASE_TX].lane[eth_if].
			eth_fifo[fifo_id].eth_fifo_ctrl._.jumbo_mode = 0;
		lb_status.tx_fifo[noc_if - ETH_BASE_TX] |= mask;
	}

	if (tx_id >= 0) {
		mppa_dnoc[noc_if]->tx_chan_route[tx_id].
			min_max_task_id[ETH_DEFAULT_CTX]._.min_max_task_id_en = 0;

		mppa_noc_dnoc_tx_free(noc_if, tx_id);

	}
	status[eth_if].refcounts.opened--;
	status[eth_if].refcounts.policy[policy]--;

	if (status[eth_if].refcounts.opened)
		goto cleanup_cluster_status;

	/* From now on, we know we are closing the last
	 * cluster that was using this lane */

	if(ethtool_stop_lane(if_id, answer))
		return -1;

	if (policy == ETH_CLUS_POLICY_HASH) {
		/* If we were the last hash policy. Clear up the tables
		 * and reset the LB hash data */
		int hash_global_count = 0;
		for (int i = 0; i < N_ETH_LANE; ++i) {
			hash_global_count += status[i].refcounts.policy[ETH_CLUS_POLICY_HASH];
		}
		if (!hash_global_count){
			for (int i = 0; i < MPPABETHLB_XT_TABLE_ARRAY_SIZE - 1; ++i){
				mppabeth_lb_cfg_extract_table_mode((void *) &(mppa_ethernet[0]->lb),
								   i, 0,
								   MPPA_ETHERNET_DISPATCH_POLICY_OFF);
			}
			lb_status.enabled = 0;
		}
	}

 cleanup_cluster_status:
	if (if_id == 4) {
		for (int i = 0; i < N_ETH_LANE; ++i) {
			_eth_cluster_status_init(&status[i].cluster[remoteClus]);
		}
	} else {
		_eth_cluster_status_init(&status[eth_if].cluster[remoteClus]);
	}
	return 0;
}

int ethtool_set_dual_mac(int enable,
			 mppa_rpc_odp_answer_t *answer)
{
	if (lb_status.dual_mac == enable)
		return 0;

	for (int i = 0; i < N_ETH_LANE; ++i) {
		if (status[i].refcounts.opened) {
			ETH_RPC_ERR_MSG(answer, "Cannot change dual mac mode when lane are active\n");
			return -1;
		}
	}
	lb_status.dual_mac = enable;
	return 0;
}

int ethtool_poll_lane(unsigned if_id)
{
	const int eth_if = if_id % 4;
	enum mppa_eth_mac_ethernet_mode_e link_speed =
		ethtool_get_mac_speed(if_id, NULL);

	if ((int)link_speed == -1)
		return 0;

	if (!mppa_eth_utils_mac_poll_state(eth_if, link_speed)) {
		/* Link is up */
		return 1;
	}
	/* Link is down */
	return 0;
}
int ethtool_lane_stats(unsigned if_id,
		       mppa_rpc_odp_answer_t *answer)
{
	const int eth_if = if_id % 4;

	if(status[eth_if].initialized == ETH_LANE_OFF){
		ETH_RPC_ERR_MSG(answer, "Trying to get stats on lane %d which is closed\n",
				eth_if);
		return -1;
	}

	mppa_rpc_odp_payload_eth_get_stat_t *stats =
		(mppa_rpc_odp_payload_eth_get_stat_t *)answer->payload;
	answer->payload_len = sizeof(*stats);

	if (lb_status.loopback){
		memset(stats, 0, sizeof(*stats));
		return 0;
	}

	stats->in_octets =
		mppabeth_mac_get_good_rx_bytes_nb((void *)&(mppa_ethernet[0]->mac),
						  eth_if);
	stats->in_ucast_pkts =
		mppabeth_mac_get_good_rx_packet_nb((void *)&(mppa_ethernet[0]->mac),
						   eth_if)-
		mppabeth_mac_get_rx_multicast((void *)&(mppa_ethernet[0]->mac),
					      eth_if) -
		mppabeth_mac_get_rx_broadcast((void *)&(mppa_ethernet[0]->mac),
					      eth_if);
	stats->in_discards =
		mppabeth_lb_get_dropped_counter((void*)&(mppa_ethernet[0]->lb),
						eth_if);;
	stats->in_errors =
		mppabeth_mac_get_total_rx_packet_nb((void *)&(mppa_ethernet[0]->mac),
						    eth_if) -
		mppabeth_mac_get_good_rx_packet_nb((void *)&(mppa_ethernet[0]->mac),
						   eth_if);

	stats->out_octets =
		mppabeth_mac_get_total_tx_bytes_nb((void *)&(mppa_ethernet[0]->mac),
						   eth_if);

	stats->out_ucast_pkts =
		mppabeth_mac_get_total_tx_packet_nb((void *)&(mppa_ethernet[0]->mac),
						    eth_if)-
		mppabeth_mac_get_tx_multicast((void *)&(mppa_ethernet[0]->mac),
					      eth_if) -
		mppabeth_mac_get_tx_broadcast((void *)&(mppa_ethernet[0]->mac),
					      eth_if);

	stats->out_discards = 0;
	for (int i = 0; i < MPPA_ETHERNET_TX_FIFO_IF_NUMBER; ++i){
		for (int j = 0; j < MPPA_ETHERNET_LANE_ETH_FIFO_NUMBER; ++j){
			stats->out_discards +=
				mppa_ethernet[0]->tx.fifo_if[i].lane[eth_if].eth_fifo[j].dropped_pkt_cnt.reg;
		}
	}
	stats->out_errors =
		mppa_ethernet[0]->mac.lane_stat[0].tx_bad_fcs.reg +
		mppa_ethernet[0]->mac.lane_stat[0].tx_bad_size._.tx_small +
		mppa_ethernet[0]->mac.lane_stat[0].tx_bad_size._.tx_large;

	return 0;
}

