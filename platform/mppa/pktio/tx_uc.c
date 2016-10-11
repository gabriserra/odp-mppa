/* Copyright (c) 2013, Linaro Limited
 * All rights reserved.
 *
 * SPDX-License-Identifier:     BSD-3-Clause
 */
#include <HAL/hal/hal.h>
#include <odp/errno.h>
#include <errno.h>
#include <mppa_noc.h>

#include "odp_packet_io_internal.h"
#include "odp_pool_internal.h"
#include "odp_tx_uc_internal.h"

extern char _heap_end;

#define COLOR_ID 1
static inline uint64_t get_mOS_uc_event(int uc_id)
{
	mOS_uc_counter_t *trs_ctr = &_scoreboard_start.SCB_UC.event_counter[uc_id];
	return __builtin_k1_ldu((void*)&trs_ctr->_dword);
}

uint64_t tx_uc_alloc_uc_slots(tx_uc_ctx_t *ctx,
			      unsigned int count)
{
	ODP_ASSERT(count <= MAX_JOB_PER_UC);

	const uint64_t head =
		odp_atomic_fetch_add_u64(&ctx->head, count);
	const uint64_t last_id = head + count - 1;
	uint64_t  ev_counter;

	/* Wait for slot */
	ev_counter = get_mOS_uc_event(ctx->dnoc_uc_id);
	while (ev_counter + MAX_JOB_PER_UC <= last_id) {
		odp_spin();
		ev_counter = get_mOS_uc_event(ctx->dnoc_uc_id);
	}

	/* Free previous packets */
	for (uint64_t pos = head; pos < head + count; pos++) {
		if(pos >= MAX_JOB_PER_UC){
			tx_uc_job_ctx_t *job = &ctx->job_ctxs[pos % MAX_JOB_PER_UC];
			unsigned free_base = 0;

			INVALIDATE(job);
			if (!job->pkt_count || job->nofree)
				continue;

			for (unsigned i = 0; i < job->pkt_count; ++i) {
				if (!((odp_packet_hdr_t*)job->pkt_table[i])->nofree)
					continue;

				/* We have a nofree. Free the previous ones */
				odp_packet_free_multi(job->pkt_table + free_base,
						  i - free_base);
				/* Switch free base to the next.
				 * This one is not to be freed */
				free_base = i + 1;
			}
			if (free_base < job->pkt_count)
				odp_packet_free_multi(job->pkt_table + free_base,
						      job->pkt_count  - free_base);
		}
	}
	return head;
}
void tx_uc_commit(tx_uc_ctx_t *ctx, uint64_t slot,
		  unsigned int count)
{
	__builtin_k1_wpurge();
	__builtin_k1_fence ();

	for (unsigned i = 0, pos = slot % MAX_JOB_PER_UC; i < count;
	     ++i, pos = (pos + 1) % MAX_JOB_PER_UC) {
		mOS_uc_transaction_t * const trs =
			&_scoreboard_start.SCB_UC.trs [ctx->dnoc_uc_id][pos];
		mOS_ucore_commit(ctx->dnoc_tx_id, trs);
	}
}

