/* Copyright (c) 2013, Linaro Limited
 * All rights reserved.
 *
 * SPDX-License-Identifier:     BSD-3-Clause
 */
#include <odp_packet_io_internal.h>
#include <odp/errno.h>
#include <errno.h>
#include "odp_classification_internal.h"
#include "../../syscall/include/common.h"

static int _magic_scall_init(void){
	return __k1_syscall0(MAGIC_SCALL_ETH_INIT);
}
static int ODP_UNUSED _magic_scall_open(const char * name, size_t len){
	return __k1_syscall2(MAGIC_SCALL_ETH_OPEN, (uint32_t)name, len);
}
static int _magic_scall_mac_get(int id, void * mac){
	return __k1_syscall2(MAGIC_SCALL_ETH_GETMAC, id, (uint32_t)mac);
}
static int _magic_scall_recv(int id, void* buf, unsigned len){
	return __k1_syscall3(MAGIC_SCALL_ETH_RECV, id, (uint32_t)buf, len);
}
static int _magic_scall_send(int id, void * buf, unsigned len){
	return __k1_syscall3(MAGIC_SCALL_ETH_SEND, id, (uint32_t)buf, len);
}
static int _magic_scall_prom_set(int id, int enable){
	return __k1_syscall2(MAGIC_SCALL_ETH_PROM_SET, id, enable);
}
static int _magic_scall_prom_get(int id){
	return __k1_syscall1(MAGIC_SCALL_ETH_PROM_GET, id);
}

static int magic_init(void)
{
	return _magic_scall_init();
}

static int magic_close(pktio_entry_t * const pktio_entry ODP_UNUSED)
{
	return 0;
}

static int magic_open(odp_pktio_t id ODP_UNUSED, pktio_entry_t *pktio_entry,
		      const char *devname, odp_pool_t pool ODP_UNUSED)
{
	if(!strncmp("magic:", devname, strlen("magic:"))){
#ifndef MAGIC_SCALL
		(void)pktio_entry;
		ODP_ERR("Trying to invoke magic interface on H/W");
		return 1;
#else
		pkt_magic_t * pkt_magic = &pktio_entry->s.pkt_magic;
		pkt_magic->fd = _magic_scall_open(devname + strlen("magic:"), strlen(devname + strlen("magic:")));
		if(pkt_magic->fd < 0)
			return pkt_magic->fd;

		pkt_magic->pool = pool;
		/* pkt buffer size */
		pkt_magic->buf_size = odp_buffer_pool_segment_size(pool) * ODP_BUFFER_MAX_SEG;
		/* max frame len taking into account the l2-offset */
		pkt_magic->max_frame_len = pkt_magic->buf_size -
			odp_buffer_pool_headroom(pool) -
			odp_buffer_pool_tailroom(pool);

		return 0;
#endif
	}

	/* The device name does not match ours */
	return -1;
}

static int magic_mac_addr_get(pktio_entry_t *pktio_entry, void * mac_addr)
{
	return _magic_scall_mac_get(pktio_entry->s.pkt_magic.fd, mac_addr);
}

static int magic_recv(pktio_entry_t *const pktio_entry, odp_packet_t pkt_table[], unsigned len)
{
	ssize_t recv_bytes;
	unsigned i;
	odp_packet_t pkt = ODP_PACKET_INVALID;
	int nb_rx = 0;
	odp_pkt_iovec_t iovecs[ODP_BUFFER_MAX_SEG];
	uint32_t iov_count;

	for (i = 0; i < len; i++) {
		if (odp_likely(pkt == ODP_PACKET_INVALID)) {
			pkt = odp_packet_alloc(pktio_entry->s.pkt_magic.pool, pktio_entry->s.pkt_magic.max_frame_len);
			if (odp_unlikely(pkt == ODP_PACKET_INVALID))
				break;
		}
		iov_count = _rx_pkt_to_iovec(pkt, iovecs);

		recv_bytes = _magic_scall_recv(pktio_entry->s.pkt_magic.fd, iovecs, iov_count);

		/* no data or error: free recv buf and break out of loop */
		if (odp_unlikely(recv_bytes < 1))
			break;

		/* Parse and set packet header data */
		packet_parse_reset((odp_packet_hdr_t *)pkt);
		odp_packet_pull_tail(pkt, pktio_entry->s.pkt_magic.max_frame_len - recv_bytes);
		packet_parse_l2((odp_packet_hdr_t *)pkt);

		pkt_table[nb_rx] = pkt;
		pkt = ODP_PACKET_INVALID;
		nb_rx++;
	} /* end for() */

	if (odp_unlikely(pkt != ODP_PACKET_INVALID))
		odp_packet_free(pkt);

	if (nb_rx && pktio_cls_enabled(pktio_entry)) {
		int defq_pkts = 0;
		for (int i = 0; i < nb_rx; ++i) {
			if (0 > _odp_packet_classifier(pktio_entry, pkt_table[i])) {
				pkt_table[defq_pkts] = pkt_table[i];
			}
		}
		nb_rx = defq_pkts;
	}

	return nb_rx;
}

static int magic_send(pktio_entry_t *const pktio_entry, odp_packet_t pkt_table[], unsigned len)
{
	odp_packet_t pkt;
	unsigned i;
	int nb_tx;
	int ret;
	odp_pkt_iovec_t iovecs[ODP_BUFFER_MAX_SEG];
	uint32_t iov_count;

	i = 0;
	while (i < len) {
		pkt = pkt_table[i];
		iov_count = _tx_pkt_to_iovec(pkt, iovecs);

		ret = _magic_scall_send(pktio_entry->s.pkt_magic.fd, iovecs, iov_count);
		if (odp_unlikely(ret == -1)) {
			break;
		}

		i++;
	}			/* end while */
	nb_tx = i;

	packet_free_multi(pkt_table, len);

	return nb_tx;
}

static int magic_promisc_mode_set(pktio_entry_t *const pktio_entry, odp_bool_t enable){
	return _magic_scall_prom_set(pktio_entry->s.pkt_magic.fd, enable);
}

static int magic_promisc_mode(pktio_entry_t *const pktio_entry){
	return _magic_scall_prom_get(pktio_entry->s.pkt_magic.fd);
}

static int magic_mtu_get(__attribute__ ((unused)) pktio_entry_t *const pktio_entry) {
	return -1;
}

const pktio_if_ops_t magic_pktio_ops = {
	.init = magic_init,
	.term = NULL,
	.open = magic_open,
	.close = magic_close,
	.start = NULL,
	.stop = NULL,
	.recv = magic_recv,
	.send = magic_send,
	.mtu_get = magic_mtu_get,
	.promisc_mode_set = magic_promisc_mode_set,
	.promisc_mode_get = magic_promisc_mode,
	.mac_get = magic_mac_addr_get,
};
