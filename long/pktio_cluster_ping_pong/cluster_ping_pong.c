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

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <mppa_power.h>
#include <HAL/hal/core/mp.h>

#define PKT_BUF_NUM            64
#define PKT_BUF_SIZE           (2 * 1024)

#define PKT_SIZE		64

#define TEST_RUN_COUNT		64

odp_pool_t pool;
odp_pktio_t pktio;
odp_pktout_queue_t outq;
odp_pktin_queue_t inq;

static int setup_test()
{
	odp_pool_param_t params;
	char pktio_name[10];
	int remote_cluster;
	odp_pktio_param_t pktio_param;
	odp_pktin_queue_param_t pktin_param;
	odp_pktout_queue_param_t pktout_param;

	memset(&params, 0, sizeof(params));
	params.pkt.seg_len = PKT_BUF_SIZE;
	params.pkt.len     = PKT_BUF_SIZE;
	params.pkt.num     = PKT_BUF_NUM;
	params.type        = ODP_POOL_PACKET;

	pool = odp_pool_create("pkt_pool_cluster", &params);
	if (ODP_POOL_INVALID == pool) {
		fprintf(stderr, "unable to create pool\n");
		return 1;
	}

	/* Just take the next or the previous one as pair */
	remote_cluster = (__k1_get_cluster_id() % 2) == 0 ? __k1_get_cluster_id() + 1 : __k1_get_cluster_id() - 1;
	sprintf(pktio_name, "cluster%d", remote_cluster);
	memset(&pktio_param, 0, sizeof(pktio_param));

	pktio_param.in_mode = ODP_PKTIN_MODE_DIRECT;

	pktio = odp_pktio_open(pktio_name, pool, &pktio_param);
	if (pktio == ODP_PKTIO_INVALID)
		return 1;

	odp_pktin_queue_param_init(&pktin_param);
	odp_pktout_queue_param_init(&pktout_param);

	test_assert_ret(odp_pktin_queue_config(pktio, &pktin_param) == 0);
	test_assert_ret(odp_pktout_queue_config(pktio, &pktout_param) == 0);
	test_assert_ret(odp_pktin_queue(pktio, &inq, 1) == 1);
	test_assert_ret(odp_pktout_queue(pktio, &outq, 1) == 1);

	test_assert_ret(odp_pktio_start(pktio) == 0);
	return 0;
}

static int term_test()
{
	odp_packet_t pkt;
	int ret;

	/* flush any pending events */
	while (1) {
		ret = odp_pktin_recv(inq, &pkt, 1);

		if (ret > 0)
			odp_packet_free(pkt);
		else
			break;
	}

	test_assert_ret(odp_pktio_stop(pktio) == 0);
	test_assert_ret(odp_pktio_close(pktio) == 0);

	test_assert_ret(odp_pool_destroy(pool) == 0);

	return 0;
}

static int run_ping_pong()
{
	int ret, i;
	uint8_t *buf;
	odp_packet_t recv_pkts[1];

	odp_packet_t packet = odp_packet_alloc (pool, PKT_SIZE);
	test_assert_ret(packet != ODP_PACKET_INVALID);

	buf = odp_packet_data(packet);
	odp_packet_l2_offset_set(packet, 0);

	for (i = 0; i < PKT_SIZE; i++)
		buf[i] = i;

	int sent = 0;
	for (i = 0; i < 10; ++i){
		sent = odp_pktout_send(outq, &packet, 1);
		if (sent) {
			break;
		} else {
			odp_time_wait_ns(100000000ULL);
		}
	}
	test_assert_ret(sent == 1);

	while (1) {
		ret = odp_pktin_recv(inq, recv_pkts, 1);

		test_assert_ret(ret >= 0);

		if (ret == 1)
			break;
	}

	test_assert_ret(odp_packet_is_valid(recv_pkts[0]) == 1);
	test_assert_ret(odp_packet_len(recv_pkts[0]) == PKT_SIZE);

	buf = odp_packet_data(recv_pkts[0]);

	for (i = 0; i < PKT_SIZE; i++) {
		test_assert_ret(buf[i] == i);
	}

	odp_packet_free(recv_pkts[0]);

	return 0;
}

int run_test()
{
	int i = 0;
	test_assert_ret(setup_test() == 0);

	for (i = 0; i < TEST_RUN_COUNT; i++) 
		test_assert_ret(run_ping_pong() == 0);

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
