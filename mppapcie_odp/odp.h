#ifndef ODP_H
#define ODP_H

#define ODP_NAPI_WEIGHT  NAPI_POLL_WEIGHT
#define ODP_MAX_TX_RECLAIM 16
#define ODP_TX_RECLAIM_PERIOD (HZ / 2)

/* Sufficient for K1B not for K1A but not expected to be used */
#define ODP_NOC_CHAN_COUNT 4
#define ODP_AUTOLOOP_DESC_COUNT 32

#define desc_info_addr(_smem_addr, addr, field)				\
	_smem_addr + addr +  offsetof(struct odp_ring_buff_desc, field)

enum _odp_if_state {
	_ODP_IF_STATE_DISABLED = 0,
	_ODP_IF_STATE_ENABLING = 1,
	_ODP_IF_STATE_ENABLED = 2,
	_ODP_IF_STATE_DISABLING = 3,
};

struct odp_tx {
	struct sk_buff *skb;
	struct scatterlist sg[MAX_SKB_FRAGS + 1];
	u32 sg_len;
	dma_cookie_t cookie;
	u32 len; /* to be able to free the skb in the TX 2nd step */
	union mppa_timestamp time;
	dma_addr_t dst_addr;
	int chanidx;
	uint32_t flags;
};

struct odp_rx {
	struct sk_buff *skb;
	struct scatterlist sg[1];
	u32 sg_len;
	dma_cookie_t cookie;
	void *entry_addr;
	u32 len; /* avoid to re-read the entry in the RX 2nd step */
};

struct odp_cache_entry {
	void *entry_addr;
	u32 addr;
	u32 flags;
};

struct odp_if_priv {
	struct napi_struct napi;

	struct mppa_pcie_device *pdata;
	struct pci_dev *pdev; /* pointer to device structure */

	struct dentry *dir;

	struct odp_if_config *config;
	struct net_device *netdev;

	atomic_t reset;

	int interrupt_status;
	u8 __iomem *interrupt_status_addr;

	/* TX ring */
	struct dma_chan *tx_chan[ODP_NOC_CHAN_COUNT+1];
	struct mppa_pcie_dma_slave_config tx_config[ODP_NOC_CHAN_COUNT+1];
	struct odp_tx *tx_ring;

	/* Position of the latest complete Tx buffer.
	 * same as tail in MPPA Tx ring buffer
	 * Range [0 .. tx_size [ */
	atomic_t tx_done;
	u8 __iomem *tx_tail_addr;

	/* Position of the latest submited desc
	 * Range [0 .. tx_size [ */
	atomic_t tx_submitted;

	atomic_t tx_head;
	u8 __iomem *tx_head_addr;

	/* Size of the Tx RB on the MPPA */
	int tx_mppa_size;
	/* Number of descriptors on the host */
	int tx_size;
	struct timer_list tx_timer; /* checks Tx queues */
	struct mppa_pcie_time *tx_time;
	uint64_t packet_id;

	/* Current idx in the autoloop MPPA RB */
	atomic_t tx_autoloop_cur;

	/* Amount of Tx cached Host side in autoloop mode. Size = tx_size */
	int tx_cached_head;
	/* Cached adresses from the MPPA side. Size = tx_mppa_size */
	struct odp_cache_entry *tx_cache;

	/* RX ring */
	struct dma_chan *rx_chan;
	struct mppa_pcie_dma_slave_config rx_config;
	struct odp_rx *rx_ring;
	int rx_used;
	int rx_avail;
	int rx_tail;
	u8 __iomem *rx_tail_addr;
	int rx_head;
	u8 __iomem *rx_head_addr;
	int rx_size;
	struct odp_c2h_ring_buff_entry *rx_mppa_entries;
	struct timer_list rx_timer; /* checks Tx queues */

};

struct odp_pdata_priv {
	struct list_head link; /* List of all devices */
	struct mppa_pcie_device *pdata;
	struct pci_dev *pdev; /* pointer to device structure */
	atomic_t state;
	struct net_device *dev[ODP_MAX_IF_COUNT];
	int if_count;
	struct odp_control control;
	struct work_struct enable; /* cannot register in interrupt context */
	struct notifier_block notifier;
};

#endif
