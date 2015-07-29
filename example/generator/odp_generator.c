/* Copyright (c) 2014, Linaro Limited
 * All rights reserved.
 *
 * SPDX-License-Identifier:     BSD-3-Clause
 */

/** enable strtok */
#define _POSIX_C_SOURCE 200112L

#include <time.h>
#include <stdlib.h>
#include <getopt.h>
#include <unistd.h>
#include <sys/time.h>

#include <example_debug.h>

#include <odp.h>

#include <odp/helper/linux.h>
#include <odp/helper/eth.h>
#include <odp/helper/ip.h>
#include <odp/helper/udp.h>
#include <odp/helper/icmp.h>

#define MAX_WORKERS            32		/**< max number of works */
#define SHM_PKT_POOL_SIZE      (64*2048)	/**< pkt pool size */
#define SHM_PKT_POOL_BUF_SIZE  1856		/**< pkt pool buf size */

#define APPL_MODE_UDP    0			/**< UDP mode */
#define APPL_MODE_PING   1			/**< ping mode */
#define APPL_MODE_RCV    2			/**< receive mode */

/** print appl mode */
#define PRINT_APPL_MODE(x) printf("%s(%i)\n", #x, (x))

/** Get rid of path in filename - only for unix-type paths using '/' */
#define NO_PATH(file_name) (strrchr((file_name), '/') ? \
			    strrchr((file_name), '/') + 1 : (file_name))
/**
 * Parsed command line application arguments
 */
typedef struct {
	int cpu_count;		/**< system CPU count */
	int if_count;		/**< Number of interfaces to be used */
	char **if_names;	/**< Array of pointers to interface names */
	char *if_str;		/**< Storage for interface names */
	odp_pool_t pool;	/**< Pool for packet IO */
	odph_ethaddr_t srcmac;	/**< src mac addr */
	odph_ethaddr_t dstmac;	/**< dest mac addr */
	unsigned int srcip;	/**< src ip addr */
	unsigned int dstip;	/**< dest ip addr */
	int mode;		/**< work mode */
	int number;		/**< packets number to be sent */
	int payload;		/**< data len */
	int timeout;		/**< wait time */
	int interval;		/**< wait interval ms between sending
				     each packet */
} appl_args_t;

/**
 * counters
*/
static struct {
	odp_atomic_u64_t seq;	/**< ip seq to be send */
	odp_atomic_u64_t ip;	/**< ip packets */
	odp_atomic_u64_t udp;	/**< udp packets */
	odp_atomic_u64_t icmp;	/**< icmp packets */
} counters;

/** * Thread specific arguments
 */
typedef struct {
	char *pktio_dev;	/**< Interface name to use */
	odp_pool_t pool;	/**< Pool for packet IO */
	odp_timer_pool_t tp;	/**< Timer pool handle */
	odp_queue_t tq;		/**< Queue for timeouts */
	odp_timer_t tim;	/**< Timer handle */
	odp_timeout_t tmo_ev;	/**< Timeout event */
	int mode;		/**< Thread mode */
} thread_args_t;

/**
 * Grouping of both parsed CL args and thread specific args - alloc together
 */
typedef struct {
	/** Application (parsed) arguments */
	appl_args_t appl;
	/** Thread specific arguments */
	thread_args_t thread[MAX_WORKERS];
} args_t;

/** Global pointer to args */
static args_t *args;

/* helper funcs */
static void parse_args(int argc, char *argv[], appl_args_t *appl_args);
static void print_info(char *progname, appl_args_t *appl_args);
static void usage(char *progname);
static int scan_ip(char *buf, unsigned int *paddr);
static int scan_mac(char *in, odph_ethaddr_t *des);
static void tv_sub(struct timeval *recvtime, struct timeval *sendtime);

/**
 * Sleep for the specified amount of milliseconds
 * Use ODP timer, busy wait until timer expired and timeout event received
 */
static void millisleep(uint32_t ms,
		       odp_timer_pool_t tp,
		       odp_timer_t tim,
		       odp_queue_t q,
		       odp_timeout_t tmo)
{
	uint64_t ticks = odp_timer_ns_to_tick(tp, 1000000ULL * ms);
	odp_event_t ev = odp_timeout_to_event(tmo);
	int rc = odp_timer_set_rel(tim, ticks, &ev);
	if (rc != ODP_TIMER_SUCCESS)
		EXAMPLE_ABORT("odp_timer_set_rel() failed\n");
	/* Spin waiting for timeout event */
	while ((ev = odp_queue_deq(q)) == ODP_EVENT_INVALID)
		(void)0;
}

