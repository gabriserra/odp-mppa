/* Copyright (c) 2014, Linaro Limited
 * All rights reserved.
 *
 * SPDX-License-Identifier:     BSD-3-Clause
 */
#include <odp.h>
#include <odp_cunit_common.h>

#include <odp/helper/eth.h>
#include <odp/helper/ip.h>
#include <odp/helper/udp.h>

#include <stdlib.h>
#include <unistd.h>

#define PKT_BUF_NUM            200
#define PKT_BUF_SIZE           (2 * 1024)
#define PKT_LEN_NORMAL         64
#define TEST_SEQ_INVALID       ((uint32_t)~0)
#define TEST_SEQ_MAGIC         0x92749451
#define TX_BATCH_LEN           4

/** interface names used for testing */
static const char *iface_name[1];

/** local container for pktio attributes */
typedef struct {
	const char *name;
	odp_pktio_t id;
	odp_queue_t outq;
	odp_queue_t inq;
	odp_pktio_input_mode_t in_mode;
} pktio_info_t;

/** magic number and sequence at start of UDP payload */
typedef struct ODP_PACKED {
	uint32be_t magic;
	uint32be_t seq;
} pkt_head_t;

/** magic number at end of UDP payload */
typedef struct ODP_PACKED {
	uint32be_t magic;
} pkt_tail_t;

/** size of transmitted packets */
static uint32_t packet_len = PKT_LEN_NORMAL;

/** default packet pool */
odp_pool_t tx_pkt_pool = ODP_POOL_INVALID;
odp_pool_t rx_pkt_pool = ODP_POOL_INVALID;

static uint32_t pktio_init_packet(odp_packet_t pkt)
{
	odph_ethhdr_t *eth;
	odph_ipv4hdr_t *ip;
	odph_udphdr_t *udp;
	char *buf;
	uint8_t mac[ODPH_ETHADDR_LEN] = {0};
	int pkt_len = odp_packet_len(pkt);

	buf = odp_packet_data(pkt);

	/* Ethernet */
	odp_packet_l2_offset_set(pkt, 0);
	eth = (odph_ethhdr_t *)buf;
	memcpy(eth->src.addr, mac, ODPH_ETHADDR_LEN);
	memcpy(eth->dst.addr, mac, ODPH_ETHADDR_LEN);
	eth->type = odp_cpu_to_be_16(ODPH_ETHTYPE_IPV4);

	/* IP */
	odp_packet_l3_offset_set(pkt, ODPH_ETHHDR_LEN);
	ip = (odph_ipv4hdr_t *)(buf + ODPH_ETHHDR_LEN);
	ip->dst_addr = odp_cpu_to_be_32(0x0a000064);
	ip->src_addr = odp_cpu_to_be_32(0x0a000001);
	ip->ver_ihl = ODPH_IPV4 << 4 | ODPH_IPV4HDR_IHL_MIN;
	ip->tot_len = odp_cpu_to_be_16(pkt_len - ODPH_ETHHDR_LEN);
	ip->ttl = 128;
	ip->proto = ODPH_IPPROTO_UDP;
	ip->id = odp_cpu_to_be_16(0);
	ip->chksum = 0;
	odph_ipv4_csum_update(pkt);

	/* UDP */
	odp_packet_l4_offset_set(pkt, ODPH_ETHHDR_LEN + ODPH_IPV4HDR_LEN);
	udp = (odph_udphdr_t *)(buf + ODPH_ETHHDR_LEN + ODPH_IPV4HDR_LEN);
	udp->src_port = odp_cpu_to_be_16(12049);
	udp->dst_port = odp_cpu_to_be_16(12050);
	udp->length = odp_cpu_to_be_16(pkt_len -
				       ODPH_ETHHDR_LEN - ODPH_IPV4HDR_LEN);
	udp->chksum = 0;

	return 0;
}

