/*
 * mppa_pcie_netdev.c: MPPA PCI Express device driver: Network Device
 *
 * (C) Copyright 2015 Kalray
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License, or (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/skbuff.h>
#include <linux/stddef.h>
#include <linux/spinlock.h>
#include <linux/jump_label.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/version.h>

#include <mppa_pcie_api.h>
#include "mppapcie_odp.h"
#include "odp.h"

#if (LINUX_VERSION_CODE > KERNEL_VERSION (3, 12, 0))
#define DMA_SUCCESS DMA_COMPLETE
#endif


/**
 * List of per-netdev device (not netdev interfaces)
 */
LIST_HEAD(netdev_device_list);
DEFINE_MUTEX(netdev_device_list_mutex);


static uint32_t mpodp_get_eth_control_addr(struct mppa_pcie_device *pdata)
{
	uint32_t addr;
	int ret =
	    mppa_pcie_get_service_addr(pdata, PCIE_SERVICE_ODP, &addr);

	if (ret < 0)
		return 0;

	return addr;
}

static void mpodp_tx_timeout(struct net_device *netdev)
{
	netdev_err(netdev, "tx timeout\n");
}

static struct net_device_stats *mpodp_get_stats(struct net_device *netdev)
{
	/* TODO: get stats from the MPPA */

	return &netdev->stats;
}

static int mpodp_rx_is_done(struct mpodp_if_priv *priv, int index)
{
	if (priv->rx_ring[index].len == 0) {
		/* error packet, always done */
		return 1;
	}

	return (dmaengine_tx_status
		(priv->rx_chan, priv->rx_ring[index].cookie,
		 NULL) == DMA_SUCCESS);
}

static int mpodp_clean_rx(struct mpodp_if_priv *priv,
			  int budget, int *work_done)
{
	struct net_device *netdev = priv->netdev;
	struct mpodp_rx *rx;
	struct dma_async_tx_descriptor *dma_txd;
	int dma_len, limit, worked = 0;

	/* RX: 2nd step: give packet to kernel and update RX head */
	while (budget-- && priv->rx_used != priv->rx_avail) {
		if (!mpodp_rx_is_done(priv, priv->rx_used)) {
			/* DMA transfer not completed */
			break;
		}

		netdev_dbg(netdev, "rx %d: transfer done\n",
			   priv->rx_used);

		/* get rx slot */
		rx = &(priv->rx_ring[priv->rx_used]);

		if (rx->len == 0) {
			/* error packet, skip it */
			goto pkt_skip;
		}

		dma_unmap_sg(&priv->pdev->dev, rx->sg, 1, DMA_FROM_DEVICE);

		/* fill skb field */
		skb_put(rx->skb, rx->len);
		rx->skb->protocol = eth_type_trans(rx->skb, netdev);
		/* rx->skb->csum = readl(rx->entry_addr +
		   offsetof(struct mpodp_c2h_ring_buff_entry, checksum); */
		/* rx->skb->ip_summed = CHECKSUM_COMPLETE; */
		napi_gro_receive(&priv->napi, rx->skb);

		/* update stats */
		netdev->stats.rx_bytes += rx->len;
		netdev->stats.rx_packets++;

	      pkt_skip:
		priv->rx_used = (priv->rx_used + 1) % priv->rx_size;

		(*work_done)++;
	}
	/* write new RX head */
	if (work_done) {
		writel(priv->rx_used, priv->rx_head_addr);
	}

	/* RX: 1st step: start transfer */
	/* read RX tail */
	priv->rx_tail = readl(priv->rx_tail_addr);

	if (priv->rx_avail > priv->rx_tail) {
		/* make a first loop to the end of the ring */
		limit = priv->rx_size;
	} else {
		limit = priv->rx_tail;
	}

      loop:
	/* get mppa entries */
	memcpy_fromio(priv->rx_mppa_entries + priv->rx_avail,
		      priv->rx_ring[priv->rx_avail].entry_addr,
		      sizeof(struct mpodp_c2h_ring_buff_entry) *
		      (limit - priv->rx_avail));
	while (priv->rx_avail != limit) {
		/* get rx slot */
		rx = &(priv->rx_ring[priv->rx_avail]);

		/* check rx status */
		if (priv->rx_mppa_entries[priv->rx_avail].status) {
			/* TODO: report correctly the error */
			rx->len = 0;	/* means this is an error packet */
			goto pkt_error;
		}

		/* read rx entry information */
		rx->len = priv->rx_mppa_entries[priv->rx_avail].len;

		/* get skb from kernel */
		rx->skb = netdev_alloc_skb_ip_align(priv->netdev, rx->len);
		if (rx->skb == NULL) {
			goto skb_failed;
		}

		/* prepare sg */
		sg_set_buf(rx->sg, rx->skb->data, rx->len);
		dma_len =
		    dma_map_sg(&priv->pdev->dev, rx->sg, 1,
			       DMA_FROM_DEVICE);
		if (dma_len == 0) {
			goto map_failed;
		}

		/* configure channel */
		priv->rx_config.cfg.src_addr =
		    priv->rx_mppa_entries[priv->rx_avail].pkt_addr;
		if (dmaengine_slave_config
		    (priv->rx_chan, &priv->rx_config.cfg)) {
			/* board has reset, wait for reset of netdev */
			netif_stop_queue(netdev);
			netif_carrier_off(netdev);
			netdev_err(netdev,
				   "rx %d: cannot configure channel\n",
				   priv->rx_avail);
			break;
		}

		/* get transfer descriptor */
		dma_txd =
		    dmaengine_prep_slave_sg(priv->rx_chan, rx->sg, dma_len,
					    DMA_DEV_TO_MEM, 0);
		if (dma_txd == NULL) {
			netdev_err(netdev,
				   "rx %d: cannot get dma descriptor",
				   priv->rx_avail);
			goto dma_failed;
		}

		netdev_dbg(netdev, "rx %d: transfer start\n",
			   priv->rx_avail);

		/* submit and issue descriptor */
		rx->cookie = dmaengine_submit(dma_txd);
		dma_async_issue_pending(priv->rx_chan);

	      pkt_error:
		priv->rx_avail++;

		worked++;

		continue;

	      dma_failed:
		dma_unmap_sg(&priv->pdev->dev, rx->sg, 1, DMA_FROM_DEVICE);
	      map_failed:
		dev_kfree_skb_any(rx->skb);
	      skb_failed:
		/* napi will be rescheduled */
		break;
	}
	if (limit != priv->rx_tail) {
		/* make the second part of the ring */
		limit = priv->rx_tail;
		priv->rx_avail = 0;
		goto loop;
	}

	return worked;
}

