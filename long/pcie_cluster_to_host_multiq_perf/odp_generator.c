/* Copyright (c) 2014, Linaro Limited
 * All rights reserved.
 *
 * SPDX-License-Identifier:     BSD-3-Clause
 */

/** enable strtok */
#define _POSIX_C_SOURCE 200112L
#define PKT_BURST_SZ 4

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
#include <HAL/hal/cluster/dsu.h>
#include <HAL/hal/core/mp.h>

#define MAX_WORKERS            32		/**< max number of works */
#define SHM_PKT_POOL_SIZE      (64*2048)	/**< pkt pool size */
#define SHM_PKT_POOL_BUF_SIZE  1856		/**< pkt pool buf size */
#define DEFAULT_PKT_INTERVAL   1000             /**< interval btw each pkt */

static int my_nanosleep(struct timespec *ts){
	uint64_t freq = 600000000ULL;

	uint64_t tdiff = (ts->tv_sec * freq) + ((ts->tv_nsec * freq) / 1000000000ULL);
	uint64_t now = __k1_read_dsu_timestamp();
	while(now + tdiff > __k1_read_dsu_timestamp())
		__k1_cpu_backoff(1000);
	return 0;
}

static unsigned int my_sleep(unsigned int seconds){
   struct timespec ts;

    ts.tv_sec = seconds;
    ts.tv_nsec = 0;
    return my_nanosleep(&ts);
};

/** Get rid of path in filename - only for unix-type paths using '/' */
#define NO_PATH(file_name) (strrchr((file_name), '/') ? \
			    strrchr((file_name), '/') + 1 : (file_name))
/**
 * Parsed command line application arguments
 */
typedef struct {
	int cpu_count;		/**< system CPU count */
	const char *mask;	/**< CPU mask */
	int if_count;		/**< Number of interfaces to be used */
	char **if_names;	/**< Array of pointers to interface names */
	char *if_str;		/**< Storage for interface names */
	odp_pool_t pool;	/**< Pool for packet IO */
	odph_ethaddr_t srcmac;	/**< src mac addr */
	odph_ethaddr_t dstmac;	/**< dest mac addr */
	unsigned int srcip;	/**< src ip addr */
	unsigned int dstip;	/**< dest ip addr */
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
	odp_atomic_u64_t tx_drops; /**< packets dropped in transmit */
} counters;

/** * Thread specific arguments
 */
