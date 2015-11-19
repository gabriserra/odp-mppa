/* Copyright (c) 2013, Linaro Limited
 * All rights reserved.
 *
 * SPDX-License-Identifier:     BSD-3-Clause
 */

/**
 * @file
 *
 * @example odp_example_ipsec.c  ODP basic packet IO cross connect with IPsec test application
 */

#define _DEFAULT_SOURCE
/* enable strtok */
#define _POSIX_C_SOURCE 200112L
#include <stdlib.h>
#include <getopt.h>
#include <unistd.h>

#include <example_debug.h>

#include <odp.h>

#include <odp/helper/linux.h>
#include <odp/helper/eth.h>
#include <odp/helper/ip.h>
#include <odp/helper/icmp.h>
#include <odp/helper/ipsec.h>

#include <odp_ipsec_misc.h>
#include <odp_ipsec_sa_db.h>
#include <odp_ipsec_sp_db.h>
#include <odp_ipsec_fwd_db.h>
#include <odp_ipsec_loop_db.h>
#include <odp_ipsec_cache.h>
#include <odp_ipsec_stream.h>

#define MAX_WORKERS     32   /**< maximum number of worker threads */

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

typedef struct _counter {
	long long  nb;
	long long           start_cycle;
	long long  sum;
	long long  min;
	long long  max;
	long long  big_max;
} counter_val_t;

typedef enum {
	CNT_GLOBAL,
	CNT_RECV,
	CNT_DEQ,
	CNT_EVENT_PACKET,
	CNT_EVENT_CRYPTO_COMPL,
	CNT_INPUT_VERIFY,
	CNT_IPSEC_IN_CLASSIFY,
	CNT_IPSEC_IN_FINISH,
	CNT_CHECK_CRYPTO_OUT,
	CNT_ROUTE_LOOKUP,
	CNT_IPSEC_OUT_CLASSIFY,
	CNT_IPSEC_OUT_SEQ,
	CNT_IPSEC_OUT_FINISH,
	CNT_CRYPTO_OPERATION,
	CNT_TRANSMIT,
	CNT_PKT_RX,
	CNT_PKT_DROP,
	CNT_PKT_XMIT,
	CNT_MAX,
} counter_type_e;

#ifdef __k1__


static counter_val_t counter_values[MAX_WORKERS][CNT_MAX] = { [ 0 ... MAX_WORKERS - 1 ] = {
	[0 ... CNT_MAX - 1] = { .nb = 0, .start_cycle = 0, .sum = 0, .min = LLONG_MAX, .max = 0, .big_max = 0 }}};

static void init_hw_counter(void) {
	__k1_counter_stop(0);
	__k1_counter_reset(0);
	__k1_counter_enable(0, _K1_CYCLE_COUNT, _K1_DIAGNOSTIC_NOT_CHAINED);
}

static void count(counter_type_e type) {
	int id = __k1_get_cpu_id();
	counter_values[id][type].nb++;
}

static void start_counter(counter_type_e type) {
	int id = __k1_get_cpu_id();
	static int init = 0;
	if ( init == 0 ) {
		init = 1;
		init_hw_counter();
	}
	counter_values[id][type].start_cycle = __k1_counter_num(0);
}

static void stop_counter(counter_type_e type) {
	int id = __k1_get_cpu_id();
	unsigned long long stop_cycle = __k1_counter_num(0);
	long long time = stop_cycle - counter_values[id][type].start_cycle;
	if ( time < 0 ) return;
	counter_values[id][type].nb++;
	counter_values[id][type].sum += time;
	if ( time > counter_values[id][type].max ) counter_values[id][type].max = time;
	if ( time < counter_values[id][type].min ) counter_values[id][type].min = time;
	if ( time > 100000 ) counter_values[id][type].big_max++;
}

#else
static
void start_counter(counter_type_e type) {
	(void)type;
}

static
void stop_counter(counter_type_e type) {
	(void)type;
}

static void count(counter_type_e type) {
	(void)type;
}
#endif

odp_pktio_t pktios[ODP_CONFIG_PKTIO_ENTRIES];
unsigned n_pktios;

/**
 * Parsed command line application arguments
 */
typedef struct {
	int cpu_count;
	int if_count;		/**< Number of interfaces to be used */
	char **if_names;	/**< Array of pointers to interface names */
	crypto_api_mode_e mode;	/**< Crypto API preferred mode */
	odp_pool_t pool;	/**< Buffer pool for packet IO */
	char *if_str;		/**< Storage for interface names */
} appl_args_t;

/**
 * Grouping of both parsed CL args and thread specific args - alloc together
 */
typedef struct {
	/** Application (parsed) arguments */
	appl_args_t appl;
} args_t;

/* helper funcs */
static void parse_args(int argc, char *argv[], appl_args_t *appl_args);
static void print_info(char *progname, appl_args_t *appl_args);
static void usage(char *progname);

/** Global pointer to args */
static args_t *args;

/**
 * Buffer pool for packet IO
 */
