/* Copyright (c) 2014, Linaro Limited
 * All rights reserved.
 *
 * SPDX-License-Identifier:     BSD-3-Clause
 */

/**
 * @file
 *
 * @example odp_l2fwd.c  ODP basic forwarding application
 */

/** enable strtok */
#define _POSIX_C_SOURCE 200112L

#include <stdlib.h>
#include <getopt.h>
#include <unistd.h>
#include <errno.h>
#include <stdio.h>
#include <assert.h>

#include <odp.h>
#include <odp/helper/linux.h>
#include <odp/helper/eth.h>
#include <odp/helper/ip.h>

/** @def MAX_WORKERS
 * @brief Maximum number of worker threads
 */
#define MAX_WORKERS            32


/** @def SHM_PKT_POOL_BUF_SIZE
 * @brief Buffer size of the packet pool buffer
 */
#define SHM_PKT_POOL_BUF_SIZE  1856

/** @def SHM_PKT_POOL_SIZE
 * @brief Size of the shared memory block
 */
#define SHM_PKT_POOL_SIZE      SHM_PKT_POOL_BUF_SIZE * 41
/** @def MAX_PKT_BURST
 * @brief Maximum number of packet in a burst
 */
#define MAX_PKT_BURST          32

/** Maximum number of pktio queues per interface */
#define MAX_QUEUES             32

/** Maximum number of pktio interfaces */
#define MAX_PKTIOS             8

/**
 * Packet input mode
 */
typedef enum pkt_in_mode_t {
	DIRECT_RECV,
} pkt_in_mode_t;

/** Get rid of path in filename - only for unix-type paths using '/' */
#define NO_PATH(file_name) (strrchr((file_name), '/') ? \
			    strrchr((file_name), '/') + 1 : (file_name))
/**
 * Parsed command line application arguments
 */
typedef struct {
	int cpu_count;
	int if_count;		/**< Number of interfaces to be used */
	char **if_names;	/**< Array of pointers to interface names */
	pkt_in_mode_t mode;	/**< Packet input mode */
	int time;		/**< Time in seconds to run. */
	int accuracy;		/**< Number of seconds to get and print statistics */
	char *if_str;		/**< Storage for interface names */
	int dst_change;		/**< Change destination eth addresses */
	int src_change;		/**< Change source eth addresses */
	int error_check;        /**< Check packet errors */
	int pktio_stats;        /**< Show pktio stats before exit */
} appl_args_t;

static int exit_threads;	/**< Break workers loop if set to 1 */

/**
 * Statistics
 */
typedef union {
	struct {
		/** Number of forwarded packets */
		uint64_t packets;
		/** Packets dropped due to receive error */
		uint64_t rx_drops;
		/** Packets dropped due to transmit error */
		uint64_t tx_drops;
	} s;

	uint8_t padding[ODP_CACHE_LINE_SIZE];
} stats_t ODP_ALIGNED_CACHE;

/**
 * Thread specific arguments
 */
typedef struct {
	int src_idx;    /**< Source interface identifier */
	stats_t *stats;	/**< Pointer to per thread stats */
} thread_args_t;

/**
 * Grouping of all global data
 */
typedef struct {
	/** Per thread packet stats */
	stats_t stats[MAX_WORKERS];
	/** Application (parsed) arguments */
	appl_args_t appl;
	/** Thread specific arguments */
	thread_args_t thread[MAX_WORKERS];
	/** Table of pktio handles */
	odp_pktio_t pktios[MAX_PKTIOS];
	/** Table of port ethernet addresses */
	odph_ethaddr_t port_eth_addr[MAX_PKTIOS];
	/** Table of dst ethernet addresses */
	odph_ethaddr_t dst_eth_addr[MAX_PKTIOS];
	/** Table of dst ports */
	int dst_port[MAX_PKTIOS];
} args_t;

/** Global pointer to args */
static args_t *gbl_args;
/** Global barrier to synchronize main and workers */
static odp_barrier_t barrier;

/* helper funcs */
static inline int find_dest_port(int port);
static inline int drop_err_pkts(odp_packet_t pkt_tbl[], unsigned num);
static void fill_eth_addrs(odp_packet_t pkt_tbl[], unsigned num,
			   int dst_port);
