/* Copyright (c) 2014, Linaro Limited
 * All rights reserved.
 *
 * SPDX-License-Identifier:     BSD-3-Clause
 */
#include <odp.h>
#include <test_helper.h>

#include <odp/helper/eth.h>
#include <odp/helper/ip.h>
#include <odp/helper/udp.h>
#include <odp/helper/linux.h>

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <HAL/hal/cluster/dsu.h>

#define MAX_PKT_BURST 1

#define PKT_BUF_NUM            256
#define PKT_BUF_SIZE           (2 * 1024)

#define PKT_SIZE		64

#define TEST_RUN_COUNT		1024

odp_pool_t pool;
odp_pktio_t pktio;
odp_pktout_queue_t outq;
odp_pktin_queue_t inq;

char *pktio_invalid_names[] = {
	"e0:loop:hashpolicy=[P8{@6}]",
	"e0:loop:hashpolicy=[P0{@6/0x10=0x800000}]",
	"e0p0:loop:hashpolicy=[P0{@6}][P0{@6}][P0{@6}]",
	"e0:loop:hashpolicy=[P0{@6}{@6}{@6}{@6}{@6}{@6}{@6}{@6}{@6}{@6}]",
	"e0:loop:hashpolicy=[P0{@6}][P0{@6}][P0{@6}][P0{@6}][P0{@6}][P0{@6}][P0{@6}][P0{@6}][P0{@6}]",
};

odph_ethaddr_t valid_eth_src  = { { 0xa0, 0xb0, 0xc0, 0xd0, 0xe0, 0xf0 } };
odph_ethaddr_t valid_eth_src_beef  = { { 0xa0, 0xb0, 0xc0, 0xd0, 0xe0, 0xf1 } };
odph_ethaddr_t invalid_eth_src  = { { 0xa0, 0xb1, 0xc0, 0xd0, 0xe0, 0xf0 } };
odph_ethaddr_t valid_eth_dst  = { { 0x11, 0x22, 0x33, 0x44, 0x55, 0x66 } };

uint32_t valid_ip_src   = 0xaabbccdd;
uint32_t invalid_ip_src   = 0xaa0bccdd;
uint32_t valid_ip_dst   = 0xa1b1c1d1;

char pktio_valid_name[] = { "e0:loop:nofree:hashpolicy=[P0{@6/0xcc=0xc0d000000800}{@26/0xf=0xaabbccdd}]"
			    "[P1{@6#0x3f/0x3f=0xa0b0c0d0e0f0}{@12/0x3=0x0800}]"
			    "[P1{@6#0x3f/0x3f=0xa0b0c0d0e0f1}{@12/0x3=0xbeef}]" };

char pool_name[] = "pkt_pool_dispatch";

static int setup_test()
{
	odp_pool_param_t params;
	odp_pktio_param_t pktio_param = {0};
	odp_pktin_queue_param_t pktin_param;
	odp_pktout_queue_param_t pktout_param;
	odp_pktio_t tmp;

	memset(&params, 0, sizeof(params));
	params.pkt.seg_len = PKT_BUF_SIZE;
	params.pkt.len     = PKT_BUF_SIZE;
	params.pkt.num     = PKT_BUF_NUM;
	params.type        = ODP_POOL_PACKET;

	pool = odp_pool_create(pool_name, &params);
	if (ODP_POOL_INVALID == pool) {
		fprintf(stderr, "unable to create pool\n");
		return 1;
	}

	pktio_param.in_mode = ODP_PKTIN_MODE_DIRECT;

	unsigned i;
	for ( i = 0; i < sizeof(pktio_invalid_names)/sizeof(pktio_invalid_names[0]); ++i ) {
		tmp = odp_pktio_open(pktio_invalid_names[i], pool, &pktio_param);
		test_assert_ret(tmp == ODP_PKTIO_INVALID);
	}

	pktio = odp_pktio_open(pktio_valid_name, pool, &pktio_param);
	test_assert_ret(pktio != ODP_PKTIO_INVALID);

	odp_pktin_queue_param_init(&pktin_param);
	odp_pktout_queue_param_init(&pktout_param);

	test_assert_ret(odp_pktin_queue_config(pktio, &pktin_param) == 0);
	test_assert_ret(odp_pktout_queue_config(pktio, &pktout_param) == 0);

	test_assert_ret(odp_pktin_queue(pktio, &inq, 1) == 1);
	test_assert_ret(odp_pktout_queue(pktio, &outq, 1) == 1);

	test_assert_ret(odp_pktio_start(pktio) == 0);

	printf("Setup ok\n");
	return 0;
}

static int term_test()
{
	test_assert_ret(odp_pktio_stop(pktio) == 0);
	test_assert_ret(odp_pktio_close(pktio) == 0);
	test_assert_ret(odp_pool_destroy(pool) == 0);
	return 0;
}

