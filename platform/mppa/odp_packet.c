/* Copyright (c) 2013, Linaro Limited
 * All rights reserved.
 *
 * SPDX-License-Identifier:     BSD-3-Clause
 */

#include <odp/packet.h>
#include <odp_packet_internal.h>
#include <odp_debug_internal.h>
#include <odp/hints.h>
#include <odp/byteorder.h>

#include <odp/helper/eth.h>
#include <odp/helper/ip.h>
#include <odp/helper/tcp.h>
#include <odp/helper/udp.h>

#include <string.h>
#include <stdio.h>
#include <errno.h>
#include "odp_rx_internal.h"

/*
 *
 * Alloc and free
 * ********************************************************
 *
 */

static inline void packet_parse_disable(odp_packet_hdr_t *pkt_hdr)
{
	pkt_hdr->input_flags.parsed_l2  = 1;
	pkt_hdr->input_flags.parsed_all = 1;
}

void packet_parse_reset(odp_packet_hdr_t *pkt_hdr)
{
	/* Reset parser metadata before new parse */
	pkt_hdr->error_flags.all  = 0;
	pkt_hdr->input_flags.all  = 0;
	pkt_hdr->output_flags.all = 0;
	pkt_hdr->l2_offset        = 0;
	pkt_hdr->l3_offset        = ODP_PACKET_OFFSET_INVALID;
	pkt_hdr->l4_offset        = ODP_PACKET_OFFSET_INVALID;
	pkt_hdr->payload_offset   = ODP_PACKET_OFFSET_INVALID;
	pkt_hdr->vlan_s_tag       = 0;
	pkt_hdr->vlan_c_tag       = 0;
	pkt_hdr->l3_protocol      = 0;
	pkt_hdr->l4_protocol      = 0;
}

void _odp_packet_mark_nofree(odp_packet_t pkt)
{
	odp_packet_hdr_t *pkt_hdr = (odp_packet_hdr_t *)pkt;
	odp_atomic_store_u32(&pkt_hdr->nofree, 1);
}

/**
 * Initialize packet buffer
 */
void packet_init(pool_entry_t *pool, odp_packet_hdr_t *pkt_hdr,
		 size_t size, int parse)
{
       /*
	* Reset parser metadata.  Note that we clear via memset to make
	* this routine indepenent of any additional adds to packet metadata.
	*/
	const size_t start_offset = ODP_FIELD_SIZEOF(odp_packet_hdr_t, buf_hdr);
	uint8_t *start;
	size_t len;

	start = (uint8_t *)pkt_hdr + start_offset;
	len = sizeof(odp_packet_hdr_t) - start_offset;
	memset(start, 0, len);

	memset(pkt_hdr->sub_packets, 0,
	       sizeof(odp_packet_hdr_t*) * _ODP_MAX_SUBPACKETS);

	/* Set metadata items that initialize to non-zero values */
	pkt_hdr->l2_offset = ODP_PACKET_OFFSET_INVALID;
	pkt_hdr->l3_offset = ODP_PACKET_OFFSET_INVALID;
	pkt_hdr->l4_offset = ODP_PACKET_OFFSET_INVALID;
	pkt_hdr->payload_offset = ODP_PACKET_OFFSET_INVALID;
	odp_atomic_init_u32(&pkt_hdr->nofree, 0);

 	/* Disable lazy parsing on user allocated packets */
	if (!parse)
		packet_parse_disable(pkt_hdr);

	/*
	* Packet headroom is set from the pool's headroom
	* Packet tailroom is rounded up to fill the last
	* segment occupied by the allocated length.
	*/
	pkt_hdr->frame_len = size;
	pkt_hdr->headroom  = pool->s.headroom;
	pkt_hdr->tailroom  = pool->s.seg_size -
		(pool->s.headroom + size);

	/* Force MPPA header to 0 to detect spurious EOT */
	mppa_ethernet_header_t *header;
	header = (mppa_ethernet_header_t*)
		(((uint8_t *)pkt_hdr->buf_hdr.addr) +
		 pkt_hdr->headroom - sizeof(*header));
	memset(header, 0, sizeof(*header));
}
odp_packet_t odp_packet_alloc(odp_pool_t pool_hdl, uint32_t len)
{
	pool_entry_t *pool = odp_pool_to_entry(pool_hdl);
	size_t pkt_size = len ? len : pool->s.params.buf.size;
	odp_packet_t pkt = ODP_PACKET_INVALID;

	if (pool->s.params.type != ODP_POOL_PACKET)
		return pkt;

	if (buffer_alloc(pool_hdl, pkt_size, (odp_buffer_hdr_t **)&pkt, 1) != 1)
		return ODP_PACKET_INVALID;

	packet_init(pool, (odp_packet_hdr_t*)pkt, pkt_size, 0);

	if (len == 0)
		pull_tail(odp_packet_hdr(pkt), pkt_size);

	return pkt;
}