static void parse_args(int argc, char *argv[], appl_args_t *appl_args);
static void print_info(char *progname, appl_args_t *appl_args);
static void usage(char *progname);

/**
 * Find the destination port for a given input port
 *
 * @param port  Input port index
 */
static inline int find_dest_port(int port)
{
	/* Even number of ports */
	if (gbl_args->appl.if_count % 2 == 0)
		return (port % 2 == 0) ? port + 1 : port - 1;

	/* Odd number of ports */
	if (port == gbl_args->appl.if_count - 1)
		return 0;
	else
		return port + 1;
}

/**
 * Packet IO worker thread accessing IO resources directly
 *
 * @param arg  thread arguments of type 'thread_args_t *'
 */
static int pktio_direct_recv_thread(void *arg)
{
	int thr;
	int pkts;
	odp_packet_t pkt_tbl[MAX_PKT_BURST];
	int src_idx, dst_idx;
	odp_pktio_t pktio_src, pktio_dst;
	odp_pktin_queue_t pktin;
	odp_pktout_queue_t pktout;
	thread_args_t *thr_args = arg;
	stats_t *stats = thr_args->stats;

	thr = odp_thread_id();

	src_idx = thr_args->src_idx;
	dst_idx = gbl_args->dst_port[src_idx];
	pktio_src = gbl_args->pktios[src_idx];
	pktio_dst = gbl_args->pktios[dst_idx];

	odp_pktin_queue(pktio_src, &pktin, 1);
	odp_pktout_queue(pktio_dst, &pktout, 1);

	printf("[%02i] srcif:%s dstif:%s spktio:%02" PRIu64
	       " dpktio:%02" PRIu64 " DIRECT RECV mode\n",
	       thr,
	       gbl_args->appl.if_names[src_idx],
	       gbl_args->appl.if_names[dst_idx],
	       odp_pktio_to_u64(pktio_src), odp_pktio_to_u64(pktio_dst));
	odp_barrier_wait(&barrier);

	/* Loop packets */
	while (!__builtin_k1_lwu(&exit_threads)) {
		int sent, i;
		unsigned tx_drops;

		pkts = odp_pktin_recv(pktin, pkt_tbl, MAX_PKT_BURST);
		if (odp_unlikely(pkts <= 0))
			continue;

		for (i = 0; i < pkts; ++i) {
			if (odp_packet_is_segmented(pkt_tbl[i])) {
				odp_packet_t sub_pkts[_ODP_MAX_SUBPACKETS + 1];
				int sub_count;

				sub_count = _odp_packet_fragment(pkt_tbl[i], sub_pkts);
				/* printf("Got a packet with %d frags\n", sub_count); */
				/* for (j = 0; j < sub_count; ++j) { */
				/* 	printf("\tFrag[%d].size = %d\n", j, */
				/* 	       (int)odp_packet_len(sub_pkts[j])); */
				/* } */
				pkt_tbl[i] = sub_pkts[0];
				odp_packet_free_multi(sub_pkts + 1, sub_count - 1);
			} else if (_ODP_MAX_FRAGS > 1){
				fprintf(stderr, "Packet is NOT fragmented\n");
				exit(1);
			}
		}
		if (gbl_args->appl.error_check) {
			int rx_drops;

			/* Drop packets with errors */
			rx_drops = drop_err_pkts(pkt_tbl, pkts);

			if (odp_unlikely(rx_drops)) {
				stats->s.rx_drops += rx_drops;
				if (pkts == rx_drops)
					continue;

				pkts -= rx_drops;
			}
		}

		fill_eth_addrs(pkt_tbl, pkts, dst_idx);

		sent = odp_pktout_send(pktout, pkt_tbl, pkts);

		sent     = odp_unlikely(sent < 0) ? 0 : sent;
		tx_drops = pkts - sent;

		if (odp_unlikely(tx_drops)) {
			stats->s.tx_drops += tx_drops;

			/* Drop rejected packets */
			for (i = sent; i < pkts; i++)
				odp_packet_free(pkt_tbl[i]);
		}

		stats->s.packets += pkts;
		odp_mb_full();
	}

	/* Make sure that latest stat writes are visible to other threads */
	odp_mb_full();

	return 0;
}

