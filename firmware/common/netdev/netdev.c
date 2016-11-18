#include <stdio.h>
#include <assert.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <inttypes.h>
#include <HAL/hal/cluster/pcie.h>

#include <pcie_service.h>
#include <pcie_queue.h>
#include <pcie_queue_protocol.h>
#include <mppa/osconfig.h>
#include "netdev.h"
#include "internal/netdev.h"
#include "internal/cache.h"

extern uint64_t RamBase;
static uintptr_t g_current_pkt_addr = (uintptr_t)&RamBase;

#ifndef LINUX_FIRMWARE
__attribute__((section(".lowmem_data") )) struct mpodp_control eth_control = {
	.magic = 0xDEADBEEF,
};
#endif

struct mpodp_control *eth_ctrl;

#define MEM_WRITE_32 0x40
#define MEM_WRITE_64 0x60
static inline
__k1_uint32_t __k1_pcie_write_32( __k1_uint32_t address, __k1_uint32_t data)
{

	static const mppa_pcie_master_itf_master_cmd_t
		master_cmd = {._ = { .start_cmd = 1,
				     .cmd_type = MEM_WRITE_32,
				     .byte_en = 0xf
		}};
	/* mppa_pcie_master_itf_mst_res_t    mst_res; */

	// write
	/* mppa_pcie_master_itf[0]->pcie_addr_hi.word = 0; */
	mppa_pcie_master_itf[0]->pcie_addr.word    = address;
	mppa_pcie_master_itf[0]->pcie_wr_data.word = data;
	mppa_pcie_master_itf[0]->master_cmd   = master_cmd;

	// FIXME: We should do this but it works without. Uncomment if transfers are weird
	/* // wait for response */
	/* do{ */
	/* 	mst_res.word = mppa_pcie_master_itf[0]->mst_res.word; */
	/* } while(mst_res._.cmd_done == 0); */

	/* if (mst_res._.cmd_status) */
	/* 	printf("BAD Status = %d\n", mst_res._.cmd_status); */
	/* return mst_res._.cmd_status; */

	return 0;
}

int netdev_c2h_is_full(struct mpodp_if_config *cfg, uint32_t c2h_q)
{

	struct mpodp_ring_buff_desc *c2h =
		(void*)(unsigned long)cfg->c2h_addr[c2h_q];
	uint32_t tail = c2h->tail;
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
	uint32_t tail = c2h->tail;
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
		old_entry->pkt_addr = entry->pkt_addr;
		old_entry->data = entry->data;
	}

	memcpy(entry, data, sizeof(*entry));
	/* __k1_wmb(); */

	/* Also store the data on the host side of things */
	uint64_t host_addr = c2h->host_addr;
	if (!host_addr)
		__builtin_k1_dinvall(&c2h->host_addr);
	host_addr = c2h->host_addr;

	if (host_addr) {
		uint64_t entry_addr = host_addr + tail * sizeof(*data);
		union {
			uint32_t val;
			struct {
				uint16_t len;
				uint16_t status;
			};
		} meta;
		meta.len = data->len;
		meta.status = data->status;
		__k1_pcie_write_32(entry_addr +
				   offsetof(struct mpodp_c2h_entry, pkt_addr),
				   data->pkt_addr);
		__k1_pcie_write_32(entry_addr +
				   offsetof(struct mpodp_c2h_entry, len),
				   meta.val);
	}

	c2h->tail = next_tail;
	/* __k1_wmb(); */

	uint64_t h_tail_addr = c2h->h_tail_addr;
	if (h_tail_addr) {
		__k1_pcie_write_32(h_tail_addr, next_tail);
	}

#ifdef NETDEV_VERBOSE
	printf("C2H data 0x%lx pushed in if:%p | at offset:%lu\n", data->pkt_addr, cfg, tail);
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
	if (if_cfg->mtu > MPODP_MAX_MTU){
		fprintf(stderr, "[netdev] MTU is too large\n");
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
			printf("C2H Packet (%lu/%lu) entry at 0x%"PRIx32"\n", i,
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

int netdev_init()
{
#ifdef LINUX_FIRMWARE
	eth_ctrl = malloc(sizeof(*eth_ctrl));
#else
	eth_ctrl = &eth_control;
#endif
	return 0;
}
int netdev_configure_interface(const eth_if_cfg_t *cfg)
{
	struct mpodp_if_config *if_cfg;
	int ret;
	uint32_t i;

	if (!eth_ctrl)
		return -1;

	if (cfg->if_id >= MPODP_MAX_IF_COUNT)
		return -1;

	if_cfg = &eth_ctrl->configs[cfg->if_id];
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

int netdev_configure(uint8_t n_if, const eth_if_cfg_t cfg[n_if]) {
	uint8_t i;
	int ret;

	if (!eth_ctrl)
		return -1;

	if (n_if > MPODP_MAX_IF_COUNT)
		return -1;

	for (i = 0; i < n_if; ++i) {
		ret = netdev_configure_interface(&cfg[i]);
		if (ret)
			return ret;
	}
	eth_ctrl->if_count = n_if;

	return 0;
}

int netdev_start()
{
	pcie_control *pcie_ctrl;

	if (!eth_ctrl)
		return -1;

#ifndef LINUX_FIRMWARE
	pcie_open();
#endif

	pcie_ctrl = (pcie_control *)(unsigned long)
		mppa_pcie_read_doorbell_user_reg(PCIE_REG_CONTROL_ADDR);

	/* Ensure coherency */
	__k1_mb();
	/* Cross fingers for everything to be setup correctly ! */
	__builtin_k1_swu(&eth_ctrl->magic, MPODP_CONTROL_STRUCT_MAGIC);
	/* Ensure coherency */
	__k1_mb();

	pcie_ctrl->services[PCIE_SERVICE_ODP].addr = (unsigned int)eth_ctrl;
	__k1_wmb();
	pcie_ctrl->services[PCIE_SERVICE_ODP].magic = PCIE_SERVICE_MAGIC;
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