int odp_packet_alloc_multi(odp_pool_t pool_hdl, uint32_t len,
			   odp_packet_t pkt[], int num)
{
	pool_entry_t *pool = odp_pool_to_entry(pool_hdl);
	size_t pkt_size = len ? len : pool->s.params.buf.size;
	int count, i;

	if (pool->s.params.type != ODP_POOL_PACKET) {
		__odp_errno = EINVAL;
		return -1;
	}

	count = buffer_alloc(pool_hdl, pkt_size, (odp_buffer_hdr_t **)pkt, num);

	for (i = 0; i < count; ++i) {
		odp_packet_hdr_t *pkt_hdr = odp_packet_hdr(pkt[i]);

		packet_init(pool, pkt_hdr, pkt_size, 0);
		if (len == 0)
			pull_tail(pkt_hdr, pkt_size);
	}

	return count;
}

void odp_packet_free(odp_packet_t pkt)
{
	odp_packet_hdr_t *const pkt_hdr = (odp_packet_hdr_t *)(pkt);
	if (odp_global_data.enable_pkt_nofree)
		if (_odp_atomic_u32_fetch_sub_mm(&pkt_hdr->nofree, 1, 0) != 0)
			return;

	if (pkt_hdr->input != ODP_PKTIO_INVALID){
		ret_buf(&((pool_entry_t*)pkt_hdr->buf_hdr.pool_hdl)->s,
			(odp_buffer_hdr_t **)&pkt_hdr, 1);
	} else {
		odp_buffer_free((odp_buffer_t)pkt);
	}
}

void odp_packet_free_multi(const odp_packet_t pkt[], int num)
{
	int free_base = 0;
	if (odp_global_data.enable_pkt_nofree) {
		for (int i = 0; i < num; ++i) {
			/* If it's 1 someone else will free it. If it's -1 someone else freed
			 * it and it should not have happened. */
			if (_odp_atomic_u32_fetch_sub_mm(&odp_packet_hdr(pkt[i])->nofree, 1, 0) == 0)
				continue;

			/* We should not free this one. Free previous ones and skip to next */
			odp_buffer_free_multi((const odp_buffer_t *)(pkt + free_base), i - free_base);
			free_base = i + 1;
		}
	}
	if (free_base < num)
		odp_buffer_free_multi((const odp_buffer_t *)(pkt + free_base), num - free_base);
}

int odp_packet_reset(odp_packet_t pkt, uint32_t len)
{
	odp_packet_hdr_t *const pkt_hdr = (odp_packet_hdr_t *)(pkt);
	pool_entry_t *pool = odp_pool_to_entry(pkt_hdr->buf_hdr.pool_hdl);
	uint32_t totsize = pool->s.headroom + len + pool->s.tailroom;

	if (totsize > pkt_hdr->buf_hdr.size)
		return -1;

	packet_init(pool, pkt_hdr, len, 0);
	return 0;
}

/*
 *
 * Pointers and lengths
 * ********************************************************
 *
 */

#if _ODP_LOG2MAX_FRAGS != 0
int odp_packet_is_segmented(odp_packet_t pkt)
{
	odp_packet_hdr_t *pkt_hdr = (odp_packet_hdr_t *)pkt;
	return pkt_hdr->sub_packets[0] != NULL;
}

int odp_packet_num_segs(odp_packet_t pkt)
{
	int i;
	odp_packet_hdr_t *pkt_hdr = (odp_packet_hdr_t *)pkt;

	for (i = 0; i < _ODP_MAX_SUBPACKETS; ++i) {
		if (!pkt_hdr->sub_packets[i])
			return i + 1;
	}
	return 1 ;
}
int _odp_packet_fragment(odp_packet_t pkt,
			 odp_packet_t sub_pkts[_ODP_MAX_FRAGS])
{
	int i, count = 1;
	odp_packet_hdr_t *pkt_hdr = (odp_packet_hdr_t *)pkt;
	sub_pkts[0] = pkt;

	for (i = 0; i < _ODP_MAX_SUBPACKETS; ++i) {
		if(!pkt_hdr->sub_packets[i])
			break;

		sub_pkts[count++] = pkt_hdr->sub_packets[i];
	}

	memset(pkt_hdr->sub_packets, 0,
	       sizeof(odp_packet_hdr_t*) * _ODP_MAX_SUBPACKETS);

	return count;
}
#endif

void *odp_packet_head(odp_packet_t pkt)
{
	odp_packet_hdr_t *pkt_hdr = (odp_packet_hdr_t *)pkt;

	return buffer_map(&pkt_hdr->buf_hdr, 0, NULL, 0);
}

uint32_t odp_packet_buf_len(odp_packet_t pkt)
{
	return ((odp_packet_hdr_t *)pkt)->buf_hdr.size;
}

void *odp_packet_data(odp_packet_t pkt)
{
	odp_packet_hdr_t *pkt_hdr = (odp_packet_hdr_t *)pkt;

	return packet_map(pkt_hdr, 0, NULL);
}

uint32_t odp_packet_seg_len(odp_packet_t pkt)
{
	odp_packet_hdr_t *pkt_hdr = (odp_packet_hdr_t *)pkt;
	uint32_t seglen;

	/* Call returns length of 1st data segment */
	packet_map(pkt_hdr, 0, &seglen);
	return seglen;
}

uint32_t odp_packet_len(odp_packet_t pkt)
{
	return ((odp_packet_hdr_t *)pkt)->frame_len;
}