static int mpodp_tx_is_done(struct mpodp_if_priv *priv, int index)
{
	return (dmaengine_tx_status
		(priv->tx_chan[priv->tx_ring[index].chanidx],
		 priv->tx_ring[index].cookie, NULL) == DMA_SUCCESS);
}

static int mpodp_clean_tx_unlocked(struct mpodp_if_priv *priv, unsigned budget)
{
	struct net_device *netdev = priv->netdev;
	struct mpodp_tx *tx;
	unsigned int packets_completed = 0;
	unsigned int bytes_completed = 0;
	unsigned int worked = 0;
	union mppa_timestamp ts;
	uint32_t tx_done, first_tx_done, last_tx_done, tx_submitted,
		tx_size, tx_head;

	tx_submitted = atomic_read(&priv->tx_submitted);
	tx_done = atomic_read(&priv->tx_done);
	first_tx_done = tx_done;
	last_tx_done = first_tx_done;

	tx_size = priv->tx_size;
	tx_head = atomic_read(&priv->tx_head);

	if (!tx_head) {
		/* No carrier yet. Check if there are any buffers yet */
		tx_head = readl(priv->tx_head_addr);
		if (tx_head) {
			/* We now have buffers */
			atomic_set(&priv->tx_head, tx_head);
			netif_carrier_on(netdev);

			netdev_dbg(netdev,"Link now has Tx (%u). Bring it up\n",
				   tx_head);
		}
		return 0;
	}

	/* TX: 2nd step: update TX tail (DMA transfer completed) */
	while (tx_done != tx_submitted && worked < budget) {
		if (!mpodp_tx_is_done(priv, tx_done)) {
			/* DMA transfer not completed */
			break;
		}

		netdev_dbg(netdev,
			   "tx %d: transfer done (head: %d submitted: %d done: %d)\n",
			   tx_done, atomic_read(&priv->tx_head),
			   tx_submitted, tx_done);

		/* get TX slot */
		tx = &(priv->tx_ring[tx_done]);

		/* free ressources */
		dma_unmap_sg(&priv->pdev->dev, tx->sg, tx->sg_len,
			     DMA_TO_DEVICE);
		consume_skb(tx->skb);

		worked++;

		tx_done += 1;
		if (tx_done == tx_size)
			tx_done = 0;
		last_tx_done = tx_done;

	}
	/* write new TX tail */
	atomic_set(&priv->tx_done, tx_done);

	/* TX: 3rd step: free finished TX slot */
	while (first_tx_done != last_tx_done) {
		netdev_dbg(netdev,
			   "tx %d: done (head: %d submitted: %d done: %d)\n",
			   first_tx_done, atomic_read(&priv->tx_head),
			   tx_submitted, tx_done);

		/* get TX slot */
		tx = &(priv->tx_ring[first_tx_done]);
		mppa_pcie_time_get(priv->tx_time, &ts);
		mppa_pcie_time_update(priv->tx_time, &tx->time, &ts);

		/* get stats */
		packets_completed++;
		bytes_completed += tx->len;

		first_tx_done += 1;
		if (first_tx_done == tx_size)
			first_tx_done = 0;
	}

	if (!packets_completed) {
		goto out;
	}

	/* update stats */
	netdev->stats.tx_bytes += bytes_completed;
	netdev->stats.tx_packets += packets_completed;

	netdev_completed_queue(netdev, packets_completed, bytes_completed);
	netif_wake_queue(netdev);
      out:
	return worked;
}

static int mpodp_clean_tx(struct mpodp_if_priv *priv, unsigned budget)
{
	struct netdev_queue *txq = netdev_get_tx_queue(priv->netdev, 0);
	int worked = 0;

	if (__netif_tx_trylock(txq)) {
		worked = mpodp_clean_tx_unlocked(priv, MPODP_MAX_TX_RECLAIM);
		__netif_tx_unlock(txq);
	}
	return worked;
}

static void mpodp_dma_callback_rx(void *param)
{
	struct mpodp_if_priv *priv = param;

	napi_schedule(&priv->napi);
}

