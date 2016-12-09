#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <stdint.h>
#include <inttypes.h>
#include <getopt.h>
#include "unistd.h"

#include <mppa/osconfig.h>
#include <HAL/hal/hal.h>

#include "mppa_pcie_netdev.h"
#include "netdev.h"
#include "internal/netdev.h"

#define IF_COUNT		8
#define RING_BUFFER_ENTRIES	32

/**
 * Transfer all packet received on host tx to host rx
 */

static uint64_t packet_nb[IF_COUNT] = {0};

void main_loop(int n_if, int drop_all)
{
	int i;
	int ret;

	for (i = 0; i < n_if; i++) {
		int src_if = i;
		int dst_if = i + 1;
		if (dst_if >= n_if)
			dst_if -= n_if;

		uint64_t now = __k1_read_dsu_timestamp();
		static uint64_t last_stat = 0;
		if ( ( now - last_stat ) > (uint64_t)__bsp_frequency ) {
			last_stat = __k1_read_dsu_timestamp();
			printf("packets: ");
			for ( int if_id = 0; if_id < n_if; ++ if_id ) {
				printf("if[%d] %llu ", if_id, packet_nb[if_id]);
			}
			printf("\n");
		}
		struct mppa_pcie_eth_if_config * src_cfg = netdev_get_eth_if_config(src_if);
		struct mppa_pcie_eth_if_config * dst_cfg = netdev_get_eth_if_config(dst_if);
		struct mppa_pcie_eth_h2c_ring_buff_entry *src_buf;

		src_buf = netdev_h2c_peek_data(src_cfg);

		/* Check if there are packets pending */
		if (src_buf == NULL)
			continue;

		if (drop_all) {
			/* Drop the packet. Repushed the same buffer in place so the host
			 * can send another apcket there */
			struct mppa_pcie_eth_h2c_ring_buff_entry pkt;
			pkt = *src_buf;
			ret = netdev_h2c_enqueue_buffer(src_cfg, &pkt);
			assert(ret == 0);
			continue;
		}
		/* Keep the packet in H2C until we have a slot in C2H RB */
		if (netdev_c2h_is_full(dst_cfg))
			continue;

		/* Create a new C2H packet with the data */
		struct mppa_pcie_eth_c2h_ring_buff_entry pkt;
		pkt.len = src_buf->len;
		pkt.status = 0;
		pkt.checksum = 0;
		pkt.pkt_addr = src_buf->pkt_addr;
		pkt.data = 0ULL;

		packet_nb[i]++;
		/* queue it an retreive the previous addr at this slot */
		struct mppa_pcie_eth_c2h_ring_buff_entry old_pkt;
		netdev_c2h_enqueue_data(dst_cfg, &pkt, &old_pkt);

		/* Create a h2c pkt with the address retreived from the C2H RB */
		struct mppa_pcie_eth_h2c_ring_buff_entry free_pkt;
		free_pkt.pkt_addr = old_pkt.pkt_addr;
		ret = netdev_h2c_enqueue_buffer(src_cfg, &free_pkt);
		assert(ret == 0);

	}
}

static eth_if_cfg_t if_cfgs[IF_COUNT] = {
	[ 0 ... IF_COUNT - 1] =
	{ .if_id = 0, .mtu = MPPA_PCIE_ETH_DEFAULT_MTU,
	  .n_c2h_entries = RING_BUFFER_ENTRIES,
	  .n_h2c_entries = RING_BUFFER_ENTRIES,
	  .mac_addr = { 0xde, 0xad, 0xbe, 0xef },
	  .flags = 0,
	}
};


int main(int argc, char* argv[])
{
	int opt, ret;

	unsigned n_if = 1;
	unsigned drop_all = 0;
	while ((opt = getopt(argc, argv, "dn:")) != -1) {
		switch (opt) {
		case 'd':
			drop_all = 1;
			break;
		case 'n':
			n_if = atoi(optarg);
			break;
		default: /* '?' */
			fprintf(stderr, "Wrong arguments for boot\n");
			return -1;
		}
	}
	printf("Starting %u interfaces\n", n_if);

	for (int i = 0; i < IF_COUNT; ++i){
		if_cfgs[i].if_id = i;
		if_cfgs[i].mac_addr[MAC_ADDR_LEN - 1] = i;
	}

	netdev_init();
	ret = netdev_configure(n_if, if_cfgs);
	assert(ret == 0);

	ret = netdev_start();
	assert(ret == 0);

	while(1) {
		main_loop(n_if, drop_all);
	}

	return 0;
}
