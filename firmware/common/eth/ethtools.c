#include <stdlib.h>
#include <string.h>
#include <inttypes.h>
#include <assert.h>
#include <HAL/hal/hal.h>

#include <odp/rpc/rpc.h>
#include <mppa_eth_core.h>
#include <mppa_eth_loadbalancer_core.h>
#include <mppa_eth_phy.h>
#include <mppa_eth_mac.h>
#include <mppa_routing.h>
#include <mppa_noc.h>
#include <mppa_eth_io_utils.h>
#include <mppa_eth_qsfp_utils.h>
#include "rpc-server.h"
#include "internal/rpc-server.h"
#include "internal/eth.h"
#include <odp/rpc/helpers.h>

enum mppa_eth_mac_ethernet_mode_e mac_get_default_mode(unsigned lane_id)
{
	(void)lane_id;
	switch (__bsp_flavour) {
	case BSP_ETH_530:
	case BSP_EXPLORER:
		return MPPA_ETH_MAC_ETHMODE_1G;
		break;
	case BSP_KONIC80:
		return MPPA_ETH_MAC_ETHMODE_40G;
		break;
	case BSP_DEVELOPER:
		if (__k1_get_cluster_id() >= 192) {
			/* IO(DDR|ETH)1 */
			if(lane_id == 0 || lane_id == 1)
				return MPPA_ETH_MAC_ETHMODE_10G_BASE_R;
			if(lane_id == 2 || lane_id == 3)
				return MPPA_ETH_MAC_ETHMODE_1G;
		} else {
			/* IO(DDR|ETH)0 => EXB03 */
			return MPPA_ETH_MAC_ETHMODE_40G;
		}
		break;
	default:
		return -1;
	}
	return -1;
}

enum mppa_eth_mac_ethernet_mode_e ethtool_get_mac_speed(unsigned if_id)
{
	int eth_if = if_id % 4;
	enum mppa_eth_mac_ethernet_mode_e link_speed =
		mac_get_default_mode(eth_if);
	if (!link_speed == MPPA_ETH_MAC_ETHMODE_40G && if_id == 4) {
		fprintf(stderr,
				"[ETH] Error: Cannot open 40G link\n");
		return -1;
	} else if (link_speed == MPPA_ETH_MAC_ETHMODE_40G && if_id < 4) {
		/* Link could do 40G but we use only one lane */
		link_speed = MPPA_ETH_MAC_ETHMODE_10G_BASE_R;
	}
	return link_speed;
}

int ethtool_init_lane(int eth_if)
{
	mppabeth_lb_cfg_header_mode((void *)&(mppa_ethernet[0]->lb),
				    eth_if, MPPABETHLB_ADD_HEADER);

	mppabeth_lb_cfg_table_rr_dispatch_trigger((void *)&(mppa_ethernet[0]->lb),
						  ETH_MATCHALL_TABLE_ID,
						  eth_if, 1);
	mppabeth_lb_cfg_default_dispatch_policy((void *)&(mppa_ethernet[0]->lb),
						eth_if,
						MPPABETHLB_DISPATCH_DEFAULT_POLICY_DROP);
	return 0;
}

int ethtool_open_cluster(unsigned remoteClus, unsigned if_id)
{
	const int eth_if = if_id % 4;
	if (if_id == 4) {
		for (int i = 0; i < N_ETH_LANE; ++i)
			if (status[i].cluster[remoteClus].opened != ETH_CLUS_STATUS_OFF)
				return -1;
		for (int i = 0; i < N_ETH_LANE; ++i)
			status[i].cluster[remoteClus].opened = ETH_CLUS_STATUS_40G;
	} else {
		if (status[eth_if].cluster[remoteClus].opened != ETH_CLUS_STATUS_OFF)
			return -1;
		status[eth_if].cluster[remoteClus].opened = ETH_CLUS_STATUS_ON;
	}
	status[eth_if].refcounts.opened++;

	return 0;
}