static void mpodp_tx_timer_cb(unsigned long data)
{
	struct mpodp_if_priv *priv = (struct mpodp_if_priv *) data;
	unsigned long worked = 0;

	worked = mpodp_clean_tx(priv, MPODP_MAX_TX_RECLAIM);

	/* check for new descriptors */
	if (atomic_read(&priv->tx_head) != 0 &&
	    priv->tx_cached_head != priv->tx_size) {
		uint32_t tx_head;
		struct mpodp_cache_entry *entry;

		tx_head = readl(priv->tx_head_addr);
		/* In autoloop, we need to cache new elements */
		while (priv->tx_cached_head < tx_head) {
			entry = &priv->tx_cache[priv->tx_cached_head];

			entry->addr =
			    readq(entry->entry_addr +
				  offsetof(struct mpodp_h2c_ring_buff_entry,
					   pkt_addr));
			entry->flags =
			    readl(entry->entry_addr +
				  offsetof(struct mpodp_h2c_ring_buff_entry,
					   flags));
			priv->tx_cached_head++;
		}
	}

	mod_timer(&priv->tx_timer, jiffies +
		  (worked < MPODP_MAX_TX_RECLAIM ?
		   MPODP_TX_RECLAIM_PERIOD : 2));
}

static int mpodp_poll(struct napi_struct *napi, int budget)
{
	struct mpodp_if_priv *priv;
	int work_done = 0, work = 0;

	priv = container_of(napi, struct mpodp_if_priv, napi);

	netdev_dbg(priv->netdev, "netdev_poll IN\n");

	work = mpodp_clean_rx(priv, budget, &work_done);

	if (work_done < budget && work < budget) {
		napi_complete(napi);
	}

	netdev_dbg(priv->netdev, "netdev_poll OUT\n");

	return work_done;
}

#ifdef CONFIG_NET_POLL_CONTROLLER
static void mpodp_poll_controller(struct net_device *netdev)
{
	struct mpodp_if_priv *priv = netdev_priv(netdev);

	napi_schedule(&priv->napi);
}
#endif

static netdev_tx_t mpodp_start_xmit(struct sk_buff *skb,
				    struct net_device *netdev)
{
	struct mpodp_if_priv *priv = netdev_priv(netdev);
	struct mpodp_tx *tx;
	struct dma_async_tx_descriptor *dma_txd;
	struct mpodp_cache_entry *entry;
	int dma_len, ret;
	uint8_t fifo_mode, requested_engine;
	struct mpodp_pkt_hdr *hdr;
	uint32_t tx_autoloop_next;
	uint32_t tx_submitted, tx_next;
	uint32_t tx_full, tx_head;
	uint32_t tx_mppa_idx;

	/* make room before adding packets */
	mpodp_clean_tx_unlocked(priv, -1);

	tx_submitted = atomic_read(&priv->tx_submitted);
	tx_next = (tx_submitted + 1);
	if (tx_next == priv->tx_size)
		tx_next = 0;
	tx_head = atomic_read(&priv->tx_head);

	/* Compute the ID of the descriptor use on the MPPA side
	 * - In std mode it's the same
	 * - In autoloop, it's not because the Host desc ring buffer does NOT
	 *   have the same size as the MPPA Tx RB */
	tx_mppa_idx = atomic_read(&priv->tx_autoloop_cur);
	tx_full = atomic_read(&priv->tx_done);

	if (tx_next == tx_full) {
		/* Ring is full */
		netdev_err(netdev, "TX ring full \n");
		netif_stop_queue(netdev);
		return NETDEV_TX_BUSY;
	}

	/* TX: 1st step: start TX transfer */

	/* get tx slot */
	tx = &(priv->tx_ring[tx_submitted]);
	entry = &(priv->tx_cache[tx_mppa_idx]);

	netdev_vdbg(netdev,
		    "Alloc TX packet descriptor %p/%d (MPPA Idx = %d)\n",
		    tx, tx_submitted, tx_mppa_idx);

	/* take the time */
	mppa_pcie_time_get(priv->tx_time, &tx->time);

	/* configure channel */
	tx->dst_addr = entry->addr;
	tx->flags = entry->flags;

	/* Check the provided address */
	ret =
	    mppa_pcie_dma_check_addr(priv->pdata, tx->dst_addr, &fifo_mode,
				     &requested_engine);
	if ((ret) ||
	    !fifo_mode ||
	    requested_engine >= MPODP_NOC_CHAN_COUNT){
		if (ret) {
			netdev_err(netdev,
				   "tx %d: invalid send address %llx\n",
				   tx_submitted, tx->dst_addr);
		} else if (!fifo_mode) {
			netdev_err(netdev,
				   "tx %d: %llx is not a PCI2Noc addres\n",
				   tx_submitted, tx->dst_addr);
		} else {
			netdev_err(netdev,
				   "tx %d: address %llx using NoC engine out of range (%d >= %d)\n",
				   tx_submitted, tx->dst_addr,
				   requested_engine, MPODP_NOC_CHAN_COUNT);
		}
		netdev->stats.tx_dropped++;
		dev_kfree_skb(skb);
		/* We can't do anything, just stop the queue artificially */
		netif_stop_queue(netdev);
		return NETDEV_TX_BUSY;
	}

	tx->chanidx = requested_engine;
	priv->tx_config[requested_engine].cfg.dst_addr = tx->dst_addr;
	priv->tx_config[requested_engine].fifo_mode = fifo_mode;
	priv->tx_config[requested_engine].requested_engine = requested_engine;

	netdev_vdbg(netdev,
		    "tx %d: sending to 0x%llx. Fifo=%d Engine=%d\n",
		    tx_submitted, (uint64_t) tx->dst_addr, fifo_mode,
		    requested_engine);

	/* The packet needs a header to determine size, add it */
	netdev_dbg(netdev, "tx %d: Adding header to packet\n",
		   tx_submitted);
	if (skb_headroom(skb) < sizeof(struct mpodp_pkt_hdr)) {
		struct sk_buff *skb_new;

		skb_new =
			skb_realloc_headroom(skb,
					     sizeof(struct
						    mpodp_pkt_hdr));
		if (!skb_new) {
			netdev->stats.tx_errors++;
			kfree_skb(skb);
			return NETDEV_TX_OK;
		}
		kfree_skb(skb);
		skb = skb_new;
	}