uint32_t odp_packet_headroom(odp_packet_t pkt)
{
	return ((odp_packet_hdr_t *)pkt)->headroom;
}

uint32_t odp_packet_tailroom(odp_packet_t pkt)
{
	return ((odp_packet_hdr_t *)pkt)->tailroom;
}

void *odp_packet_tail(odp_packet_t pkt)
{
	odp_packet_hdr_t *pkt_hdr = (odp_packet_hdr_t *)pkt;

	return packet_map(pkt_hdr, pkt_hdr->frame_len, NULL);
}

void *odp_packet_push_head(odp_packet_t pkt, uint32_t len)
{
	odp_packet_hdr_t *pkt_hdr = (odp_packet_hdr_t *)pkt;

	if (len > pkt_hdr->headroom)
		return NULL;

	push_head(pkt_hdr, len);
	return packet_map(pkt_hdr, 0, NULL);
}

void *odp_packet_pull_head(odp_packet_t pkt, uint32_t len)
{
	odp_packet_hdr_t *pkt_hdr = (odp_packet_hdr_t *)pkt;

	if (len > pkt_hdr->frame_len)
		return NULL;

	pull_head(pkt_hdr, len);
	return packet_map(pkt_hdr, 0, NULL);
}

void *odp_packet_push_tail(odp_packet_t pkt, uint32_t len)
{
	odp_packet_hdr_t *pkt_hdr = (odp_packet_hdr_t *)pkt;
	uint32_t origin = pkt_hdr->frame_len;

	if (len > pkt_hdr->tailroom)
		return NULL;

	push_tail(pkt_hdr, len);
	return packet_map(pkt_hdr, origin, NULL);
}

void *odp_packet_pull_tail(odp_packet_t pkt, uint32_t len)
{
	odp_packet_hdr_t *pkt_hdr = (odp_packet_hdr_t *)pkt;

	if (len > pkt_hdr->frame_len)
		return NULL;

	pull_tail(pkt_hdr, len);
	return packet_map(pkt_hdr, pkt_hdr->frame_len, NULL);
}

void *odp_packet_offset(odp_packet_t pkt, uint32_t offset, uint32_t *len,
			odp_packet_seg_t *seg)
{
	odp_packet_hdr_t *pkt_hdr = (odp_packet_hdr_t *)pkt;
	void *addr = packet_map(pkt_hdr, offset, len);

	if (addr && seg)
		*seg = (odp_packet_seg_t)pkt;

	return addr;
}

/*
 *
 * Meta-data
 * ********************************************************
 *
 */

odp_pool_t odp_packet_pool(odp_packet_t pkt)
{
	return ((odp_packet_hdr_t *)pkt)->buf_hdr.pool_hdl;
}

odp_pktio_t odp_packet_input(odp_packet_t pkt)
{
	return ((odp_packet_hdr_t *)pkt)->input;
}

void *odp_packet_user_ptr(odp_packet_t pkt)
{
	return ((odp_packet_hdr_t *)pkt)->buf_hdr.buf_ctx;
}

void odp_packet_user_ptr_set(odp_packet_t pkt, const void *ctx)
{
	((odp_packet_hdr_t *)pkt)->buf_hdr.buf_cctx = ctx;
}

void *odp_packet_user_area(odp_packet_t pkt)
{
	return ((odp_packet_hdr_t *)pkt)->buf_hdr.uarea_addr;
}

uint32_t odp_packet_user_area_size(odp_packet_t pkt)
{
	return odp_pool_to_entry(odp_packet_pool(pkt))->s.udata_size;
}

void *odp_packet_l2_ptr(odp_packet_t pkt, uint32_t *len)
{
	odp_packet_hdr_t *pkt_hdr = (odp_packet_hdr_t *)pkt;
	return packet_map(pkt_hdr, pkt_hdr->l2_offset, len);
}

uint32_t odp_packet_l2_offset(odp_packet_t pkt)
{
	odp_packet_hdr_t *pkt_hdr = (odp_packet_hdr_t *)pkt;
	return pkt_hdr->l2_offset;
}

int odp_packet_l2_offset_set(odp_packet_t pkt, uint32_t offset)
{
	odp_packet_hdr_t *pkt_hdr = (odp_packet_hdr_t *)pkt;

	if (offset >= pkt_hdr->frame_len)
		return -1;


	pkt_hdr->l2_offset = offset;
	return 0;
}

void *odp_packet_l3_ptr(odp_packet_t pkt, uint32_t *len)
{
	odp_packet_hdr_t *pkt_hdr = (odp_packet_hdr_t *)pkt;

	if (packet_parse_not_complete(pkt_hdr))
		packet_parse_full(pkt_hdr);
	return packet_map(pkt_hdr, pkt_hdr->l3_offset, len);
}

uint32_t odp_packet_l3_offset(odp_packet_t pkt)
{
	odp_packet_hdr_t *pkt_hdr = (odp_packet_hdr_t *)pkt;

	if (packet_parse_not_complete(pkt_hdr))
		packet_parse_full(pkt_hdr);
	return pkt_hdr->l3_offset;
}