int ethtool_setup_eth2clus(unsigned remoteClus, int if_id,
			   int nocIf, int externalAddress,
			   int min_rx, int max_rx)
{
	int ret;

	mppa_dnoc_header_t header = { 0 };
	mppa_dnoc_channel_config_t config = { 0 };
	unsigned nocTx;
	int eth_if = if_id % 4;

	if (!status[eth_if].cluster[remoteClus].rx_enabled)
		return 0;

	ret = mppa_routing_get_dnoc_unicast_route(externalAddress,
						  odp_rpc_undensify_cluster_id(remoteClus), &config, &header);
	if (ret != MPPA_ROUTING_RET_SUCCESS) {
		fprintf(stderr, "[ETH] Error: Failed to route to cluster %d\n", remoteClus);
		return -1;
	}

	ret = mppa_noc_dnoc_tx_alloc_auto(nocIf, &nocTx, MPPA_NOC_BLOCKING);
	if (ret != MPPA_NOC_RET_SUCCESS) {
		fprintf(stderr, "[ETH] Error: Failed to find an available Tx on DMA %d\n", nocIf);
		return -1;
	}

	config._.loopback_multicast = 0;
	config._.cfg_pe_en = 1;
	config._.cfg_user_en = 1;
	config._.write_pe_en = 1;
	config._.write_user_en = 1;
	config._.decounter_id = 0;
	config._.decounted = 0;
	config._.payload_min = 1;
	config._.payload_max = 32;
	config._.bw_current_credit = 0xff;
	config._.bw_max_credit     = 0xff;
	config._.bw_fast_delay     = 0x00;
	config._.bw_slow_delay     = 0x00;

	header._.tag = min_rx;
	header._.valid = 1;
	header._.multicast = 0;

	ret = mppa_noc_dnoc_tx_configure(nocIf, nocTx, header, config);
	if (ret != MPPA_NOC_RET_SUCCESS) {
		fprintf(stderr, "[ETH] Error: Failed to configure Tx\n");
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


int ethtool_setup_clus2eth(unsigned remoteClus, int if_id, int nocIf)
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
		fprintf(stderr, "[ETH] Error: Failed to find an available Rx on DMA %d\n", nocIf);
		return -1;
	}

	mppa_dnoc_queue_event_it_target_t it_targets = {
		.reg = 0
	};
	int fifo_id;
	uint16_t fifo_mask;

	if (lb_status.tx_fifo[nocIf - 4] == 0) {
		fprintf(stderr, "[ETH] Error: No more Ethernet Tx fifo available on NoC interface %d\n", nocIf);
		return  -1;
	}
	fifo_id = __builtin_k1_ctz(lb_status.tx_fifo[nocIf - 4]);
	fifo_mask = if_id == 4 ? (0xf << fifo_id) : (1 << fifo_id);
	if ((fifo_mask & lb_status.tx_fifo[nocIf - 4]) != fifo_mask) {
		/* This should never happen */
		return -1;
	}
	lb_status.tx_fifo[nocIf - 4] &= ~fifo_mask;


	/* If we are using 40G */
	if (if_id == 4) {
		mppa_ethernet[0]->tx.fifo_if[nocIf - ETH_BASE_TX].lane[eth_if].
			eth_fifo[fifo_id].eth_fifo_ctrl._.jumbo_mode = 1;
	}
	status[eth_if].cluster[remoteClus].eth_tx_fifo = fifo_id;

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
		fprintf(stderr, "[ETH] Error: Failed to configure Rx\n");
		mppa_noc_dnoc_rx_free(nocIf, rx_port);
		return -1;
	}

	status[eth_if].cluster[remoteClus].nocIf = nocIf;
	status[eth_if].cluster[remoteClus].rx_tag = rx_port;
	return 0;
}