	hdr =
		(struct mpodp_pkt_hdr *) skb_push(skb,
						  sizeof(struct
							 mpodp_pkt_hdr));
	hdr->timestamp = priv->packet_id;
	hdr->info.dword = 0ULL;
	hdr->info._.pkt_size =
		(skb->len - sizeof(struct mpodp_pkt_hdr));
	hdr->info._.pkt_id = priv->packet_id;
	priv->packet_id++;

	/* save skb to free it later */
	tx->skb = skb;
	tx->len = skb->len;

	/* prepare sg */
	sg_init_table(tx->sg, MAX_SKB_FRAGS + 1);
	tx->sg_len = skb_to_sgvec(skb, tx->sg, 0, skb->len);
	dma_len =
	    dma_map_sg(&priv->pdev->dev, tx->sg, tx->sg_len,
		       DMA_TO_DEVICE);
	if (dma_len == 0) {
		/* dma_map_sg failed, retry */
		netdev_err(netdev, "tx %d: failed to map sg to dma\n",
			   tx_submitted);
		goto busy;
	}

	if (dmaengine_slave_config(priv->tx_chan[requested_engine],
				   &priv->tx_config[requested_engine].cfg)) {
		/* board has reset, wait for reset of netdev */
		netif_stop_queue(netdev);
		netif_carrier_off(netdev);
		netdev_err(netdev, "tx %d: cannot configure channel\n",
			   tx_submitted);
		goto busy;
	}

	/* get transfer descriptor */
	dma_txd =
	    dmaengine_prep_slave_sg(priv->tx_chan[requested_engine], tx->sg,
				    dma_len, DMA_MEM_TO_DEV, 0);
	if (dma_txd == NULL) {
		/* dmaengine_prep_slave_sg failed, retry */
		netdev_err(netdev, "tx %d: cannot get dma descriptor\n",
			   tx_submitted);
		goto busy;
	}
	netdev_vdbg(netdev,
		    "tx %d: transfer start (head: %d submitted: %d done: %d) len=%d, sg_len=%d\n",
		    tx_submitted, atomic_read(&priv->tx_head), tx_next,
		    atomic_read(&priv->tx_done), tx->len, tx->sg_len);

	skb_orphan(skb);

	/* submit and issue descriptor */
	tx->cookie = dmaengine_submit(dma_txd);
	dma_async_issue_pending(priv->tx_chan[requested_engine]);

	netdev_sent_queue(netdev, skb->len);

	/* Increment tail pointer locally */
	atomic_set(&priv->tx_submitted, tx_next);

	tx_autoloop_next = tx_mppa_idx + 1;
	if (tx_autoloop_next == priv->tx_cached_head)
		tx_autoloop_next = 0;
	atomic_set(&priv->tx_autoloop_cur, tx_autoloop_next);

	skb_tx_timestamp(skb);

	tx_next = (tx_next + 1);
	if (tx_next == priv->tx_size)
		tx_next = 0;

	if (tx_next == tx_full) {
		/* Ring is full, stop the queue */
		netdev_dbg(netdev, "TX ring full \n");
		netif_stop_queue(netdev);
	}

	return NETDEV_TX_OK;

      busy:
	dma_unmap_sg(&priv->pdev->dev, tx->sg, tx->sg_len, DMA_TO_DEVICE);
	return NETDEV_TX_BUSY;
}


static int mpodp_open(struct net_device *netdev)
{
	struct mpodp_if_priv *priv = netdev_priv(netdev);
	uint32_t tx_tail;

	tx_tail = readl(priv->tx_tail_addr);
	atomic_set(&priv->tx_submitted, tx_tail);
	atomic_set(&priv->tx_head, readl(priv->tx_head_addr));
	atomic_set(&priv->tx_done, tx_tail);
	atomic_set(&priv->tx_autoloop_cur, 0);

	priv->rx_tail = readl(priv->rx_tail_addr);
	priv->rx_head = readl(priv->rx_head_addr);
	priv->rx_used = priv->rx_head;
	priv->rx_avail = priv->rx_tail;

	atomic_set(&priv->reset, 0);

	netif_start_queue(netdev);
	napi_enable(&priv->napi);

	priv->interrupt_status = 1;
	writel(priv->interrupt_status, priv->interrupt_status_addr);

	mod_timer(&priv->tx_timer, jiffies + MPODP_TX_RECLAIM_PERIOD);

	if (atomic_read(&priv->tx_head) != 0) {
		netif_carrier_on(netdev);
	} else {
		netif_carrier_off(netdev);
	}

	return 0;
}

static int mpodp_close(struct net_device *netdev)
{
	struct mpodp_if_priv *priv = netdev_priv(netdev);

	priv->interrupt_status = 0;
	writel(priv->interrupt_status, priv->interrupt_status_addr);

	if (priv->tx_timer.function)
		del_timer_sync(&priv->tx_timer);

	netif_carrier_off(netdev);

	napi_disable(&priv->napi);
	netif_stop_queue(netdev);

	mpodp_clean_tx(priv, -1);

	return 0;
}

static const struct net_device_ops mpodp_ops = {
	.ndo_open = mpodp_open,
	.ndo_stop = mpodp_close,
	.ndo_start_xmit = mpodp_start_xmit,
	.ndo_get_stats = mpodp_get_stats,
	.ndo_tx_timeout = mpodp_tx_timeout,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller = mpodp_poll_controller,
#endif
};