int tx_uc_init(tx_uc_ctx_t *uc_ctx_table, int n_uc_ctx,
	       uintptr_t ucode, int add_header)
{
	int i;
	mppa_noc_ret_t ret;
	mppa_noc_dnoc_uc_configuration_t uc_conf =
		MPPA_NOC_DNOC_UC_CONFIGURATION_INIT;

	uc_conf.program_start = ucode;
	uc_conf.buffer_base = (uintptr_t)&_data_start;
	uc_conf.buffer_size = (uintptr_t)&_heap_end - (uintptr_t)&_data_start;

	for (i = 0; i < n_uc_ctx; i++) {
		if (uc_ctx_table[i].init)
			continue;
		odp_atomic_init_u64(&uc_ctx_table[i].head, 0);
		/* DNoC */
		ret = mppa_noc_dnoc_tx_alloc_auto(DNOC_CLUS_IFACE_ID,
						  &uc_ctx_table[i].dnoc_tx_id,
						  MPPA_NOC_BLOCKING);
		if (ret != MPPA_NOC_RET_SUCCESS)
			return 1;

		ret = mppa_noc_dnoc_uc_alloc_auto(DNOC_CLUS_IFACE_ID,
						  &uc_ctx_table[i].dnoc_uc_id,
						  MPPA_NOC_BLOCKING);
		if (ret != MPPA_NOC_RET_SUCCESS)
			return 1;

		/* We will only use events */
		mppa_noc_disable_interrupt_handler(DNOC_CLUS_IFACE_ID,
						   MPPA_NOC_INTERRUPT_LINE_DNOC_TX,
						   uc_ctx_table[i].dnoc_uc_id);


		ret = mppa_noc_dnoc_uc_link(DNOC_CLUS_IFACE_ID,
					    uc_ctx_table[i].dnoc_uc_id,
					    uc_ctx_table[i].dnoc_tx_id, uc_conf);
		if (ret != MPPA_NOC_RET_SUCCESS)
			return 1;

		for (int j = 0; j < MOS_NB_UC_TRS; j++) {
			mOS_uc_transaction_t  * trs =
				& _scoreboard_start.SCB_UC.trs[uc_ctx_table[i].dnoc_uc_id][j];
			trs->notify._word = 0;
			trs->desc.param_set = 0xff;
			trs->desc.pointer_set = 0xf;
			trs->desc.path_updt = 1;
			trs->desc.vid = COLOR_ID;
		}
		uc_ctx_table[i].add_header = add_header;
		uc_ctx_table[i].init = 1;
	}

	return 0;
}

static int _tx_uc_send_packets(const pkt_tx_uc_config *tx_config,
			       tx_uc_ctx_t *ctx, odp_packet_t pkt_table[],
			       int pkt_count, int mtu, int *err)
{
	const uint64_t head = tx_uc_alloc_uc_slots(ctx, 1);
	const unsigned slot_id = head % MAX_JOB_PER_UC;
	tx_uc_job_ctx_t * job = &ctx->job_ctxs[slot_id];
	mOS_uc_transaction_t * const trs =
		&_scoreboard_start.SCB_UC.trs [ctx->dnoc_uc_id][slot_id];
	const odp_packet_hdr_t * pkt_hdr;

	*err = 0;
	job->nofree = tx_config->nofree;
	for (int i = 0; i < pkt_count; ++i ){
		job->pkt_table[i] = pkt_table[i];
		pkt_hdr = odp_packet_hdr(pkt_table[i]);

		int len = pkt_hdr->frame_len;
		uint8_t * base_addr = (uint8_t*)pkt_hdr->buf_hdr.addr +
			pkt_hdr->headroom;

		if (len > mtu) {
			pkt_count = i;
			*err = EINVAL;
			break;
		}
		if (ctx->add_header){
			mppa_ethernet_header_t *hdr;

			base_addr -= sizeof(*hdr);
			hdr = (mppa_ethernet_header_t*) base_addr;

			hdr->timestamp = 4 * head + i;
			hdr->info.dword = 0ULL;
			hdr->info._.pkt_id = 4 * head + i;
			hdr->info._.pkt_size = len + sizeof(*hdr);
			/* Add the packet end marker */
			if (tx_config->add_end_marker && i == (pkt_count - 1))
				hdr->info._.hash_key |= END_OF_PACKETS;

			len += sizeof(*hdr);
		}

		trs->parameter.array[2 * i + 0] = len / sizeof(uint64_t);
		trs->parameter.array[2 * i + 1] = len % sizeof(uint64_t);

		trs->pointer.array[i] =
			(unsigned long)(base_addr - (uint8_t*)&_data_start);
	}
	for (int i = pkt_count; i < 4; ++i) {
		trs->parameter.array[2 * i + 0] = 0;
		trs->parameter.array[2 * i + 1] = 0;
	}

	trs->path.legacy.header = tx_config->header;
	trs->path.legacy.config = tx_config->config;

	job->pkt_count = pkt_count;

	tx_uc_commit(ctx, head, 1);
	return pkt_count;
}

int tx_uc_send_packets(const pkt_tx_uc_config *tx_config,
		       tx_uc_ctx_t *ctx, odp_packet_t pkt_table[],
		       int len, int mtu)
{
	int sent = 0;
	int pkt_count;

	while(sent < (int)len) {
		int ret, uc_sent;

		pkt_count = (len - sent) > 4 ? 4 :
			(len - sent);

		uc_sent = _tx_uc_send_packets(tx_config, ctx,
						&pkt_table[sent], pkt_count,
						mtu, &ret);
		sent += uc_sent;
		if (ret) {
			if (!sent) {
				__odp_errno = ret;
				return -1;
			}
			return sent;
		}
	}

	return sent;
}