/**
 * Scan ip
 * Parse ip address.
 *
 * @param buf ip address string xxx.xxx.xxx.xx
 * @param paddr ip address for odp_packet
 * @return 1 success, 0 failed
*/
static int scan_ip(char *buf, unsigned int *paddr)
{
	int part1, part2, part3, part4;
	char tail = 0;
	int field;

	if (buf == NULL)
		return 0;

	field = sscanf(buf, "%d . %d . %d . %d %c",
		       &part1, &part2, &part3, &part4, &tail);

	if (field < 4 || field > 5) {
		printf("expect 4 field,get %d/n", field);
		return 0;
	}

	if (tail != 0) {
		printf("ip address mixed with non number/n");
		return 0;
	}

	if ((part1 >= 0 && part1 <= 255) && (part2 >= 0 && part2 <= 255) &&
	    (part3 >= 0 && part3 <= 255) && (part4 >= 0 && part4 <= 255)) {
		if (paddr)
			*paddr = part1 << 24 | part2 << 16 | part3 << 8 | part4;
		return 1;
	} else {
		printf("not good ip %d:%d:%d:%d/n", part1, part2, part3, part4);
	}

	return 0;
}

/**
 * Scan mac addr form string
 *
 * @param  in mac string
 * @param  des mac for odp_packet
 * @return 1 success, 0 failed
 */
static int scan_mac(char *in, odph_ethaddr_t *des)
{
	int field;
	int i;
	unsigned int mac[7];

	field = sscanf(in, "%2x:%2x:%2x:%2x:%2x:%2x",
		       &mac[0], &mac[1], &mac[2], &mac[3], &mac[4], &mac[5]);

	for (i = 0; i < 6; i++)
		des->addr[i] = mac[i];

	if (field != 6)
		return 0;
	return 1;
}

/**
 * set up an udp packet
 *
 * @param pool Buffer pool to create packet in
 *
 * @return Handle of created packet
 * @retval ODP_PACKET_INVALID  Packet could not be created
 */
static odp_packet_t pack_udp_pkt(odp_pool_t pool)
{
	odp_packet_t pkt;
	char *buf;
	odph_ethhdr_t *eth;
	odph_ipv4hdr_t *ip;
	odph_udphdr_t *udp;
	unsigned short seq;

	pkt = odp_packet_alloc(pool, args->appl.payload + ODPH_UDPHDR_LEN +
			       ODPH_IPV4HDR_LEN + ODPH_ETHHDR_LEN);

	if (pkt == ODP_PACKET_INVALID)
		return pkt;

	buf = odp_packet_data(pkt);

	/* ether */
	odp_packet_l2_offset_set(pkt, 0);
	eth = (odph_ethhdr_t *)buf;
	memcpy((char *)eth->src.addr, args->appl.srcmac.addr, ODPH_ETHADDR_LEN);
	memcpy((char *)eth->dst.addr, args->appl.dstmac.addr, ODPH_ETHADDR_LEN);
	eth->type = odp_cpu_to_be_16(ODPH_ETHTYPE_IPV4);
	/* ip */
	odp_packet_l3_offset_set(pkt, ODPH_ETHHDR_LEN);
	ip = (odph_ipv4hdr_t *)(buf + ODPH_ETHHDR_LEN);
	ip->dst_addr = odp_cpu_to_be_32(args->appl.dstip);
	ip->src_addr = odp_cpu_to_be_32(args->appl.srcip);
	ip->ver_ihl = ODPH_IPV4 << 4 | ODPH_IPV4HDR_IHL_MIN;
	ip->tot_len = odp_cpu_to_be_16(args->appl.payload + ODPH_UDPHDR_LEN +
				       ODPH_IPV4HDR_LEN);
	ip->proto = ODPH_IPPROTO_UDP;
	seq = odp_atomic_fetch_add_u64(&counters.seq, 1) % 0xFFFF;
	ip->id = odp_cpu_to_be_16(seq);
	ip->chksum = 0;
	odph_ipv4_csum_update(pkt);
	/* udp */
	odp_packet_l4_offset_set(pkt, ODPH_ETHHDR_LEN + ODPH_IPV4HDR_LEN);
	udp = (odph_udphdr_t *)(buf + ODPH_ETHHDR_LEN + ODPH_IPV4HDR_LEN);
	udp->src_port = 0;
	udp->dst_port = 0;
	udp->length = odp_cpu_to_be_16(args->appl.payload + ODPH_UDPHDR_LEN);
	udp->chksum = 0;
	udp->chksum = odph_ipv4_udp_chksum(pkt);

	return pkt;
}

/**
 * Set up an icmp packet
 *
 * @param pool Buffer pool to create packet in
 *
 * @return Handle of created packet
 * @retval ODP_PACKET_INVALID  Packet could not be created
 */