static void mpodp_remove(struct net_device *netdev)
{
	struct mpodp_if_priv *priv = netdev_priv(netdev);
	int chanidx;


	/* unregister */
	unregister_netdev(netdev);

	/* clean */
	for (chanidx = 0; chanidx < MPODP_NOC_CHAN_COUNT; chanidx++) {
		dma_release_channel(priv->tx_chan[chanidx]);
	}
	kfree(priv->tx_cache);
	kfree(priv->tx_ring);
	dma_release_channel(priv->rx_chan);
	kfree(priv->rx_ring);
	mppa_pcie_time_destroy(priv->tx_time);
	netif_napi_del(&priv->napi);

	if (priv->tx_timer.function)
		del_timer_sync(&priv->tx_timer);

	free_netdev(netdev);
}

static struct net_device *mpodp_create(struct mppa_pcie_device *pdata,
				       struct mpodp_if_config *config,
				       uint32_t eth_control_addr, int id)
{
	struct net_device *netdev;
	struct mpodp_if_priv *priv;
	dma_cap_mask_t mask;
	char name[64];
	int i, entries_addr;
	int chanidx;
	struct mppa_pcie_id mppa_id;
	struct pci_dev *pdev = mppa_pcie_get_pci_dev(pdata);
	u8 __iomem *smem_vaddr;
	struct mppa_pcie_dmae_filter filter;

	/* initialize mask for dma channel */
	dma_cap_zero(mask);
	dma_cap_set(DMA_SLAVE, mask);
	dma_cap_set(DMA_PRIVATE, mask);

	filter.wanted_device = mppa_pcie_get_dma_device(pdata);

	mppa_pcie_get_id(pdata, &mppa_id);
	snprintf(name, 64, "mppa%d.%d.%d.%d", mppa_id.board_id,
		 mppa_id.chip_id, mppa_id.ioddr_id, id);

	/* alloc netdev */
	if (!(netdev = alloc_netdev(sizeof(struct mpodp_if_priv), name,
#if (LINUX_VERSION_CODE > KERNEL_VERSION (3, 16, 0))
				    NET_NAME_UNKNOWN,
#endif
				    ether_setup))) {
		dev_err(&pdev->dev, "netdev allocation failed\n");
		return NULL;
	}
	if (dev_alloc_name(netdev, netdev->name)) {
		dev_err(&pdev->dev, "netdev name allocation failed\n");
		goto name_alloc_failed;
	}

	/* init netdev */
	netdev->netdev_ops = &mpodp_ops;
	netdev->needed_headroom = sizeof(struct mpodp_pkt_hdr);
	netdev->mtu = config->mtu;
	memcpy(netdev->dev_addr, &(config->mac_addr), 6);
	netdev->features |= NETIF_F_SG | NETIF_F_HIGHDMA | NETIF_F_HW_CSUM;

	/* init priv */
	priv = netdev_priv(netdev);
	priv->pdata = pdata;
	priv->pdev = pdev;
	priv->config = config;
	priv->netdev = netdev;

	smem_vaddr = mppa_pcie_get_smem_vaddr(pdata);
	priv->interrupt_status_addr = smem_vaddr
	    + eth_control_addr + offsetof(struct mpodp_control, configs)
	+ id * sizeof(struct mpodp_if_config)
	+ offsetof(struct mpodp_if_config, interrupt_status);
	netif_napi_add(netdev, &priv->napi, mpodp_poll, MPODP_NAPI_WEIGHT);

	/* init fs */
	snprintf(name, 64, "netdev%d-txtime", id);
	priv->tx_time =
	    mppa_pcie_time_create(name, mppapciefs_get_dentry(pdata),
				  25000, 25000, 40, MPPA_TIME_TYPE_NS);

	/* init RX ring */
	priv->rx_size = readl(desc_info_addr(smem_vaddr,
					     config->
					     c2h_ring_buf_desc_addr,
					     ring_buffer_entries_count));
	priv->rx_head_addr =
	    desc_info_addr(smem_vaddr, config->c2h_ring_buf_desc_addr,
			   head);
	priv->rx_tail_addr =
	    desc_info_addr(smem_vaddr, config->c2h_ring_buf_desc_addr,
			   tail);
	priv->rx_head = readl(priv->rx_head_addr);
	priv->rx_tail = readl(priv->rx_tail_addr);
	entries_addr = readl(desc_info_addr(smem_vaddr,
					    config->c2h_ring_buf_desc_addr,
					    ring_buffer_entries_addr));
	priv->rx_ring =
	    kzalloc(priv->rx_size * sizeof(struct mpodp_rx), GFP_ATOMIC);
	if (priv->rx_ring == NULL) {
		dev_err(&pdev->dev, "RX ring allocation failed\n");
		goto rx_alloc_failed;
	}

	for (i = 0; i < priv->rx_size; ++i) {
		/* initialize scatterlist to 1 as the RX skb is in one chunk */
		sg_init_table(priv->rx_ring[i].sg, 1);
		/* set the RX ring entry address */
		priv->rx_ring[i].entry_addr = smem_vaddr
		    + entries_addr
		    + i * sizeof(struct mpodp_c2h_ring_buff_entry);
	}
	priv->rx_config.cfg.direction = DMA_DEV_TO_MEM;
	priv->rx_config.fifo_mode = _MPPA_PCIE_ENGINE_FIFO_MODE_DISABLED;
	priv->rx_config.short_latency_load_threshold = -1;

	filter.direction = _MPPA_PCIE_ENGINE_DIRECTION_MPPA_TO_HOST;
	priv->rx_chan =
	    dma_request_channel(mask, mppa_pcie_dmae_chan_filter, &filter);
	if (priv->rx_chan == NULL) {
		dev_err(&pdev->dev, "RX chan request failed\n");
		goto rx_chan_failed;
	}
	mppa_pcie_dmaengine_set_channel_interrupt_mode(priv->rx_chan,
						       _MPPA_PCIE_ENGINE_INTERRUPT_CHAN_DISABLED);
	mppa_pcie_dmaengine_set_channel_callback(priv->rx_chan,
						 mpodp_dma_callback_rx,
						 priv);
	mppa_pcie_dmaengine_set_channel_interrupt_mode(priv->rx_chan,
						       _MPPA_PCIE_ENGINE_INTERRUPT_CHAN_ENABLED);
	priv->rx_mppa_entries =
	    kmalloc(priv->rx_size * sizeof(struct mpodp_c2h_ring_buff_entry),
		    GFP_ATOMIC);

	/* init TX ring */
	priv->tx_mppa_size = readl(desc_info_addr(smem_vaddr,
						  config->
						  h2c_ring_buf_desc_addr,
						  ring_buffer_entries_count));
	priv->tx_size = MPODP_AUTOLOOP_DESC_COUNT;

	priv->tx_head_addr = desc_info_addr(smem_vaddr,
					    config->h2c_ring_buf_desc_addr,
					    head);
	priv->tx_tail_addr = desc_info_addr(smem_vaddr,
					    config->h2c_ring_buf_desc_addr,
					    tail);

	/* Setup Host TX Ring */
	priv->tx_ring =
	    kzalloc(priv->tx_size * sizeof(struct mpodp_tx), GFP_ATOMIC);
	if (priv->tx_ring == NULL) {
		dev_err(&pdev->dev, "TX ring allocation failed\n");
		goto tx_alloc_failed;
	}
	for (i = 0; i < priv->tx_size; ++i) {
		/* initialize scatterlist to the maximum size */
		sg_init_table(priv->tx_ring[i].sg, MAX_SKB_FRAGS + 1);
	}

	/* Pre cache MPPA TX Ring */
	priv->tx_cache =
	    kzalloc(priv->tx_mppa_size * sizeof(*priv->tx_cache),
		    GFP_ATOMIC);
	if (priv->tx_cache == NULL) {
		dev_err(&pdev->dev, "TX cache allocation failed\n");
		goto tx_cache_alloc_failed;
	}
	entries_addr = readl(desc_info_addr(smem_vaddr,
					    config->h2c_ring_buf_desc_addr,
					    ring_buffer_entries_addr));
	for (i = 0; i < priv->tx_mppa_size; ++i) {
		/* set the TX ring entry address */
		priv->tx_cache[i].entry_addr = smem_vaddr
		    + entries_addr
		    + i * sizeof(struct mpodp_h2c_ring_buff_entry);
	}

	priv->tx_cached_head = 0;
	for (chanidx = 0; chanidx < MPODP_NOC_CHAN_COUNT; chanidx++) {
		priv->tx_config[chanidx].cfg.direction = DMA_MEM_TO_DEV;
		priv->tx_config[chanidx].fifo_mode =
		    _MPPA_PCIE_ENGINE_FIFO_MODE_DISABLED;
		priv->tx_config[chanidx].short_latency_load_threshold =
		    INT_MAX;

		filter.direction =
		    _MPPA_PCIE_ENGINE_DIRECTION_HOST_TO_MPPA;
		priv->tx_chan[chanidx] =
		    dma_request_channel(mask, mppa_pcie_dmae_chan_filter,
					&filter);
		if (priv->tx_chan[chanidx] == NULL) {
			dev_err(&pdev->dev, "TX chan request failed\n");
			chanidx--;
			goto tx_chan_failed;
		}
		mppa_pcie_dmaengine_set_channel_interrupt_mode(priv->
							       tx_chan
							       [chanidx],
							       _MPPA_PCIE_ENGINE_INTERRUPT_CHAN_DISABLED);

	}
	priv->packet_id = 0ULL;

	/* register netdev */
	if (register_netdev(netdev)) {
		dev_err(&pdev->dev, "failed to register netdev\n");
		goto register_failed;
	}

	/* Tx poll timer */
	setup_timer(&priv->tx_timer, mpodp_tx_timer_cb,
		    (unsigned long) priv);

	printk(KERN_INFO "Registered netdev for %s (ring rx:%d, tx:%d)\n",
	       netdev->name, priv->rx_size, priv->tx_size);

	return netdev;

      register_failed:
      tx_chan_failed:
	while (chanidx >= 0) {
		dma_release_channel(priv->tx_chan[chanidx]);
		chanidx--;
	}
	kfree(priv->tx_cache);
      tx_cache_alloc_failed:
	kfree(priv->tx_ring);
      tx_alloc_failed:
	dma_release_channel(priv->rx_chan);
      rx_chan_failed:
	kfree(priv->rx_ring);
      rx_alloc_failed:
	mppa_pcie_time_destroy(priv->tx_time);
	netif_napi_del(&priv->napi);
      name_alloc_failed:
	free_netdev(netdev);

	return NULL;
}

