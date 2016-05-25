/*
 * odp_tx.c: MPPA PCI Express device driver: Network Device for ODP Packet Tx
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

void mpodp_tx_timeout(struct net_device *netdev)
{
	netdev_err(netdev, "tx timeout\n");
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

int mpodp_clean_tx(struct mpodp_if_priv *priv, unsigned budget)
{
	struct netdev_queue *txq = netdev_get_tx_queue(priv->netdev, 0);
	int worked = 0;

	if (__netif_tx_trylock(txq)) {
		worked = mpodp_clean_tx_unlocked(priv, MPODP_MAX_TX_RECLAIM);
		__netif_tx_unlock(txq);
	}
	return worked;
}

netdev_tx_t mpodp_start_xmit(struct sk_buff *skb,
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
	uint32_t tx_full;
	uint32_t tx_mppa_idx;

	/* make room before adding packets */
	mpodp_clean_tx_unlocked(priv, -1);

	tx_submitted = atomic_read(&priv->tx_submitted);
	/* Compute txd id */
	tx_next = (tx_submitted + 1);
	if (tx_next == priv->tx_size)
		tx_next = 0;

	/* MPPA H2C Entry to use */
	tx_mppa_idx = atomic_read(&priv->tx_autoloop_cur);

	/* Check if there are txd available */
	tx_full = atomic_read(&priv->tx_done);
	if (tx_next == tx_full) {
		/* Ring is full */
		netdev_err(netdev, "TX ring full \n");
		netif_stop_queue(netdev);
		return NETDEV_TX_BUSY;
	}

	tx = &(priv->tx_ring[tx_submitted]);
	entry = &(priv->tx_cache[tx_mppa_idx]);

	netdev_vdbg(netdev,
		    "Alloc TX packet descriptor %p/%d (MPPA Idx = %d)\n",
		    tx, tx_submitted, tx_mppa_idx);

	/* take the time */
	mppa_pcie_time_get(priv->tx_time, &tx->time);

	/* configure channel */
	tx->dst_addr = entry->addr;

	/* Check the provided address */
	ret =
	    mppa_pcie_dma_check_addr(priv->pdata, tx->dst_addr, &fifo_mode,
				     &requested_engine);
	if (ret) {
		netdev_err(netdev, "tx %d: invalid send address %llx\n",
			   tx_submitted, tx->dst_addr);
		goto addr_error;
	}
	if (!fifo_mode) {
		netdev_err(netdev, "tx %d: %llx is not a PCI2Noc addres\n",
			   tx_submitted, tx->dst_addr);
		goto addr_error;
	}
	if (requested_engine >= MPODP_NOC_CHAN_COUNT) {
		netdev_err(netdev,
			   "tx %d: address %llx using NoC engine out of range (%d >= %d)\n",
			   tx_submitted, tx->dst_addr,
			   requested_engine, MPODP_NOC_CHAN_COUNT);
		goto addr_error;
	}

	tx->chanidx = requested_engine;

	/* Prepare slave args */
	priv->tx_config[requested_engine].cfg.dst_addr = tx->dst_addr;
	priv->tx_config[requested_engine].requested_engine = requested_engine;
	/* FIFO mode, direction, latency were filled at setup */

	netdev_vdbg(netdev,
		    "tx %d: sending to 0x%llx. Fifo=%d Engine=%d\n",
		    tx_submitted, (uint64_t) tx->dst_addr, fifo_mode,
		    requested_engine);

	/* The packet needs a header to determine size,timestamp, etc.
	 * Add it */
	if (skb_headroom(skb) < sizeof(struct mpodp_pkt_hdr)) {
		struct sk_buff *skb_new;

		skb_new =
			skb_realloc_headroom(skb, sizeof(struct mpodp_pkt_hdr));
		if (!skb_new) {
			netdev->stats.tx_errors++;
			kfree_skb(skb);
			return NETDEV_TX_OK;
		}
		kfree_skb(skb);
		skb = skb_new;
	}

	hdr = (struct mpodp_pkt_hdr *)
		skb_push(skb, sizeof(struct mpodp_pkt_hdr));
	hdr->timestamp = priv->packet_id;
	hdr->info._.pkt_id = priv->packet_id;
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
	dma_len = dma_map_sg(&priv->pdev->dev, tx->sg,
			     tx->sg_len, DMA_TO_DEVICE);
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
		    "tx %d: transfer start (submitted: %d done: %d) len=%d, sg_len=%d\n",
		    tx_submitted, tx_next, atomic_read(&priv->tx_done),
		    tx->len, tx->sg_len);

	skb_orphan(skb);

	/* submit and issue descriptor */
	tx->cookie = dmaengine_submit(dma_txd);
	dma_async_issue_pending(priv->tx_chan[requested_engine]);

	/* Count number of bytes on the fly for DQL */
	netdev_sent_queue(netdev, skb->len);

	/* Increment tail pointer locally */
	atomic_set(&priv->tx_submitted, tx_next);

	/* Update H2C entry offset */
	tx_autoloop_next = tx_mppa_idx + 1;
	if (tx_autoloop_next == priv->tx_cached_head)
		tx_autoloop_next = 0;
	atomic_set(&priv->tx_autoloop_cur, tx_autoloop_next);

	skb_tx_timestamp(skb);

	/* Check if there is room for another txd
	 * or stop the queue if there is not */
	tx_next = (tx_next + 1);
	if (tx_next == priv->tx_size)
		tx_next = 0;

	if (tx_next == tx_full) {
		netdev_dbg(netdev, "TX ring full \n");
		netif_stop_queue(netdev);
	}

	return NETDEV_TX_OK;

      busy:
	dma_unmap_sg(&priv->pdev->dev, tx->sg, tx->sg_len, DMA_TO_DEVICE);
	return NETDEV_TX_BUSY;

 addr_error:
	netdev->stats.tx_dropped++;
	dev_kfree_skb(skb);
	/* We can't do anything, just stop the queue artificially */
	netif_stop_queue(netdev);
	return NETDEV_TX_BUSY;
}

void mpodp_tx_timer_cb(unsigned long data)
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
			priv->tx_cached_head++;
		}
	}

	mod_timer(&priv->tx_timer, jiffies +
		  (worked < MPODP_MAX_TX_RECLAIM ?
		   MPODP_TX_RECLAIM_PERIOD : 2));
}
