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
#define MPODP_AUTOLOOP_DESC_COUNT 256

#define desc_info_addr(_smem_addr, addr, field)				\
	_smem_addr + addr +  offsetof(struct mpodp_ring_buff_desc, field)

enum _mpodp_if_state {
	_MPODP_IF_STATE_DISABLED = 0,
	_MPODP_IF_STATE_ENABLING = 1,
	_MPODP_IF_STATE_ENABLED = 2,
	_MPODP_IF_STATE_DISABLING = 3,
	_MPODP_IF_STATE_REMOVING = 4,
};

struct mpodp_tx {
	struct sk_buff *skb;
	struct scatterlist sg[MAX_SKB_FRAGS + 1];
	u32 sg_len;
	dma_cookie_t cookie;
	u32 len;		/* to be able to free the skb in the TX 2nd step */
	union mppa_timestamp time;
	int jiffies;
	dma_addr_t dst_addr;
	int chanidx;
};

struct mpodp_rx {
	struct sk_buff *skb;
	struct scatterlist sg[1];
	u32 sg_len;
	u32 dma_len;
	dma_cookie_t cookie;
	void *entry_addr;
	u32 len;		/* avoid to re-read the entry in the RX 2nd step */
};

struct mpodp_cache_entry {
	void *entry_addr;
	u32 addr;
};

struct mpodp_txq {
	int id;

	/* Pointer to the netdev associated txq */
	struct netdev_queue *txq;

	struct mpodp_tx *ring;
	/* Position of the latest complete Tx buffer.
	 * same as tail in MPPA Tx ring buffer
	 * Range [0 .. tx_size [ */
	atomic_t done;

	/* Position of the latest submited desc
	 * Range [0 .. tx_size [ */
	atomic_t submitted;

	/* Current idx in the autoloop MPPA RB */
	atomic_t autoloop_cur;

	/* Current Tx head on the MPPA (equals number of targets) */
	atomic_t head;
	/* Host mapped addres to reload tx_head from the MPPA */
	u8 __iomem *head_addr;

	/* Size of the Tx RB on the MPPA */
	int mppa_size;
	/* Number of descriptors on the host */
	int size;

	/* Amount of Tx cached Host side in autoloop mode. Size = tx_size */
	int cached_head;
	/* Cached adresses from the MPPA side. Size = tx_mppa_size */
	struct mpodp_cache_entry *cache;
};

struct mpodp_rxq {
	int id;
	struct mpodp_rx *ring;
	int used;
	int avail;
	int tail;
	u8 __iomem *tail_addr;
	uint16_t  *tail_host_addr;
	dma_addr_t tail_handle;
	int head;
	u8 __iomem *head_addr;
	int size;
	struct mpodp_c2h_entry *mppa_entries;
	dma_addr_t mppa_entries_handle;
};

struct mpodp_if_priv {
	struct napi_struct napi;

	struct mppa_pcie_device *pdata;
	struct pci_dev *pdev;	/* pointer to device structure */
	const struct mpodp_pdata_priv *pdata_priv;
	struct dentry *dir;

	struct mpodp_if_config *config;
	struct net_device *netdev;

	atomic_t reset;

	int interrupt_status;
	u8 __iomem *interrupt_status_addr;

	/* TX ring */
	spinlock_t tx_lock[MPODP_NOC_CHAN_COUNT];
	struct dma_chan *tx_chan[MPODP_NOC_CHAN_COUNT];
	struct mppa_pcie_dma_slave_config tx_config[MPODP_NOC_CHAN_COUNT];
	struct mpodp_txq txqs[MPODP_MAX_TX_QUEUES];
	int n_txqs;

	struct timer_list tx_timer;	/* checks Tx queues */
	struct mppa_pcie_time *tx_time;
	uint64_t packet_id;

	/* RX ring */
	struct dma_chan *rx_chan;
	struct mppa_pcie_dma_slave_config rx_config;
	struct mpodp_rxq rxqs[MPODP_MAX_RX_QUEUES];
	int n_rxqs;

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
u16 mpodp_select_queue(struct net_device *dev, struct sk_buff *skb
#if (LINUX_VERSION_CODE > KERNEL_VERSION (3, 13, 0))
		       , void *accel_priv, select_queue_fallback_t fallback
#endif
);

void mpodp_tx_timeout(struct net_device *netdev);
void mpodp_tx_update_cache(struct mpodp_if_priv *priv);
void mpodp_tx_timer_cb(unsigned long data);
int mpodp_clean_tx(struct mpodp_if_priv *priv, unsigned budget);

int mpodp_start_rx(struct mpodp_if_priv *priv, struct mpodp_rxq *rxq);
int mpodp_clean_rx(struct mpodp_if_priv *priv, struct mpodp_rxq *rxq, int budget);

#endif