static odp_packet_t pack_icmp_pkt(odp_pool_t pool)
{
	odp_packet_t pkt;
	char *buf;
	odph_ethhdr_t *eth;
	odph_ipv4hdr_t *ip;
	odph_icmphdr_t *icmp;
	struct timeval tval;
	uint8_t *tval_d;
	unsigned short seq;

	args->appl.payload = 56;
	pkt = odp_packet_alloc(pool, args->appl.payload + ODPH_ICMPHDR_LEN +
			       ODPH_IPV4HDR_LEN + ODPH_ETHHDR_LEN);

	if (pkt == ODP_PACKET_INVALID)
		return pkt;

	buf = odp_packet_data(pkt);

	/* ether */
	odp_packet_l2_offset_set(pkt, 0);
	eth = (odph_ethhdr_t *)buf;
	memcpy((char *)eth->src.addr, args->appl.srcmac.addr, ODPH_ETHADDR_LEN);
	memcpy((char *)eth->dst.addr, args->appl.dstmac.addr, ODPH_ETHADDR_LEN);
	eth->type = odp_cpu_to_be_16(ODPH_ETHTYPE_IPV4);
	/* ip */
	odp_packet_l3_offset_set(pkt, ODPH_ETHHDR_LEN);
	ip = (odph_ipv4hdr_t *)(buf + ODPH_ETHHDR_LEN);
	ip->dst_addr = odp_cpu_to_be_32(args->appl.dstip);
	ip->src_addr = odp_cpu_to_be_32(args->appl.srcip);
	ip->ver_ihl = ODPH_IPV4 << 4 | ODPH_IPV4HDR_IHL_MIN;
	ip->tot_len = odp_cpu_to_be_16(args->appl.payload + ODPH_ICMPHDR_LEN +
				       ODPH_IPV4HDR_LEN);
	ip->proto = ODPH_IPPROTO_ICMP;
	seq = odp_atomic_fetch_add_u64(&counters.seq, 1) % 0xffff;
	ip->id = odp_cpu_to_be_16(seq);
	ip->chksum = 0;
	odph_ipv4_csum_update(pkt);
	/* icmp */
	icmp = (odph_icmphdr_t *)(buf + ODPH_ETHHDR_LEN + ODPH_IPV4HDR_LEN);
	icmp->type = ICMP_ECHO;
	icmp->code = 0;
	icmp->un.echo.id = 0;
	icmp->un.echo.sequence = ip->id;
	tval_d = (uint8_t *)(buf + ODPH_ETHHDR_LEN + ODPH_IPV4HDR_LEN +
				  ODPH_ICMPHDR_LEN);
	/* TODO This should be changed to use an
	 * ODP timer API once one exists. */
	gettimeofday(&tval, NULL);
	memcpy(tval_d, &tval, sizeof(struct timeval));
	icmp->chksum = 0;
	icmp->chksum = odp_chksum(icmp, args->appl.payload +
				  ODPH_ICMPHDR_LEN);

	return pkt;
}

/**
 * Create a pktio object
 *
 * @param dev Name of device to open
 * @param pool Pool to associate with device for packet RX/TX
 *
 * @return The handle of the created pktio object.
 * @warning This routine aborts if the create is unsuccessful.
 */
static odp_pktio_t create_pktio(const char *dev, odp_pool_t pool)
{
	odp_queue_param_t qparam;
	char inq_name[ODP_QUEUE_NAME_LEN];
	odp_pktio_t pktio;
	int ret;
	odp_queue_t inq_def;

	/* Open a packet IO instance */
	pktio = odp_pktio_open(dev, pool);

	if (pktio == ODP_PKTIO_INVALID)
		EXAMPLE_ABORT("Error: pktio create failed for %s\n", dev);

	/*
	 * Create and set the default INPUT queue associated with the 'pktio'
	 * resource
	 */
	qparam.sched.prio  = ODP_SCHED_PRIO_DEFAULT;
	qparam.sched.sync  = ODP_SCHED_SYNC_ATOMIC;
	qparam.sched.group = ODP_SCHED_GROUP_DEFAULT;
	snprintf(inq_name, sizeof(inq_name), "%" PRIu64 "-pktio_inq_def",
		 odp_pktio_to_u64(pktio));
	inq_name[ODP_QUEUE_NAME_LEN - 1] = '\0';

	inq_def = odp_queue_create(inq_name, ODP_QUEUE_TYPE_PKTIN, &qparam);
	if (inq_def == ODP_QUEUE_INVALID)
		EXAMPLE_ABORT("Error: pktio inq create failed for %s\n", dev);

	ret = odp_pktio_inq_setdef(pktio, inq_def);
	if (ret != 0)
		EXAMPLE_ABORT("Error: default input-Q setup for %s\n", dev);

	printf("  created pktio:%02" PRIu64
	       ", dev:%s, queue mode (ATOMIC queues)\n"
	       "          default pktio%02" PRIu64
	       "-INPUT queue:%" PRIu64 "\n",
	       odp_pktio_to_u64(pktio), dev,
	       odp_pktio_to_u64(pktio), odp_queue_to_u64(inq_def));

	return pktio;
}

/**
 * Packet IO loopback worker thread using ODP queues
 *
 * @param arg  thread arguments of type 'thread_args_t *'
 */