#ifdef __k1__
#define SHM_PKT_POOL_BUF_COUNT 500
#define SHM_PKT_POOL_BUF_SIZE  1600
#else
#define SHM_PKT_POOL_BUF_COUNT 1024
#define SHM_PKT_POOL_BUF_SIZE  4096
#endif
#define SHM_PKT_POOL_SIZE      (SHM_PKT_POOL_BUF_COUNT * SHM_PKT_POOL_BUF_SIZE)

static odp_pool_t pkt_pool = ODP_POOL_INVALID;

/** ATOMIC queue for IPsec sequence number assignment */
static odp_queue_t seqnumq;

/** Synchronize threads before packet processing begins */
static odp_barrier_t sync_barrier;

/**
 * Packet processing states/steps
 */
typedef enum {
	PKT_STATE_INPUT_VERIFY,        /**< Verify IPv4 and ETH */
	PKT_STATE_CHECK_CRYPTO_OUT,
	PKT_STATE_ROUTE_LOOKUP,        /**< Use DST IP to find output IF */
	PKT_STATE_IPSEC_OUT_CLASSIFY,  /**< Intiate output IPsec */
	PKT_STATE_IPSEC_OUT_SEQ,       /**< Assign IPsec sequence numbers */
	PKT_STATE_IPSEC_OUT_FINISH,    /**< Finish output IPsec */
	PKT_STATE_TRANSMIT,            /**< Send packet to output IF queue */
} pkt_state_e;

/**
 * Packet processing result codes
 */
typedef enum {
	PKT_CONTINUE,    /**< No events posted, keep processing */
	PKT_POSTED,      /**< Event posted, stop processing */
	PKT_DROP,        /**< Reason to drop detected, stop processing */
	PKT_DONE         /**< Finished with packet, stop processing */
} pkt_disposition_e;

/**
 * Per packet IPsec processing context
 */
typedef struct {
	uint8_t  ip_tos;         /**< Saved IP TOS value */
	uint16_t ip_frag_offset; /**< Saved IP flags value */
	uint8_t  ip_ttl;         /**< Saved IP TTL value */
	int      hdr_len;        /**< Length of IPsec headers */
	int      trl_len;        /**< Length of IPsec trailers */
	uint16_t tun_hdr_offset; /**< Offset of tunnel header from
				      buffer start */
	uint16_t ah_offset;      /**< Offset of AH header from buffer start */
	uint16_t esp_offset;     /**< Offset of ESP header from buffer start */

	/* Input only */
	uint32_t src_ip;         /**< SA source IP address */
	uint32_t dst_ip;         /**< SA dest IP address */

	/* Output only */
	odp_crypto_op_params_t params;  /**< Parameters for crypto call */
	uint32_t *ah_seq;               /**< AH sequence number location */
	uint32_t *esp_seq;              /**< ESP sequence number location */
	uint16_t *tun_hdr_id;           /**< Tunnel header ID > */
} ipsec_ctx_t;

/**
 * Per packet processing context
 */
typedef struct {
	ipsec_ctx_t  ipsec;   /**< IPsec specific context */
} pkt_ctx_t;

/**
 * IPsec pre argument processing intialization
 */
static
void ipsec_init_pre(void)
{
	odp_queue_param_t qparam;

	/*
	 * Create queues
	 *
	 *  - completion queue (should eventually be ORDERED)
	 *  - sequence number queue (must be ATOMIC)
	 */
	odp_queue_param_init(&qparam);
	qparam.sched.prio  = ODP_SCHED_PRIO_HIGHEST;
	qparam.sched.sync  = ODP_SCHED_SYNC_NONE;
	qparam.sched.group = ODP_SCHED_GROUP_ALL;

	seqnumq = odp_queue_create("seqnum",
				   ODP_QUEUE_TYPE_POLL,
				   &qparam);
	if (ODP_QUEUE_INVALID == seqnumq) {
		EXAMPLE_ERR("Error: sequence number queue creation failed\n");
		exit(EXIT_FAILURE);
	}

	/* Initialize our data bases */
	init_sp_db();
	init_sa_db();
	init_tun_db();
	init_ipsec_cache();
}

/**
 * IPsec post argument processing intialization
 *
 * Resolve SP DB with SA DB and create corresponding IPsec cache entries
 *
 * @param api_mode  Mode to use when invoking per packet crypto API
 */
static
void ipsec_init_post(crypto_api_mode_e api_mode)
{
	sp_db_entry_t *entry;

	/* Attempt to find appropriate SA for each SP */
	for (entry = sp_db->list; NULL != entry; entry = entry->next) {
		sa_db_entry_t *cipher_sa = NULL;
		sa_db_entry_t *auth_sa = NULL;
		tun_db_entry_t *tun = NULL;

		if (entry->esp) {
			cipher_sa = find_sa_db_entry(&entry->src_subnet,
						     &entry->dst_subnet,
						     1);
			tun = find_tun_db_entry(cipher_sa->src_ip,
						cipher_sa->dst_ip);
		}
		if (entry->ah) {
			auth_sa = find_sa_db_entry(&entry->src_subnet,
						   &entry->dst_subnet,
						   0);
			tun = find_tun_db_entry(auth_sa->src_ip,
						auth_sa->dst_ip);
		}

		if (cipher_sa || auth_sa) {
			if (create_ipsec_cache_entry(cipher_sa,
						     auth_sa,
						     tun,
						     api_mode,
						     entry->input,
						     ODP_QUEUE_INVALID,
						     ODP_POOL_INVALID)) {
				EXAMPLE_ERR("Error: IPSec cache entry failed.\n"
						);
				exit(EXIT_FAILURE);
			}
		} else {
			printf(" WARNING: SA not found for SP\n");
			dump_sp_db_entry(entry);
		}
	}
}

