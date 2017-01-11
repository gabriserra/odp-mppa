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

#define TX_POLL_THRESHOLD 32
void mpodp_tx_timeout(struct net_device *netdev)
{
	struct mpodp_if_priv *priv = netdev_priv(netdev);
	if (netif_msg_tx_err(priv))
		netdev_err(netdev, "tx timeout\n");
}

static int mpodp_tx_is_done(struct mpodp_if_priv *priv, struct mpodp_txq *txq,
			    int index)
{
	return (dmaengine_tx_status
		(priv->tx_chan[txq->ring[index].chanidx],
		 txq->ring[index].cookie, NULL) == DMA_SUCCESS);
}

static void unmap_skb(struct device *dev, const struct sk_buff *skb,
		      const struct mpodp_tx *tx)
{
	const skb_frag_t *fp, *end;
	const struct skb_shared_info *si;
	int count = 1;

	dma_unmap_single(dev, sg_dma_address(&tx->sg[0]), skb_headlen(skb), DMA_TO_DEVICE);

	si = skb_shinfo(skb);
	end = &si->frags[si->nr_frags];
	for (fp = si->frags; fp < end; fp++, count++) {
		dma_unmap_page(dev, sg_dma_address(&tx->sg[count]), skb_frag_size(fp), DMA_TO_DEVICE);
	}
}

static int map_skb(struct device *dev, const struct sk_buff *skb,
		   struct mpodp_tx *tx)
{
	const skb_frag_t *fp, *end;
	const struct skb_shared_info *si;
	int count = 1;
	dma_addr_t handler;

	sg_init_table(tx->sg, MAX_SKB_FRAGS + 1);
	handler = dma_map_single(dev, skb->data, skb_headlen(skb), DMA_TO_DEVICE);
	if (dma_mapping_error(dev, handler))
		goto out_err;
	sg_dma_address(&tx->sg[0]) = handler;
	sg_dma_len(&tx->sg[0]) = skb_headlen(skb);

	si = skb_shinfo(skb);
	end = &si->frags[si->nr_frags];
	for (fp = si->frags; fp < end; fp++, count++) {
		handler = skb_frag_dma_map(dev, fp, 0, skb_frag_size(fp),
					 DMA_TO_DEVICE);
		if (dma_mapping_error(dev, handler))
			goto unwind;

		sg_dma_address(&tx->sg[count]) = handler;
		sg_dma_len(&tx->sg[count]) = skb_frag_size(fp);

	}
	sg_mark_end(&tx->sg[count - 1]);
	tx->sg_len = count;

	return 0;

unwind:
	while (fp-- > si->frags)
		dma_unmap_page(dev, sg_dma_address(&tx->sg[--count]),
			       skb_frag_size(fp), DMA_TO_DEVICE);
	dma_unmap_single(dev, sg_dma_address(&tx->sg[0]),
			 skb_headlen(skb), DMA_TO_DEVICE);

out_err:
	return -ENOMEM;
}

static int mpodp_clean_tx_unlocked(struct mpodp_if_priv *priv,
				   struct mpodp_txq *txq,  unsigned budget)
{
	struct net_device *netdev = priv->netdev;
	struct mpodp_tx *tx;
	unsigned int packets_completed = 0;
	unsigned int bytes_completed = 0;
	unsigned int worked = 0;
	union mppa_timestamp ts;
	uint32_t tx_done, first_tx_done, last_tx_done, tx_submitted,
		tx_size, tx_head;

	tx_submitted = atomic_read(&txq->submitted);
	tx_done = atomic_read(&txq->done);
	first_tx_done = tx_done;
	last_tx_done = first_tx_done;

	tx_size = txq->size;
	tx_head = atomic_read(&txq->head);

	if (!tx_head) {
		/* No carrier yet. Check if there are any buffers yet */
		tx_head = readl(txq->head_addr);
		if (tx_head) {
			/* We now have buffers */
			atomic_set(&txq->head, tx_head);

			if (netif_msg_link(priv))
				netdev_info(netdev,"txq[%d]  now has Tx (%u).\n",
					    txq->id, tx_head);
		}
		return 0;
	}

