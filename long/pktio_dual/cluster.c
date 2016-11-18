/* Copyright (c) 2014, Linaro Limited
 * All rights reserved.
 *
 * SPDX-License-Identifier:     BSD-3-Clause
 */
#include <odp.h>
#include <test_helper.h>
#include <odp_cunit_common.h>

#include <odp/helper/eth.h>
#include <odp/helper/ip.h>
#include <odp/helper/udp.h>

#include <stdlib.h>
#include <mppa_power.h>
#include <HAL/hal/core/mp.h>

#define PKT_BUF_NUM            64
#define PKT_BUF_SIZE           (2 * 1024)

#define PKT_SIZE		64

odp_pool_t pool;
odp_pktio_t pktio;
odp_pktout_queue_t pktout;
odp_pktin_queue_t pktin;

static int sync_clusters()
{
	/* Send a packet and wait for one */
	int ret;
	odp_packet_t recv_pkts;
	odp_packet_t packet = odp_packet_alloc (pool, PKT_SIZE);
	test_assert_ret(packet != ODP_PACKET_INVALID);

	while((ret = odp_pktout_send(pktout, &packet, 1)) == 0);
	test_assert_ret(ret == 1);

	while (1) {
		ret = odp_pktin_recv(pktin, &recv_pkts, 1);
		test_assert_ret(ret >= 0);

		if (ret == 1)
			break;
	}
	odp_packet_free(recv_pkts);
	return 0;
}
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
	test_assert_ret(odp_pktin_queue(pktio, &pktin, 1) == 1);
	test_assert_ret(odp_pktout_queue(pktio, &pktout, 1) == 1);
	test_assert_ret(odp_pktio_start(pktio) == 0);
	printf("Ready togo\n");
	return 0;
}
static int try_pktio(const char *name)
{
	odp_pktio_param_t pktio_param;
	odp_pktin_queue_param_t pktin_param;
	odp_pktout_queue_param_t pktout_param;

	memset(&pktio_param, 0, sizeof(pktio_param));
	pktio_param.in_mode = ODP_PKTIN_MODE_DIRECT;

	sync_clusters();
	printf("Ready to test %s\n", name);

	/* Open pktio */
	odp_pktio_t pktio = odp_pktio_open(name, pool, &pktio_param);
	test_assert_ret(pktio != ODP_PKTIO_INVALID);

	odp_pktin_queue_param_init(&pktin_param);
	odp_pktout_queue_param_init(&pktout_param);
	test_assert_ret(odp_pktin_queue_config(pktio, &pktin_param) == 0);
	test_assert_ret(odp_pktout_queue_config(pktio, &pktout_param) == 0);
	test_assert_ret(odp_pktio_start(pktio) == 0);
	test_assert_ret(odp_pktio_stop(pktio) == 0);
	test_assert_ret(odp_pktio_close(pktio) == 0);
	printf("Started %s\n", name);
	sync_clusters();
	return 0;
}
static int term_test()
{
	test_assert_ret(odp_pktio_stop(pktio) == 0);
	test_assert_ret(odp_pktio_close(pktio) == 0);
	test_assert_ret(odp_pool_destroy(pool) == 0);

	return 0;
}

int main(int argc, char **argv)
{
	odp_instance_t instance;
	(void) argc;
	(void) argv;

	if (0 != odp_init_global(&instance, NULL, NULL)) {
		fprintf(stderr, "error: odp_init_global() failed.\n");
		return 1;
	}
	if (0 != odp_init_local(instance, ODP_THREAD_CONTROL)) {
		fprintf(stderr, "error: odp_init_local() failed.\n");
		return 1;
	}

	test_assert_ret(setup_test() == 0);
	test_assert_ret(try_pktio("e0:loop") == 0);
	test_assert_ret(try_pktio("e0p0:loop") == 0);
	test_assert_ret(try_pktio("e0p1:loop") == 0);
	test_assert_ret(try_pktio("e0p2:loop") == 0);
	test_assert_ret(try_pktio("e0p3:loop") == 0);
	test_assert_ret(try_pktio("e0:loop") == 0);
	test_assert_ret(try_pktio("e1:loop") == 0);
	test_assert_ret(try_pktio("e1p0:loop") == 0);
	test_assert_ret(try_pktio("e1p1:loop") == 0);
	test_assert_ret(try_pktio("e1p2:loop") == 0);
	test_assert_ret(try_pktio("e1p3:loop") == 0);
	test_assert_ret(try_pktio("e1:loop") == 0);
	test_assert_ret(term_test() == 0);

	if (0 != odp_term_local()) {
		fprintf(stderr, "error: odp_term_local() failed.\n");
		return 1;
	}

	if (0 != odp_term_global(instance)) {
		fprintf(stderr, "error: odp_term_global() failed.\n");
		return 1;
	}

	return 0;
}