/**
 * Initialize interface
 *
 * Initialize ODP pktio and queues, query MAC address and update
 * forwarding database.
 *
 * @param intf     Interface name string
 */
static
void initialize_intf(char *intf)
{
	odp_pktio_t pktio;
	odp_queue_t outq_def;
	odp_queue_t inq_def;
	char inq_name[ODP_QUEUE_NAME_LEN];
	odp_queue_param_t qparam;
	int ret;
	uint8_t src_mac[ODPH_ETHADDR_LEN];
	char src_mac_str[MAX_STRING];
	odp_pktio_param_t pktio_param;

	odp_pktio_param_init(&pktio_param);
	pktio_param.in_mode = ODP_PKTIN_MODE_POLL;

	/*
	 * Open a packet IO instance for thread and get default output queue
	 */
	pktio = odp_pktio_open(intf, pkt_pool, &pktio_param);
	if (ODP_PKTIO_INVALID == pktio) {
		EXAMPLE_ERR("Error: pktio create failed for %s\n", intf);
		exit(EXIT_FAILURE);
	}
	pktios[n_pktios++] = pktio;
	outq_def = odp_pktio_outq_getdef(pktio);

	/*
	 * Create and set the default INPUT queue associated with the 'pktio'
	 * resource
	 */
	odp_queue_param_init(&qparam);
	qparam.sched.prio  = ODP_SCHED_PRIO_DEFAULT;
	qparam.sched.sync  = ODP_SCHED_SYNC_NONE;
	qparam.sched.group = ODP_SCHED_GROUP_ALL;
	snprintf(inq_name, sizeof(inq_name), "%" PRIu64 "-pktio_inq_def",
		 odp_pktio_to_u64(pktio));
	inq_name[ODP_QUEUE_NAME_LEN - 1] = '\0';

	inq_def = odp_queue_create(inq_name, ODP_QUEUE_TYPE_PKTIN, &qparam);
	if (ODP_QUEUE_INVALID == inq_def) {
		EXAMPLE_ERR("Error: pktio queue creation failed for %s\n",
			    intf);
		exit(EXIT_FAILURE);
	}

	ret = odp_pktio_inq_setdef(pktio, inq_def);
	if (ret) {
		EXAMPLE_ERR("Error: default input-Q setup for %s\n", intf);
		exit(EXIT_FAILURE);
	}

	ret = odp_pktio_start(pktio);
	if (ret) {
		EXAMPLE_ERR("Error: unable to start %s\n", intf);
		exit(EXIT_FAILURE);
	}

	/* Read the source MAC address for this interface */
	ret = odp_pktio_mac_addr(pktio, src_mac, sizeof(src_mac));
	if (ret <= 0) {
		EXAMPLE_ERR("Error: failed during MAC address get for %s\n",
			    intf);
		exit(EXIT_FAILURE);
	}

	printf("Created pktio:%02" PRIu64 ", queue mode (ATOMIC queues)\n"
	       "          default pktio%02" PRIu64 "-INPUT queue:%" PRIu64 "\n"
	       "          source mac address %s\n",
	       odp_pktio_to_u64(pktio), odp_pktio_to_u64(pktio),
	       odp_queue_to_u64(inq_def),
	       mac_addr_str(src_mac_str, src_mac));

	/* Resolve any routes using this interface for output */
	resolve_fwd_db(intf, outq_def, src_mac);
}

/**
 * Packet Processing - Input verification
 *
 * @param pkt  Packet to inspect
 * @param ctx  Packet process context (not used)
 *
 * @return PKT_CONTINUE if good, supported packet else PKT_DROP
 */
static
pkt_disposition_e do_input_verify(odp_packet_t pkt)
{
	if (odp_unlikely(odp_packet_has_error(pkt)))
		return PKT_DROP;

	if (!odp_packet_has_eth(pkt))
		return PKT_DROP;

	if (!odp_packet_has_ipv4(pkt))
		return PKT_DROP;

	return PKT_CONTINUE;
}

/**
 * Packet Processing - Route lookup in forwarding database
 *
 * @param pkt  Packet to route
 * @param ctx  Packet process context
 *
 * @return PKT_CONTINUE if route found else PKT_DROP
 */
static
pkt_disposition_e do_route_fwd_db(odp_packet_t pkt)
{
	odph_ipv4hdr_t *ip = (odph_ipv4hdr_t *)odp_packet_l3_ptr(pkt, NULL);
	fwd_db_entry_t *entry;

	entry = find_fwd_db_entry(odp_be_to_cpu_32(ip->dst_addr));

	if (entry) {
		odph_ethhdr_t *eth =
			(odph_ethhdr_t *)odp_packet_l2_ptr(pkt, NULL);

		memcpy(&eth->dst, entry->dst_mac, ODPH_ETHADDR_LEN);
		memcpy(&eth->src, entry->src_mac, ODPH_ETHADDR_LEN);
		odp_packet_user_ptr_set(pkt, (void*)entry->queue);

		return PKT_CONTINUE;
	}
	return PKT_DROP;
}