static int _tx_uc_send_aligned_packets(const pkt_tx_uc_config *tx_config,
				       tx_uc_ctx_t *ctx, odp_packet_t pkt_table[],
				       int pkt_count, int mtu, int *err)
{
	const uint64_t head = tx_uc_alloc_uc_slots(ctx, 1);
	const unsigned slot_id = head % MAX_JOB_PER_UC;
	tx_uc_job_ctx_t * job = &ctx->job_ctxs[slot_id];
	mOS_uc_transaction_t * const trs =
		&_scoreboard_start.SCB_UC.trs [ctx->dnoc_uc_id][slot_id];
	const odp_packet_hdr_t * pkt_hdr;

	*err = 0;
	job->nofree = tx_config->nofree;
	for (int i = 0; i < pkt_count; ++i ){
		job->pkt_table[i] = pkt_table[i];
		pkt_hdr = odp_packet_hdr(pkt_table[i]);

		int len = pkt_hdr->frame_len;
		uint8_t * base_addr = (uint8_t*)pkt_hdr->buf_hdr.addr +
			pkt_hdr->headroom;

		if (len > mtu) {
			pkt_count = i;
			*err = EINVAL;
			break;
		}
		if (ctx->add_header){
			mppa_ethernet_header_t *hdr;

			base_addr -= sizeof(*hdr);
			hdr = (mppa_ethernet_header_t*) base_addr;

			hdr->timestamp = 8 * head + i;
			hdr->info.dword = 0ULL;
			hdr->info._.pkt_id = 8 * head + i;
			hdr->info._.pkt_size = len; /* This one is an exception.
						     * It is for the IO thread for the netdev
						     * and they must NOT count the extra header size */
			/* Add the packet end marker */
			if (tx_config->add_end_marker && i == (pkt_count - 1))
				hdr->info._.hash_key |= END_OF_PACKETS;

			len += sizeof(*hdr);
		}
		/* ROund up to superior multiple of 8B */
		len = ( ( len + sizeof(uint64_t) - 1 ) / sizeof(uint64_t) ) * sizeof(uint64_t);
		trs->parameter.array[i] = len / sizeof(uint64_t);

		trs->pointer.array[i] =
			(unsigned long)(base_addr - (uint8_t*)&_data_start);
	}

	for (int i = pkt_count; i < 8; ++i) {
		trs->parameter.array[i] = 0;
	}

	trs->path.legacy.header = tx_config->header;
	trs->path.legacy.config = tx_config->config;

	job->pkt_count = pkt_count;

	tx_uc_commit(ctx, head, 1);
	return pkt_count;
}

int tx_uc_send_aligned_packets(const pkt_tx_uc_config *tx_config,
			       tx_uc_ctx_t *ctx, odp_packet_t pkt_table[],
			       int len, int mtu)
{
	int sent = 0;
	int pkt_count;

	while(sent < (int)len) {
		int ret, uc_sent;

		pkt_count = (len - sent) > 8 ? 8 :
			(len - sent);

		uc_sent = _tx_uc_send_aligned_packets(tx_config, ctx,
						      &pkt_table[sent], pkt_count,
						      mtu, &ret);
		sent += uc_sent;
		if (ret) {
			if (!sent) {
				__odp_errno = ret;
				return -1;
			}
			return sent;
		}
	}

	return sent;
}

void tx_uc_flush(tx_uc_ctx_t *ctx)
{
	const uint64_t head = tx_uc_alloc_uc_slots(ctx, MAX_JOB_PER_UC);

	for (int slot_id = 0; slot_id < MAX_JOB_PER_UC; ++slot_id) {
		tx_uc_job_ctx_t * job = &ctx->job_ctxs[slot_id];
		mOS_uc_transaction_t * const trs =
			&_scoreboard_start.SCB_UC.trs[ctx->dnoc_uc_id][slot_id];
		for (unsigned i = 0; i < 8; ++i ){
			trs->parameter.array[i] = 0;
		}
		job->pkt_count = 0;
		job->nofree = 1;
	}
	tx_uc_commit(ctx, head, MAX_JOB_PER_UC);
}