int odp_packet_l3_offset_set(odp_packet_t pkt, uint32_t offset)
{
	odp_packet_hdr_t *pkt_hdr = (odp_packet_hdr_t *)pkt;

	if (offset >= pkt_hdr->frame_len)
		return -1;

	if (packet_parse_not_complete(pkt_hdr))
		packet_parse_full(pkt_hdr);
	pkt_hdr->l3_offset = offset;
	return 0;
}

void *odp_packet_l4_ptr(odp_packet_t pkt, uint32_t *len)
{
	odp_packet_hdr_t *pkt_hdr = (odp_packet_hdr_t *)pkt;

	if (packet_parse_not_complete(pkt_hdr))
		packet_parse_full(pkt_hdr);
	return packet_map(pkt_hdr, pkt_hdr->l4_offset, len);
}

uint32_t odp_packet_l4_offset(odp_packet_t pkt)
{
	odp_packet_hdr_t *pkt_hdr = (odp_packet_hdr_t *)pkt;

	if (packet_parse_not_complete(pkt_hdr))
		packet_parse_full(pkt_hdr);
	return pkt_hdr->l4_offset;
}

int odp_packet_l4_offset_set(odp_packet_t pkt, uint32_t offset)
{
	odp_packet_hdr_t *pkt_hdr = (odp_packet_hdr_t *)pkt;

	if (offset >= pkt_hdr->frame_len)
		return -1;

	if (packet_parse_not_complete(pkt_hdr))
		packet_parse_full(pkt_hdr);
	pkt_hdr->l4_offset = offset;
	return 0;
}

uint32_t odp_packet_flow_hash(odp_packet_t pkt)
{
	odp_packet_hdr_t *pkt_hdr = odp_packet_hdr(pkt);

	return pkt_hdr->flow_hash;
}

void odp_packet_flow_hash_set(odp_packet_t pkt, uint32_t flow_hash)
{
	odp_packet_hdr_t *pkt_hdr = odp_packet_hdr(pkt);

	pkt_hdr->flow_hash = flow_hash;
	pkt_hdr->has_hash = 1;
}

/*
 *
 * Segment level
 * ********************************************************
 *
 */

void *odp_packet_seg_buf_addr(odp_packet_t pkt,
			      odp_packet_seg_t seg ODP_UNUSED)
{
	odp_packet_hdr_t *pkt_hdr = (odp_packet_hdr_t *)pkt;

	return buffer_map(&pkt_hdr->buf_hdr, 0, NULL,
			  pkt_hdr->headroom + pkt_hdr->frame_len);
}

uint32_t odp_packet_seg_buf_len(odp_packet_t pkt,
				odp_packet_seg_t seg ODP_UNUSED)
{
	return ((odp_packet_hdr_t *)pkt)->buf_hdr.size;
}

void *odp_packet_seg_data(odp_packet_t pkt,
			  odp_packet_seg_t seg ODP_UNUSED)
{
	odp_packet_hdr_t *pkt_hdr = (odp_packet_hdr_t *)pkt;

	return buffer_map(&pkt_hdr->buf_hdr, pkt_hdr->headroom, NULL,
			  pkt_hdr->headroom + pkt_hdr->frame_len);
}

uint32_t odp_packet_seg_data_len(odp_packet_t pkt,
				 odp_packet_seg_t seg ODP_UNUSED)
{
	odp_packet_hdr_t *pkt_hdr = (odp_packet_hdr_t *)pkt;
	uint32_t seglen = 0;

	buffer_map(&pkt_hdr->buf_hdr, pkt_hdr->headroom, &seglen,
		   pkt_hdr->headroom + pkt_hdr->frame_len);

	return seglen;
}

/*
 *
 * Manipulation
 * ********************************************************
 *
 */

odp_packet_t odp_packet_add_data(odp_packet_t pkt, uint32_t offset,
				 uint32_t len)
{
	odp_packet_hdr_t *pkt_hdr = (odp_packet_hdr_t *)pkt;
	uint32_t pktlen = pkt_hdr->frame_len;
	odp_packet_t newpkt;

	if (offset > pktlen)
		return ODP_PACKET_INVALID;

	newpkt = odp_packet_alloc(pkt_hdr->buf_hdr.pool_hdl, pktlen + len);

	if (newpkt != ODP_PACKET_INVALID) {
		if (_odp_packet_copy_to_packet(pkt, 0,
					       newpkt, 0, offset) != 0 ||
		    _odp_packet_copy_to_packet(pkt, offset, newpkt,
					       offset + len,
					       pktlen - offset) != 0) {
			odp_packet_free(newpkt);
			newpkt = ODP_PACKET_INVALID;
		} else {
			_odp_packet_copy_md_to_packet(pkt, newpkt);
			odp_packet_free(pkt);
		}
	}

	return newpkt;
}