static int require_ipsec_out(odp_packet_t pkt)
{
	odph_ipv4hdr_t *ip = (odph_ipv4hdr_t *)odp_packet_l3_ptr(pkt, NULL);

	/* Find record */
	if (find_ipsec_cache_entry_out(odp_be_to_cpu_32(ip->src_addr),
				       odp_be_to_cpu_32(ip->dst_addr),
				       ip->proto))
	    return 1;
	return 0;
}

/**
 * Packet Processing - Output IPsec packet classification
 *
 * Verify the outbound packet has a match in the IPsec cache,
 * if so issue prepend IPsec headers and prepare parameters
 * for crypto API call.  Post the packet to ATOMIC queue so
 * that sequence numbers can be applied in packet order as
 * the next processing step.
 *
 * @param pkt   Packet to classify
 * @param ctx   Packet process context
 * @param skip  Pointer to return "skip" indication
 *
 * @return PKT_CONTINUE if done else PKT_POSTED
 */
static
pkt_disposition_e do_ipsec_out_classify(odp_packet_t pkt,
					pkt_ctx_t *ctx,
					odp_bool_t *skip)
{
	uint8_t *buf = odp_packet_data(pkt);
	odph_ipv4hdr_t *ip = (odph_ipv4hdr_t *)odp_packet_l3_ptr(pkt, NULL);
	uint16_t ip_data_len = ipv4_data_len(ip);
	uint8_t *ip_data = ipv4_data_p(ip);
	ipsec_cache_entry_t *entry;
	odp_crypto_op_params_t *params = &ctx->ipsec.params;
	int ipsec_hdr_len = 0;
	int trl_len = 0;
	odph_ahhdr_t *ah = NULL;
	odph_esphdr_t *esp = NULL;

	/* Default to skip IPsec */
	*skip = TRUE;

	/* Find record */
	entry = find_ipsec_cache_entry_out(odp_be_to_cpu_32(ip->src_addr),
					   odp_be_to_cpu_32(ip->dst_addr),
					   ip->proto);
	if (!entry) {
		return PKT_CONTINUE;
	}

	/* Save IPv4 stuff */
	ctx->ipsec.ip_tos = ip->tos;
	ctx->ipsec.ip_frag_offset = odp_be_to_cpu_16(ip->frag_offset);
	ctx->ipsec.ip_ttl = ip->ttl;

	/* Initialize parameters block */
	memset(params, 0, sizeof(odp_crypto_op_params_t));
	params->session = entry->state.session;
	params->ctx = ctx;
	params->pkt = pkt;
	params->out_pkt = pkt;

	int ah_hdr_len = 0;
	int esp_hdr_len = 0;
	/* Compute ah and esp, determine length of headers, move the data */
	if (entry->ah.alg) {
		ipsec_hdr_len += ah_hdr_len = sizeof(odph_ahhdr_t) + entry->ah.icv_len;
	}
	if (entry->esp.alg) {
		ipsec_hdr_len += esp_hdr_len = sizeof(odph_esphdr_t) + entry->esp.iv_len;
	}

	int l2_l3_hdr_len = ip_data - buf;

	void* head_ptr = odp_packet_push_head(pkt, ipsec_hdr_len);
	if (head_ptr == NULL) {
		printf("odp_packet_push_head returned NULL (not enough headroom: %d) \n", ipsec_hdr_len);
		fflush(stdout);
		abort();
	}
	memmove(head_ptr, buf, l2_l3_hdr_len);
	buf = head_ptr;

	ip = (odph_ipv4hdr_t *)odp_packet_l3_ptr(pkt, NULL);

	/* update outer header in tunnel mode */
	if (entry->mode == IPSEC_SA_MODE_TUNNEL) {
		/* tunnel addresses */
		ip->src_addr = odp_cpu_to_be_32(entry->tun_src_ip);
		ip->dst_addr = odp_cpu_to_be_32(entry->tun_dst_ip);
	}

	/* For cipher, compute encrypt length, build headers and request */
	if (entry->esp.alg) {
		esp = (odph_esphdr_t *)(buf + l2_l3_hdr_len + ah_hdr_len);

		uint32_t encrypt_len;
		odph_esptrl_t *esp_t;

		encrypt_len = ESP_ENCODE_LEN(ip_data_len +
					     sizeof(*esp_t),
					     entry->esp.block_len);
		trl_len = encrypt_len - ip_data_len;

		esp->spi = odp_cpu_to_be_32(entry->esp.spi);
		memcpy(esp + 1, entry->state.iv, entry->esp.iv_len);

		esp_t = (odph_esptrl_t *)(ip_data + encrypt_len) - 1;
		esp_t->pad_len     = trl_len - sizeof(*esp_t);
		if (entry->mode == IPSEC_SA_MODE_TUNNEL)
			esp_t->next_header = ODPH_IPV4;
		else
			esp_t->next_header = ip->proto;
		ip->proto = ODPH_IPPROTO_ESP;

		params->cipher_range.offset = ip_data - buf;
		params->cipher_range.length = encrypt_len;
	}

	/* For authentication, build header clear mutables and build request */
	if (entry->ah.alg) {
		ah = (odph_ahhdr_t *)(buf + l2_l3_hdr_len);

		memset(ah, 0, sizeof(*ah) + entry->ah.icv_len);
		ah->spi = odp_cpu_to_be_32(entry->ah.spi);
		ah->ah_len = 1 + (entry->ah.icv_len / 4);
		if (entry->mode == IPSEC_SA_MODE_TUNNEL && !esp)
			ah->next_header = ODPH_IPV4;
		else
			ah->next_header = ip->proto;
		ip->proto = ODPH_IPPROTO_AH;

		ip->chksum = 0;
		ip->tos = 0;
		ip->frag_offset = 0;
		ip->ttl = 0;

		params->auth_range.offset = ((uint8_t *)ip) - buf;
		params->auth_range.length =
			odp_be_to_cpu_16(ip->tot_len) + (ipsec_hdr_len + trl_len);
		params->hash_result_offset = ah->icv - buf;
	}

	/* Set IPv4 length before authentication */
	ipv4_adjust_len(ip, ipsec_hdr_len + trl_len);
	if (!odp_packet_push_tail(pkt, trl_len)) {
		printf ("Tail failure !\n");
		return PKT_DROP;
	}

	/* Save remaining context */
	ctx->ipsec.hdr_len = ipsec_hdr_len;
	ctx->ipsec.trl_len = trl_len;
	ctx->ipsec.ah_offset = ah ? ((uint8_t *)ah) - buf : 0;
	ctx->ipsec.esp_offset = esp ? ((uint8_t *)esp) - buf : 0;
	ctx->ipsec.tun_hdr_offset = (entry->mode == IPSEC_SA_MODE_TUNNEL) ?
				       ((uint8_t *)ip - buf) : 0;
	ctx->ipsec.ah_seq = &entry->state.ah_seq;
	ctx->ipsec.esp_seq = &entry->state.esp_seq;
	ctx->ipsec.tun_hdr_id = &entry->state.tun_hdr_id;

	*skip = FALSE;

	return PKT_CONTINUE;
}