static int tx_pool_create(void)
{
	odp_pool_param_t params;
	char pool_name[ODP_POOL_NAME_LEN];

	if (tx_pkt_pool != ODP_POOL_INVALID)
		return -1;

	memset(&params, 0, sizeof(params));
	params.pkt.seg_len = 0;
	params.pkt.len     = PKT_BUF_SIZE;
	params.pkt.num     = PKT_BUF_NUM;
	params.type        = ODP_POOL_PACKET;

	snprintf(pool_name, sizeof(pool_name),
		 "pkt_pool_tx");
	tx_pkt_pool = odp_pool_create(pool_name, &params);
	if (tx_pkt_pool == ODP_POOL_INVALID)
		return -1;

	return 0;
}

static int rx_pool_create(void)
{
	odp_pool_param_t params;
	char pool_name[ODP_POOL_NAME_LEN];

	if (rx_pkt_pool != ODP_POOL_INVALID)
		return -1;

	memset(&params, 0, sizeof(params));
	params.pkt.seg_len = 0;
	params.pkt.len     = PKT_BUF_SIZE;
	params.pkt.num     = 21;
	params.type        = ODP_POOL_PACKET;

	snprintf(pool_name, sizeof(pool_name),
		 "pkt_pool_rx");
	rx_pkt_pool = odp_pool_create(pool_name, &params);
	if (rx_pkt_pool == ODP_POOL_INVALID)
		return -1;

	return 0;
}

static odp_pktio_t create_pktio(int iface_idx, odp_pktio_input_mode_t imode,
				odp_pktio_output_mode_t omode)
{
	odp_pktio_t pktio;
	odp_pktio_param_t pktio_param;
	const char *iface = iface_name[iface_idx];

	odp_pktio_param_init(&pktio_param);

	pktio_param.in_mode = imode;
	pktio_param.out_mode = omode;

	pktio = odp_pktio_open(iface, rx_pkt_pool, &pktio_param);
	if (pktio == ODP_PKTIO_INVALID)
		pktio = odp_pktio_lookup(iface);
	CU_ASSERT(pktio != ODP_PKTIO_INVALID);
	CU_ASSERT(odp_pktio_to_u64(pktio) !=
		  odp_pktio_to_u64(ODP_PKTIO_INVALID));
	/* Print pktio debug info and test that the odp_pktio_print() function
	 * is implemented. */
	if (pktio != ODP_PKTIO_INVALID)
		odp_pktio_print(pktio);

	return pktio;
}

static int create_inq(odp_pktio_t pktio, odp_queue_type_t qtype)
{
	odp_queue_param_t qparam;
	odp_queue_t inq_def;
	char inq_name[ODP_QUEUE_NAME_LEN];

	odp_queue_param_init(&qparam);
	qparam.sched.prio  = ODP_SCHED_PRIO_DEFAULT;
	qparam.sched.sync  = ODP_SCHED_SYNC_ATOMIC;
	qparam.sched.group = ODP_SCHED_GROUP_ALL;

	snprintf(inq_name, sizeof(inq_name), "inq-pktio-%" PRIu64,
		 odp_pktio_to_u64(pktio));
	inq_def = odp_queue_lookup(inq_name);
	if (inq_def == ODP_QUEUE_INVALID)
		inq_def = odp_queue_create(
				inq_name,
				ODP_QUEUE_TYPE_PKTIN,
				qtype == ODP_QUEUE_TYPE_POLL ? NULL : &qparam);

	CU_ASSERT(inq_def != ODP_QUEUE_INVALID);

	return odp_pktio_inq_setdef(pktio, inq_def);
}

static int destroy_inq(odp_pktio_t pktio)
{
	odp_queue_t inq;
	odp_event_t ev;
	odp_queue_type_t q_type;

	inq = odp_pktio_inq_getdef(pktio);

	if (inq == ODP_QUEUE_INVALID) {
		CU_FAIL("attempting to destroy invalid inq");
		return -1;
	}

	CU_ASSERT(odp_pktio_inq_remdef(pktio) == 0);

	q_type = odp_queue_type(inq);

	/* flush any pending events */
	while (1) {
		if (q_type == ODP_QUEUE_TYPE_POLL)
			ev = odp_queue_deq(inq);
		else
			ev = odp_schedule(NULL, ODP_SCHED_NO_WAIT);

		if (ev != ODP_EVENT_INVALID)
			odp_event_free(ev);
		else
			break;
	}

	return odp_queue_destroy(inq);
}