	/* TX: 2nd step: update TX tail (DMA transfer completed) */
	while (tx_done != tx_submitted && worked < budget) {
		if (!mpodp_tx_is_done(priv, txq, tx_done)) {
			/* DMA transfer not completed */
			break;
		}

		if (netif_msg_tx_done(priv))
			netdev_info(netdev,
				    "txq[%d] tx[%d]: transfer done (head: %d submitted: %d done: %d)\n",
				    txq->id, tx_done, atomic_read(&txq->head),
				    tx_submitted, tx_done);

		/* get TX slot */
		tx = &(txq->ring[tx_done]);

		/* free ressources */
		unmap_skb(&priv->pdev->dev, tx->skb, tx);
		consume_skb(tx->skb);

		worked++;

		tx_done += 1;
		if (tx_done == tx_size)
			tx_done = 0;
		last_tx_done = tx_done;

	}
	/* write new TX tail */
	atomic_set(&txq->done, tx_done);

	/* TX: 3rd step: free finished TX slot */
	while (first_tx_done != last_tx_done) {
		if (netif_msg_tx_done(priv))
			netdev_info(netdev,
				    "txq[%d] tx[%d]: done (head: %d submitted: %d done: %d)\n",
				    txq->id, first_tx_done, atomic_read(&txq->head),
				    tx_submitted, tx_done);

		/* get TX slot */
		tx = &(txq->ring[first_tx_done]);
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

	netdev_tx_completed_queue(txq->txq, packets_completed, bytes_completed);
	netif_tx_wake_queue(txq->txq);
      out:
	return worked;
}

int mpodp_clean_tx(struct mpodp_if_priv *priv, unsigned budget)
{
	int i;
	int worked = 0;

	for (i = 0; i < priv->n_txqs; ++i){
		struct netdev_queue *txq = priv->txqs[i].txq;
		if (__netif_tx_trylock(txq)) {
			worked += mpodp_clean_tx_unlocked(priv, &priv->txqs[i],
							  MPODP_MAX_TX_RECLAIM);
			__netif_tx_unlock(txq);
		}
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
	int ret;
	uint8_t fifo_mode;
	int16_t requested_engine;
	struct mpodp_pkt_hdr *hdr;
	uint32_t tx_autoloop_next;
	uint32_t tx_submitted, tx_next, tx_done;
	uint32_t tx_mppa_idx;
	int qidx;
	struct mpodp_txq *txq;

	/* Fetch HW queue selected by the kernel */
	qidx = skb_get_queue_mapping(skb);
	txq = &priv->txqs[qidx];

	if (atomic_read(&priv->reset) == 1) {
		mpodp_clean_tx_unlocked(priv, txq, -1);
		goto addr_error;
	}

	tx_submitted = atomic_read(&txq->submitted);
	/* Compute txd id */
	tx_next = (tx_submitted + 1);
	if (tx_next == txq->size)
		tx_next = 0;

	/* MPPA H2C Entry to use */
	tx_mppa_idx = atomic_read(&txq->autoloop_cur);

	tx_done = atomic_read(&txq->done);
	if (tx_done != tx_submitted &&
	    ((txq->ring[tx_done].jiffies + msecs_to_jiffies(5) >= jiffies) ||
	     (tx_submitted < tx_done && tx_submitted + txq->size - tx_done >= TX_POLL_THRESHOLD) ||
	     (tx_submitted >= tx_done && tx_submitted - tx_done >= TX_POLL_THRESHOLD))) {
		mpodp_clean_tx_unlocked(priv, txq, -1);
	}

	/* Check if there are txd available */
	if (tx_next == atomic_read(&txq->done)) {
		/* Ring is full */
		if (netif_msg_tx_err(priv))
			netdev_err(netdev, "txq[%d]: ring full \n", txq->id);
		netif_tx_stop_queue(txq->txq);
		return NETDEV_TX_BUSY;
	}

	tx = &(txq->ring[tx_submitted]);
	entry = &(txq->cache[tx_mppa_idx]);

	/* take the time */
	mppa_pcie_time_get(priv->tx_time, &tx->time);

	/* configure channel */
	tx->dst_addr = entry->addr;

	/* Check the provided address */
	ret =
	    mppa_pcie_dma_check_addr(priv->pdata, tx->dst_addr, &fifo_mode,
				     &requested_engine);
	if (ret) {
		if (netif_msg_tx_err(priv))
			netdev_err(netdev, "txq[%d] tx[%d]: invalid send address %llx\n",
				   txq->id, tx_submitted, tx->dst_addr);
		goto addr_error;
	}
	if (!fifo_mode) {
		if (netif_msg_tx_err(priv))
			netdev_err(netdev, "txq[%d] tx[%d]: %llx is not a PCI2Noc addres\n",
				   txq->id, tx_submitted, tx->dst_addr);
		goto addr_error;
	}
	if (requested_engine >= MPODP_NOC_CHAN_COUNT) {
		if (netif_msg_tx_err(priv))
			netdev_err(netdev,
				   "txq[%d] tx[%d]: address %llx using NoC engine out of range (%d >= %d)\n",
				   txq->id, tx_submitted, tx->dst_addr,
				   requested_engine, MPODP_NOC_CHAN_COUNT);
		goto addr_error;
	}

	tx->chanidx = requested_engine;

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
	hdr->info._.pkt_size = skb->len; /* Also count the header size */
	hdr->info._.pkt_id = priv->packet_id;
	priv->packet_id++;

	/* save skb to free it later */
	tx->skb = skb;
	tx->len = skb->len;

	/* prepare sg */
	if (map_skb(&priv->pdev->dev, skb, tx)){
		if (netif_msg_tx_err(priv))
			netdev_err(netdev, "tx %d: failed to map skb to dma\n",
				   tx_submitted);
		goto busy;
	}

	if (priv->n_txqs > MPODP_NOC_CHAN_COUNT)
		spin_lock(&priv->tx_lock[requested_engine]);

	/* Prepare slave args */
	priv->tx_config[requested_engine].cfg.dst_addr = tx->dst_addr;
	priv->tx_config[requested_engine].requested_engine = requested_engine;
	/* FIFO mode, direction, latency were filled at setup */

	if (dmaengine_slave_config(priv->tx_chan[requested_engine],
				   &priv->tx_config[requested_engine].cfg)) {
		/* board has reset, wait for reset of netdev */
		netif_tx_stop_queue(txq->txq);
		netif_carrier_off(netdev);
		if (netif_msg_tx_err(priv))
			netdev_err(netdev, "txq[%d] tx[%d]: cannot configure channel\n",
				   txq->id, tx_submitted);
		goto busy;
	}

	/* get transfer descriptor */
	dma_txd =
	    dmaengine_prep_slave_sg(priv->tx_chan[requested_engine], tx->sg,
				    tx->sg_len, DMA_MEM_TO_DEV, 0);
	if (dma_txd == NULL) {
		/* dmaengine_prep_slave_sg failed, retry */
		if (netif_msg_tx_err(priv))
			netdev_err(netdev, "txq[%d] tx[%d]: cannot get dma descriptor\n",
				   txq->id, tx_submitted);
		goto busy;
	}
	if (netif_msg_tx_queued(priv))
		netdev_info(netdev,
			    "txq[%d] tx[%d]: transfer start (submitted: %d done: %d) len=%d, sg_len=%d\n",
			    txq->id, tx_submitted, tx_next, atomic_read(&txq->done),
			    tx->len, tx->sg_len);

	skb_orphan(skb);

	/* submit and issue descriptor */
	tx->jiffies = jiffies;
	tx->cookie = dmaengine_submit(dma_txd);
	dma_async_issue_pending(priv->tx_chan[requested_engine]);

	if (priv->n_txqs > MPODP_NOC_CHAN_COUNT)
		spin_unlock(&priv->tx_lock[requested_engine]);

	/* Count number of bytes on the fly for DQL */
	netdev_tx_sent_queue(txq->txq, skb->len);
	if (test_bit(__QUEUE_STATE_STACK_XOFF, &txq->txq->state)){
		/* We reached over the limit of DQL. Try to clean some
		 * tx so we are rescheduled right now */
		mpodp_clean_tx_unlocked(priv, txq, -1);
	}

	/* Increment tail pointer locally */
	atomic_set(&txq->submitted, tx_next);

	/* Update H2C entry offset */
	tx_autoloop_next = tx_mppa_idx + 1;
	if (tx_autoloop_next == txq->cached_head)
		tx_autoloop_next = 0;
	atomic_set(&txq->autoloop_cur, tx_autoloop_next);

	skb_tx_timestamp(skb);

	/* Check if there is room for another txd
	 * or stop the queue if there is not */
	tx_next = (tx_next + 1);
	if (tx_next == txq->size)
		tx_next = 0;

	if (tx_next == atomic_read(&txq->done)) {
		if (netif_msg_tx_queued(priv))
			netdev_info(netdev, "txq[%d]: ring full \n", txq->id);
		netif_tx_stop_queue(txq->txq);
	}

	return NETDEV_TX_OK;

      busy:
	unmap_skb(&priv->pdev->dev, skb, tx);
	return NETDEV_TX_BUSY;

 addr_error:
	netdev->stats.tx_dropped++;
	dev_kfree_skb(skb);
	/* We can't do anything, just stop the queue artificially */
	netif_tx_stop_queue(txq->txq);
	return NETDEV_TX_OK;
}

u16 mpodp_select_queue(struct net_device *dev, struct sk_buff *skb
#if (LINUX_VERSION_CODE > KERNEL_VERSION (3, 13, 0))
		       , void *accel_priv, select_queue_fallback_t fallback
#endif
		       )
{
	int txq;

	txq = (skb_rx_queue_recorded(skb)
	       ? skb_get_rx_queue(skb)
	       : smp_processor_id());

	txq = txq % dev->real_num_tx_queues;

	return txq;

#if (LINUX_VERSION_CODE > KERNEL_VERSION (3, 13, 0))
	return fallback(dev, skb) % dev->real_num_tx_queues;
#else
	return __skb_tx_hash(dev, skb, dev->real_num_tx_queues);
#endif
}
void mpodp_tx_update_cache(struct mpodp_if_priv *priv)
{
	int i;
	for (i = 0; i < priv->n_txqs; ++i) {
		struct mpodp_txq *txq = &priv->txqs[i];

		/* check for new descriptors */
		if (atomic_read(&txq->head) != 0 &&
		    txq->cached_head != txq->size) {
			uint32_t tx_head;
			struct mpodp_cache_entry *entry;

			tx_head = readl(txq->head_addr);
			/* Nothing yet */
			if (tx_head < 0)
				continue;

			if (tx_head >= txq->mppa_size) {
				if (netif_msg_tx_err(priv))
					netdev_err(priv->netdev,
						   "Invalid head %d set in Txq[%d]\n", tx_head, txq->id);
				return;
			}
			/* In autoloop, we need to cache new elements */
			while (txq->cached_head < tx_head) {
				entry = &txq->cache[txq->cached_head];

				entry->addr =
					readq(entry->entry_addr +
					      offsetof(struct mpodp_h2c_entry,
						       pkt_addr));
				txq->cached_head++;
			}
		}
	}
}

void mpodp_tx_timer_cb(unsigned long data)
{
	struct mpodp_if_priv *priv = (struct mpodp_if_priv *) data;
	unsigned long worked = 0;

	worked = mpodp_clean_tx(priv, MPODP_MAX_TX_RECLAIM);

	if (!netif_carrier_ok(priv->netdev)) {
		int i, ready = 1;
		for (i = 0; i < priv->n_txqs; ++i) {
			struct mpodp_txq *txq = &priv->txqs[i];

			/* Check if this txq is ready */
			if (atomic_read(&txq->head) == 0) {
				ready = 0;
				break;
			}
		}
		if (ready) {
			mpodp_tx_update_cache(priv);
			if (netif_msg_link(priv))
				netdev_info(priv->netdev, "all queues are ready. Turning carrier ON\n");
			netif_carrier_on(priv->netdev);
		}
	}
	mpodp_tx_update_cache(priv);

	mod_timer(&priv->tx_timer, jiffies +
		  (worked < MPODP_MAX_TX_RECLAIM ?
		   MPODP_TX_RECLAIM_PERIOD : 2));
}