odp_packet_t odp_packet_rem_data(odp_packet_t pkt, uint32_t offset,
				 uint32_t len)
{
	odp_packet_hdr_t *pkt_hdr = (odp_packet_hdr_t *)pkt;
	uint32_t pktlen = pkt_hdr->frame_len;
	odp_packet_t newpkt;

	if (offset > pktlen || offset + len > pktlen)
		return ODP_PACKET_INVALID;

	newpkt = odp_packet_alloc(pkt_hdr->buf_hdr.pool_hdl, pktlen - len);

	if (newpkt != ODP_PACKET_INVALID) {
		if (_odp_packet_copy_to_packet(pkt, 0,
					       newpkt, 0, offset) != 0 ||
		    _odp_packet_copy_to_packet(pkt, offset + len,
					       newpkt, offset,
					       pktlen - offset - len) != 0) {
			odp_packet_free(newpkt);
			newpkt = ODP_PACKET_INVALID;
		} else {
			_odp_packet_copy_md_to_packet(pkt, newpkt);
			odp_packet_free(pkt);
		}
	}

	return newpkt;
}

/*
 *
 * Copy
 * ********************************************************
 *
 */

odp_packet_t odp_packet_copy(odp_packet_t pkt, odp_pool_t pool)
{
	odp_packet_hdr_t *srchdr = (odp_packet_hdr_t *)pkt;
	uint32_t pktlen = srchdr->frame_len;
	uint32_t meta_offset = ODP_FIELD_SIZEOF(odp_packet_hdr_t, buf_hdr);
	odp_packet_t newpkt = odp_packet_alloc(pool, pktlen);

	if (newpkt != ODP_PACKET_INVALID) {
		odp_packet_hdr_t *newhdr = odp_packet_hdr(newpkt);
		uint8_t *newstart, *srcstart;

		/* Must copy metadata first, followed by packet data */
		newstart = (uint8_t *)newhdr + meta_offset;
		srcstart = (uint8_t *)srchdr + meta_offset;

		memcpy(newstart, srcstart,
		       sizeof(odp_packet_hdr_t) - meta_offset);

		if (_odp_packet_copy_to_packet(pkt, 0,
					       newpkt, 0, pktlen) != 0) {
			odp_packet_free(newpkt);
			newpkt = ODP_PACKET_INVALID;
		}
	}

	return newpkt;
}

int odp_packet_copydata_out(odp_packet_t pkt, uint32_t offset,
			    uint32_t len, void *dst)
{
	void *mapaddr;
	uint32_t seglen = 0; /* GCC */
	uint32_t cpylen;
	uint8_t *dstaddr = (uint8_t *)dst;
	odp_packet_hdr_t *pkt_hdr = (odp_packet_hdr_t *)pkt;

	if (offset + len > pkt_hdr->frame_len)
		return -1;

	while (len > 0) {
		mapaddr = packet_map(pkt_hdr, offset, &seglen);
		cpylen = len > seglen ? seglen : len;
		memcpy(dstaddr, mapaddr, cpylen);
		offset  += cpylen;
		dstaddr += cpylen;
		len     -= cpylen;
	}

	return 0;
}

int odp_packet_copydata_in(odp_packet_t pkt, uint32_t offset,
			   uint32_t len, const void *src)
{
	void *mapaddr;
	uint32_t seglen = 0; /* GCC */
	uint32_t cpylen;
	const uint8_t *srcaddr = (const uint8_t *)src;
	odp_packet_hdr_t *pkt_hdr = (odp_packet_hdr_t *)pkt;

	if (offset + len > pkt_hdr->frame_len)
		return -1;

	while (len > 0) {
		mapaddr = packet_map(pkt_hdr, offset, &seglen);
		cpylen = len > seglen ? seglen : len;
		memcpy(mapaddr, srcaddr, cpylen);
		offset  += cpylen;
		srcaddr += cpylen;
		len     -= cpylen;
	}

	return 0;
}

/*
 *
 * Debugging
 * ********************************************************
 *
 */

void odp_packet_print(odp_packet_t pkt)
{
	int max_len = 512;
	char str[max_len];
	int len = 0;
	int n = max_len - 1;
	odp_packet_hdr_t *hdr = (odp_packet_hdr_t *)pkt;

	len += snprintf(&str[len], n - len, "Packet ");
	len += odp_buffer_snprint(&str[len], n - len, (odp_buffer_t)pkt);
	len += snprintf(&str[len], n - len,
			"  input_flags  0x%" PRIx32 "\n", hdr->input_flags.all);
	len += snprintf(&str[len], n - len,
			"  error_flags  0x%x\n", hdr->error_flags.all);
	len += snprintf(&str[len], n - len,
			"  output_flags 0x%x\n", hdr->output_flags.all);
	len += snprintf(&str[len], n - len,
			"  l2_offset    %u\n", hdr->l2_offset);
	len += snprintf(&str[len], n - len,
			"  l3_offset    %u\n", hdr->l3_offset);
	len += snprintf(&str[len], n - len,
			"  l4_offset    %u\n", hdr->l4_offset);
	len += snprintf(&str[len], n - len,
			"  frame_len    %u\n", hdr->frame_len);
	len += snprintf(&str[len], n - len,
			"  input        %" PRIu64 "\n",
			odp_pktio_to_u64(hdr->input));
	str[len] = '\0';

	ODP_PRINT("\n%s\n", str);
}

int odp_packet_is_valid(odp_packet_t pkt)
{
	odp_buffer_hdr_t *buf = validate_buf((odp_buffer_t)pkt);

	return (buf && buf->type == ODP_EVENT_PACKET);
}

