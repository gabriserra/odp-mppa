/**
 * @file
 *
 * ODP packet IO - implementation internal
 */

#ifndef ODP_RX_THREAD_INTERNAL_H_
#define ODP_RX_THREAD_INTERNAL_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <odp_buffer_ring_internal.h>

#define N_EV_MASKS 4

#define RX_ETH_IF_BASE 0
#define MAX_RX_ETH_IF 10

#define RX_PCIE_IF_BASE (RX_ETH_IF_BASE + MAX_RX_ETH_IF)
#define MAX_RX_PCIE_IF 8

#define RX_C2C_IF_BASE (RX_PCIE_IF_BASE + MAX_RX_PCIE_IF)
#define MAX_RX_C2C_IF 16

#define RX_IODDR_IF_BASE (RX_C2C_IF_BASE + MAX_RX_C2C_IF)
#define MAX_RX_IODDR_IF 2

#define MAX_RX_IF (MAX_RX_ETH_IF + MAX_RX_PCIE_IF + MAX_RX_C2C_IF + MAX_RX_IODDR_IF)

/** Maximum number of threads dedicated for Ethernet */
#define MAX_RX_THR 6

/** Default number of Rx threads */
#if defined(K1B_EXPLORER)
#define DEF_N_RX_THR 1
#else
#define DEF_N_RX_THR 2
#endif

typedef enum {
	RX_IF_TYPE_ETH,
	RX_IF_TYPE_PCI,
	RX_IF_TYPE_C2C,
	RX_IF_TYPE_IODDR,
} rx_if_type_e;

typedef struct {
	uint8_t pktio_id;        /**< Unique pktio [0..MAX_RX_IF[ */
	odp_pool_t pool;         /**< pool to alloc packets from */
	uint8_t dma_if;          /**< DMA Rx Interface */
	uint8_t min_port;
	uint8_t max_port;
	uint8_t header_sz;
	uint8_t pkt_offset;
	rx_if_type_e if_type;
	odp_buffer_ring_t *ring;
	struct {
		uint8_t flow_controlled : 1;
	};
} rx_config_t;

union mppa_ethernet_header_info_t {
	mppa_uint64 dword;
	mppa_uint32 word[2];
	mppa_uint16 hword[4];
	mppa_uint8 bword[8];
	struct {
		mppa_uint32 pkt_size : 16;
		mppa_uint32 hash_key : 16;
		mppa_uint32 lane_id  : 2;
		mppa_uint32 io_id    : 1;
		mppa_uint32 rule_id  : 4;
		mppa_uint32 pkt_id   : 25;
	} _;
};

typedef struct mppa_ethernet_header_s {
	mppa_uint64 timestamp;
	union mppa_ethernet_header_info_t info;
} mppa_ethernet_header_t;


typedef struct {
	int nRx;
	int rr_policy;
	int rr_offset;
	int flow_controlled;
	int min_rx;
	int max_rx;
} rx_opts_t;

static inline void mppa_ethernet_header_print(const mppa_ethernet_header_t *hdr)
{
	printf("EthPkt %p => {Size=%d,Hash=%d,Lane=%d,IO=%d,Rule=%03d,Id=%04d} @ %llu\n",
	       hdr,
	       hdr->info._.pkt_size,
	       hdr->info._.hash_key,
	       hdr->info._.lane_id,
	       hdr->info._.io_id,
	       hdr->info._.rule_id,
	       hdr->info._.pkt_id, hdr->timestamp);
}

int rx_thread_init(void);
int rx_thread_link_open(rx_config_t *rx_config, const rx_opts_t *opts);
int rx_thread_link_close(uint8_t pktio_id);
int rx_thread_destroy(void);
int rx_thread_fetch_stats(uint8_t pktio_id, uint64_t *dropped,
			  uint64_t *oom);
void rx_options_default(rx_opts_t *options);
int rx_parse_options(const char **str, rx_opts_t *options);

#ifdef __cplusplus
}
#endif

#endif