static void *gen_send_thread(void *arg)
{
	int thr;
	odp_pktio_t pktio;
	thread_args_t *thr_args;
	odp_queue_t outq_def;
	odp_packet_t pkt;

	thr = odp_thread_id();
	thr_args = arg;

	pktio = odp_pktio_lookup(thr_args->pktio_dev);
	if (pktio == ODP_PKTIO_INVALID) {
		EXAMPLE_ERR("  [%02i] Error: lookup of pktio %s failed\n",
			    thr, thr_args->pktio_dev);
		return NULL;
	}

	outq_def = odp_pktio_outq_getdef(pktio);
	if (outq_def == ODP_QUEUE_INVALID) {
		EXAMPLE_ERR("  [%02i] Error: def output-Q query\n", thr);
		return NULL;
	}

	printf("  [%02i] created mode: SEND\n", thr);
	for (;;) {
		int err;

		if (args->appl.mode == APPL_MODE_UDP)
			pkt = pack_udp_pkt(thr_args->pool);
		else if (args->appl.mode == APPL_MODE_PING)
			pkt = pack_icmp_pkt(thr_args->pool);
		else
			pkt = ODP_PACKET_INVALID;

		if (!odp_packet_is_valid(pkt)) {
			EXAMPLE_ERR("  [%2i] alloc_single failed\n", thr);
			return NULL;
		}

		err = odp_queue_enq(outq_def, odp_packet_to_event(pkt));
		if (err != 0) {
			EXAMPLE_ERR("  [%02i] send pkt err!\n", thr);
			odp_packet_free(pkt);
			return NULL;
		}

		if (args->appl.interval != 0) {
			printf("  [%02i] send pkt no:%ju seq %ju\n",
			       thr,
			       odp_atomic_load_u64(&counters.seq),
			       odp_atomic_load_u64(&counters.seq)%0xffff);
			millisleep(args->appl.interval,
				   thr_args->tp,
				   thr_args->tim,
				   thr_args->tq,
				   thr_args->tmo_ev);

		}
		if (args->appl.number != -1 &&
		    odp_atomic_load_u64(&counters.seq)
		    >= (unsigned int)args->appl.number) {
			break;
		}
	}

	/* receive number of reply pks until timeout */
	if (args->appl.mode == APPL_MODE_PING && args->appl.number > 0) {
		while (args->appl.timeout >= 0) {
			if (odp_atomic_load_u64(&counters.icmp) >=
			    (unsigned int)args->appl.number)
				break;
			millisleep(1000,
				   thr_args->tp,
				   thr_args->tim,
				   thr_args->tq,
				   thr_args->tmo_ev);
			args->appl.timeout--;
		}
	}

	/* print info */
	if (args->appl.mode == APPL_MODE_UDP) {
		printf("  [%02i] total send: %ju\n",
		       thr, odp_atomic_load_u64(&counters.seq));
	} else if (args->appl.mode == APPL_MODE_PING) {
		printf("  [%02i] total send: %ju total receive: %ju\n",
		       thr, odp_atomic_load_u64(&counters.seq),
		       odp_atomic_load_u64(&counters.icmp));
	}
	return arg;
}

/**
 * Print odp packets
 *
 * @param  thr worker id
 * @param  pkt_tbl packets to be print
 * @param  len packet number
 */
static void print_pkts(int thr, odp_packet_t pkt_tbl[], unsigned len)
{
	odp_packet_t pkt;
	char *buf;
	odph_ipv4hdr_t *ip;
	odph_udphdr_t *udp;
	odph_icmphdr_t *icmp;
	struct timeval tvrecv;
	struct timeval tvsend;
	double rtt;
	unsigned i;
	size_t offset;
	char msg[1024];
	int rlen;
	for (i = 0; i < len; ++i) {
		pkt = pkt_tbl[i];
		rlen = 0;

		/* only ip pkts */
		if (!odp_packet_has_ipv4(pkt))
			continue;

		odp_atomic_inc_u64(&counters.ip);
		rlen += sprintf(msg, "receive Packet proto:IP ");
		buf = odp_packet_data(pkt);
		ip = (odph_ipv4hdr_t *)(buf + odp_packet_l3_offset(pkt));
		rlen += sprintf(msg + rlen, "id %d ",
				odp_be_to_cpu_16(ip->id));
		offset = odp_packet_l4_offset(pkt);

		/* udp */
		if (ip->proto == ODPH_IPPROTO_UDP) {
			odp_atomic_inc_u64(&counters.udp);
			udp = (odph_udphdr_t *)(buf + offset);
			rlen += sprintf(msg + rlen, "UDP payload %d ",
					odp_be_to_cpu_16(udp->length) -
					ODPH_UDPHDR_LEN);
		}

		/* icmp */
		if (ip->proto == ODPH_IPPROTO_ICMP) {
			icmp = (odph_icmphdr_t *)(buf + offset);
			/* echo reply */
			if (icmp->type == ICMP_ECHOREPLY) {
				odp_atomic_inc_u64(&counters.icmp);
				memcpy(&tvsend, buf + offset + ODPH_ICMPHDR_LEN,
				       sizeof(struct timeval));
				/* TODO This should be changed to use an
				 * ODP timer API once one exists. */
				gettimeofday(&tvrecv, NULL);
				tv_sub(&tvrecv, &tvsend);
				rtt = tvrecv.tv_sec*1000 + tvrecv.tv_usec/1000;
				rlen += sprintf(msg + rlen,
					"ICMP Echo Reply seq %d time %.1f ",
					odp_be_to_cpu_16(icmp->un.echo.sequence)
					, rtt);
			} else if (icmp->type == ICMP_ECHO) {
				rlen += sprintf(msg + rlen,
						"Icmp Echo Request");
			}
		}

		msg[rlen] = '\0';
		printf("  [%02i] %s\n", thr, msg);
	}
}