/**
 * Packet Processing - Output IPsec packet sequence number assignment
 *
 * Assign the necessary sequence numbers and then issue the crypto API call
 *
 * @param pkt  Packet to handle
 * @param ctx  Packet process context
 *
 * @return PKT_CONTINUE if done else PKT_POSTED
 */
static
pkt_disposition_e do_ipsec_out_seq(odp_packet_t pkt,
				   pkt_ctx_t *ctx,
				   odp_crypto_op_result_t *result)
{
	uint8_t *buf = odp_packet_data(pkt);
	odp_bool_t posted = 0;

	/* We were dispatched from atomic queue, assign sequence numbers */
	if (ctx->ipsec.ah_offset) {
		odph_ahhdr_t *ah;

		ah = (odph_ahhdr_t *)(ctx->ipsec.ah_offset + buf);
		ah->seq_no = odp_cpu_to_be_32((*ctx->ipsec.ah_seq)++);
	}
	if (ctx->ipsec.esp_offset) {
		odph_esphdr_t *esp;

		esp = (odph_esphdr_t *)(ctx->ipsec.esp_offset + buf);
		esp->seq_no = odp_cpu_to_be_32((*ctx->ipsec.esp_seq)++);
	}
	if (ctx->ipsec.tun_hdr_offset) {
		odph_ipv4hdr_t *ip;
		int ret;

		ip = (odph_ipv4hdr_t *)(ctx->ipsec.tun_hdr_offset + buf);
		ip->id = odp_cpu_to_be_16((*ctx->ipsec.tun_hdr_id)++);
		if (!ip->id && 0) {
			/* re-init tunnel hdr id */
			ret = odp_random_data((uint8_t *)ctx->ipsec.tun_hdr_id,
					      sizeof(*ctx->ipsec.tun_hdr_id),
					      1);
			if (ret != sizeof(*ctx->ipsec.tun_hdr_id))
				abort();
		}
	}

	start_counter(CNT_CRYPTO_OPERATION);
	/* Issue crypto request */
	int ret = odp_crypto_operation(&ctx->ipsec.params,
				       &posted,
				       result);
	if (ret){
		printf("Ret=%d\n", ret);
		abort();
	}
	stop_counter(CNT_CRYPTO_OPERATION);
	return (posted) ? PKT_POSTED : PKT_CONTINUE;
}

/**
 * Packet Processing - Output IPsec packet processing cleanup
 *
 * @param pkt  Packet to handle
 * @param ctx  Packet process context
 *
 * @return PKT_CONTINUE if successful else PKT_DROP
 */