/*
 *
 * Internal Use Routines
 * ********************************************************
 *
 */

void _odp_packet_copy_md_to_packet(odp_packet_t srcpkt, odp_packet_t dstpkt)
{
	odp_packet_hdr_t *srchdr = odp_packet_hdr(srcpkt);
	odp_packet_hdr_t *dsthdr = odp_packet_hdr(dstpkt);

	dsthdr->input = srchdr->input;
	dsthdr->buf_hdr.buf_ctx = srchdr->buf_hdr.buf_ctx;
	if (dsthdr->buf_hdr.uarea_addr &&
	    srchdr->buf_hdr.uarea_addr) {
		uint16_t src_size = odp_pool_to_entry(srchdr->buf_hdr.pool_hdl)
			->s.udata_size;
		uint16_t dst_size = odp_pool_to_entry(dsthdr->buf_hdr.pool_hdl)
			->s.udata_size;

		memcpy(dsthdr->buf_hdr.uarea_addr,
		       srchdr->buf_hdr.uarea_addr,
		       dst_size <= src_size ? dst_size : src_size);
	}
	odp_atomic_store_u32(
		&dsthdr->buf_hdr.ref_count,
		odp_atomic_load_u32(
			&srchdr->buf_hdr.ref_count));
	copy_packet_parser_metadata(srchdr, dsthdr);
}

int _odp_packet_copy_to_packet(odp_packet_t srcpkt, uint32_t srcoffset,
			       odp_packet_t dstpkt, uint32_t dstoffset,
			       uint32_t len)
{
	odp_packet_hdr_t *srchdr = odp_packet_hdr(srcpkt);
	odp_packet_hdr_t *dsthdr = odp_packet_hdr(dstpkt);
	void *srcmap;
	void *dstmap;
	uint32_t cpylen, minseg;
	uint32_t srcseglen = 0; /* GCC */
	uint32_t dstseglen = 0; /* GCC */

	if (srcoffset + len > srchdr->frame_len ||
	    dstoffset + len > dsthdr->frame_len)
		return -1;

	while (len > 0) {
		srcmap = packet_map(srchdr, srcoffset, &srcseglen);
		dstmap = packet_map(dsthdr, dstoffset, &dstseglen);

		minseg = dstseglen > srcseglen ? srcseglen : dstseglen;
		cpylen = len > minseg ? minseg : len;
		memcpy(dstmap, srcmap, cpylen);

		srcoffset += cpylen;
		dstoffset += cpylen;
		len       -= cpylen;
	}

	return 0;
}

odp_packet_t _odp_packet_alloc(odp_pool_t pool_hdl)
{
	pool_entry_t *pool = odp_pool_to_entry(pool_hdl);
	odp_packet_t pkt = ODP_PACKET_INVALID;

	if (pool->s.params.type != ODP_POOL_PACKET)
		return pkt;

	buffer_alloc(pool_hdl, pool->s.params.buf.size,
		     (odp_buffer_hdr_t **)&pkt, 1);

	return pkt;
}

/**
 * Parser helper function for IPv4
 */
static inline uint8_t parse_ipv4(odp_packet_hdr_t *pkt_hdr,
				 odp_packet_parsing_ctx_t *ctx)
{
	const odph_ipv4hdr_t *ipv4 = (const odph_ipv4hdr_t *)ctx->parseptr;
	uint8_t ver = ODPH_IPV4HDR_VER(ipv4->ver_ihl);
	uint8_t ihl = ODPH_IPV4HDR_IHL(ipv4->ver_ihl);
	uint16_t frag_offset;

	ctx->l3_len = odp_be_to_cpu_16(ipv4->tot_len);

	if (odp_unlikely(ihl < ODPH_IPV4HDR_IHL_MIN) ||
	    odp_unlikely(ver != 4) ||
	    (ctx->l3_len > pkt_hdr->frame_len - ctx->offset)) {
		pkt_hdr->error_flags.ip_err = 1;
		return 0;
	}

	ctx->offset   += ihl * 4;
	ctx->parseptr += ihl * 4;

	if (odp_unlikely(ihl > ODPH_IPV4HDR_IHL_MIN))
		pkt_hdr->input_flags.ipopt = 1;

	/* A packet is a fragment if:
	*  "more fragments" flag is set (all fragments except the last)
	*     OR
	*  "fragment offset" field is nonzero (all fragments except the first)
	*/
	frag_offset = odp_be_to_cpu_16(ipv4->frag_offset);
	if (odp_unlikely(ODPH_IPV4HDR_IS_FRAGMENT(frag_offset)))
		pkt_hdr->input_flags.ipfrag = 1;

	return ipv4->proto;
}

/**
 * Parser helper function for IPv6
 */