/**
 * Main receive function
 *
 * @param arg  thread arguments of type 'thread_args_t *'
 */
static void *gen_recv_thread(void *arg)
{
	int thr;
	odp_pktio_t pktio;
	thread_args_t *thr_args;
	odp_packet_t pkt;
	odp_event_t ev;

	thr = odp_thread_id();
	thr_args = arg;

	pktio = odp_pktio_lookup(thr_args->pktio_dev);
	if (pktio == ODP_PKTIO_INVALID) {
		EXAMPLE_ERR("  [%02i] Error: lookup of pktio %s failed\n",
			    thr, thr_args->pktio_dev);
		return NULL;
	}

	printf("  [%02i] created mode: RECEIVE\n", thr);

	for (;;) {
		/* Use schedule to get buf from any input queue */
		ev = odp_schedule(NULL, ODP_SCHED_WAIT);

		pkt = odp_packet_from_event(ev);
		/* Drop packets with errors */
		if (odp_unlikely(odp_packet_has_error(pkt))) {
			odp_packet_free(pkt);
			continue;
		}

		print_pkts(thr, &pkt, 1);

		odp_packet_free(pkt);
	}

	return arg;
}
/**
 * ODP packet example main function
 */
int main(int argc, char *argv[])
{
	odph_linux_pthread_t thread_tbl[MAX_WORKERS];
	odp_pool_t pool;
	int num_workers;
	int i;
	odp_shm_t shm;
	odp_cpumask_t cpumask;
	char cpumaskstr[ODP_CPUMASK_STR_SIZE];
	odp_pool_param_t params;
	odp_timer_pool_param_t tparams;
	odp_timer_pool_t tp;
	odp_pool_t tmop;

	/* Init ODP before calling anything else */
	if (odp_init_global(NULL, NULL)) {
		EXAMPLE_ERR("Error: ODP global init failed.\n");
		exit(EXIT_FAILURE);
	}

	if (odp_init_local(ODP_THREAD_CONTROL)) {
		EXAMPLE_ERR("Error: ODP local init failed.\n");
		exit(EXIT_FAILURE);
	}

	/* init counters */
	odp_atomic_init_u64(&counters.seq, 0);
	odp_atomic_init_u64(&counters.ip, 0);
	odp_atomic_init_u64(&counters.udp, 0);
	odp_atomic_init_u64(&counters.icmp, 0);

	/* Reserve memory for args from shared mem */
	shm = odp_shm_reserve("shm_args", sizeof(args_t),
			      ODP_CACHE_LINE_SIZE, 0);
	args = odp_shm_addr(shm);

	if (args == NULL) {
		EXAMPLE_ERR("Error: shared mem alloc failed.\n");
		exit(EXIT_FAILURE);
	}
	memset(args, 0, sizeof(*args));

	/* Parse and store the application arguments */
	parse_args(argc, argv, &args->appl);

	/* Print both system and application information */
	print_info(NO_PATH(argv[0]), &args->appl);

	/* Default to system CPU count unless user specified */
	num_workers = MAX_WORKERS;
	if (args->appl.cpu_count)
		num_workers = args->appl.cpu_count;

	/* ping mode need two worker */
	if (args->appl.mode == APPL_MODE_PING)
		num_workers = 2;

	/* Get default worker cpumask */
	num_workers = odp_cpumask_def_worker(&cpumask, num_workers);
	(void)odp_cpumask_to_str(&cpumask, cpumaskstr, sizeof(cpumaskstr));

	printf("num worker threads: %i\n", num_workers);
	printf("first CPU:          %i\n", odp_cpumask_first(&cpumask));
	printf("cpu mask:           %s\n", cpumaskstr);

	/* Create packet pool */
	memset(&params, 0, sizeof(params));
	params.pkt.seg_len = SHM_PKT_POOL_BUF_SIZE;
	params.pkt.len     = SHM_PKT_POOL_BUF_SIZE;
	params.pkt.num     = SHM_PKT_POOL_SIZE/SHM_PKT_POOL_BUF_SIZE;
	params.type        = ODP_POOL_PACKET;

	pool = odp_pool_create("packet_pool", &params);

	if (pool == ODP_POOL_INVALID) {
		EXAMPLE_ERR("Error: packet pool create failed.\n");
		exit(EXIT_FAILURE);
	}
	odp_pool_print(pool);

	/* Create timer pool */
	tparams.res_ns = 1 * ODP_TIME_MSEC;
	tparams.min_tmo = 0;
	tparams.max_tmo = 10000 * ODP_TIME_SEC;
	tparams.num_timers = num_workers; /* One timer per worker */
	tparams.priv = 0; /* Shared */
	tparams.clk_src = ODP_CLOCK_CPU;
	tp = odp_timer_pool_create("timer_pool", &tparams);
	if (tp == ODP_TIMER_POOL_INVALID) {
		EXAMPLE_ERR("Timer pool create failed.\n");
		exit(EXIT_FAILURE);
	}
	odp_timer_pool_start();

	/* Create timeout pool */
	memset(&params, 0, sizeof(params));
	params.tmo.num     = tparams.num_timers; /* One timeout per timer */
	params.type	   = ODP_POOL_TIMEOUT;

	tmop = odp_pool_create("timeout_pool", &params);

	if (pool == ODP_POOL_INVALID) {
		EXAMPLE_ERR("Error: packet pool create failed.\n");
		exit(EXIT_FAILURE);
	}
	for (i = 0; i < args->appl.if_count; ++i)
		create_pktio(args->appl.if_names[i], pool);

	/* Create and init worker threads */
	memset(thread_tbl, 0, sizeof(thread_tbl));

	if (args->appl.mode == APPL_MODE_PING) {
		odp_cpumask_t cpu0_mask;
		odp_queue_t tq;

		/* Previous code forced both threads to CPU 0 */
		odp_cpumask_zero(&cpu0_mask);
		odp_cpumask_set(&cpu0_mask, 0);

		tq = odp_queue_create("", ODP_QUEUE_TYPE_POLL, NULL);
		if (tq == ODP_QUEUE_INVALID)
			abort();
		args->thread[1].pktio_dev = args->appl.if_names[0];
		args->thread[1].pool = pool;
		args->thread[1].tp = tp;
		args->thread[1].tq = tq;
		args->thread[1].tim = odp_timer_alloc(tp, tq, NULL);
		if (args->thread[1].tim == ODP_TIMER_INVALID)
			abort();
		args->thread[1].tmo_ev = odp_timeout_alloc(tmop);
		if (args->thread[1].tmo_ev == ODP_TIMEOUT_INVALID)
			abort();
		args->thread[1].mode = args->appl.mode;
		odph_linux_pthread_create(&thread_tbl[1], &cpu0_mask,
					  gen_recv_thread, &args->thread[1]);

		tq = odp_queue_create("", ODP_QUEUE_TYPE_POLL, NULL);
		if (tq == ODP_QUEUE_INVALID)
			abort();
		args->thread[0].pktio_dev = args->appl.if_names[0];
		args->thread[0].pool = pool;
		args->thread[0].tp = tp;
		args->thread[0].tq = tq;
		args->thread[0].tim = odp_timer_alloc(tp, tq, NULL);
		if (args->thread[0].tim == ODP_TIMER_INVALID)
			abort();
		args->thread[0].tmo_ev = odp_timeout_alloc(tmop);
		if (args->thread[0].tmo_ev == ODP_TIMEOUT_INVALID)
			abort();
		args->thread[0].mode = args->appl.mode;
		odph_linux_pthread_create(&thread_tbl[0], &cpu0_mask,
					  gen_send_thread, &args->thread[0]);

		/* only wait send thread to join */
		num_workers = 1;
	} else {
		int cpu = odp_cpumask_first(&cpumask);
		for (i = 0; i < num_workers; ++i) {
			odp_cpumask_t thd_mask;
			void *(*thr_run_func) (void *);
			int if_idx;
			odp_queue_t tq;

			if_idx = i % args->appl.if_count;

			args->thread[i].pktio_dev = args->appl.if_names[if_idx];
			tq = odp_queue_create("", ODP_QUEUE_TYPE_POLL, NULL);
			if (tq == ODP_QUEUE_INVALID)
				abort();
			args->thread[i].pool = pool;
			args->thread[i].tp = tp;
			args->thread[i].tq = tq;
			args->thread[i].tim = odp_timer_alloc(tp, tq, NULL);
			if (args->thread[i].tim == ODP_TIMER_INVALID)
				abort();
			args->thread[i].tmo_ev = odp_timeout_alloc(tmop);
			if (args->thread[i].tmo_ev == ODP_TIMEOUT_INVALID)
				abort();
			args->thread[i].mode = args->appl.mode;

			if (args->appl.mode == APPL_MODE_UDP) {
				thr_run_func = gen_send_thread;
			} else if (args->appl.mode == APPL_MODE_RCV) {
				thr_run_func = gen_recv_thread;
			} else {
				EXAMPLE_ERR("ERR MODE\n");
				exit(EXIT_FAILURE);
			}
			/*
			 * Create threads one-by-one instead of all-at-once,
			 * because each thread might get different arguments.
			 * Calls odp_thread_create(cpu) for each thread
			 */
			odp_cpumask_zero(&thd_mask);
			odp_cpumask_set(&thd_mask, cpu);
			odph_linux_pthread_create(&thread_tbl[i],
						  &thd_mask,
						  thr_run_func,
						  &args->thread[i]);
			cpu = odp_cpumask_next(&cpumask, cpu);

		}
	}

	/* Master thread waits for other threads to exit */
	odph_linux_pthread_join(thread_tbl, num_workers);

	free(args->appl.if_names);
	free(args->appl.if_str);
	printf("Exit\n\n");

	return 0;
}