static
pkt_disposition_e do_ipsec_out_finish(odp_packet_t pkt,
				      pkt_ctx_t *ctx,
				      odp_crypto_op_result_t *result)
{
	odph_ipv4hdr_t *ip;

	/* Check crypto result */
	if (!result->ok) {
		if (!is_crypto_compl_status_ok(&result->cipher_status))
			return PKT_DROP;
		if (!is_crypto_compl_status_ok(&result->auth_status))
			return PKT_DROP;
	}
	ip = (odph_ipv4hdr_t *)odp_packet_l3_ptr(pkt, NULL);

	/* Finalize the IPv4 header */
	ip->ttl = ctx->ipsec.ip_ttl;
	ip->tos = ctx->ipsec.ip_tos;
	ip->frag_offset = odp_cpu_to_be_16(ctx->ipsec.ip_frag_offset);
	ip->chksum = 0;
	odph_ipv4_csum_update(pkt);

	/* Fall through to next state */
	return PKT_CONTINUE;
}

static int num_workers;
/**
 * Packet IO worker thread
 *
 * Loop calling odp_schedule to obtain packets from one of three sources,
 * and continue processing the packet based on the state stored in its
 * per packet context.
 *
 *  - Input interfaces (i.e. new work)
 *  - Sequence number assignment queue
 *  - Per packet crypto API completion queue
 *
 * @param arg  Required by "odph_linux_pthread_create", unused
 *
 * @return NULL (should never return)
 */
static
void *pktio_thread(void *arg EXAMPLE_UNUSED)
{
	int thr;
	odp_event_t ev;
	pkt_disposition_e rc;

	thr = odp_thread_id();

	printf("Pktio thread [%02i] starts\n", thr);

	odp_barrier_wait(&sync_barrier);

	for(;;) {
		if(thr % 5 == 0){
			int n_pkt;
			odp_packet_t pkts[4];
			odp_packet_t crypt_pkts[4];
			int n_crypt = 0;
			int pkt_off = 0;

			do {
				start_counter(CNT_RECV);
				n_pkt = odp_pktio_recv(pktios[0], pkts, 4);
				stop_counter(CNT_RECV);
			} while(n_pkt == 0);
			count(CNT_PKT_RX);

			for (int i = 0; i < n_pkt; ++i) {
				start_counter(CNT_GLOBAL);
				start_counter(CNT_INPUT_VERIFY);
				rc = do_input_verify(pkts[i]);
				stop_counter(CNT_INPUT_VERIFY);

				if(rc != PKT_CONTINUE)
					goto end;

				start_counter(CNT_ROUTE_LOOKUP);
				rc = do_route_fwd_db(pkts[i]);
				stop_counter(CNT_ROUTE_LOOKUP);

				if(rc != PKT_CONTINUE)
					goto end;

				start_counter(CNT_CHECK_CRYPTO_OUT);
				if (require_ipsec_out(pkts[i])){
					crypt_pkts[n_crypt++] = pkts[i];
					continue;
				} else {
					rc = PKT_CONTINUE;
				}
				stop_counter(CNT_CHECK_CRYPTO_OUT);
			end:
				/* Check for drop */
				if (PKT_DROP == rc) {
					odp_packet_free(pkts[i]);
					count(CNT_PKT_DROP);
				}
				else {
					pkts[pkt_off++] = pkts[i];
				}
			}
			if (pkt_off) {
				start_counter(CNT_TRANSMIT);
				odp_queue_enq_multi((odp_queue_t)odp_packet_user_ptr(pkts[0]), (odp_event_t*)pkts, pkt_off);
				rc = PKT_DONE;
				stop_counter(CNT_TRANSMIT);
			}
			if (n_crypt) {
				odp_queue_enq_multi(seqnumq, (odp_event_t*)crypt_pkts, n_crypt);
			}


		} else {
			odp_bool_t skip = FALSE;
			pkt_ctx_t   ctx;
			odp_crypto_op_result_t result;
			odp_packet_t pkts[4];
			odp_packet_t crypt_pkts[4];
			int n_pkt;
			int n_crypt = 0;
			int ret = 0;
			do {
				start_counter(CNT_DEQ);
				n_pkt = odp_queue_deq_multi(seqnumq, (odp_event_t*)pkts, 4);
				stop_counter(CNT_DEQ);
			} while(n_pkt == 0);

			for (int i = 0; i < n_pkt; ++i){
				ev = (odp_event_t) pkts[i];
				count(CNT_PKT_RX);

				start_counter(CNT_GLOBAL);

				odp_packet_t pkt = odp_packet_from_event(ev);

				start_counter(CNT_IPSEC_OUT_CLASSIFY);
				rc = do_ipsec_out_classify(pkt, &ctx, &skip);
				if (odp_unlikely(skip)) {
					goto multi_end;
				}
				stop_counter(CNT_IPSEC_OUT_CLASSIFY);
				if(rc != PKT_CONTINUE)
					goto multi_end;

				start_counter(CNT_IPSEC_OUT_SEQ);
				rc = do_ipsec_out_seq(pkt, &ctx, &result);
				stop_counter(CNT_IPSEC_OUT_SEQ);
				if(rc != PKT_CONTINUE)
					goto multi_end;

				start_counter(CNT_IPSEC_OUT_FINISH);
				rc = do_ipsec_out_finish(pkt, &ctx, &result);
				stop_counter(CNT_IPSEC_OUT_FINISH);
				if(rc != PKT_CONTINUE)
					goto multi_end;
				crypt_pkts[n_crypt++] = pkts[i];
				continue;
			multi_end:
				odp_packet_free(pkts[i]);
			}

			start_counter(CNT_TRANSMIT);
			ret = 0;/* odp_queue_enq_multi((odp_queue_t)odp_packet_user_ptr(pkts[0]), */
				/* 		  (odp_event_t*)crypt_pkts, n_crypt); */
			rc = PKT_DONE;
			stop_counter(CNT_TRANSMIT);

			for (int i = ret; i < n_crypt; ++i)
				odp_packet_free(crypt_pkts[i]);
		}
		stop_counter(CNT_GLOBAL);

	}


	/* unreachable */
	return NULL;
}