static inline uint8_t parse_ipv6(odp_packet_hdr_t *pkt_hdr,
				 odp_packet_parsing_ctx_t *ctx)
{
	const odph_ipv6hdr_t *ipv6 = (const odph_ipv6hdr_t *)ctx->parseptr;
	const odph_ipv6hdr_ext_t *ipv6ext;

	ctx->l3_len = odp_be_to_cpu_16(ipv6->payload_len);

	/* Basic sanity checks on IPv6 header */
	if ((odp_be_to_cpu_32(ipv6->ver_tc_flow) >> 28) != 6 ||
	    ctx->l3_len > pkt_hdr->frame_len - ctx->offset) {
		pkt_hdr->error_flags.ip_err = 1;
		return 0;
	}

	/* Skip past IPv6 header */
	ctx->offset   += sizeof(odph_ipv6hdr_t);
	ctx->parseptr += sizeof(odph_ipv6hdr_t);

	/* Skip past any IPv6 extension headers */
	if (ipv6->next_hdr == ODPH_IPPROTO_HOPOPTS ||
	    ipv6->next_hdr == ODPH_IPPROTO_ROUTE) {
		pkt_hdr->input_flags.ipopt = 1;

		do  {
			ipv6ext    = (const odph_ipv6hdr_ext_t *)ctx->parseptr;
			uint16_t extlen = 8 + ipv6ext->ext_len * 8;

			ctx->offset   += extlen;
			ctx->parseptr += extlen;
		} while ((ipv6ext->next_hdr == ODPH_IPPROTO_HOPOPTS ||
			  ipv6ext->next_hdr == ODPH_IPPROTO_ROUTE) &&
			ctx->offset < pkt_hdr->frame_len);

		if (ctx->offset >= pkt_hdr->l3_offset +
		    odp_be_to_cpu_16(ipv6->payload_len)) {
			pkt_hdr->error_flags.ip_err = 1;
			return 0;
		}

		if (ipv6ext->next_hdr == ODPH_IPPROTO_FRAG)
			pkt_hdr->input_flags.ipfrag = 1;

		return ipv6ext->next_hdr;
	}

	if (odp_unlikely(ipv6->next_hdr == ODPH_IPPROTO_FRAG)) {
		pkt_hdr->input_flags.ipopt = 1;
		pkt_hdr->input_flags.ipfrag = 1;
	}

	return ipv6->next_hdr;
}

/**
 * Parser helper function for TCP
 */
static inline void parse_tcp(odp_packet_hdr_t *pkt_hdr,
			     odp_packet_parsing_ctx_t *ctx)
{
	const odph_tcphdr_t *tcp = (const odph_tcphdr_t *)ctx->parseptr;

	if (tcp->hl < sizeof(odph_tcphdr_t) / sizeof(uint32_t))
		pkt_hdr->error_flags.tcp_err = 1;
	else if ((uint32_t)tcp->hl * 4 > sizeof(odph_tcphdr_t))
		pkt_hdr->input_flags.tcpopt = 1;

	ctx->l4_len = ctx->l3_len +
		pkt_hdr->l3_offset - pkt_hdr->l4_offset;

	ctx->offset   += (uint32_t)tcp->hl * 4;
	ctx->parseptr += (uint32_t)tcp->hl * 4;
}

/**
 * Parser helper function for UDP
 */
static inline void parse_udp(odp_packet_hdr_t *pkt_hdr,
			     odp_packet_parsing_ctx_t *ctx)
{
	const odph_udphdr_t *udp = (const odph_udphdr_t *)ctx->parseptr;
	uint32_t udplen = odp_be_to_cpu_16(udp->length);

	if (udplen < sizeof(odph_udphdr_t) ||
	    udplen > (uint32_t)(ctx->l3_len +
				pkt_hdr->l3_offset - pkt_hdr->l4_offset)) {
		pkt_hdr->error_flags.udp_err = 1;
	}

	ctx->l4_len = udplen;

	ctx->offset   += sizeof(odph_udphdr_t);
	ctx->parseptr += sizeof(odph_udphdr_t);
}

/**
 * Initialize L2 related parser flags and metadata
 */
void packet_parse_l2(odp_packet_hdr_t *pkt_hdr)
{
	/* Packet alloc or reset have already init other offsets and flags */

	/* We only support Ethernet for now */
	pkt_hdr->input_flags.eth = 1;

	/* Detect jumbo frames */
	if (pkt_hdr->frame_len > ODPH_ETH_LEN_MAX)
		pkt_hdr->input_flags.jumbo = 1;

	/* Assume valid L2 header, no CRC/FCS check in SW */
	pkt_hdr->input_flags.l2 = 1;

	pkt_hdr->input_flags.parsed_l2 = 1;
}

/**
 * Simple packet parser
 */

