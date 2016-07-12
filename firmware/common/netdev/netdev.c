#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <inttypes.h>

#include <pcie_service.h>
#include <pcie_queue.h>
#include <pcie_queue_protocol.h>
#include <mppa/osconfig.h>
#include <HAL/hal/hal.h>
#include "netdev.h"
#include "internal/cache.h"

#define DDR_BUFFER_BASE_ADDR	0x80000000

static uintptr_t g_current_pkt_addr = DDR_BUFFER_BASE_ADDR;

__attribute__((section(".lowmem_data") )) struct mpodp_control eth_control = {
	.magic = 0xDEADBEEF,
};

int netdev_c2h_is_full(struct mpodp_if_config *cfg, uint32_t c2h_q)
{

	struct mpodp_ring_buff_desc *c2h =
		(void*)(unsigned long)cfg->c2h_addr[c2h_q];
	uint32_t tail = LOAD_U32(c2h->tail);
	uint32_t next_tail = tail + 1;

	if (next_tail == c2h->count)
		next_tail = 0;

	if(next_tail == LOAD_U32(c2h->head)) {
		/* Ring is full of data */
		return 1;
	}

	return 0;
}

int netdev_c2h_enqueue_data(struct mpodp_if_config *cfg,
			    uint32_t c2h_q,
			    struct mpodp_c2h_entry *data,
			    struct mpodp_c2h_entry *old_entry,
			    int it)
{
	struct mpodp_ring_buff_desc *c2h =
		(void*)(unsigned long)cfg->c2h_addr[c2h_q];
	uint32_t tail = LOAD_U32(c2h->tail);
	uint32_t next_tail = tail + 1;

	if (next_tail == c2h->count)
		next_tail = 0;

	if(next_tail == LOAD_U32(c2h->head)) {
		/* Ring is full of data */
		return -1;
	}

	struct mpodp_c2h_entry *entry_base =
		(void*)(unsigned long)c2h->addr;
	struct mpodp_c2h_entry *entry = entry_base + tail;

	if (old_entry) {
		old_entry->pkt_addr = LOAD_U64(entry->pkt_addr);
		old_entry->data = LOAD_U64(entry->data);
	}

	memcpy(entry, data, sizeof(*entry));
	__k1_wmb();

	STORE_U32(c2h->tail, next_tail);

#ifdef NETDEV_VERBOSE
	printf("C2H data 0x%llx pushed in if:%p | at offset:%lu\n", data->pkt_addr, cfg, tail);
#endif
	if (it)
		mppa_pcie_send_it_to_host();

	return 0;
}

int netdev_h2c_enqueue_buffer(struct mpodp_if_config *cfg,
			      uint32_t h2c_q,
			      struct mpodp_h2c_entry *buffer)
{
	struct mpodp_ring_buff_desc *h2c =
		(void*)(unsigned long)cfg->h2c_addr[h2c_q];
	uint32_t head = LOAD_U32(h2c->head);

	if (head == h2c->count - 1) {
		/* Ring is full of FIFO address */
		return -1;
	}

	struct mpodp_h2c_entry *entry_base =
		(void*)(unsigned long)h2c->addr;
	struct mpodp_h2c_entry *entry = entry_base + head;

	memcpy(entry, buffer, sizeof(*entry));
	__k1_wmb();

	uint32_t next_head = head + 1;
	if (next_head == h2c->count)
		next_head = 0;

	STORE_U32(h2c->head, next_head);
#ifdef NETDEV_VERBOSE
	printf("H2C buffer 0x%llx pushed in if:%p | at offset:%lu\n", buffer->pkt_addr, cfg, head);
#endif

	mppa_pcie_send_it_to_host();

	return 0;

}

struct mpodp_h2c_entry *
netdev_h2c_peek_data(const struct mpodp_if_config *cfg __attribute__((unused)),
		     uint32_t h2c_q __attribute__((unused)))
{
	return NULL;
}