void pktio_test_stats(void)
{
	odp_pktio_t pktio;
	odp_packet_t pkt;
	odp_event_t tx_ev[100];
	odp_event_t ev;
	int i, pkts, ret, alloc = 0;
	odp_queue_t outq;
	uint64_t wait = odp_schedule_wait_time(ODP_TIME_MSEC_IN_NS);
	_odp_pktio_stats_t stats;

	pktio = create_pktio(0, ODP_PKTIN_MODE_SCHED,
			     ODP_PKTOUT_MODE_SEND);
	CU_ASSERT_FATAL(pktio != ODP_PKTIO_INVALID);
	create_inq(pktio,  ODP_QUEUE_TYPE_SCHED);

	outq = odp_pktio_outq_getdef(pktio);

	/* start first */
	ret = odp_pktio_start(pktio);
	CU_ASSERT(ret == 0);

	/* alloc */
	for (alloc = 0; alloc < 100; alloc++) {
		pkt = odp_packet_alloc(tx_pkt_pool, packet_len);
		if (pkt == ODP_PACKET_INVALID)
			break;
		pktio_init_packet(pkt);
		tx_ev[alloc] = odp_packet_to_event(pkt);
	}

	/* send */
	for (pkts = 0; pkts != alloc; ) {
		ret = odp_queue_enq_multi(outq, &tx_ev[pkts], alloc - pkts);
		if (ret < 0) {
			CU_FAIL("unable to enqueue packet\n");
			break;
		}
		pkts += ret;
	}

	/* get */
	for (i = 0, pkts = 0; i < 100; i++) {
		ev = odp_schedule(NULL, wait);
		if (ev != ODP_EVENT_INVALID) {
			if (odp_event_type(ev) == ODP_EVENT_PACKET) {
				pkt = odp_packet_from_event(ev);
				pkts++;
			}
			odp_event_free(ev);
		}
	}
	CU_ASSERT(pkts == 20);
	ret = _odp_pktio_stats(pktio, &stats);
	CU_ASSERT(ret == 0);
	CU_ASSERT(stats.in_discards == 80);
	CU_ASSERT(stats.in_dropped == 0);

	CU_ASSERT(odp_pktio_stop(pktio) == 0);
	destroy_inq(pktio);
	CU_ASSERT(odp_pktio_close(pktio) == 0);
}

static int pktio_suite_init(void)
{
	iface_name[0] = "e0:loop:tags=20";

	if (!iface_name[0]) {
		printf("No interfaces specified, using default \"loop\".\n");
		iface_name[0] = "loop";
	}

	if (tx_pool_create() != 0) {
		fprintf(stderr, "error: failed to create default Tx pool\n");
		return -1;
	}
	if (rx_pool_create() != 0) {
		fprintf(stderr, "error: failed to create default Rx pool\n");
		return -1;
	}

	return 0;
}

int pktio_suite_term(void)
{
	int ret = 0;

	if (odp_pool_destroy(tx_pkt_pool) != 0) {
		fprintf(stderr, "error: failed to destroy default Tx pool\n");
		ret = -1;
	}
	tx_pkt_pool = ODP_POOL_INVALID;

	if (odp_pool_destroy(rx_pkt_pool) != 0) {
		fprintf(stderr, "error: failed to destroy default Rx pool\n");
		ret = -1;
	}
	rx_pkt_pool = ODP_POOL_INVALID;

	return ret;
}

odp_testinfo_t pktio_suite[] = {
	ODP_TEST_INFO(pktio_test_stats),
	ODP_TEST_INFO_NULL
};

odp_suiteinfo_t pktio_suites[] = {
	{"Packet I/O", pktio_suite_init,
	 pktio_suite_term, pktio_suite},
	ODP_SUITE_INFO_NULL
};

int main(void)
{
	int ret = odp_cunit_register(pktio_suites);

	if (ret == 0)
		ret = odp_cunit_run();

	return ret;
}