/**
 * Create a pktio handle, optionally associating a default input queue.
 *
 * @param dev Name of device to open
 * @param pool Pool to associate with device for packet RX/TX
 *
 * @return The handle of the created pktio object.
 * @retval ODP_PKTIO_INVALID if the create fails.
 */
static odp_pktio_t create_pktio(const char *dev, odp_pool_t pool)
{
	odp_pktio_t pktio;
	odp_pktio_param_t pktio_param;
	odp_pktin_queue_param_t pktin_param;
	odp_pktout_queue_param_t pktout_param;

	odp_pktio_param_init(&pktio_param);

	pktio_param.in_mode = ODP_PKTIN_MODE_DIRECT;

	pktio = odp_pktio_open(dev, pool, &pktio_param);
	if (pktio == ODP_PKTIO_INVALID) {
		fprintf(stderr, "Error: failed to open %s\n", dev);
		return ODP_PKTIO_INVALID;
	}

	printf("created pktio %" PRIu64 " (%s)\n",
	       odp_pktio_to_u64(pktio), dev);

	odp_pktin_queue_param_init(&pktin_param);
	odp_pktout_queue_param_init(&pktout_param);

	if (odp_pktin_queue_config(pktio, &pktin_param)) {
		fprintf(stderr, "Error: input queue config failed %s\n", dev);
		return ODP_PKTIO_INVALID;
	}

	if (odp_pktout_queue_config(pktio, &pktout_param)) {
		fprintf(stderr, "Error: output queue config failed %s\n", dev);
		return ODP_PKTIO_INVALID;
	}

	return pktio;
}

/**
 *  Print statistics
 *
 * @param num_workers Number of worker threads
 * @param thr_stats Pointer to stats storage
 * @param duration Number of seconds to loop in
 * @param timeout Number of seconds for stats calculation
 *
 */
static int print_speed_stats(int num_workers, stats_t *thr_stats,
			     int duration, int timeout)
{
	uint64_t pkts = 0;
	uint64_t pkts_prev = 0;
	uint64_t pps;
	uint64_t rx_drops, tx_drops;
	uint64_t maximum_pps = 0;
	int i;
	int elapsed = 0;
	int stats_enabled = 1;
	int loop_forever = (duration == 0);

	if (timeout <= 0) {
		stats_enabled = 0;
		timeout = 1;
	}
	/* Wait for all threads to be ready*/
	odp_barrier_wait(&barrier);

	do {
		pkts = 0;
		rx_drops = 0;
		tx_drops = 0;

		sleep(timeout);

		for (i = 0; i < num_workers; i++) {
			pkts += LOAD_U64(thr_stats[i].s.packets);
			rx_drops += LOAD_U64(thr_stats[i].s.rx_drops);
			tx_drops += LOAD_U64(thr_stats[i].s.tx_drops);
		}
		if (stats_enabled) {
			pps = (pkts - pkts_prev) / timeout;
			if (pps > maximum_pps)
				maximum_pps = pps;
			printf("%" PRIu64 " pps, %" PRIu64 " max pps, ",  pps,
			       maximum_pps);

			printf(" %" PRIu64 " rx drops, %" PRIu64 " tx drops\n",
			       rx_drops, tx_drops);

			pkts_prev = pkts;
		}
		elapsed += timeout;
	} while (loop_forever || (elapsed < duration));

	if (stats_enabled)
		printf("TEST RESULT: %" PRIu64 " maximum packets per second.\n",
		       maximum_pps);
	printf("%lld packets total\n", pkts);
	return (pkts ==  1200000 / _ODP_MAX_FRAGS) ? 0 : -1;
}

/**
 * ODP L2 forwarding main function
 */
