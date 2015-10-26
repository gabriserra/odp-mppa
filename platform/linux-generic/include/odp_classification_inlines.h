/* Copyright (c) 2014, Linaro Limited
 * All rights reserved.
 *
 * SPDX-License-Identifier:     BSD-3-Clause
 */


/**
 * @file
 *
 * ODP Classification Inlines
 * Classification Inlines Functions
 */
#ifndef __ODP_CLASSIFICATION_INLINES_H_
#define __ODP_CLASSIFICATION_INLINES_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <odp/debug.h>
#include <odp/helper/eth.h>
#include <odp/helper/ip.h>
#include <odp/helper/ipsec.h>
#include <odp/helper/udp.h>
#include <odp/helper/tcp.h>

/* PMR term value verification function
These functions verify the given PMR term value with the value in the packet
These following functions return 1 on success and 0 on failure
*/

static inline int verify_pmr_packet_len(odp_packet_hdr_t *pkt_hdr,
					pmr_term_value_t *term_value)
{
	if (term_value->val == (pkt_hdr->frame_len &
				     term_value->mask))
		return 1;

	return 0;
}
static inline int verify_pmr_ip_proto(uint8_t *pkt_addr,
				      odp_packet_hdr_t *pkt_hdr,
				      pmr_term_value_t *term_value)
{
	odph_ipv4hdr_t *ip;
	uint8_t proto;
	if (!pkt_hdr->input_flags.ipv4)
		return 0;
	ip = (odph_ipv4hdr_t *)(pkt_addr + pkt_hdr->l3_offset);
	proto = ip->proto;
	if (term_value->val == (proto & term_value->mask))
		return 1;

	return 0;
}

static inline int verify_pmr_ipv4_saddr(uint8_t *pkt_addr,
					odp_packet_hdr_t *pkt_hdr,
					pmr_term_value_t *term_value)
{
	odph_ipv4hdr_t *ip;
	uint32_t ipaddr;
	if (!pkt_hdr->input_flags.ipv4)
		return 0;
	ip = (odph_ipv4hdr_t *)(pkt_addr + pkt_hdr->l3_offset);
	ipaddr = odp_be_to_cpu_32(ip->src_addr);
	if (term_value->val == (ipaddr & term_value->mask))
		return 1;

	return 0;
}

static inline int verify_pmr_ipv4_daddr(uint8_t *pkt_addr,
					odp_packet_hdr_t *pkt_hdr,
					pmr_term_value_t *term_value)
{
	odph_ipv4hdr_t *ip;
	uint32_t ipaddr;
	if (!pkt_hdr->input_flags.ipv4)
		return 0;
	ip = (odph_ipv4hdr_t *)(pkt_addr + pkt_hdr->l3_offset);
	ipaddr = odp_be_to_cpu_32(ip->dst_addr);
	if (term_value->val == (ipaddr & term_value->mask))
		return 1;

	return 0;
}

static inline int verify_pmr_tcp_sport(uint8_t *pkt_addr,
				       odp_packet_hdr_t *pkt_hdr,
				       pmr_term_value_t *term_value)
{
	uint16_t sport;
	odph_tcphdr_t *tcp;
	if (!pkt_hdr->input_flags.tcp)
		return 0;
	tcp = (odph_tcphdr_t *)(pkt_addr + pkt_hdr->l4_offset);
	sport = odp_be_to_cpu_16(tcp->src_port);
	if (term_value->val == (sport & term_value->mask))
		return 1;

	return 0;
}

static inline int verify_pmr_tcp_dport(uint8_t *pkt_addr,
				       odp_packet_hdr_t *pkt_hdr,
				       pmr_term_value_t *term_value)
{
	uint16_t dport;
	odph_tcphdr_t *tcp;
	if (!pkt_hdr->input_flags.tcp)
		return 0;
	tcp = (odph_tcphdr_t *)(pkt_addr + pkt_hdr->l4_offset);
	dport = odp_be_to_cpu_16(tcp->dst_port);
	if (term_value->val == (dport & term_value->mask))
		return 1;

	return 0;
}

static inline int verify_pmr_udp_dport(uint8_t *pkt_addr,
				       odp_packet_hdr_t *pkt_hdr,
				       pmr_term_value_t *term_value)
{
	uint16_t dport;
	odph_udphdr_t *udp;
	if (!pkt_hdr->input_flags.udp)
		return 0;
	udp = (odph_udphdr_t *)(pkt_addr + pkt_hdr->l4_offset);
	dport = odp_be_to_cpu_16(udp->dst_port);
	if (term_value->val == (dport & term_value->mask))
			return 1;

	return 0;
}
static inline int verify_pmr_udp_sport(uint8_t *pkt_addr,
				       odp_packet_hdr_t *pkt_hdr,
				       pmr_term_value_t *term_value)
{
	uint16_t sport;
	odph_udphdr_t *udp;
	if (!pkt_hdr->input_flags.udp)
		return 0;
	udp = (odph_udphdr_t *)(pkt_addr + pkt_hdr->l4_offset);
	sport = odp_be_to_cpu_16(udp->src_port);
	if (term_value->val == (sport & term_value->mask))
		return 1;

	return 0;
}