int _odp_parse_common(odp_packet_hdr_t *pkt_hdr, const uint8_t *ptr)
{
	const odph_ethhdr_t *eth;
	const odph_vlanhdr_t *vlan;
	uint16_t ethtype;
	uint32_t seglen;
	uint8_t ip_proto = 0;

	odp_packet_parsing_ctx_t ctx;
	memset(&ctx, 0, sizeof(ctx));

	INVALIDATE_AREA(pkt_hdr->buf_hdr.addr, pkt_hdr->frame_len);
	if (packet_parse_l2_not_done(pkt_hdr))
		packet_parse_l2(pkt_hdr);

	ctx.offset = sizeof(odph_ethhdr_t);
	if (ptr == NULL) {
		eth = (odph_ethhdr_t *)packet_map(pkt_hdr, 0, &seglen);
		ctx.parseptr = (uint8_t *)&eth->type;
	} else {
		eth = (const odph_ethhdr_t *)ptr;
		ctx.parseptr = (const uint8_t *)&eth->type;
	}
	ethtype = odp_be_to_cpu_16(*((uint16_t *)(void *)ctx.parseptr));

	/* Parse the VLAN header(s), if present */
	if (ethtype == ODPH_ETHTYPE_VLAN_OUTER) {
		pkt_hdr->input_flags.vlan_qinq = 1;
		pkt_hdr->input_flags.vlan = 1;
		vlan = (const odph_vlanhdr_t *)(const void *)ctx.parseptr;
		pkt_hdr->vlan_s_tag = ((ethtype << 16) |
				       odp_be_to_cpu_16(vlan->tci));
		ctx.offset += sizeof(odph_vlanhdr_t);
		ctx.parseptr += sizeof(odph_vlanhdr_t);
		ethtype = odp_be_to_cpu_16(*((const uint16_t *)(const void *)ctx.parseptr));
	}

	if (ethtype == ODPH_ETHTYPE_VLAN) {
		pkt_hdr->input_flags.vlan = 1;
		vlan = (const odph_vlanhdr_t *)(const void *)ctx.parseptr;
		pkt_hdr->vlan_c_tag = ((ethtype << 16) |
				       odp_be_to_cpu_16(vlan->tci));
		ctx.offset += sizeof(odph_vlanhdr_t);
		ctx.parseptr += sizeof(odph_vlanhdr_t);
		ethtype = odp_be_to_cpu_16(*((const uint16_t *)(const void *)ctx.parseptr));
	}

	/* Check for SNAP vs. DIX */
	if (ethtype < ODPH_ETH_LEN_MAX) {
		pkt_hdr->input_flags.snap = 1;
		if (ethtype > pkt_hdr->frame_len - ctx.offset) {
			pkt_hdr->error_flags.snap_len = 1;
			goto parse_exit;
		}
		ctx.offset   += 8;
		ctx.parseptr += 8;
		ethtype = odp_be_to_cpu_16(*((const uint16_t *)(const void *)ctx.parseptr));
	}

	/* Consume Ethertype for Layer 3 parse */
	ctx.parseptr += 2;

	/* Set l3_offset+flag only for known ethtypes */
	pkt_hdr->input_flags.l3 = 1;
	pkt_hdr->l3_offset = ctx.offset;
	pkt_hdr->l3_protocol = ethtype;

	/* Parse Layer 3 headers */
	switch (ethtype) {
	case ODPH_ETHTYPE_IPV4:
		pkt_hdr->input_flags.ipv4 = 1;
		ip_proto = parse_ipv4(pkt_hdr, &ctx);
		break;

	case ODPH_ETHTYPE_IPV6:
		pkt_hdr->input_flags.ipv6 = 1;
		ip_proto = parse_ipv6(pkt_hdr, &ctx);
		break;

	case ODPH_ETHTYPE_ARP:
		pkt_hdr->input_flags.arp = 1;
		ip_proto = 255;  /* Reserved invalid by IANA */
		break;

	default:
		pkt_hdr->input_flags.l3 = 0;
		pkt_hdr->l3_offset = ODP_PACKET_OFFSET_INVALID;
		ip_proto = 255;  /* Reserved invalid by IANA */
	}

	/* Set l4_offset+flag only for known ip_proto */
	pkt_hdr->input_flags.l4 = 1;
	pkt_hdr->l4_offset = ctx.offset;
	pkt_hdr->l4_protocol = ip_proto;

	/* Parse Layer 4 headers */
	switch (ip_proto) {
	case ODPH_IPPROTO_ICMP:
		pkt_hdr->input_flags.icmp = 1;
		break;

	case ODPH_IPPROTO_TCP:
		pkt_hdr->input_flags.tcp = 1;
		parse_tcp(pkt_hdr, &ctx);
		break;

	case ODPH_IPPROTO_UDP:
		pkt_hdr->input_flags.udp = 1;
		parse_udp(pkt_hdr, &ctx);
		break;

	case ODPH_IPPROTO_AH:
	case ODPH_IPPROTO_ESP:
		pkt_hdr->input_flags.ipsec = 1;
		break;

	default:
		pkt_hdr->input_flags.l4 = 0;
		pkt_hdr->l4_offset = ODP_PACKET_OFFSET_INVALID;
		break;
	}

       /*
	* Anything beyond what we parse here is considered payload.
	* Note: Payload is really only relevant for TCP and UDP.  For
	* all other protocols, the payload offset will point to the
	* final header (ARP, ICMP, AH, ESP, or IP Fragment).
	*/
	pkt_hdr->payload_offset = ctx.offset;

parse_exit:
	pkt_hdr->input_flags.parsed_all = 1;
	return pkt_hdr->error_flags.all != 0;
}

int _odp_cls_parse(odp_packet_hdr_t *pkt_hdr, const uint8_t *parseptr)
{
	return _odp_parse_common(pkt_hdr, parseptr);
}

/**
 * Simple packet parser
 */
int packet_parse_full(odp_packet_hdr_t *pkt_hdr)
{
	return _odp_parse_common(pkt_hdr, NULL);
}