int main(int argc, char *argv[])
{
	odp_instance_t instance;
	odph_odpthread_t thread_tbl[MAX_WORKERS];
	odp_pool_t pool;
	int i;
	int cpu;
	int num_workers;
	odp_shm_t shm;
	odp_cpumask_t cpumask;
	char cpumaskstr[ODP_CPUMASK_STR_SIZE];
	odph_ethaddr_t new_addr;
	odp_pktio_t pktio;
	odp_pool_param_t params;
	int ret;
	stats_t *stats;
	odp_platform_init_t platform_params;

	memset(&platform_params, 0, sizeof(platform_params));
	platform_params.n_rx_thr = 1;

	/* Init ODP before calling anything else */
	if (odp_init_global(&instance, NULL, NULL)) {
		fprintf(stderr, "Error: ODP global init failed.\n");
		exit(EXIT_FAILURE);
	}

	/* Init this thread */
	if (odp_init_local(instance, ODP_THREAD_CONTROL)) {
		fprintf(stderr, "Error: ODP local init failed.\n");
		exit(EXIT_FAILURE);
	}

	/* Reserve memory for args from shared mem */
	shm = odp_shm_reserve("shm_args", sizeof(args_t),
			      ODP_CACHE_LINE_SIZE, 0);
	gbl_args = odp_shm_addr(shm);

	if (gbl_args == NULL) {
		fprintf(stderr, "Error: shared mem alloc failed.\n");
		exit(EXIT_FAILURE);
	}
	memset(gbl_args, 0, sizeof(*gbl_args));

	/* Parse and store the application arguments */
	parse_args(argc, argv, &gbl_args->appl);

	/* Print both system and application information */
	print_info(NO_PATH(argv[0]), &gbl_args->appl);

	/* Default to system CPU count unless user specified */
	num_workers = MAX_WORKERS;
	if (gbl_args->appl.cpu_count)
		num_workers = gbl_args->appl.cpu_count;

	/* Get default worker cpumask */
	num_workers = odp_cpumask_default_worker(&cpumask, num_workers);
	(void)odp_cpumask_to_str(&cpumask, cpumaskstr, sizeof(cpumaskstr));

	printf("num worker threads: %i\n", num_workers);
	printf("first CPU:          %i\n", odp_cpumask_first(&cpumask));
	printf("cpu mask:           %s\n", cpumaskstr);

	if (num_workers < gbl_args->appl.if_count) {
		fprintf(stderr, "Error: CPU count %d less than interface count\n",
			num_workers);
		exit(EXIT_FAILURE);
	}

	/* Create packet pool */
	odp_pool_param_init(&params);
	params.pkt.seg_len = SHM_PKT_POOL_BUF_SIZE;
	params.pkt.len     = SHM_PKT_POOL_BUF_SIZE;
	params.pkt.num     = SHM_PKT_POOL_SIZE/SHM_PKT_POOL_BUF_SIZE;
	params.type        = ODP_POOL_PACKET;

	pool = odp_pool_create("packet pool", &params);

	if (pool == ODP_POOL_INVALID) {
		fprintf(stderr, "Error: packet pool create failed.\n");
		exit(EXIT_FAILURE);
	}
	odp_pool_print(pool);

	for (i = 0; i < gbl_args->appl.if_count; ++i) {
		pktio = create_pktio(gbl_args->appl.if_names[i], pool);
		if (pktio == ODP_PKTIO_INVALID)
			exit(EXIT_FAILURE);
		gbl_args->pktios[i] = pktio;

		/* Save interface ethernet address */
		if (odp_pktio_mac_addr(pktio, gbl_args->port_eth_addr[i].addr,
				       ODPH_ETHADDR_LEN) != ODPH_ETHADDR_LEN) {
			fprintf(stderr, "Error: interface ethernet address unknown\n");
			exit(EXIT_FAILURE);
		}

		/* Save destination eth address */
		if (gbl_args->appl.dst_change) {
			/* 02:00:00:00:00:XX */
			memset(&new_addr, 0, sizeof(odph_ethaddr_t));
			new_addr.addr[0] = 0x02;
			new_addr.addr[5] = i;
			gbl_args->dst_eth_addr[i] = new_addr;
		}

		/* Save interface destination port */
		gbl_args->dst_port[i] = find_dest_port(i);
	}

	gbl_args->pktios[i] = ODP_PKTIO_INVALID;

	memset(thread_tbl, 0, sizeof(thread_tbl));

	stats = gbl_args->stats;

	odp_barrier_init(&barrier, num_workers + 1);

	/* Create worker threads */
	cpu = odp_cpumask_first(&cpumask);
	for (i = 0; i < num_workers; ++i) {
		odp_cpumask_t thd_mask;
		odph_odpthread_params_t thr_params;

		memset(&thr_params, 0, sizeof(thr_params));
		thr_params.start    = pktio_direct_recv_thread;
		thr_params.arg      = &gbl_args->thread[i];
		thr_params.thr_type = ODP_THREAD_WORKER;
		thr_params.instance = instance;

		gbl_args->thread[i].stats = &stats[i];

		odp_cpumask_zero(&thd_mask);
		odp_cpumask_set(&thd_mask, cpu);
		odph_odpthreads_create(&thread_tbl[i], &thd_mask,
				       &thr_params);
		cpu = odp_cpumask_next(&cpumask, cpu);
	}

	/* Start packet receive and transmit */
	for (i = 0; i < gbl_args->appl.if_count; ++i) {
		pktio = gbl_args->pktios[i];
		ret   = odp_pktio_start(pktio);
		if (ret) {
			fprintf(stderr, "Error: unable to start %s\n",
				gbl_args->appl.if_names[i]);
			exit(EXIT_FAILURE);
		}
	}

	ret = print_speed_stats(num_workers, stats, gbl_args->appl.time,
				gbl_args->appl.accuracy);
	exit_threads = 1;
	__k1_wmb();
#ifdef __K1__

	for (i = 0; i < gbl_args->appl.if_count; ++i) {
		odp_pktio_stats_t pktio_stats;
		pktio = gbl_args->pktios[i];
		odp_pktio_stats(pktio, &pktio_stats);
		_odp_pktio_stats_print(pktio, &pktio_stats);
		if (pktio_stats.in_unknown_protos || pktio_stats.in_discards)
			ret = 1;
	}
#endif
	/* Master thread waits for other threads to exit */
	for (i = 0; i < num_workers; ++i)
		odph_odpthreads_join(&thread_tbl[i]);

	free(gbl_args->appl.if_names);
	free(gbl_args->appl.if_str);

	return ret;
}