static int mpodp_is_magic_set(struct mppa_pcie_device *pdata)
{
	uint32_t eth_control_addr;
	int magic;

	eth_control_addr = mpodp_get_eth_control_addr(pdata);
	if (eth_control_addr != 0) {
		/* read magic struct */
		magic = readl(mppa_pcie_get_smem_vaddr(pdata)
			      + eth_control_addr
			      + offsetof(struct mpodp_control, magic));
		if (magic == MPODP_CONTROL_STRUCT_MAGIC) {
			dev_dbg(&(mppa_pcie_get_pci_dev(pdata)->dev),
				"MPPA netdev control struct (0x%x) ready\n",
				eth_control_addr);
			return 1;
		} else {
			dev_dbg(&(mppa_pcie_get_pci_dev(pdata)->dev),
				"MPPA netdev control struct (0x%x) not ready\n",
				eth_control_addr);
			return 0;
		}
	}
	return 0;
}

static void mpodp_enable(struct mppa_pcie_device *pdata,
			 struct mpodp_pdata_priv *netdev)
{
	int i;
	enum _mpodp_if_state last_state;
	uint32_t eth_control_addr;
	int if_count;

	last_state =
	    atomic_cmpxchg(&netdev->state, _MPODP_IF_STATE_DISABLED,
			   _MPODP_IF_STATE_ENABLING);
	if (last_state != _MPODP_IF_STATE_DISABLED) {
		return;
	}