int ethtool_start_lane(unsigned if_id, int loopback)
{
	int ret;
	int eth_if = if_id % 4;
	if (lb_status.opened_refcount) {
		if (loopback && !lb_status.loopback) {
			fprintf(stderr,
					"[ETH] Error: One lane was enabled. Cannot set lane %d in loopback\n",
					eth_if);
			return -1;
		} else if (!loopback && lb_status.loopback) {
			fprintf(stderr,
					"[ETH] Error: Eth is in MAC loopback. Cannot enable lane %d\n",
					eth_if);
			return -1;
		}
	}

	switch (status[eth_if].initialized) {
	case ETH_LANE_OFF:
		if (loopback) {
#ifdef VERBOSE
			printf("[ETH] Initializing lane %d in loopback\n", eth_if);
#endif
			mppabeth_mac_enable_loopback_bypass((void *)&(mppa_ethernet[0]->mac));
			lb_status.loopback = 1;
		} else {
			enum mppa_eth_mac_ethernet_mode_e link_speed = ethtool_get_mac_speed(if_id);
			if ((int)link_speed < 0)
				return link_speed;
				mac_get_default_mode(eth_if);
#ifdef VERBOSE
			printf("[ETH] Initializing global MAC @ %d\n", link_speed);
#endif
			mppabeth_mac_cfg_mode((void*) &(mppa_ethernet[0]->mac), link_speed);

			/* Init MAC */
#ifdef VERBOSE
			printf("[ETH] Initializing MAC for lane %d\n", eth_if);
#endif
			ret = mppa_eth_utils_init_mac(eth_if, link_speed);
			if (ret == BAD_VENDOR) {
				fprintf(stderr,
					"[ETH] Warning: QSFP coonector is not supported\n");
			} else if (ret == -EBUSY) {
				/* lane is already configured. Ignore */
				mppa_ethernet[0]->mac.port_ctl._.rx_enable |= ~(1 << eth_if);
			} else if(ret < 0) {
				fprintf(stderr,
					"[ETH] Error: Failed to initialize lane %d (%d)\n",
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
#ifdef VERBOSE
			printf("[ETH] Starting MAC for lane %d\n", eth_if);
#endif
			mppa_eth_utils_start_lane(eth_if, link_speed);

		}
		if (if_id == 4) {
			for (int i = 0; i < N_ETH_LANE; ++i)
				status[i].initialized = ETH_LANE_ON_40G;
		} else
			status[eth_if].initialized = ETH_LANE_ON;
		break;
	case ETH_LANE_ON:
		if (if_id == 4) {
			fprintf(stderr,
				"[ETH] Error: One lane was enabled in 1 or 10G. Cannot set lane %d in 40G\n",
				eth_if);
			return -1;
		}
		break;
	case ETH_LANE_ON_40G:
		if (if_id < 4) {
			fprintf(stderr,
				"[ETH] Error: Interface was enabled in 40G. Cannot set lane %d in 1 or 10G mode\n",
				eth_if);
			return -1;
		}
		break;
	default:
		return -1;
	}

	lb_status.opened_refcount++;
	return 0;
}

int ethtool_stop_lane(unsigned if_id)
{
	const int eth_if = if_id % 4;

	/* Close the lane ! */
	mppa_ethernet[0]->mac.port_ctl._.rx_enable &= ~(1 << eth_if);

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

static int check_rules_identical(const pkt_rule_t *rules, int nb_rules)
{
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

// dispatch hash lut between registered clusters
static void update_lut(unsigned if_id)
{

	if (lb_status.enabled)
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
	int chunks[nb_registered];

	for ( int j = 0; j < nb_registered; ++j ) {
		chunks[j] = MPPABETHLB_LUT_ARRAY_SIZE / nb_registered +
			( ( j < ( MPPABETHLB_LUT_ARRAY_SIZE % nb_registered ) ) ? 1 : 0 );
	}

	for ( int i = 0, j = 0; i < MPPABETHLB_LUT_ARRAY_SIZE ; i+= chunks[j], j++ ) {
		int registered_cluster = __k1_ctz(clusters);
		clusters &= ~(1 << registered_cluster);
		int tx_id = status[eth_if].cluster[registered_cluster].txId;
		int noc_if = status[eth_if].cluster[registered_cluster].nocIf;
#ifdef VERBOSE
		printf("config lut[%3d-%3d] -> C%2d: %d %d %d %d\n",
			   i, i + chunks[j] - 1, registered_cluster,
			   eth_if, tx_id, ETH_DEFAULT_CTX, noc_if - 4);
#endif
		for ( int lut_id = i; lut_id < i + chunks[j] ; ++lut_id ) {
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
int ethtool_apply_rules(unsigned remoteClus, unsigned if_id,
			int nb_rules, const pkt_rule_t rules[nb_rules] )
{
	const int eth_if = if_id % 4;

	if (!nb_rules) {
		if (lb_status.dual_mac)
			status[eth_if].cluster[remoteClus].policy = ETH_CLUS_POLICY_MAC_MATCH;
		else
			status[eth_if].cluster[remoteClus].policy = ETH_CLUS_POLICY_FALLTHROUGH;
		return 0;
	}

#ifdef VERBOSE
	printf("Applying %d rules for cluster %d\n", nb_rules, remoteClus);
#endif

	if (!lb_status.enabled) {
		/* No Hash rules yet. Make sure no one opened anything yet */
		for (int i =0; i < N_ETH_LANE; ++i){
			if (status[i].initialized != ETH_LANE_OFF) {
				fprintf(stderr, "[ETH] Error: Lane %d already opened without hashpolicy\n", i);
				return -1;
			}
		}
		/* Configure the LB */
		for (int i = 0, hw_rule_id = 0; i < ((lb_status.dual_mac && if_id < 4) ? 4 : 1); ++i) {
			uint64_t mac = 0;

			for (int rule_id = 0; rule_id < nb_rules; ++rule_id, ++hw_rule_id) {
				for ( int entry_id = 0; entry_id < rules[rule_id].nb_entries; ++entry_id) {
#ifdef VERBOSE
					printf("Rule[%d] => [%d] (P%d) Entry[%d]: offset %d cmp_mask 0x%02x cmp_value "
					       "0x%016llx hash_mask 0x%02x>\n",
					       rule_id, hw_rule_id,
					       rules[rule_id].priority,
					       entry_id,
					       rules[rule_id].entries[entry_id].offset,
					       rules[rule_id].entries[entry_id].cmp_mask,
					       rules[rule_id].entries[entry_id].cmp_value,
					       rules[rule_id].entries[entry_id].hash_mask);
#endif
					mppabeth_lb_cfg_rule((void *) &(mppa_ethernet[0]->lb),
							     hw_rule_id, entry_id,
							     rules[rule_id].entries[entry_id].offset,
							     rules[rule_id].entries[entry_id].cmp_mask,
							     rules[rule_id].entries[entry_id].cmp_value,
							     rules[rule_id].entries[entry_id].hash_mask);
					mppabeth_lb_cfg_min_max_swap((void *) &(mppa_ethernet[0]->lb),
								     hw_rule_id, (entry_id >> 1), 0);
				}
				if (lb_status.dual_mac) {
					/* Add a filter to make sure we match the target mac */
					unsigned entry_id = rules[rule_id].nb_entries;
					mppabeth_lb_cfg_rule((void *) &(mppa_ethernet[0]->lb),
							     hw_rule_id, entry_id,
							     0, 0x3f, h2n_order(mac, 0x3f), 0);
					mppabeth_lb_cfg_min_max_swap((void *) &(mppa_ethernet[0]->lb),
								     hw_rule_id, (entry_id >> 1), 0);
				}
				mppabeth_lb_cfg_extract_table_mode((void *) &(mppa_ethernet[0]->lb),
								   hw_rule_id,
								   rules[rule_id].priority,
								   MPPA_ETHERNET_DISPATCH_POLICY_DROP);
			}
		}
		lb_status.enabled = 1;
		lb_status.nb_rules = ((lb_status.dual_mac && if_id < 4) ? 4 : 1) * nb_rules;
	} else if ( check_rules_identical(rules, nb_rules) ) {
		fprintf(stderr, "[ETH] Error: Non matching hash policy already registered\n");
		return -1;
	}
	status[eth_if].cluster[remoteClus].policy = ETH_CLUS_POLICY_HASH;
	return 0;
}

int ethtool_enable_cluster(unsigned remoteClus, unsigned if_id)
{
	const int eth_if = if_id % 4;
	const int noc_if = status[eth_if].cluster[remoteClus].nocIf;
	const int tx_id = status[eth_if].cluster[remoteClus].txId;
	const eth_cluster_policy_t policy = status[eth_if].cluster[remoteClus].policy;

	if(noc_if < 0 ||
	   status[eth_if].cluster[remoteClus].enabled) {
		return -1;
	}
	/* Enable on single lane while 40G mode is one */
	if (if_id < 4 && status[eth_if].cluster[remoteClus].opened == ETH_CLUS_STATUS_40G)
		return -1;
	/* Enable on all lanes while 1 or 10G mode is one */
	if (if_id == 4 && status[eth_if].cluster[remoteClus].opened == ETH_CLUS_STATUS_ON)
		return -1;

	/* Make sure link is up */
	if (!lb_status.loopback){
		enum mppa_eth_mac_ethernet_mode_e link_speed =
			ethtool_get_mac_speed(if_id);
		unsigned long long start = __k1_read_dsu_timestamp();
		int up = 0;
		while (__k1_read_dsu_timestamp() - start < 3ULL * __bsp_frequency) {
			if (!mppa_eth_utils_mac_poll_state(eth_if, link_speed)) {
				up = 1;
				break;
			}
		}
		if (!up) {
			fprintf(stderr, "[ETH] Error:No carrier on lane %d\n", eth_if);
			return -1;
		}
	}

	status[eth_if].cluster[remoteClus].enabled = 1;
	status[eth_if].refcounts.policy[policy]++;
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
			const uint64_t mac = 0ULL;
			/* "MATCH_ALL" Rule */
			mppabeth_lb_cfg_rule((void *)&(mppa_ethernet[0]->lb),
					     eth_if, ETH_MATCHALL_RULE_ID,
					     /* offset */ 0, /* Cmp Mask */0x3f,
					     /* Espected Value */ h2n_order(mac, 0x3f),
					     /* Hash. Unused */0);
			mppabeth_lb_cfg_extract_table_mode((void *)&(mppa_ethernet[0]->lb),
							   ETH_MATCHALL_TABLE_ID, /* Priority */ 0,
							   MPPABETHLB_DISPATCH_POLICY_RR);
		}
		status[eth_if].rx_refcounts.policy[ETH_CLUS_POLICY_MAC_MATCH]++;
		mppabeth_lb_cfg_table_rr_dispatch_channel((void *)&(mppa_ethernet[0]->lb),
							  eth_if, eth_if, noc_if - ETH_BASE_TX, tx_id,
							  (1 << ETH_DEFAULT_CTX));
		break;
	default:
		return -1;
	}

	status[eth_if].rx_refcounts.enabled++;

	return 0;
}

int ethtool_disable_cluster(unsigned remoteClus, unsigned if_id)
{

	const int eth_if = if_id % 4;
	const int noc_if = status[eth_if].cluster[remoteClus].nocIf;
	const int tx_id = status[eth_if].cluster[remoteClus].txId;
	const eth_cluster_policy_t policy = status[eth_if].cluster[remoteClus].policy;

	/* Disable on single lane while 40G mode is one */
	if (if_id < 4 && status[eth_if].cluster[remoteClus].opened == ETH_CLUS_STATUS_40G)
		return -1;
	/* Disable on all lanes while 1 or 10G mode is one */
	if (if_id == 4 && status[eth_if].cluster[remoteClus].opened == ETH_CLUS_STATUS_ON)
		return -1;

	if(status[eth_if].cluster[remoteClus].nocIf < 0 ||
	   status[eth_if].cluster[remoteClus].enabled == 0) {
		return -1;
	}
	status[eth_if].cluster[remoteClus].enabled = 0;
	status[eth_if].refcounts.policy[policy]--;
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
		return -1;
	}
	status[eth_if].rx_refcounts.enabled--;

	return 0;
}

int ethtool_close_cluster(unsigned remoteClus, unsigned if_id)
{
	int eth_if = if_id % 4;
	int noc_if = status[eth_if].cluster[remoteClus].nocIf;
	int tx_id = status[eth_if].cluster[remoteClus].txId;
	int rx_tag = status[eth_if].cluster[remoteClus].rx_tag;
	int fifo_id = status[eth_if].cluster[remoteClus].eth_tx_fifo;

	if (if_id == 4) {
		for (int i = 0; i < N_ETH_LANE; ++i)
			if (status[i].cluster[remoteClus].enabled)
				return -1;
	} else {
		if (status[eth_if].cluster[remoteClus].enabled)
			return -1;
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

	if (!status[eth_if].refcounts.opened)
		ethtool_stop_lane(if_id);

	if (status[eth_if].cluster[remoteClus].policy == ETH_CLUS_POLICY_HASH) {
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
	if (if_id == 4) {
		for (int i = 0; i < N_ETH_LANE; ++i) {
			_eth_cluster_status_init(&status[i].cluster[remoteClus]);
		}
	} else {
		_eth_cluster_status_init(&status[eth_if].cluster[remoteClus]);
	}
	return 0;
}

int ethtool_set_dual_mac(int enable)
{
	if (lb_status.dual_mac == enable)
		return 0;

	for (int i = 0; i < N_ETH_LANE; ++i) {
		if (status[i].refcounts.opened) {
			fprintf(stderr, "[ETH] Cannot change dual mac mode when lane are active\n");
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
		ethtool_get_mac_speed(if_id);
	if (!mppa_eth_utils_mac_poll_state(eth_if, link_speed)) {
		/* Link is up */
		return 1;
	}
	/* Link is down */
	return 0;
}
int ethtool_lane_stats(unsigned if_id,
		       odp_rpc_payload_eth_get_stat_t *stats)
{
	const int eth_if = if_id % 4;

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

	stats->in_unknown_protos = 0;
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
		mppa_ethernet[0]->mac.lane_stat[0].tx_bad_size.reg;

	return 0;
}