typedef struct {
	char *pktio_dev;	/**< Interface name to use */
	odp_pool_t pool;	/**< Pool for packet IO */
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

/** Barrier to sync threads execution */
static odp_barrier_t barrier;

/* helper funcs */
static void parse_args(int argc, char *argv[], appl_args_t *appl_args);
static void print_info(char *progname, appl_args_t *appl_args);
static void usage(char *progname);
static void print_global_stats(int num_workers);


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
 * set up a packet
 *
 * @param pool Buffer pool to create packet in
 *
 * @return Handle of created packet
 * @retval ODP_PACKET_INVALID  Packet could not be created
 */
static odp_packet_t pack_pkt(odp_pool_t pool)
{
	odp_packet_t pkt;
	char *buf;
	odph_ethhdr_t *eth;

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
	odp_pktio_t pktio;
	int ret;
	odp_pktio_param_t pktio_param;
	odp_pktin_queue_param_t pktin_param;

	odp_pktio_param_init(&pktio_param);
	pktio_param.in_mode = ODP_PKTIN_MODE_DIRECT;

	/* Open a packet IO instance */
	pktio = odp_pktio_open(dev, pool, &pktio_param);

	if (pktio == ODP_PKTIO_INVALID) {
		EXAMPLE_ERR("Error: pktio create failed for %s\n", dev);
		exit(EXIT_FAILURE);
	}

	odp_pktin_queue_param_init(&pktin_param);
	pktin_param.queue_param.sched.sync = ODP_SCHED_SYNC_ATOMIC;

	if (odp_pktin_queue_config(pktio, &pktin_param)) {
		EXAMPLE_ERR("Error: pktin queue config failed for %s\n", dev);
		exit(EXIT_FAILURE);
	}

	if (odp_pktout_queue_config(pktio, NULL)) {
		EXAMPLE_ERR("Error: pktout queue config failed for %s\n", dev);
		exit(EXIT_FAILURE);
	}

	ret = odp_pktio_start(pktio);
	if (ret)
		EXAMPLE_ABORT("Error: unable to start %s\n", dev);

	printf("  created pktio:%02" PRIu64
	       ", dev:%s, queue mode (ATOMIC queues)\n"
	       "          default pktio%02" PRIu64 "\n",
	       odp_pktio_to_u64(pktio), dev,
	       odp_pktio_to_u64(pktio));

	return pktio;
}

/**
 * Packet IO loopback worker thread using ODP queues
 *
 * @param arg  thread arguments of type 'thread_args_t *'
 */

static int gen_send_thread(void *arg)
{
	int thr;
	odp_pktio_t pktio;
	thread_args_t *thr_args;
	odp_pktout_queue_t pktout;
	odp_packet_t pkt[PKT_BURST_SZ];

	thr = odp_thread_id();
	thr_args = arg;

	pktio = odp_pktio_lookup(thr_args->pktio_dev);
	if (pktio == ODP_PKTIO_INVALID) {
		EXAMPLE_ERR("  [%02i] Error: lookup of pktio %s failed\n",
			    thr, thr_args->pktio_dev);
		return -1;
	}

	if (odp_pktout_queue(pktio, &pktout, 1) != 1) {
		EXAMPLE_ERR("  [%02i] Error: no output queue\n", thr);
		return -1;
	}

	printf("  [%02i] created mode: SEND\n", thr);
	for (int i = 0; i < PKT_BURST_SZ; ++i) {
		pkt[i] = pack_pkt(thr_args->pool);

		if (!odp_packet_is_valid(pkt[i])) {
			EXAMPLE_ERR("  [%2i] alloc_single failed\n", thr);
			return -1;
		}
	}

	odp_barrier_wait(&barrier);

	for (;;) {
		int err;

		err = odp_pktout_send(pktout, pkt, PKT_BURST_SZ);
		if (err > 0)
			odp_atomic_fetch_add_u64(&counters.seq, err);
		if (err != PKT_BURST_SZ) {
			odp_atomic_add_u64(&counters.tx_drops, PKT_BURST_SZ - err);
		}

		static uint64_t toto = 0;
		if (args->appl.interval != 0) {
			printf("  [%02i] send pkt no:%"PRIu64" seq %"PRIu64"\n",
			       thr,
			       odp_atomic_load_u64(&counters.seq),
				   toto++);
			usleep(args->appl.interval);

		}
	}
	printf("Done\n");
	_exit(0);

	return 0;
}

/**
 * printing verbose statistics
 *
 */
static void print_global_stats(int num_workers)
{
	odp_time_t cur, wait, next;
	uint64_t pkts, pkts_prev = 0, pps, maximum_pps = 0;
	int verbose_interval = 20;
	odp_thrmask_t thrd_mask;

	odp_barrier_wait(&barrier);

	wait = odp_time_local_from_ns(verbose_interval * ODP_TIME_SEC_IN_NS);
	next = odp_time_sum(odp_time_local(), wait);

	while (odp_thrmask_worker(&thrd_mask) == num_workers) {
		if (args->appl.number != -1 &&
		    odp_atomic_load_u64(&counters.seq) >=
		    (unsigned int)args->appl.number) {
			break;
		}

		cur = odp_time_local();
		if (odp_time_cmp(next, cur) > 0)
			continue;

		next = odp_time_sum(cur, wait);

		pkts = odp_atomic_load_u64(&counters.seq);
		printf(" total sent: %" PRIu64 ", drops: %" PRIu64 "\n", pkts,
		       odp_atomic_load_u64(&counters.tx_drops));

		pps = (pkts - pkts_prev) / verbose_interval;
		if (pps > maximum_pps)
			maximum_pps = pps;
		printf(" %" PRIu64 " pps, %" PRIu64 " max pps\n",
		       pps, maximum_pps);
		pkts_prev = pkts;
	}
}

/**
 * ODP packet example main function
 */
int main(int argc, char * argv[])
{
	odph_odpthread_t thread_tbl[MAX_WORKERS];
	odp_pool_t pool;
	int num_workers;
	int i;
	odp_shm_t shm;
	odp_cpumask_t cpumask;
	char cpumaskstr[ODP_CPUMASK_STR_SIZE];
	odp_pool_param_t params;
	odp_instance_t instance;
	odph_odpthread_params_t thr_params;

	/* Init ODP before calling anything else */
	if (odp_init_global(&instance, NULL, NULL)) {
		EXAMPLE_ERR("Error: ODP global init failed.\n");
		exit(EXIT_FAILURE);
	}

	if (odp_init_local(instance, ODP_THREAD_CONTROL)) {
		EXAMPLE_ERR("Error: ODP local init failed.\n");
		exit(EXIT_FAILURE);
	}
	my_sleep(1 + __k1_get_cluster_id() / 4);

	/* init counters */
	odp_atomic_init_u64(&counters.seq, 0);
	odp_atomic_init_u64(&counters.tx_drops, 0);

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

	num_workers = odp_cpumask_default_worker(&cpumask, num_workers);
	if (args->appl.mask) {
		odp_cpumask_from_str(&cpumask, args->appl.mask);
		num_workers = odp_cpumask_count(&cpumask);
	}

	(void)odp_cpumask_to_str(&cpumask, cpumaskstr, sizeof(cpumaskstr));

	printf("num worker threads: %i\n", num_workers);
	printf("first CPU:          %i\n", odp_cpumask_first(&cpumask));
	printf("cpu mask:           %s\n", cpumaskstr);

	/* Create packet pool */
	odp_pool_param_init(&params);
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

	for (i = 0; i < args->appl.if_count; ++i)
		create_pktio(args->appl.if_names[i], pool);

	/* Create and init worker threads */
	memset(thread_tbl, 0, sizeof(thread_tbl));

	/* Init threads params */
	memset(&thr_params, 0, sizeof(thr_params));
	thr_params.thr_type = ODP_THREAD_WORKER;
	thr_params.instance = instance;

	/* num workers + print thread */
	odp_barrier_init(&barrier, num_workers + 1);


	{
		int cpu = odp_cpumask_first(&cpumask);
		for (i = 0; i < num_workers; ++i) {
			odp_cpumask_t thd_mask;
			int (*thr_run_func)(void *);
			int if_idx;

			if_idx = i % args->appl.if_count;

			args->thread[i].pktio_dev = args->appl.if_names[if_idx];
			args->thread[i].pool = pool;
			thr_run_func = gen_send_thread;

			/*
			 * Create threads one-by-one instead of all-at-once,
			 * because each thread might get different arguments.
			 * Calls odp_thread_create(cpu) for each thread
			 */
			odp_cpumask_zero(&thd_mask);
			odp_cpumask_set(&thd_mask, cpu);

			thr_params.start = thr_run_func;
			thr_params.arg   = &args->thread[i];

			odph_odpthreads_create(&thread_tbl[i],
					       &thd_mask, &thr_params);
			cpu = odp_cpumask_next(&cpumask, cpu);

		}
	}

	print_global_stats(num_workers);

	/* Master thread waits for other threads to exit */
	for (i = 0; i < num_workers; ++i)
		odph_odpthreads_join(&thread_tbl[i]);

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
	odp_cpumask_t cpumask, cpumask_args, cpumask_and;
	int i, num_workers;
	static struct option longopts[] = {
		{"interface", required_argument, NULL, 'I'},
		{"workers", required_argument, NULL, 'w'},
		{"cpumask", required_argument, NULL, 'c'},
		{"srcmac", required_argument, NULL, 'a'},
		{"dstmac", required_argument, NULL, 'b'},
		{"srcip", required_argument, NULL, 's'},
		{"dstip", required_argument, NULL, 'd'},
		{"packetsize", required_argument, NULL, 'p'},
		{"count", required_argument, NULL, 'n'},
		{"timeout", required_argument, NULL, 't'},
		{"interval", required_argument, NULL, 'i'},
		{"help", no_argument, NULL, 'h'},
		{NULL, 0, NULL, 0}
	};

	appl_args->number = -1;
	appl_args->payload = 56;
	appl_args->timeout = -1;
	int remove_header_size = 0;

	while (1) {
		opt = getopt_long(argc, argv, "+I:a:b:s:d:p:P:i:m:n:t:w:c:h",
				  longopts, &long_index);
		if (opt == -1)
			break;	/* No more options */

		switch (opt) {
		case 'w':
			appl_args->cpu_count = atoi(optarg);
			break;
		case 'c':
			appl_args->mask = optarg;
			odp_cpumask_from_str(&cpumask_args, args->appl.mask);
			num_workers = odp_cpumask_default_worker(&cpumask, 0);
			odp_cpumask_and(&cpumask_and, &cpumask_args, &cpumask);
			if (odp_cpumask_count(&cpumask_and) <
			    odp_cpumask_count(&cpumask_args)) {
				EXAMPLE_ERR("Wrong cpu mask, max cpu's:%d\n",
					    num_workers);
				exit(EXIT_FAILURE);
			}
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

		case 's':
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

		case 'p':
			appl_args->payload = atoi(optarg);
			break;

		case 'P':
			appl_args->payload = atoi(optarg);
			remove_header_size = 1;
			break;

		case 'n':
			appl_args->number = atoi(optarg);
			break;

		case 't':
			appl_args->timeout = atoi(optarg);
			break;

		case 'i':
			appl_args->interval = atoi(optarg);
			break;

		case 'h':
			usage(argv[0]);
			exit(EXIT_SUCCESS);
			break;

		default:
			break;
		}
	}

	if ( remove_header_size ) {
		int header_size = 0;
		header_size = ODPH_UDPHDR_LEN + ODPH_IPV4HDR_LEN + ODPH_ETHHDR_LEN;
		appl_args->payload -= header_size;
	}

	printf("PAYLOAD = %i\n", appl_args->payload);
	if (appl_args->if_count == 0) {
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
	       odp_version_api_str(), odp_cpu_model_str(), odp_cpu_hz_max(),
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
	       "      odp_generator -I eth0 --srcmac fe:0f:97:c9:e0:44  --dstmac 32:cb:9b:27:2f:1a --srcip 192.168.0.1 --dstip 192.168.0.2 --cpumask 0xc -m p\n"
	       "\n"
	       "Mandatory OPTIONS:\n"
	       "  -I, --interface Eth interfaces (comma-separated, no spaces)\n"
	       "  -a, --srcmac src mac address\n"
	       "  -b, --dstmac dst mac address\n"
	       "  -s, --srcip src ip address\n"
	       "  -d, --dstip dst ip address\n"
	       "\n"
	       "Optional OPTIONS\n"
	       "  -h, --help       Display help and exit.\n"
	       " environment variables: ODP_PKTIO_DISABLE_NETMAP\n"
	       "                        ODP_PKTIO_DISABLE_SOCKET_MMAP\n"
	       "                        ODP_PKTIO_DISABLE_SOCKET_MMSG\n"
	       " can be used to advanced pkt I/O selection for linux-generic\n"
	       "  -p, --payloadsize payload length of the packets\n"
	       "  -P, --packetsize payload + header length of the packets\n"
	       "  -t, --timeout only for ping mode, wait ICMP reply timeout seconds\n"
	       "  -i, --interval wait interval ms between sending each packet\n"
	       "                 default is 1000ms. 0 for flood mode\n"
	       "  -w, --workers specify number of workers need to be assigned to application\n"
	       "	         default is to assign all\n"
	       "  -n, --count the number of packets to be send\n"
	       "  -c, --cpumask to set on cores\n"
	       "\n", NO_PATH(progname), NO_PATH(progname)
	      );
}