/**
 * ODP ipsec example main function
 */
int
main(int argc, char *argv[])
{
	odph_linux_pthread_t thread_tbl[MAX_WORKERS];
	int i;
	int stream_count;
	odp_shm_t shm;
	odp_cpumask_t cpumask;
	char cpumaskstr[ODP_CPUMASK_STR_SIZE];
	odp_pool_param_t params;

	/* Init ODP before calling anything else */
	if (odp_init_global(NULL, NULL)) {
		EXAMPLE_ERR("Error: ODP global init failed.\n");
		exit(EXIT_FAILURE);
	}

	/* Init this thread */
	if (odp_init_local(ODP_THREAD_CONTROL)) {
		EXAMPLE_ERR("Error: ODP local init failed.\n");
		exit(EXIT_FAILURE);
	}
	my_sleep(__k1_get_cluster_id());

	/* Reserve memory for args from shared mem */
	shm = odp_shm_reserve("shm_args", sizeof(args_t), ODP_CACHE_LINE_SIZE,
			      0);

	args = odp_shm_addr(shm);

	if (NULL == args) {
		EXAMPLE_ERR("Error: shared mem alloc failed.\n");
		exit(EXIT_FAILURE);
	}
	memset(args, 0, sizeof(*args));

	/* Must init our databases before parsing args */
	ipsec_init_pre();
	init_fwd_db();
	init_loopback_db();
	init_stream_db();

	/* Parse and store the application arguments */
	parse_args(argc, argv, &args->appl);

	/* Print both system and application information */
	print_info(NO_PATH(argv[0]), &args->appl);

	/* Default to system CPU count unless user specified */
	num_workers = MAX_WORKERS;
	if (args->appl.cpu_count)
		num_workers = args->appl.cpu_count;

	/* Get default worker cpumask */
	num_workers = odp_cpumask_default_worker(&cpumask, num_workers);
	(void)odp_cpumask_to_str(&cpumask, cpumaskstr, sizeof(cpumaskstr));

	printf("num worker threads: %i\n", num_workers);
	printf("first CPU:          %i\n", odp_cpumask_first(&cpumask));
	printf("cpu mask:           %s\n", cpumaskstr);

	/* Create a barrier to synchronize thread startup */
	odp_barrier_init(&sync_barrier, num_workers);

	/* Create packet buffer pool */
	odp_pool_param_init(&params);
	params.pkt.seg_len = SHM_PKT_POOL_BUF_SIZE;
	params.pkt.len     = SHM_PKT_POOL_BUF_SIZE;
	params.pkt.num     = SHM_PKT_POOL_BUF_COUNT;
	params.type        = ODP_POOL_PACKET;

	pkt_pool = odp_pool_create("packet_pool", &params);

	if (ODP_POOL_INVALID == pkt_pool) {
		EXAMPLE_ERR("Error: packet pool create failed.\n");
		exit(EXIT_FAILURE);
	}

	/* Populate our IPsec cache */
	printf("Using %s mode for crypto API\n\n",
	       (CRYPTO_API_SYNC == args->appl.mode) ? "SYNC" :
	       (CRYPTO_API_ASYNC_IN_PLACE == args->appl.mode) ?
	       "ASYNC_IN_PLACE" : "ASYNC_NEW_BUFFER");
	ipsec_init_post(args->appl.mode);

	/* Initialize interfaces (which resolves FWD DB entries */
	for (i = 0; i < args->appl.if_count; i++) {
		initialize_intf(args->appl.if_names[i]);
	}

	/* If we have test streams build them before starting workers */
	resolve_stream_db();
	stream_count = create_stream_db_inputs();

	/*
	 * Create and init worker threads
	 */
	odph_linux_pthread_create(thread_tbl, &cpumask,
				  pktio_thread, NULL);

	/*
	 * If there are streams attempt to verify them else
	 * wait indefinitely
	 */
	if (stream_count) {
		odp_bool_t done;
		do {
			done = verify_stream_db_outputs();
			sleep(1);
		} while (!done);
		printf("All received\n");
	} else {
		odph_linux_pthread_join(thread_tbl, num_workers);
	}

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
	int rc = 0;
	int i;

	static struct option longopts[] = {
		{"count", required_argument, NULL, 'c'},
		{"interface", required_argument, NULL, 'i'},	/* return 'i' */
		{"mode", required_argument, NULL, 'm'},		/* return 'm' */
		{"route", required_argument, NULL, 'r'},	/* return 'r' */
		{"policy", required_argument, NULL, 'p'},	/* return 'p' */
		{"ah", required_argument, NULL, 'a'},		/* return 'a' */
		{"esp", required_argument, NULL, 'e'},		/* return 'e' */
		{"tunnel", required_argument, NULL, 't'},       /* return 't' */
		{"stream", required_argument, NULL, 's'},	/* return 's' */
		{"help", no_argument, NULL, 'h'},		/* return 'h' */
		{NULL, 0, NULL, 0}
	};

	printf("\nParsing command line options\n");

	appl_args->mode = 0;  /* turn off async crypto API by default */

	while (!rc) {
		opt = getopt_long(argc, argv, "+c:i:m:h:r:p:a:e:t:s:",
				  longopts, &long_index);

		if (-1 == opt)
			break;	/* No more options */

		switch (opt) {
		case 'c':
			appl_args->cpu_count = atoi(optarg);
			break;
			/* parse packet-io interface names */
		case 'i':
			len = strlen(optarg);
			if (0 == len) {
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

			if (0 == appl_args->if_count) {
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
			appl_args->mode = atoi(optarg);
			break;

		case 'r':
			rc = create_fwd_db_entry(optarg);
			break;

		case 'p':
			rc = create_sp_db_entry(optarg);
			break;

		case 'a':
			rc = create_sa_db_entry(optarg, FALSE);
			break;

		case 'e':
			rc = create_sa_db_entry(optarg, TRUE);
			break;

		case 't':
			rc = create_tun_db_entry(optarg);
			break;

		case 's':
			rc = create_stream_db_entry(optarg);
			break;

		case 'h':
			usage(argv[0]);
			exit(EXIT_SUCCESS);
			break;

		default:
			break;
		}
	}

	if (rc) {
		printf("ERROR: failed parsing -%c option\n", opt);
		usage(argv[0]);
		exit(EXIT_FAILURE);
	}

	if (0 == appl_args->if_count) {
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

	printf("\n");

	dump_fwd_db();
	dump_sp_db();
	dump_sa_db();
	dump_tun_db();
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
	       "  E.g. %s -i eth1,eth2,eth3 -m 0\n"
	       "\n"
	       "OpenDataPlane example application.\n"
	       "\n"
	       "Mandatory OPTIONS:\n"
	       " -i, --interface Eth interfaces (comma-separated, no spaces)\n"
	       " -m, --mode   0: SYNC\n"
	       "              1: ASYNC_IN_PLACE\n"
	       "              2: ASYNC_NEW_BUFFER\n"
	       "         Default: 0: SYNC api mode\n"
	       "\n"
	       "Routing / IPSec OPTIONS:\n"
	       " -r, --route SubNet:Intf:NextHopMAC\n"
	       " -p, --policy SrcSubNet:DstSubNet:(in|out):(ah|esp|both)\n"
	       " -e, --esp SrcIP:DstIP:(3des|null):SPI:Key192\n"
	       " -a, --ah SrcIP:DstIP:(sha256|md5|null):SPI:Key(256|128)\n"
	       "\n"
	       "  Where: NextHopMAC is raw hex/dot notation, i.e. 03.BA.44.9A.CE.02\n"
	       "         IP is decimal/dot notation, i.e. 192.168.1.1\n"
	       "         SubNet is decimal/dot/slash notation, i.e 192.168.0.0/16\n"
	       "         SPI is raw hex, 32 bits\n"
	       "         KeyXXX is raw hex, XXX bits long\n"
	       "\n"
	       "  Examples:\n"
	       "     -r 192.168.222.0/24:p8p1:08.00.27.F5.8B.DB\n"
	       "     -p 192.168.111.0/24:192.168.222.0/24:out:esp\n"
	       "     -e 192.168.111.2:192.168.222.2:3des:201:656c8523255ccc23a66c1917aa0cf30991fce83532a4b224\n"
	       "     -a 192.168.111.2:192.168.222.2:md5:201:a731649644c5dee92cbd9c2e7e188ee6\n"
	       "\n"
	       "Optional OPTIONS\n"
	       "  -c, --count <number> CPU count.\n"
	       "  -h, --help           Display help and exit.\n"
	       " environment variables: ODP_PKTIO_DISABLE_NETMAP\n"
	       "                        ODP_PKTIO_DISABLE_SOCKET_MMAP\n"
	       "                        ODP_PKTIO_DISABLE_SOCKET_MMSG\n"
	       " can be used to advanced pkt I/O selection for linux-generic\n"
	       "                        ODP_IPSEC_USE_POLL_QUEUES\n"
	       " to enable use of poll queues instead of scheduled (default)\n"
	       "                        ODP_IPSEC_STREAM_VERIFY_MDEQ\n"
	       " to enable use of multiple dequeue for queue draining during\n"
	       " stream verification instead of single dequeue (default)\n"
	       "\n", NO_PATH(progname), NO_PATH(progname)
		);
}