/**
 * Drop packets which input parsing marked as containing errors.
 *
 * Frees packets with error and modifies pkt_tbl[] to only contain packets with
 * no detected errors.
 *
 * @param pkt_tbl  Array of packets
 * @param num      Number of packets in pkt_tbl[]
 *
 * @return Number of packets dropped
 */
static int drop_err_pkts(odp_packet_t pkt_tbl[], unsigned num)
{
	odp_packet_t pkt;
	unsigned dropped = 0;
	unsigned i, j;

	for (i = 0, j = 0; i < num; ++i) {
		pkt = pkt_tbl[i];

		if (odp_unlikely(odp_packet_has_error(pkt))) {
			odp_packet_free(pkt); /* Drop */
			dropped++;
		} else if (odp_unlikely(i != j++)) {
			pkt_tbl[j-1] = pkt;
		}
	}

	return dropped;
}

/**
 * Fill packets' eth addresses according to the destination port
 *
 * @param pkt_tbl  Array of packets
 * @param num      Number of packets in the array
 * @param dst_port Destination port
 */
static void fill_eth_addrs(odp_packet_t pkt_tbl[], unsigned num, int dst_port)
{
	odp_packet_t pkt;
	odph_ethhdr_t *eth;
	unsigned i;

	if (!gbl_args->appl.dst_change && !gbl_args->appl.src_change)
		return;

	for (i = 0; i < num; ++i) {
		pkt = pkt_tbl[i];
		if (odp_packet_has_eth(pkt)) {
			eth = (odph_ethhdr_t *)odp_packet_l2_ptr(pkt, NULL);

			if (gbl_args->appl.src_change)
				eth->src = gbl_args->port_eth_addr[dst_port];

			if (gbl_args->appl.dst_change)
				eth->dst = gbl_args->dst_eth_addr[dst_port];
		}
	}
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
		{"count", required_argument, NULL, 'c'},
		{"time", required_argument, NULL, 't'},
		{"accuracy", required_argument, NULL, 'a'},
		{"interface", required_argument, NULL, 'i'},
		{"mode", required_argument, NULL, 'm'},
		{"dst_change", required_argument, NULL, 'd'},
		{"src_change", required_argument, NULL, 's'},
		{"error_check", required_argument, NULL, 'e'},
		{"pktio_stats", no_argument, NULL, 'S'},
		{"help", no_argument, NULL, 'h'},
		{NULL, 0, NULL, 0}
	};

	appl_args->time = 0; /* loop forever if time to run is 0 */
	appl_args->accuracy = 1; /* get and print pps stats second */
	appl_args->src_change = 1; /* change eth src address by default */
	appl_args->error_check = 0; /* don't check packet errors by default */
	appl_args->pktio_stats = 0;

	while (1) {
		opt = getopt_long(argc, argv, "+c:+t:+a:i:m:d:Ss:e:h",
				  longopts, &long_index);

		if (opt == -1)
			break;	/* No more options */

		switch (opt) {
		case 'c':
			appl_args->cpu_count = atoi(optarg);
			break;
		case 't':
			appl_args->time = atoi(optarg);
			break;
		case 'a':
			appl_args->accuracy = atoi(optarg);
			break;
			/* parse packet-io interface names */
		case 'i':
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
			i = atoi(optarg);
			appl_args->mode = DIRECT_RECV;
			break;
		case 'd':
			appl_args->dst_change = atoi(optarg);
			break;
		case 'S':
			appl_args->pktio_stats = 1;
			break;
		case 's':
			appl_args->src_change = atoi(optarg);
			break;
		case 'e':
			appl_args->error_check = atoi(optarg);
			break;
		case 'h':
			usage(argv[0]);
			exit(EXIT_SUCCESS);
			break;
		default:
			break;
		}
	}

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
	       "ODP impl name:   %s\n"
	       "CPU model:       %s\n"
	       "CPU freq (hz):   %" PRIu64 "\n"
	       "Cache line size: %i\n"
	       "CPU count:       %i\n"
	       "\n",
	       odp_version_api_str(), odp_version_impl_name(),
	       odp_cpu_model_str(), odp_cpu_hz_max(),
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
	printf("PKTIN_DIRECT, ");
	printf("PKTOUT_DIRECT");

	printf("\n\n");
	fflush(NULL);
}