static int netdev_setup_c2h(struct mpodp_if_config *if_cfg,
			    const int c2h_q,
			    const eth_if_cfg_t *cfg)
{
	struct mpodp_ring_buff_desc *c2h;
	struct mpodp_c2h_entry *entries;
	uint32_t i;

	if (if_cfg->mtu == 0) {
		fprintf(stderr, "[netdev] MTU not configured\n");
		return -1;
	}
	c2h = calloc (1, sizeof(*c2h));
	if (!c2h)
		return -1;

	entries = calloc(cfg->n_c2h_entries, sizeof(*entries));
	if(!entries) {
		free(c2h);
		return -1;
	}

	if (!cfg->noalloc) {
		for(i = 0; i < cfg->n_c2h_entries; i++) {
			entries[i].pkt_addr = g_current_pkt_addr;
			g_current_pkt_addr += if_cfg->mtu;
#ifdef NETDEV_VERBOSE
			printf("C2H Packet (%lu/%lu) entry at 0x%"PRIx64"\n", i,
			       cfg->n_c2h_entries, entries[i].pkt_addr);
#endif
		}
	}

	c2h->count = cfg->n_c2h_entries;
	c2h->addr = (uintptr_t) entries;
	if_cfg->c2h_addr[c2h_q] = (uint64_t)(unsigned long)c2h;

	return 0;
}

static int netdev_setup_h2c(struct mpodp_if_config *if_cfg,
			    const int h2c_q,
			    const eth_if_cfg_t *cfg)
{
	struct mpodp_ring_buff_desc *h2c;
	struct mpodp_h2c_entry *entries;

	if (if_cfg->mtu == 0) {
		fprintf(stderr, "[netdev] MTU not configured\n");
		return -1;
	}
	h2c = calloc (1, sizeof(*h2c));
	if (!h2c)
		return -1;

	entries = calloc(cfg->n_h2c_entries, sizeof(*entries));
	if(!entries) {
		free(h2c);
		return -1;
	}

	h2c->count = cfg->n_h2c_entries;
	h2c->addr = (uintptr_t) entries;
	if_cfg->h2c_addr[h2c_q] = (uint64_t)(unsigned long)h2c;

	return 0;
}

int netdev_init_interface(const eth_if_cfg_t *cfg)
{
	struct mpodp_if_config *if_cfg;
	int ret;
	uint32_t i;

	if (cfg->if_id >= MPODP_MAX_IF_COUNT)
		return -1;

	if_cfg = &eth_control.configs[cfg->if_id];
	if_cfg->mtu = cfg->mtu;
	if_cfg->flags = cfg->flags;
	if_cfg->interrupt_status = 1;
	memcpy(if_cfg->mac_addr, cfg->mac_addr, MAC_ADDR_LEN);

	if (cfg->n_c2h_q > MPODP_MAX_RX_QUEUES)
		return -1;
	if_cfg->n_rxqs = cfg->n_c2h_q;
	for (i = 0; i < cfg->n_c2h_q; ++i) {
		ret = netdev_setup_c2h(if_cfg, i, cfg);
		if (ret)
			return ret;
	}

	if (cfg->n_h2c_q > MPODP_MAX_TX_QUEUES)
		return -1;
	if_cfg->n_txqs = cfg->n_h2c_q;
	for (i = 0; i < cfg->n_h2c_q; ++i) {
		ret = netdev_setup_h2c(if_cfg, i, cfg);
		if (ret)
			return ret;
	}
	return 0;
}

int netdev_init(uint8_t n_if, const eth_if_cfg_t cfg[n_if]) {
	uint8_t i;
	int ret;

	if (n_if > MPODP_MAX_IF_COUNT)
		return -1;

	for (i = 0; i < n_if; ++i) {
		ret = netdev_init_interface(&cfg[i]);
		if (ret)
			return ret;
	}
	eth_control.if_count = n_if;

	return 0;
}

int netdev_start()
{
	pcie_open();

	/* Ensure coherency */
	__k1_mb();
	/* Cross fingers for everything to be setup correctly ! */
	__builtin_k1_swu(&eth_control.magic, MPODP_CONTROL_STRUCT_MAGIC);
	/* Ensure coherency */
	__k1_mb();

	__mppa_pcie_control.services[PCIE_SERVICE_ODP].addr = (unsigned int)&eth_control;
	__k1_wmb();
	__mppa_pcie_control.services[PCIE_SERVICE_ODP].magic = PCIE_SERVICE_MAGIC;
	__k1_wmb();


#ifdef __mos__
	mOS_pcie_write_usr_it(0);
	mOS_pcie_write_usr_it(1);
#else
	mppa_pcie_send_it_to_host();
#endif
#ifdef VERBOSE
	printf("Net dev interface(s) are up\n");
#endif
	return 0;
}