/**
 * Parse and store the command line arguments
 *
 * @param argc       argument count
 * @param argv[]     argument vector
 * @param appl_args  Store application arguments here
 */
static void parse_args(int argc, char *argv[], appl_args_t *appl_args)
{
	int opt;
	int long_index;
	char *token;
	size_t len;
	int i;
	static struct option longopts[] = {
		{"interface", required_argument, NULL, 'I'},
		{"workers", required_argument, NULL, 'w'},
		{"srcmac", required_argument, NULL, 'a'},
		{"dstmac", required_argument, NULL, 'b'},
		{"srcip", required_argument, NULL, 'c'},
		{"dstip", required_argument, NULL, 'd'},
		{"packetsize", required_argument, NULL, 's'},
		{"mode", required_argument, NULL, 'm'},
		{"count", required_argument, NULL, 'n'},
		{"timeout", required_argument, NULL, 't'},
		{"interval", required_argument, NULL, 'i'},
		{"help", no_argument, NULL, 'h'},
		{NULL, 0, NULL, 0}
	};

	appl_args->mode = -1; /* Invalid, must be changed by parsing */
	appl_args->number = -1;
	appl_args->payload = 56;
	appl_args->timeout = -1;

	while (1) {
		opt = getopt_long(argc, argv, "+I:a:b:c:d:s:i:m:n:t:w:h",
					longopts, &long_index);
		if (opt == -1)
			break;	/* No more options */

		switch (opt) {
		case 'w':
			appl_args->cpu_count = atoi(optarg);
			break;
		/* parse packet-io interface names */
		case 'I':
			len = strlen(optarg);
			if (len == 0) {
				usage(argv[0]);
				exit(EXIT_FAILURE);
			}
			len += 1;	/* add room for '\0' */

			appl_args->if_str = malloc(len);
			if (appl_args->if_str == NULL) {
				usage(argv[0]);
				exit(EXIT_FAILURE);
			}

			/* count the number of tokens separated by ',' */
			strcpy(appl_args->if_str, optarg);
			for (token = strtok(appl_args->if_str, ","), i = 0;
			     token != NULL;
			     token = strtok(NULL, ","), i++)
				;

			appl_args->if_count = i;

			if (appl_args->if_count == 0) {
				usage(argv[0]);
				exit(EXIT_FAILURE);
			}

			/* allocate storage for the if names */
			appl_args->if_names =
			    calloc(appl_args->if_count, sizeof(char *));

			/* store the if names (reset names string) */
			strcpy(appl_args->if_str, optarg);
			for (token = strtok(appl_args->if_str, ","), i = 0;
			     token != NULL; token = strtok(NULL, ","), i++) {
				appl_args->if_names[i] = token;
			}
			break;

		case 'm':
			if (optarg[0] == 'u') {
				appl_args->mode = APPL_MODE_UDP;
			} else if (optarg[0] == 'p') {
				appl_args->mode = APPL_MODE_PING;
			} else if (optarg[0] == 'r') {
				appl_args->mode = APPL_MODE_RCV;
			} else {
				EXAMPLE_ERR("wrong mode!\n");
				exit(EXIT_FAILURE);
			}
			break;

		case 'a':
			if (scan_mac(optarg, &appl_args->srcmac) != 1) {
				EXAMPLE_ERR("wrong src mac:%s\n", optarg);
				exit(EXIT_FAILURE);
			}
			break;

		case 'b':
			if (scan_mac(optarg, &appl_args->dstmac) != 1) {
				EXAMPLE_ERR("wrong dst mac:%s\n", optarg);
				exit(EXIT_FAILURE);
			}
			break;

		case 'c':
			if (scan_ip(optarg, &appl_args->srcip) != 1) {
				EXAMPLE_ERR("wrong src ip:%s\n", optarg);
				exit(EXIT_FAILURE);
			}
			break;

		case 'd':
			if (scan_ip(optarg, &appl_args->dstip) != 1) {
				EXAMPLE_ERR("wrong dst ip:%s\n", optarg);
				exit(EXIT_FAILURE);
			}
			break;

		case 's':
			appl_args->payload = atoi(optarg);
			break;

		case 'n':
			appl_args->number = atoi(optarg);
			break;

		case 't':
			appl_args->timeout = atoi(optarg);
			break;

		case 'i':
			appl_args->interval = atoi(optarg);
			if (appl_args->interval <= 200) {
				EXAMPLE_ERR("should be root user\n");
				exit(EXIT_FAILURE);
			}
			break;

		case 'h':
			usage(argv[0]);
			exit(EXIT_SUCCESS);
			break;

		default:
			break;
		}
	}

	if (appl_args->if_count == 0 || appl_args->mode == -1) {
		usage(argv[0]);
		exit(EXIT_FAILURE);
	}

	optind = 1;		/* reset 'extern optind' from the getopt lib */
}