	eth_control_addr = mpodp_get_eth_control_addr(pdata);

	memcpy_fromio(&netdev->control, mppa_pcie_get_smem_vaddr(pdata)
		      + eth_control_addr, sizeof(struct mpodp_control));

	if (netdev->control.magic != MPODP_CONTROL_STRUCT_MAGIC) {
		atomic_set(&netdev->state, _MPODP_IF_STATE_DISABLED);
		return;
	}

	if_count = netdev->control.if_count;

	dev_dbg(&netdev->pdev->dev,
		"enable: initialization of %d interface(s)\n",
		netdev->control.if_count);

	/* add net devices */
	for (i = 0; i < if_count; ++i) {
		netdev->dev[i] =
		    mpodp_create(pdata, netdev->control.configs + i,
			       eth_control_addr, i);
		if (!netdev->dev[i])
			break;
	}
	netdev->if_count = i;

	atomic_set(&netdev->state, _MPODP_IF_STATE_ENABLED);
}

static void mpodp_pre_reset(struct net_device *netdev)
{
	struct mpodp_if_priv *priv = netdev_priv(netdev);
	int now;

	/* There is no real need to lock these changes
	 * but it makes sure that there is no packet being sent
	 * when we release the lock so we just have to wai for the
	 * one being transmitted. */
	netif_tx_lock(netdev);
	atomic_set(&priv->reset, 1);
	netif_stop_queue(netdev);
	netif_carrier_off(netdev);
	netif_tx_unlock(netdev);

	now = jiffies;
	while (atomic_read(&priv->tx_done) !=
	       atomic_read(&priv->tx_submitted)
	       || priv->rx_used != priv->rx_avail) {
		msleep(10);
		if (jiffies - now >= msecs_to_jiffies(1000)) {
			netdev_err(netdev,
				   "Transfer stalled. Removing netdev anyway\n");
			break;
		}
	}
}

static void mpodp_pre_reset_all(struct mpodp_pdata_priv *netdev,
			      struct mppa_pcie_device *pdata)
{
	int i;

	for (i = 0; i < netdev->if_count; ++i) {
		mpodp_pre_reset(netdev->dev[i]);
	}
}


static void mpodp_disable(struct mpodp_pdata_priv *netdev,
			struct mppa_pcie_device *pdata)
{
	int i;
	enum _mpodp_if_state last_state;
	uint32_t eth_control_addr;

	if (!netdev)
		return;

	do {
		last_state = atomic_cmpxchg(&netdev->state,
					    _MPODP_IF_STATE_ENABLED,
					    _MPODP_IF_STATE_DISABLING);

		if (last_state == _MPODP_IF_STATE_ENABLING) {
			msleep(10);
		} else if (last_state != _MPODP_IF_STATE_ENABLED) {
			return;
		}
	} while (last_state == _MPODP_IF_STATE_ENABLING);

	dev_dbg(&(mppa_pcie_get_pci_dev(pdata)->dev),
		"disable: remove interface(s)\n");

	eth_control_addr = mpodp_get_eth_control_addr(pdata);
	writel(0, mppa_pcie_get_smem_vaddr(pdata)
	       + eth_control_addr + offsetof(struct mpodp_control, magic));

	/* delete net devices */
	for (i = 0; i < netdev->if_count; ++i) {
		mpodp_remove(netdev->dev[i]);
	}

	netdev->if_count = 0;

	atomic_set(&netdev->state, _MPODP_IF_STATE_DISABLED);
}

static void mpodp_enable_task(struct work_struct *work)
{
	struct mpodp_pdata_priv *netdev =
	    container_of(work, struct mpodp_pdata_priv, enable);

	mpodp_enable(netdev->pdata, netdev);
}

