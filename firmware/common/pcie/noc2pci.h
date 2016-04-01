#ifndef NOC2PCI__H
#define RX_NOC2PCI__H

#define MAX_RX 					(30 * 4)
#define RX_THREAD_COUNT			2
#define IF_PER_THREAD			(MPPA_PCIE_USABLE_DNOC_IF / RX_THREAD_COUNT)
#define RX_PER_IF				256

#define MPPA_PCIE_RM_COUNT		4

#define RX_RM_START		5
#define RX_RM_COUNT		2
#define PCIE_TX_RM		(RX_RM_START + RX_RM_COUNT)

/**
 * Credits are sent back every CREDIT_CHUNK buffers.
 */
#define CREDIT_CHUNK	(MPPA_PCIE_NOC_RX_NB / 4)

/**
 * WARNING: struct from odp_tx_uc_internal
 */
 
#define END_OF_PACKETS		(1 << 0)

typedef union {
	struct {
		uint16_t pkt_size;
		uint16_t flags;
	};
	uint64_t dword;
} tx_uc_header_t;


typedef struct tx_credit {
	uint8_t min_tx_tag;
	uint8_t max_tx_tag;
	mppa_cnoc_config_t config;
	mppa_cnoc_header_t header;
	uint8_t cnoc_tx;
	uint8_t remote_cnoc_rx;
	uint8_t next_tag;
	uint8_t cluster;
	uint64_t credit;
} tx_credit_t;

typedef struct rx_cfg {
	mppa_pcie_noc_rx_buf_t *mapped_buf;
	uint8_t pcie_eth_if; /* PCIe ethernet interface */
	uint8_t broken;
	tx_credit_t *tx_credit;
} rx_cfg_t;

typedef struct rx_iface {
	/* 256 rx per interface */
	uint64_t ev_mask[4];
	rx_cfg_t rx_cfgs[RX_PER_IF];
	int iface_id;
} rx_iface_t;

typedef struct rx_thread {
	rx_iface_t iface[IF_PER_THREAD];
} rx_thread_t;

extern rx_thread_t g_rx_threads[RX_THREAD_COUNT];

#endif