static inline int verify_pmr_dmac(uint8_t *pkt_addr ODP_UNUSED,
				  odp_packet_hdr_t *pkt_hdr ODP_UNUSED,
				  pmr_term_value_t *term_value ODP_UNUSED)
{
	ODP_UNIMPLEMENTED();
	return 0;
}

static inline int verify_pmr_ipv6_saddr(uint8_t *pkt_addr ODP_UNUSED,
					odp_packet_hdr_t *pkt_hdr ODP_UNUSED,
					pmr_term_value_t *term_value ODP_UNUSED)
{
	ODP_UNIMPLEMENTED();
	return 0;
}
static inline int verify_pmr_ipv6_daddr(uint8_t *pkt_addr ODP_UNUSED,
					odp_packet_hdr_t *pkt_hdr ODP_UNUSED,
					pmr_term_value_t *term_value ODP_UNUSED)
{
	ODP_UNIMPLEMENTED();
	return 0;
}
static inline int verify_pmr_vlan_id_0(uint8_t *pkt_addr ODP_UNUSED,
				       odp_packet_hdr_t *pkt_hdr ODP_UNUSED,
				       pmr_term_value_t *term_value ODP_UNUSED)
{
	ODP_UNIMPLEMENTED();
	return 0;
}
static inline int verify_pmr_vlan_id_x(uint8_t *pkt_addr ODP_UNUSED,
				       odp_packet_hdr_t *pkt_hdr ODP_UNUSED,
				       pmr_term_value_t *term_value ODP_UNUSED)
{
	ODP_UNIMPLEMENTED();
	return 0;
}

static inline int verify_pmr_ipsec_spi(uint8_t *pkt_addr,
				       odp_packet_hdr_t *pkt_hdr,
				       pmr_term_value_t *term_value)
{
	uint32_t spi;

	if (!pkt_hdr->input_flags.ipsec)
		return 0;

	pkt_addr += pkt_hdr->l4_offset;

	if (pkt_hdr->l4_protocol == ODPH_IPPROTO_AH) {
		odph_ahhdr_t *ahhdr = (odph_ahhdr_t *)pkt_addr;

		spi = odp_be_to_cpu_32(ahhdr->spi);
	} else if (pkt_hdr->l4_protocol == ODPH_IPPROTO_ESP) {
		odph_esphdr_t *esphdr = (odph_esphdr_t *)pkt_addr;

		spi = odp_be_to_cpu_32(esphdr->spi);
	} else {
		return 0;
	}

	if (term_value->val == (spi & term_value->mask))
		return 1;

	return 0;
}

static inline int verify_pmr_ld_vni(uint8_t *pkt_addr ODP_UNUSED,
				    odp_packet_hdr_t *pkt_hdr ODP_UNUSED,
				    pmr_term_value_t *term_value ODP_UNUSED)
{
	ODP_UNIMPLEMENTED();
	return 0;
}

static inline int verify_pmr_custom_frame(uint8_t *pkt_addr,
					  odp_packet_hdr_t *pkt_hdr,
					  pmr_term_value_t *term_value)
{
	uint64_t val = 0;
	uint32_t offset = term_value->offset;
	uint32_t val_sz = term_value->val_sz;

	ODP_ASSERT(val_sz <= ODP_PMR_TERM_BYTES_MAX);

	if (pkt_hdr->frame_len <= offset + val_sz)
		return 0;

	memcpy(&val, pkt_addr + offset, val_sz);
	if (term_value->val == (val & term_value->mask))
		return 1;

	return 0;
}

static inline int verify_pmr_eth_type_0(uint8_t *pkt_addr ODP_UNUSED,
					odp_packet_hdr_t *pkt_hdr ODP_UNUSED,
					pmr_term_value_t *term_value ODP_UNUSED)
{
	ODP_UNIMPLEMENTED();
	return 0;
}
static inline int verify_pmr_eth_type_x(uint8_t *pkt_addr ODP_UNUSED,
					odp_packet_hdr_t *pkt_hdr ODP_UNUSED,
					pmr_term_value_t *term_value ODP_UNUSED)
{
	ODP_UNIMPLEMENTED();
	return 0;
}
#ifdef __cplusplus
}
#endif
#endif