/**
 * Print system and application info
 */
static void print_info(char *progname, appl_args_t *appl_args)
{
	int i;

	printf("\n"
	       "ODP system info\n"
	       "---------------\n"
	       "ODP API version: %s\n"
	       "CPU model:       %s\n"
	       "CPU freq (hz):   %"PRIu64"\n"
	       "Cache line size: %i\n"
	       "CPU count:       %i\n"
	       "\n",
	       odp_version_api_str(), odp_sys_cpu_model_str(), odp_sys_cpu_hz(),
	       odp_sys_cache_line_size(), odp_cpu_count());

	printf("Running ODP appl: \"%s\"\n"
	       "-----------------\n"
	       "IF-count:        %i\n"
	       "Using IFs:      ",
	       progname, appl_args->if_count);
	for (i = 0; i < appl_args->if_count; ++i)
		printf(" %s", appl_args->if_names[i]);
	printf("\n"
	       "Mode:            ");
	if (appl_args->mode == 0)
		PRINT_APPL_MODE(APPL_MODE_UDP);
	else if (appl_args->mode == 1)
		PRINT_APPL_MODE(APPL_MODE_PING);
	else
		PRINT_APPL_MODE(APPL_MODE_RCV);
	printf("\n\n");
	fflush(NULL);
}

