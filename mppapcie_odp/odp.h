#ifndef ODP_H
#define ODP_H

#if (LINUX_VERSION_CODE > KERNEL_VERSION (3, 12, 0))
#define DMA_SUCCESS DMA_COMPLETE
#endif

#define MPODP_NAPI_WEIGHT  NAPI_POLL_WEIGHT
#define MPODP_MAX_TX_RECLAIM 16
#define MPODP_TX_RECLAIM_PERIOD (HZ / 2)

/* Sufficient for K1B not for K1A but not expected to be used */
#define MPODP_NOC_CHAN_COUNT 4
#define MPODP_AUTOLOOP_DESC_COUNT 32

#define desc_info_addr(_smem_addr, addr, field)				\
	_smem_addr + addr +  offsetof(struct mpodp_ring_buff_desc, field)

enum _mpodp_if_state {
	_MPODP_IF_STATE_DISABLED = 0,
	_MPODP_IF_STATE_ENABLING = 1,
	_MPODP_IF_STATE_ENABLED = 2,
	_MPODP_IF_STATE_DISABLING = 3,
};

struct mpodp_tx {
	struct sk_buff *skb;
	struct scatterlist sg[MAX_SKB_FRAGS + 1];
	u32 sg_len;
	dma_cookie_t cookie;
	u32 len;		/* to be able to free the skb in the TX 2nd step */
	union mppa_timestamp time;
	dma_addr_t dst_addr;
	int chanidx;
};

struct mpodp_rx {
	struct sk_buff *skb;
	struct scatterlist sg[1];
	u32 sg_len;
	dma_cookie_t cookie;
	void *entry_addr;
	u32 len;		/* avoid to re-read the entry in the RX 2nd step */
};

struct mpodp_cache_entry {
	void *entry_addr;
	u32 addr;
};

struct mpodp_if_priv {
	struct napi_struct napi;

	struct mppa_pcie_device *pdata;
	struct pci_dev *pdev;	/* pointer to device structure */

	struct dentry *dir;

	struct mpodp_if_config *config;
	struct net_device *netdev;

	atomic_t reset;

	int interrupt_status;
	u8 __iomem *interrupt_status_addr;

	/* TX ring */
	struct dma_chan *tx_chan[MPODP_NOC_CHAN_COUNT + 1];
	struct mppa_pcie_dma_slave_config tx_config[MPODP_NOC_CHAN_COUNT];
	struct mpodp_tx *tx_ring;

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
	struct timer_list tx_timer;	/* checks Tx queues */
	struct mppa_pcie_time *tx_time;
	uint64_t packet_id;

	/* Current idx in the autoloop MPPA RB */
	atomic_t tx_autoloop_cur;

	/* Amount of Tx cached Host side in autoloop mode. Size = tx_size */
	int tx_cached_head;
	/* Cached adresses from the MPPA side. Size = tx_mppa_size */
	struct mpodp_cache_entry *tx_cache;

	/* RX ring */
	struct dma_chan *rx_chan;
	struct mppa_pcie_dma_slave_config rx_config;
	struct mpodp_rx *rx_ring;
	int rx_used;
	int rx_avail;
	int rx_tail;
	u8 __iomem *rx_tail_addr;
	int rx_head;
	u8 __iomem *rx_head_addr;
	int rx_size;
	struct mpodp_c2h_ring_buff_entry *rx_mppa_entries;
	struct timer_list rx_timer;	/* checks Tx queues */

};

struct mpodp_pdata_priv {
	struct list_head link;	/* List of all devices */
	struct mppa_pcie_device *pdata;
	struct pci_dev *pdev;	/* pointer to device structure */
	atomic_t state;
	struct net_device *dev[MPODP_MAX_IF_COUNT];
	int if_count;
	struct mpodp_control control;
	struct work_struct enable;	/* cannot register in interrupt context */
	struct notifier_block notifier;
};

netdev_tx_t mpodp_start_xmit(struct sk_buff *skb,
			     struct net_device *netdev);
void mpodp_tx_timeout(struct net_device *netdev);
void mpodp_tx_timer_cb(unsigned long data);
int mpodp_clean_tx(struct mpodp_if_priv *priv, unsigned budget);

int mpodp_start_rx(struct mpodp_if_priv *priv);
int mpodp_clean_rx(struct mpodp_if_priv *priv, int budget);

#endif