static irqreturn_t mpodp_interrupt(int irq, void *arg)
{
	struct mpodp_pdata_priv *netdev = arg;
	struct mpodp_if_priv *priv;
	int i;
	enum _mpodp_if_state last_state;
	uint32_t tx_limit;

	dev_dbg(&netdev->pdev->dev, "netdev interrupt IN\n");

	last_state = atomic_read(&netdev->state);

	/* disabled : try to enable */
	if (last_state == _MPODP_IF_STATE_DISABLED) {
		if (mpodp_is_magic_set(netdev->pdata)) {
			schedule_work(&netdev->enable);
		}
	}
	/* not enabled, stop here */
	if (last_state != _MPODP_IF_STATE_ENABLED) {
		dev_dbg(&netdev->pdev->dev,
			"netdev is disabled. interrupt OUT\n");
		return IRQ_HANDLED;
	}

	/* schedule poll call */
	for (i = 0; i < netdev->if_count; ++i) {
		if (!netif_running(netdev->dev[i])) {
			netdev_dbg(netdev->dev[i],
				   "netdev[%d] is not running\n", i);
			continue;
		}

		priv = netdev_priv(netdev->dev[i]);
		if (priv->interrupt_status) {
			netdev_dbg(netdev->dev[i], "Schedule NAPI\n");
			napi_schedule(&priv->napi);
		}

		if (!netif_queue_stopped(netdev->dev[i])
		    || atomic_read(&priv->reset)) {
			netdev_dbg(netdev->dev[i],
				   "netdev[%d] is not stopped (%d) or in reset (%d)\n",
				   i, !netif_queue_stopped(netdev->dev[i]),
				   atomic_read(&priv->reset));
			continue;
		}

		/* Compute the limit where the Tx queue would have been saturated
		 * - In autoloop, submitted is just before done (we don't care at all about head)
		 */
		if (!atomic_read(&priv->tx_head))
			continue;
		tx_limit = atomic_read(&priv->tx_done);
	}
	dev_dbg(&netdev->pdev->dev, "netdev interrupt OUT\n");
	return IRQ_HANDLED;
}

static int
mpodp_dev_notifier_call(struct notifier_block *nb, unsigned long action,
		      void *data)
{
	struct mppa_pcie_device *pdata = (struct mppa_pcie_device *) data;
	struct mpodp_pdata_priv *netdev =
	    container_of(nb, struct mpodp_pdata_priv, notifier);

	switch (action) {
	case MPPA_PCIE_DEV_EVENT_RESET:
		mpodp_pre_reset_all(netdev, pdata);
		mpodp_disable(netdev, pdata);
		break;
	case MPPA_PCIE_DEV_EVENT_POST_RESET:
	case MPPA_PCIE_DEV_EVENT_STATUS:
		break;
	default:
		return NOTIFY_DONE;
	}

	return NOTIFY_OK;
}

static int mpodp_add_device(struct mppa_pcie_device *pdata)
{
	struct mpodp_pdata_priv *netdev;
	int res, irq;

	netdev = kzalloc(sizeof(*netdev), GFP_KERNEL);
	if (!netdev)
		return 1;

	netdev->pdata = pdata;
	netdev->pdev = mppa_pcie_get_pci_dev(pdata);
	dev_dbg(&netdev->pdev->dev, "Attaching a netdev\n");

	irq = mppa_pcie_get_irq(pdata, MPPA_PCIE_IRQ_USER_IT, 0);
	if (irq < 0)
		goto free_netdev;

	dev_dbg(&netdev->pdev->dev, "Registering IRQ %d\n", irq);
	res = request_irq(irq, mpodp_interrupt, IRQF_SHARED, "mppa", netdev);
	if (res)
		goto free_netdev;

	netdev->notifier.notifier_call = mpodp_dev_notifier_call;
	mppa_pcie_dev_register_notifier(pdata, &netdev->notifier);

	atomic_set(&netdev->state, _MPODP_IF_STATE_DISABLED);
	netdev->if_count = 0;

	INIT_WORK(&netdev->enable, mpodp_enable_task);


	if (mpodp_is_magic_set(pdata)) {
		mpodp_enable(pdata, netdev);
	}

	mutex_lock(&netdev_device_list_mutex);
	list_add_tail(&netdev->link, &netdev_device_list);
	mutex_unlock(&netdev_device_list_mutex);

	return 0;

      free_netdev:
	kfree(netdev);
	dev_warn(&netdev->pdev->dev, "Failed to attached a netdev\n");
	return -1;
}

static int mpodp_remove_device(struct mppa_pcie_device *pdata)
{
	struct mpodp_pdata_priv *netdev = NULL, *dev;

	mutex_lock(&netdev_device_list_mutex);
	list_for_each_entry(dev, &netdev_device_list, link) {
		if (dev->pdata == pdata) {
			netdev = dev;
			break;
		}
	}
	if (netdev)
		list_del(&netdev->link);

	mutex_unlock(&netdev_device_list_mutex);

	if (!netdev)
		return 0;

	mpodp_disable(netdev, pdata);

	dev_dbg(&netdev->pdev->dev, "Removing the associated netdev\n");
	free_irq(mppa_pcie_get_irq(pdata, MPPA_PCIE_IRQ_USER_IT, 0),
		 netdev);

	mppa_pcie_dev_unregister_notifier(pdata, &netdev->notifier);
	kfree(netdev);

	return 0;
}

static int
mpodp_notifier_call(struct notifier_block *nb, unsigned long action,
		  void *data)
{
	struct mppa_pcie_device *pdata = (struct mppa_pcie_device *) data;

	switch (action) {
	case MPPA_PCIE_EVENT_PROBE:
		mpodp_add_device(pdata);
		break;
	case MPPA_PCIE_EVENT_REMOVE:
		mpodp_remove_device(pdata);
		break;
	default:
		return NOTIFY_DONE;
	}

	return NOTIFY_OK;
}

static struct notifier_block mpodp_notifier = {
	.notifier_call = mpodp_notifier_call,
};

static int mpodp_init(void)
{
	printk(KERN_INFO "Loading PCIe ethernet driver\n");
	mppa_pcie_register_notifier(&mpodp_notifier);

	return 0;
}

static void mpodp_exit(void)
{
	mppa_pcie_unregister_notifier(&mpodp_notifier);
}

module_init(mpodp_init);
module_exit(mpodp_exit);

MODULE_AUTHOR("Alexis Cellier <alexis.cellier@openwide.fr>");
MODULE_DESCRIPTION("MPPA PCIe Network Device Driver");
MODULE_LICENSE("GPL");