/**
 * Prinf usage information
 */
static void usage(char *progname)
{
	printf("\n"
	       "Usage: %s OPTIONS\n"
	       "  E.g. %s -I eth1 -r\n"
	       "\n"
	       "OpenDataPlane example application.\n"
	       "\n"
	       "  Work mode:\n"
	       "    1.send udp packets\n"
	       "      odp_generator -I eth0 --srcmac fe:0f:97:c9:e0:44  --dstmac 32:cb:9b:27:2f:1a --srcip 192.168.0.1 --dstip 192.168.0.2 -m u\n"
	       "    2.receive udp packets\n"
	       "      odp_generator -I eth0 -m r\n"
	       "    3.work likes ping\n"
	       "      odp_generator -I eth0 --srcmac fe:0f:97:c9:e0:44  --dstmac 32:cb:9b:27:2f:1a --srcip 192.168.0.1 --dstip 192.168.0.2 -m p\n"
	       "\n"
	       "Mandatory OPTIONS:\n"
	       "  -I, --interface Eth interfaces (comma-separated, no spaces)\n"
	       "  -a, --srcmac src mac address\n"
	       "  -b, --dstmac dst mac address\n"
	       "  -c, --srcip src ip address\n"
	       "  -d, --dstip dst ip address\n"
	       "  -s, --packetsize payload length of the packets\n"
	       "  -m, --mode work mode: send udp(u), receive(r), send icmp(p)\n"
	       "  -n, --count the number of packets to be send\n"
	       "  -t, --timeout only for ping mode, wait ICMP reply timeout seconds\n"
	       "  -i, --interval wait interval ms between sending each packet\n"
	       "                 default is 1000ms. 0 for flood mode\n"
	       "\n"
	       "Optional OPTIONS\n"
	       "  -h, --help       Display help and exit.\n"
	       " environment variables: ODP_PKTIO_DISABLE_SOCKET_MMAP\n"
	       "                        ODP_PKTIO_DISABLE_SOCKET_MMSG\n"
	       "                        ODP_PKTIO_DISABLE_SOCKET_BASIC\n"
	       " can be used to advanced pkt I/O selection for linux-generic\n"
	       "\n", NO_PATH(progname), NO_PATH(progname)
	      );
}
/**
 * calc time period
 *
 *@param recvtime start time
 *@param sendtime end time
*/
static void tv_sub(struct timeval *recvtime, struct timeval *sendtime)
{
	long sec = recvtime->tv_sec - sendtime->tv_sec;
	long usec = recvtime->tv_usec - sendtime->tv_usec;
	if (usec >= 0) {
		recvtime->tv_sec = sec;
		recvtime->tv_usec = usec;
	} else {
		recvtime->tv_sec = sec - 1;
		recvtime->tv_usec = -usec;
	}
}