/**
 * Prinf usage information
 */
static void usage(char *progname)
{
	printf("\n"
	       "OpenDataPlane L2 forwarding application.\n"
	       "\n"
	       "Usage: %s OPTIONS\n"
	       "  E.g. %s -i eth0,eth1,eth2,eth3 -m 0 -t 1\n"
	       " In the above example,\n"
	       " eth0 will send pkts to eth1 and vice versa\n"
	       " eth2 will send pkts to eth3 and vice versa\n"
	       "\n"
	       "Mandatory OPTIONS:\n"
	       "  -i, --interface Eth interfaces (comma-separated, no spaces)\n"
	       "\n"
	       "Optional OPTIONS\n"
	       "  -m, --mode      0: Send&receive packets directly from NIC (default)\n"
	       "                  1: Send&receive packets through scheduler sync none queues\n"
	       "                  2: Send&receive packets through scheduler sync atomic queues\n"
	       "                  3: Send&receive packets through scheduler sync ordered queues\n"
	       "  -c, --count <number> CPU count.\n"
	       "  -t, --time  <number> Time in seconds to run.\n"
	       "  -a, --accuracy <number> Time in seconds get print statistics\n"
	       "                          (default is 1 second).\n"
	       "  -d, --dst_change  0: Don't change packets' dst eth addresses (default)\n"
	       "                    1: Change packets' dst eth addresses\n"
	       "  -s, --src_change  0: Don't change packets' src eth addresses\n"
	       "                    1: Change packets' src eth addresses (default)\n"
	       "  -e, --error_check 0: Don't check packet errors (default)\n"
	       "                    1: Check packet errors\n"
	       "  -S, --pktio_stats  : Display pktio statistics before exiting\n"
	       "  -h, --help           Display help and exit.\n\n"
	       " environment variables: ODP_PKTIO_DISABLE_NETMAP\n"
	       "                        ODP_PKTIO_DISABLE_SOCKET_MMAP\n"
	       "                        ODP_PKTIO_DISABLE_SOCKET_MMSG\n"
	       " can be used to advanced pkt I/O selection for linux-generic\n"
	       "\n", NO_PATH(progname), NO_PATH(progname)
	    );
}