static odp_packet_t pack_pkt(odp_pool_t pool, odph_ethaddr_t eth_src, odph_ethaddr_t eth_dst,
		 uint32_t ip_src, uint32_t ip_dst, int payload, int is_ip)
{
	odp_packet_t pkt;
	char *buf;
	odph_ethhdr_t *eth;
	odph_ipv4hdr_t *ip;
	odph_udphdr_t *udp;
	static unsigned short seq = 0;

	pkt = odp_packet_alloc(pool, payload + ODPH_UDPHDR_LEN +
			       ODPH_IPV4HDR_LEN + ODPH_ETHHDR_LEN);

	if (pkt == ODP_PACKET_INVALID)
		return pkt;

	buf = odp_packet_data(pkt);

	/* ether */
	odp_packet_l2_offset_set(pkt, 0);
	eth = (odph_ethhdr_t *)buf;
	memcpy((char *)eth->src.addr, eth_src.addr, ODPH_ETHADDR_LEN);
	memcpy((char *)eth->dst.addr, eth_dst.addr, ODPH_ETHADDR_LEN);

	if ( is_ip ) {
		eth->type = odp_cpu_to_be_16(ODPH_ETHTYPE_IPV4);
		odp_packet_l3_offset_set(pkt, ODPH_ETHHDR_LEN);
		ip = (odph_ipv4hdr_t *)(buf + ODPH_ETHHDR_LEN);
		ip->dst_addr = odp_cpu_to_be_32(ip_dst);
		ip->src_addr = odp_cpu_to_be_32(ip_src);
		ip->ver_ihl = ODPH_IPV4 << 4 | ODPH_IPV4HDR_IHL_MIN;
		ip->tot_len = odp_cpu_to_be_16(payload + ODPH_UDPHDR_LEN +
				ODPH_IPV4HDR_LEN);
		ip->proto = ODPH_IPPROTO_UDP;
		seq++;
		ip->id = odp_cpu_to_be_16(seq);
		ip->chksum = 0;
		odph_ipv4_csum_update(pkt);
		odp_packet_l4_offset_set(pkt, ODPH_ETHHDR_LEN + ODPH_IPV4HDR_LEN);
		udp = (odph_udphdr_t *)(buf + ODPH_ETHHDR_LEN + ODPH_IPV4HDR_LEN);
		udp->src_port = 0;
		udp->dst_port = 0;
		udp->length = odp_cpu_to_be_16(payload + ODPH_UDPHDR_LEN);
		udp->chksum = 0;
		udp->chksum = odp_cpu_to_be_16(odph_ipv4_udp_chksum(pkt));
	}
	else {
		eth->type = odp_cpu_to_be_16(0xbeef);
	}
	return pkt;
}

static int send_recv(odp_packet_t packet, int send_nb, int expt_nb)
{
	static int test_id = 0;
	printf("send_recv %d\n", test_id++);
	odp_pktio_t pktio;
	odp_packet_t pkt_tbl[MAX_PKT_BURST];

	pktio = odp_pktio_lookup(pktio_valid_name);

	if (pktio == ODP_PKTIO_INVALID) {
		printf("Error: lookup of pktio %s failed\n",
			    pktio_valid_name);
		return 1;
	}
	int i;
	for ( i = 0; i < send_nb; ++i ) {
		odp_pktout_send(outq, &packet, 1);
	}
	int ret = 0;

	int start = __k1_read_dsu_timestamp();
	while ( ret >= 0 && ret < expt_nb && ( __k1_read_dsu_timestamp() - start ) < 10 * __bsp_frequency ) {
		int n_pkt = odp_pktin_recv(inq, pkt_tbl, MAX_PKT_BURST);
		ret += n_pkt;
		if (n_pkt > 0)
			odp_packet_free_multi(pkt_tbl, n_pkt);
	}
	test_assert_ret(ret == expt_nb);
	odp_packet_free(packet);
	printf("send_recv %d OK\n", test_id);

	return 0;
}

int run_test()
{
	test_assert_ret(setup_test() == 0);

	odp_packet_t pkt_valid_eth_no_ip =
		pack_pkt(pool, valid_eth_src_beef, valid_eth_dst, 0, 0, 64, 0);
	odp_packet_t pkt_valid_eth_valid_ip =
		pack_pkt(pool, valid_eth_src, valid_eth_dst, valid_ip_src, valid_ip_dst, 64, 1);
	odp_packet_t pkt_valid_eth_invalid_ip =
		pack_pkt(pool, valid_eth_src, valid_eth_dst, invalid_ip_src, valid_ip_dst, 64, 1);

	odp_packet_t pkt_invalid_eth_no_ip =
		pack_pkt(pool, invalid_eth_src, valid_eth_dst, 0, 0, 64, 0);
	odp_packet_t pkt_invalid_eth_valid_ip =
		pack_pkt(pool, invalid_eth_src, valid_eth_dst, valid_ip_src, valid_ip_dst, 64, 1);
	odp_packet_t pkt_invalid_eth_invalid_ip =
		pack_pkt(pool, invalid_eth_src, valid_eth_dst, invalid_ip_src, valid_ip_dst, 64, 1);

	send_recv(pkt_valid_eth_no_ip, 10, 10);
	send_recv(pkt_valid_eth_valid_ip, 10, 10);
	send_recv(pkt_valid_eth_invalid_ip, 10, 10);

	send_recv(pkt_invalid_eth_no_ip, 10, 0);
	send_recv(pkt_invalid_eth_valid_ip, 10, 10);
	send_recv(pkt_invalid_eth_invalid_ip, 10, 0);

	test_assert_ret(term_test() == 0);

	return 0;
}

int main(int argc, char **argv)
{
	odp_instance_t instance;
	(void) argc;
	(void) argv;
	test_assert_ret(odp_init_global(&instance, NULL, NULL) == 0);
	test_assert_ret(odp_init_local(instance, ODP_THREAD_CONTROL) == 0);

	test_assert_ret(run_test() == 0);

	test_assert_ret(odp_term_local() == 0);
	test_assert_ret(odp_term_global(instance) == 0);

	return 0;
}
