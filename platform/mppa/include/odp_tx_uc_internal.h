/**
 * @file
 *
 * ODP Rx thread - implementation internal
 */

#ifndef ODP_TX_UC_INTERNAL_H_
#define ODP_TX_UC_INTERNAL_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <odp_packet_io_internal.h>
#include <mppa_noc.h>

#define MAX_PKT_PER_UC		8
#define MAX_JOB_PER_UC          MOS_NB_UC_TRS
#define DNOC_CLUS_IFACE_ID	0

#define END_OF_PACKETS		(1 << 0)

typedef struct eth_uc_job_ctx {
	odp_packet_t pkt_table[MAX_PKT_PER_UC];
	unsigned int pkt_count;
	struct {
		uint8_t nofree : 1;
	};
} tx_uc_job_ctx_t;

typedef struct {
	uint8_t add_header : 1;
	uint8_t exclude_hdr_size : 1;
	uint8_t round_up : 1;
	uint8_t add_end_marker : 1;
} tx_uc_flags_t;

#define TX_UC_FLAGS_DEFAULT { .add_header = 0, .exclude_hdr_size = 0, \
			.round_up = 0, .add_end_marker = 0 }

typedef struct {
	uint8_t init;
	tx_uc_flags_t flags;
	unsigned int dnoc_tx_id;
	unsigned int dnoc_uc_id;
	odp_atomic_u64_t head;
	tx_uc_job_ctx_t job_ctxs[MAX_JOB_PER_UC];
} tx_uc_ctx_t;

int tx_uc_init(tx_uc_ctx_t *uc_ctx_table, int n_uc_ctx,
	       uintptr_t ucode, tx_uc_flags_t flags,
	       uint32_t pointer_mask);
uint64_t tx_uc_alloc_uc_slots(tx_uc_ctx_t *ctx,
			      unsigned int count);

void tx_uc_commit(tx_uc_ctx_t *ctx, uint64_t slot,
		  unsigned int count);
int tx_uc_send_packets(const pkt_tx_uc_config *tx_config,
		       tx_uc_ctx_t *ctx, odp_packet_t pkt_table[],
		       int len, int mtu);
/* Round up packet size to 8B to send more at once */
int tx_uc_send_aligned_packets(const pkt_tx_uc_config *tx_config,
			       tx_uc_ctx_t *ctx, odp_packet_t pkt_table[],
			       int len, int mtu);
void tx_uc_flush(tx_uc_ctx_t *ctx);

#ifdef __cplusplus
}
#endif

#endif
