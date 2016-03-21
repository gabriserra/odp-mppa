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

#include "mppa_pcie_dmaengine_priv.h"
#include "mppa_pcie_priv.h"
#include "mppa_pcie_netdev.h"
#include "mppa_pcie_netdev_priv.h"
#include "mppa_pcie_fs.h"

static uint32_t mppa_pcie_netdev_get_eth_control_addr(struct mppa_pcie_device *pdata)
{
	uint32_t control_addr;
	int ret = mppa_pcie_get_service_addr(pdata, PCIE_SERVICE_ETH, &control_addr);
	return ret == 0 ? control_addr : 0;
}



/* TODO: this function should be provide by mppa dma */
static bool mppa_pcie_netdev_chan_filter(struct dma_chan *chan, void *arg)
{
	struct dma_device *wanted_dma = arg;

	if (!wanted_dma)
		return false;

	return wanted_dma == chan->device;
}

static void mppa_pcie_netdev_tx_timeout(struct net_device *netdev)
{
	struct mppa_pcie_netdev_priv *priv = netdev_priv(netdev);

	netdev_dbg(netdev, "timeout\n");

	/* TODO: what to do? */
	/* TODO: remove this once the above question is anwsered */
	napi_schedule(&priv->napi);
}

static struct net_device_stats *mppa_pcie_netdev_get_stats(struct net_device *netdev)
{
	/* TODO: get stats from the MPPA */

        return &netdev->stats;
}

static void mppa_pcie_netdev_schedule_napi(struct mppa_pcie_netdev_priv *priv)
{
	if (napi_schedule_prep(&priv->napi)) {
		netdev_dbg(priv->netdev, "interrupt: disable\n");
		mppa_pcie_dmaengine_set_channel_interrupt_mode(priv->rx_chan,
							       _MPPA_PCIE_ENGINE_INTERRUPT_CHAN_DISABLED);
		priv->interrupt_status = 0;
		writel(priv->interrupt_status, priv->interrupt_status_addr);
		__napi_schedule(&priv->napi);
	}
}

static void mppa_pcie_netdev_dma_callback(void *param)
{
	struct mppa_pcie_netdev_priv *priv = param;

	netdev_dbg(priv->netdev, "callback\n");

	mppa_pcie_netdev_schedule_napi(priv);
}

static int mppa_pcie_netdev_rx_is_done(struct mppa_pcie_netdev_priv *priv,
				       int index)
{
	if (priv->rx_ring[index].len == 0) {
		/* error packet, always done */
		return 1;
	}

	return (priv->pdata->dma.device_tx_status(priv->rx_chan,
						  priv->rx_ring[index].cookie,
						  NULL) == DMA_SUCCESS);
}

static int mppa_pcie_netdev_clean_rx(struct mppa_pcie_netdev_priv *priv,
				     int budget,
				     int *work_done)
{
	struct net_device *netdev = priv->netdev;
	struct mppa_pcie_netdev_rx *rx;
	struct dma_async_tx_descriptor *dma_txd;
	int dma_len, limit, worked = 0;

	/* RX: 2nd step: give packet to kernel and update RX head */
	while (budget-- && priv->rx_used != priv->rx_avail) {
		if (!mppa_pcie_netdev_rx_is_done(priv, priv->rx_used)) {
			/* DMA transfer not completed */
			break;
		}

		netdev_dbg(netdev, "rx %d: transfer done\n", priv->rx_used);

		/* get rx slot */
		rx = &(priv->rx_ring[priv->rx_used]);

		if (rx->len == 0) {
			/* error packet, skip it */
			goto pkt_skip;
		}

		dma_unmap_sg(&priv->pdata->pdev->dev, rx->sg, 1, DMA_FROM_DEVICE);

		/* fill skb field */
		skb_put(rx->skb, rx->len);
		rx->skb->protocol = eth_type_trans(rx->skb, netdev);
		/* rx->skb->csum = readl(rx->entry_addr + offsetof(struct mppa_pcie_eth_c2h_ring_buff_entry, checksum); */
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
	memcpy_fromio(priv->rx_mppa_entries + priv->rx_avail, priv->rx_ring[priv->rx_avail].entry_addr,
		      sizeof(struct mppa_pcie_eth_c2h_ring_buff_entry) * (limit - priv->rx_avail));
	while (priv->rx_avail != limit) {
		/* get rx slot */
		rx = &(priv->rx_ring[priv->rx_avail]);

		/* check rx status */
		if (priv->rx_mppa_entries[priv->rx_avail].status) {
			/* TODO: report correctly the error */
			rx->len = 0; /* means this is an error packet */
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
	        dma_len = dma_map_sg(&priv->pdata->pdev->dev, rx->sg, 1, DMA_FROM_DEVICE);
		if (dma_len == 0) {
			goto map_failed;
		}

		/* configure channel */
		priv->rx_config.cfg.src_addr = priv->rx_mppa_entries[priv->rx_avail].pkt_addr;
		if (dmaengine_slave_config(priv->rx_chan, &priv->rx_config.cfg)) {
			/* board has reset, wait for reset of netdev */
			netif_carrier_off(netdev);
			netdev_err(netdev, "rx %d: cannot configure channel\n", priv->rx_avail);
			break;
		}

		/* get transfer descriptor */
		dma_txd = dmaengine_prep_slave_sg(priv->rx_chan, rx->sg, dma_len, DMA_DEV_TO_MEM, 0);
		if (dma_txd == NULL) {
			netdev_err(netdev, "rx %d: cannot get dma descriptor", priv->rx_avail);
			goto dma_failed;
		}

		netdev_dbg(netdev, "rx %d: transfer start\n", priv->rx_avail);

		/* submit and issue descriptor */
		rx->cookie = dmaengine_submit(dma_txd);
		dma_async_issue_pending(priv->rx_chan);

	pkt_error:
		priv->rx_avail++;

		worked++;

		continue;

	dma_failed:
		dma_unmap_sg(&priv->pdata->pdev->dev, rx->sg, 1, DMA_FROM_DEVICE);
	map_failed:
		dev_kfree_skb_any(rx->skb);
	skb_failed:
		/* napi will be rescheduled */
		priv->schedule_napi = 1;
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


static int mppa_pcie_netdev_tx_is_done(struct mppa_pcie_netdev_priv *priv,
				       int index)
{
	return (priv->pdata->dma.device_tx_status(priv->tx_chan[priv->tx_ring[index].chanidx],
						  priv->tx_ring[index].cookie,
						  NULL) == DMA_SUCCESS);
}

static int mppa_pcie_netdev_clean_tx(struct mppa_pcie_netdev_priv *priv,
				     int budget)
{
	struct net_device *netdev = priv->netdev;
	struct mppa_pcie_netdev_tx *tx;
	unsigned int packets_completed = 0;
	unsigned int bytes_completed = 0;
	unsigned int worked = 0;
	struct timespec ts;
	uint32_t tx_done, first_tx_done, last_tx_done, tx_submitted, tx_size;
	int chanidx;

	tx_submitted = atomic_read(&priv->tx_submitted);
	tx_done = atomic_read(&priv->tx_done);
	first_tx_done = tx_done;
	last_tx_done = first_tx_done;

	tx_size = priv->tx_size;
	if (priv->config->flags & MPPA_PCIE_ETH_CONFIG_RING_AUTOLOOP) {
		int tx_head = atomic_read(&priv->tx_head);

		if (!tx_head) {
			/* No carrier yet. Check if there are any buffers yet */
			tx_head = readl(priv->tx_head_addr);
			if (tx_head) {
				/* We now have buffers */
				atomic_set(&priv->tx_head, tx_head);
				netif_carrier_on(netdev);

				netdev_dbg(netdev, "Link now has Tx (%u). Bring it up\n", tx_head);
			}
			return 0;

		}
	}

	/* TX: 2nd step: update TX tail (DMA transfer completed) */
	while (tx_done != tx_submitted) {
		if (!mppa_pcie_netdev_tx_is_done(priv, tx_done)) {
			/* DMA transfer not completed */
			break;
		}

		netdev_dbg(netdev, "tx %d: transfer done (head: %d submitted: %d done: %d)\n", tx_done,
			   atomic_read(&priv->tx_head), tx_submitted, tx_done);

		/* get TX slot */
		tx = &(priv->tx_ring[tx_done]);

		/* free ressources */
		dma_unmap_sg(&priv->pdata->pdev->dev, tx->sg, tx->sg_len, DMA_TO_DEVICE);
		dev_kfree_skb_any(tx->skb);

		worked++;

		tx_done += 1;
		if (tx_done == tx_size)
			tx_done = 0;
		last_tx_done = tx_done;

	}
	/* write new TX tail */
	atomic_set(&priv->tx_done, tx_done);
	if (worked && !(priv->config->flags & MPPA_PCIE_ETH_CONFIG_RING_AUTOLOOP)) {
		writel(tx_done, priv->tx_tail_addr);
	}

	/* TX: 3rd step: free finished TX slot */
	while (first_tx_done != last_tx_done) {
		netdev_dbg(netdev, "tx %d: done (head: %d submitted: %d done: %d)\n", first_tx_done,
			   atomic_read(&priv->tx_head), tx_submitted, tx_done);

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

	for(chanidx=0; chanidx <= MPPA_PCIE_NETDEV_NOC_CHAN_COUNT; chanidx ++) {
		mppa_pcie_dmaengine_set_channel_interrupt_mode(priv->tx_chan[chanidx],
							       _MPPA_PCIE_ENGINE_INTERRUPT_CHAN_DISABLED);
	}
	netif_wake_queue(netdev);
 out:
	return worked;
}


static int mppa_pcie_netdev_poll(struct napi_struct *napi, int budget)
{
	struct mppa_pcie_netdev_priv *priv;
	int work_done = 0, work = 0;

	priv = container_of(napi, struct mppa_pcie_netdev_priv, napi);

	netdev_dbg(priv->netdev, "netdev_poll IN\n");

	work = mppa_pcie_netdev_clean_tx(priv, -1);
	work += mppa_pcie_netdev_clean_rx(priv, budget, &work_done);

	if (work_done < budget && work < budget && !priv->schedule_napi) {
		napi_complete(napi);
		netdev_dbg(priv->netdev, "interrupt: enable\n");
		mppa_pcie_dmaengine_set_channel_interrupt_mode(priv->rx_chan,
							       _MPPA_PCIE_ENGINE_INTERRUPT_CHAN_ENABLED);
		priv->interrupt_status = 1;
		writel(priv->interrupt_status, priv->interrupt_status_addr);
	}

	priv->schedule_napi = 0;
	netdev_dbg(priv->netdev, "netdev_poll OUT\n");

	return work_done;
}

#ifdef CONFIG_NET_POLL_CONTROLLER
static void mppa_pcie_netdev_poll_controller(struct net_device *netdev)
{
	struct mppa_pcie_netdev_priv *priv = netdev_priv(netdev);

	napi_schedule(&priv->napi);
}
#endif

static netdev_tx_t mppa_pcie_netdev_start_xmit(struct sk_buff *skb,
					       struct net_device *netdev)
{
	struct mppa_pcie_netdev_priv *priv = netdev_priv(netdev);
	struct mppa_pcie_netdev_tx *tx;
	struct dma_async_tx_descriptor *dma_txd;
	struct mppa_pcie_tx_cache_entry *entry;
	int dma_len, ret;
	uint8_t fifo_mode, requested_engine;
	struct mppa_pcie_eth_pkt_hdr *hdr;

	uint32_t tx_submitted, tx_next;
	uint32_t tx_full, tx_head;
	uint32_t tx_mppa_idx;
	int chanidx = 0;
	const int autoloop = priv->config->flags & MPPA_PCIE_ETH_CONFIG_RING_AUTOLOOP;
	bool refresh_head = false;

	/* make room before adding packets */
	mppa_pcie_netdev_clean_tx(priv, -1);

	tx_submitted = atomic_read(&priv->tx_submitted);
	tx_next = (tx_submitted + 1) % priv->tx_size;
	tx_head = atomic_read(&priv->tx_head);



	/* Compute the ID of the descriptor use on the MPPA side
	 * - In std mode it's the same
	 * - In autoloop, it's not because the Host desc ring buffer does NOT
	 *   have the same size as the MPPA Tx RB */
	if (autoloop) {
		tx_mppa_idx = atomic_read(&priv->tx_autoloop_cur);
		refresh_head = (tx_mppa_idx + 1) == tx_head;
	} else {
		tx_mppa_idx = tx_submitted;
		refresh_head = (tx_next == tx_head);
	}

	/* End of the MPPA side of the list.
	 * Update the head value
	 * - In std mode to see if there may be more free slots
	 * - In autoloop to check for new entries */
	if (refresh_head) {
		/* Try to update first */
		tx_head = readl(priv->tx_head_addr);
		atomic_set(&priv->tx_head, tx_head);

		/* In autoloop, we need to cache new elements */
		while (autoloop && priv->tx_cached_head < tx_head) {
			entry = &priv->tx_cache[priv->tx_cached_head];

			entry->addr =
				readq(entry->entry_addr +
				      offsetof(struct mppa_pcie_eth_h2c_ring_buff_entry, pkt_addr));
			entry->flags =
				readl(entry->entry_addr +
				      offsetof(struct mppa_pcie_eth_h2c_ring_buff_entry, flags));
			priv->tx_cached_head++;
		}
	}

	if (autoloop) {
		tx_full = atomic_read(&priv->tx_done);
	} else {
		tx_full = tx_head;
	}

	if (tx_next == tx_full) {
		/* Ring is full */
		netdev_dbg(netdev, "TX ring full \n");
		netif_stop_queue(netdev);
		for(chanidx=0; chanidx <= MPPA_PCIE_NETDEV_NOC_CHAN_COUNT; chanidx ++) {
			mppa_pcie_dmaengine_set_channel_interrupt_mode(priv->tx_chan[chanidx],
								       _MPPA_PCIE_ENGINE_INTERRUPT_CHAN_ENABLED);
		}
		return NETDEV_TX_BUSY;
	}

	/* TX: 1st step: start TX transfer */

	/* get tx slot */
	tx = &(priv->tx_ring[tx_submitted]);
	entry = &(priv->tx_cache[tx_mppa_idx]);

	netdev_dbg(netdev, "Alloc TX packet descriptor %p/%d (MPPA Idx = %d)\n", tx, tx_submitted, tx_mppa_idx);

	/* take the time */
	mppa_pcie_time_get(priv->tx_time, &tx->time);

	/* configure channel */
	if (!autoloop) {
		tx->dst_addr = readq(entry->entry_addr +
				     offsetof(struct mppa_pcie_eth_h2c_ring_buff_entry, pkt_addr));
		tx->flags = readl(entry->entry_addr +
				  offsetof(struct mppa_pcie_eth_h2c_ring_buff_entry, flags));
	} else {
		tx->dst_addr = entry->addr;
		tx->flags = entry->flags;
	}

	/* Check the provided address */
	ret = mppa_pcie_dma_check_addr(priv->pdata, tx->dst_addr, &fifo_mode, &requested_engine);
	if ((ret) || (fifo_mode && (requested_engine >= MPPA_PCIE_NETDEV_NOC_CHAN_COUNT))) {
		if (ret) {
			netdev_err(netdev, "tx %d: invalid send address %llx\n",
				tx_submitted, tx->dst_addr);
		} else {
			netdev_err(netdev, "tx %d: address %llx using NoC engine out of range (%d >= %d)\n",
				tx_submitted, tx->dst_addr, requested_engine, MPPA_PCIE_NETDEV_NOC_CHAN_COUNT);
		}
		netdev->stats.tx_dropped++;
		dev_kfree_skb(skb);
		/* We can't do anything, just stop the queue artificially */
		netif_stop_queue(netdev);
		return NETDEV_TX_BUSY;
	}
	if (fifo_mode)
		chanidx = requested_engine + 1;
	tx->chanidx = chanidx;
	priv->tx_config[chanidx].cfg.dst_addr = tx->dst_addr;
	priv->tx_config[chanidx].fifo_mode = fifo_mode;
	priv->tx_config[chanidx].requested_engine = requested_engine;

	netdev_dbg(netdev, "tx %d: sending to 0x%llx. Fifo=%d Engine=%d\n", tx_submitted,
		   (uint64_t)tx->dst_addr, fifo_mode, requested_engine);

	/* If the packet needs a header to determine size, add it */
	if (tx->flags & MPPA_PCIE_ETH_NEED_PKT_HDR) {
		netdev_dbg(netdev, "tx %d: Adding header to packet\n", tx_submitted);
		if (skb_headroom(skb) < sizeof(struct mppa_pcie_eth_pkt_hdr)) {
			struct sk_buff *skb_new;

			skb_new = skb_realloc_headroom(skb, sizeof(struct mppa_pcie_eth_pkt_hdr));
			if (!skb_new) {
				netdev->stats.tx_errors++;
				kfree_skb(skb);
				return NETDEV_TX_OK;
			}
			kfree_skb(skb);
			skb = skb_new;
		}

		hdr = (struct mppa_pcie_eth_pkt_hdr *) skb_push(skb, sizeof(struct mppa_pcie_eth_pkt_hdr));
		hdr->length = (skb->len - sizeof(struct mppa_pcie_eth_pkt_hdr));
	}

	/* save skb to free it later */
	tx->skb = skb;
	tx->len = skb->len;

	/* write TX entry length field */
	if (!autoloop)
		writel(skb->len, entry->entry_addr +
		       offsetof(struct mppa_pcie_eth_h2c_ring_buff_entry, len));

	/* prepare sg */
	sg_init_table(tx->sg, MAX_SKB_FRAGS + 1);
	tx->sg_len = skb_to_sgvec(skb, tx->sg, 0, skb->len);
        dma_len = dma_map_sg(&priv->pdata->pdev->dev, tx->sg, tx->sg_len, DMA_TO_DEVICE);
	if (dma_len == 0) {
		/* dma_map_sg failed, retry */
		netdev_err(netdev, "tx %d: failed to map sg to dma\n", tx_submitted);
		goto busy;
	}

	if (dmaengine_slave_config(priv->tx_chan[chanidx], &priv->tx_config[chanidx].cfg)) {
		/* board has reset, wait for reset of netdev */
		netif_carrier_off(netdev);
		netdev_err(netdev, "tx %d: cannot configure channel\n", tx_submitted);
		goto busy;
	}

	/* get transfer descriptor */
	dma_txd = dmaengine_prep_slave_sg(priv->tx_chan[chanidx], tx->sg, dma_len, DMA_MEM_TO_DEV, 0);
	if (dma_txd == NULL) {
		/* dmaengine_prep_slave_sg failed, retry */
		netdev_err(netdev, "tx %d: cannot get dma descriptor\n", tx_submitted);
		goto busy;
	}
	netdev_dbg(netdev, "tx %d: transfer start (head: %d submitted: %d done: %d) len=%d, sg_len=%d\n", tx_submitted,
		   atomic_read(&priv->tx_head), tx_next, atomic_read(&priv->tx_done), tx->len, tx->sg_len);

	/* submit and issue descriptor */
	tx->cookie = dmaengine_submit(dma_txd);
	dma_async_issue_pending(priv->tx_chan[chanidx]);

	/* Increment tail pointer locally */
	atomic_set(&priv->tx_submitted, tx_next);

	if (autoloop) {
		uint32_t tx_autoloop_next = tx_mppa_idx + 1;
		if (tx_autoloop_next == tx_head)
			tx_autoloop_next = 0;
		atomic_set(&priv->tx_autoloop_cur, tx_autoloop_next);
	}
	skb_tx_timestamp(skb);

	return NETDEV_TX_OK;

busy:
	dma_unmap_sg(&priv->pdata->pdev->dev, tx->sg, tx->sg_len, DMA_TO_DEVICE);
	return NETDEV_TX_BUSY;
}


static int mppa_pcie_netdev_open(struct net_device *netdev)
{
	struct mppa_pcie_netdev_priv *priv = netdev_priv(netdev);
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

	priv->schedule_napi = 0;

	atomic_set(&priv->reset, 0);

	netif_start_queue(netdev);
	napi_enable(&priv->napi);

	priv->interrupt_status = 1;
	writel(priv->interrupt_status, priv->interrupt_status_addr);

	if (!(priv->config->flags & MPPA_PCIE_ETH_CONFIG_RING_AUTOLOOP) ||
	    atomic_read(&priv->tx_head) != 0) {
		/* If we are in autoloop mode, we might not have any buffer available et
		 * so keep the carrier off */
		netif_carrier_on(netdev);
	} else {
		netif_carrier_off(netdev);
		netdev_dbg(netdev, "Autoloop enabled but no Tx. Mark link as down\n");
	}

	return 0;
}

static int mppa_pcie_netdev_close(struct net_device *netdev)
{
	struct mppa_pcie_netdev_priv *priv = netdev_priv(netdev);

	priv->interrupt_status = 0;
	writel(priv->interrupt_status, priv->interrupt_status_addr);

	netif_carrier_off(netdev);

	napi_disable(&priv->napi);
	netif_stop_queue(netdev);

	mppa_pcie_netdev_clean_tx(priv, -1);

	return 0;
}

static const struct net_device_ops mppa_pcie_netdev_ops = {
        .ndo_open               = mppa_pcie_netdev_open,
        .ndo_stop               = mppa_pcie_netdev_close,
        .ndo_start_xmit         = mppa_pcie_netdev_start_xmit,
	.ndo_get_stats          = mppa_pcie_netdev_get_stats,
	.ndo_tx_timeout         = mppa_pcie_netdev_tx_timeout,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller    = mppa_pcie_netdev_poll_controller,
#endif
};

static void mppa_pcie_netdev_remove(struct net_device *netdev)
{
	struct mppa_pcie_netdev_priv *priv = netdev_priv(netdev);
	int chanidx;


	/* unregister */
	unregister_netdev(netdev);

	/* clean */
	for(chanidx=0; chanidx <= MPPA_PCIE_NETDEV_NOC_CHAN_COUNT; chanidx ++) {
		dma_release_channel(priv->tx_chan[chanidx]);
	}
	kfree(priv->tx_ring);
	dma_release_channel(priv->rx_chan);
	kfree(priv->rx_ring);
	mppa_pcie_time_destroy(priv->tx_time);
	netif_napi_del(&priv->napi);
	free_netdev(netdev);
}

static struct net_device *mppa_pcie_netdev_create(struct mppa_pcie_device *pdata,
						  struct mppa_pcie_eth_if_config *config,
						  uint32_t eth_control_addr,
						  int id)
{
	struct net_device *netdev;
	struct mppa_pcie_netdev_priv *priv;
	dma_cap_mask_t mask;
	char name[64];
	int i, entries_addr;
	int chanidx;
	const bool autoloop = config->flags & MPPA_PCIE_ETH_CONFIG_RING_AUTOLOOP;

	/* initialize mask for dma channel */
	dma_cap_zero(mask);
	dma_cap_set(DMA_SLAVE, mask);
	dma_cap_set(DMA_PRIVATE, mask);

	snprintf(name, 64, "mppa%d.%d.%d.%d", pdata->chip->board->id, pdata->chip->mppa_id, pdata->io_id, id);

	/* alloc netdev */
	if (!(netdev = alloc_netdev(sizeof (struct mppa_pcie_netdev_priv), name,
	#if (LINUX_VERSION_CODE > KERNEL_VERSION (3, 16, 0))
		NET_NAME_UNKNOWN,
	#endif
		ether_setup))) {
		dev_err(&pdata->pdev->dev, "netdev allocation failed\n");
		return NULL;
	}
	if (dev_alloc_name(netdev, netdev->name)) {
		dev_err(&pdata->pdev->dev, "netdev name allocation failed\n");
		goto name_alloc_failed;
	}

	/* init netdev */
	netdev->netdev_ops = &mppa_pcie_netdev_ops;
	netdev->needed_headroom = sizeof(struct mppa_pcie_eth_pkt_hdr);
	netdev->mtu = config->mtu;
	memcpy(netdev->dev_addr, &(config->mac_addr), 6);
	netdev->features |= NETIF_F_SG
		| NETIF_F_FRAGLIST
		| NETIF_F_HIGHDMA
		| NETIF_F_HW_CSUM;

	/* init priv */
	priv = netdev_priv(netdev);
	priv->pdata = pdata;
	priv->config = config;
	priv->netdev = netdev;
	priv->interrupt_status_addr = SMEM_BAR_VADDR(pdata)
		+ eth_control_addr
		+ offsetof(struct mppa_pcie_eth_control, configs)
		+ id * sizeof(struct mppa_pcie_eth_if_config)
		+ offsetof(struct mppa_pcie_eth_if_config, interrupt_status);
	netif_napi_add(netdev, &priv->napi, mppa_pcie_netdev_poll, MPPA_PCIE_NETDEV_NAPI_WEIGHT);

	/* init fs */
	snprintf(name, 64, "netdev%d-txtime", id);
	priv->tx_time = mppa_pcie_time_create(name, ((struct mppapciefs_devdata *)pdata->fs_data)->ioddr_dir, 25000, 25000, 40);

	/* init RX ring */
   	priv->rx_size = readl(desc_info_addr(pdata, config->c2h_ring_buf_desc_addr, ring_buffer_entries_count));
	priv->rx_head_addr = desc_info_addr(pdata, config->c2h_ring_buf_desc_addr, head);
	priv->rx_tail_addr = desc_info_addr(pdata, config->c2h_ring_buf_desc_addr, tail);
	priv->rx_head = readl(priv->rx_head_addr);
	priv->rx_tail = readl(priv->rx_tail_addr);
	entries_addr = readl(desc_info_addr(pdata, config->c2h_ring_buf_desc_addr, ring_buffer_entries_addr));
	priv->rx_ring = kzalloc(priv->rx_size * sizeof (struct mppa_pcie_netdev_rx), GFP_ATOMIC);
	if (priv->rx_ring == NULL) {
		dev_err(&pdata->pdev->dev, "RX ring allocation failed\n");
		goto rx_alloc_failed;
	}

	for (i = 0; i < priv->rx_size; ++i) {
		/* initialize scatterlist to 1 as the RX skb is in one chunk */
		sg_init_table(priv->rx_ring[i].sg, 1);
		/* set the RX ring entry address */
		priv->rx_ring[i].entry_addr = SMEM_BAR_VADDR(pdata)
			+ entries_addr
			+ i * sizeof (struct mppa_pcie_eth_c2h_ring_buff_entry);
	}
	priv->rx_config.cfg.direction = DMA_DEV_TO_MEM;
	priv->rx_config.fifo_mode = _MPPA_PCIE_ENGINE_FIFO_MODE_DISABLED;
	priv->rx_config.short_latency_load_threshold = -1;
	priv->rx_chan = dma_request_channel(mask, mppa_pcie_netdev_chan_filter, &pdata->dma);
	if (priv->rx_chan == NULL) {
		dev_err(&pdata->pdev->dev, "RX chan request failed\n");
		goto rx_chan_failed;
	}
	mppa_pcie_dmaengine_set_channel_callback(priv->rx_chan, mppa_pcie_netdev_dma_callback, priv);
	mppa_pcie_dmaengine_set_channel_interrupt_mode(priv->rx_chan,
						       _MPPA_PCIE_ENGINE_INTERRUPT_CHAN_ENABLED);
	priv->rx_mppa_entries = kmalloc(priv->rx_size * sizeof (struct mppa_pcie_eth_c2h_ring_buff_entry), GFP_ATOMIC);

	/* init TX ring */
	priv->tx_mppa_size = readl(desc_info_addr(pdata, config->h2c_ring_buf_desc_addr, ring_buffer_entries_count));
	if (autoloop) {
		priv->tx_size = MPPA_PCIE_NETDEV_AUTOLOOP_DESC_COUNT;
	} else {
		priv->tx_size = priv->tx_mppa_size;
	}

	priv->tx_head_addr = desc_info_addr(pdata, config->h2c_ring_buf_desc_addr, head);
	priv->tx_tail_addr = desc_info_addr(pdata, config->h2c_ring_buf_desc_addr, tail);

	/* Setup Host TX Ring */
	priv->tx_ring = kzalloc(priv->tx_size * sizeof (struct mppa_pcie_netdev_tx), GFP_ATOMIC);
	if (priv->tx_ring == NULL) {
		dev_err(&pdata->pdev->dev, "TX ring allocation failed\n");
		goto tx_alloc_failed;
	}
	for (i = 0; i < priv->tx_size; ++i) {
		/* initialize scatterlist to the maximum size */
		sg_init_table(priv->tx_ring[i].sg, MAX_SKB_FRAGS + 1);
	}

	/* Pre cache MPPA TX Ring */
	priv->tx_cache = kzalloc(priv->tx_mppa_size * sizeof(*priv->tx_cache), GFP_ATOMIC);
	if (priv->tx_cache == NULL) {
		dev_err(&pdata->pdev->dev, "TX cache allocation failed\n");
		goto tx_alloc_failed;
	}
	entries_addr = readl(desc_info_addr(pdata, config->h2c_ring_buf_desc_addr, ring_buffer_entries_addr));
	for (i = 0; i < priv->tx_mppa_size; ++i) {
		/* set the TX ring entry address */
		priv->tx_cache[i].entry_addr = SMEM_BAR_VADDR(pdata)
			+ entries_addr
			+ i * sizeof (struct mppa_pcie_eth_h2c_ring_buff_entry);
	}

	priv->tx_cached_head = 0;
	for(chanidx=0; chanidx <= MPPA_PCIE_NETDEV_NOC_CHAN_COUNT; chanidx ++) {
		priv->tx_config[chanidx].cfg.direction = DMA_MEM_TO_DEV;
		priv->tx_config[chanidx].fifo_mode = _MPPA_PCIE_ENGINE_FIFO_MODE_DISABLED;
		priv->tx_config[chanidx].short_latency_load_threshold = INT_MAX;
		priv->tx_chan[chanidx] = dma_request_channel(mask, mppa_pcie_netdev_chan_filter, &pdata->dma);
		if (priv->tx_chan[chanidx] == NULL) {
			dev_err(&pdata->pdev->dev, "TX chan request failed\n");
			chanidx--;
			goto tx_chan_failed;
		}
		mppa_pcie_dmaengine_set_channel_callback(priv->tx_chan[chanidx], mppa_pcie_netdev_dma_callback, priv);
		mppa_pcie_dmaengine_set_channel_interrupt_mode(priv->tx_chan[chanidx],
							       _MPPA_PCIE_ENGINE_INTERRUPT_CHAN_DISABLED);
	}
	/* register netdev */
	if (register_netdev(netdev)) {
		dev_err(&pdata->pdev->dev, "failed to register netdev\n");
		goto register_failed;
	}
	printk(KERN_INFO "Registered netdev for %s (ring rx:%d, tx:%d)\n", netdev->name, priv->rx_size, priv->tx_size);

	return netdev;

 register_failed:
 tx_chan_failed:
	while(chanidx > 0) {
		dma_release_channel(priv->tx_chan[chanidx]);
		chanidx--;
	}
	kfree(priv->tx_ring);
 tx_alloc_failed:
	dma_release_channel(priv->rx_chan);
 rx_chan_failed:
	kfree(priv->rx_ring);
 rx_alloc_failed:
	netif_napi_del(&priv->napi);
 name_alloc_failed:
	free_netdev(netdev);

	return NULL;
}

static int mppa_pcie_netdev_is_magic_set(struct mppa_pcie_device *pdata)
{
	uint32_t eth_control_addr;
	int magic;

	eth_control_addr = mppa_pcie_netdev_get_eth_control_addr(pdata);
	if (eth_control_addr != 0) {
		/* read magic struct */
		magic = readl(SMEM_BAR_VADDR(pdata)
			      + eth_control_addr
			      + offsetof(struct mppa_pcie_eth_control, magic));
		if (magic == MPPA_PCIE_ETH_CONTROL_STRUCT_MAGIC) {
			dev_dbg(&pdata->pdev->dev, "MPPA netdev control struct (0x%x) ready\n", eth_control_addr);
			return 1;
		} else {
			dev_dbg(&pdata->pdev->dev, "MPPA netdev control struct (0x%x) not ready\n", eth_control_addr);
			return 0;
		}
	}
	return 0;
}

static void mppa_pcie_netdev_enable(struct mppa_pcie_device *pdata)
{
	int i;
	enum _mppa_pcie_netdev_state last_state;
	uint32_t eth_control_addr;
	struct mppa_pcie_pdata_netdev * netdev = pdata->netdev;
	int if_count;

	last_state = atomic_cmpxchg(&netdev->state, _MPPA_PCIE_NETDEV_STATE_DISABLED, _MPPA_PCIE_NETDEV_STATE_ENABLING);
	if (last_state != _MPPA_PCIE_NETDEV_STATE_DISABLED) {
		return;
	}

	eth_control_addr = mppa_pcie_netdev_get_eth_control_addr(pdata);

	memcpy_fromio(&netdev->control, SMEM_BAR_VADDR(pdata)
		      + eth_control_addr,
		      sizeof (struct mppa_pcie_eth_control));

	if (netdev->control.magic != MPPA_PCIE_ETH_CONTROL_STRUCT_MAGIC) {
		atomic_set(&netdev->state, _MPPA_PCIE_NETDEV_STATE_DISABLED);
		return;
	}

	if_count = netdev->control.if_count;

	dev_dbg(&pdata->pdev->dev, "enable: initialization of %d interface(s)\n", netdev->control.if_count);

	/* add net devices */
	for (i = 0; i < if_count; ++i) {
		netdev->dev[i] = mppa_pcie_netdev_create(pdata, netdev->control.configs + i, eth_control_addr, i);
		if (!netdev->dev[i])
			break;
	}
	netdev->if_count = i;

	atomic_set(&netdev->state, _MPPA_PCIE_NETDEV_STATE_ENABLED);
}

static void mppa_pcie_netdev_pre_reset(struct net_device *netdev)
{
	struct mppa_pcie_netdev_priv *priv = netdev_priv(netdev);

	atomic_set(&priv->reset, 1);
	netif_carrier_off(netdev);

	while (atomic_read(&priv->tx_done) != atomic_read(&priv->tx_submitted) || priv->rx_used != priv->rx_avail) {
		msleep(10);
	}
}

static void mppa_pcie_netdev_pre_reset_all(struct mppa_pcie_device *pdata)
{
	int i;
	struct mppa_pcie_pdata_netdev * netdev = pdata->netdev;

	for (i = 0; i < netdev->if_count; ++i) {
		mppa_pcie_netdev_pre_reset(netdev->dev[i]);
	}
}


static void mppa_pcie_netdev_disable(struct mppa_pcie_device *pdata)
{
	int i;
	enum _mppa_pcie_netdev_state last_state;
	uint32_t eth_control_addr;
	struct mppa_pcie_pdata_netdev * netdev = pdata->netdev;

	if (!netdev)
		return;

	do {
		last_state = atomic_cmpxchg(&netdev->state,
					    _MPPA_PCIE_NETDEV_STATE_ENABLED,
					    _MPPA_PCIE_NETDEV_STATE_DISABLING);

		if (last_state == _MPPA_PCIE_NETDEV_STATE_ENABLING) {
			msleep(10);
		} else if (last_state != _MPPA_PCIE_NETDEV_STATE_ENABLED) {
			return;
		}
	} while (last_state == _MPPA_PCIE_NETDEV_STATE_ENABLING);

	dev_dbg(&pdata->pdev->dev, "disable: remove interface(s)\n");

	eth_control_addr = mppa_pcie_netdev_get_eth_control_addr(pdata);
	writel(0, SMEM_BAR_VADDR(pdata)
			      + eth_control_addr
			      + offsetof(struct mppa_pcie_eth_control, magic));

	/* delete net devices */
	for (i = 0; i < netdev->if_count; ++i) {
		mppa_pcie_netdev_remove(netdev->dev[i]);
	}

	netdev->if_count = 0;

	atomic_set(&netdev->state, _MPPA_PCIE_NETDEV_STATE_DISABLED);
}

static void mppa_pcie_netdev_enable_task(struct work_struct *work)
{
	struct mppa_pcie_pdata_netdev *netdev =
		container_of(work, struct mppa_pcie_pdata_netdev, enable);

	mppa_pcie_netdev_enable(netdev->pdata);
}

static irqreturn_t mppa_pcie_netdev_interrupt(int irq, void *arg)
{
	struct mppa_pcie_device *pdata = arg;
	struct mppa_pcie_pdata_netdev * netdev = pdata->netdev;
	struct mppa_pcie_netdev_priv *priv;
	int i;
	enum _mppa_pcie_netdev_state last_state;
	uint32_t tx_limit;
	int chanidx;

	dev_dbg(&pdata->pdev->dev, "netdev interrupt IN\n");

	last_state = atomic_read(&netdev->state);

	/* disabled : try to enable */
	if (last_state == _MPPA_PCIE_NETDEV_STATE_DISABLED) {
		if (mppa_pcie_netdev_is_magic_set(pdata)) {
				schedule_work(&netdev->enable);
		}
	}
	/* not enabled, stop here */
	if (last_state != _MPPA_PCIE_NETDEV_STATE_ENABLED) {
		dev_dbg(&pdata->pdev->dev, "netdev is disabled. interrupt OUT\n");
		return IRQ_HANDLED;
	}

	/* schedule poll call */
	for (i = 0; i < netdev->if_count; ++i) {
		if (!netif_running(netdev->dev[i])) {
			netdev_dbg(netdev->dev[i], "netdev[%d] is not running\n", i);
			continue;
		}

		priv = netdev_priv(netdev->dev[i]);
		if (priv->interrupt_status) {
			netdev_dbg(netdev->dev[i], "Schedule NAPI\n");
			mppa_pcie_netdev_schedule_napi(priv);
		}

		if (!netif_queue_stopped(netdev->dev[i]) || atomic_read(&priv->reset)){
			netdev_dbg(netdev->dev[i], "netdev[%d] is not stopped (%d) or in reset (%d)\n",
				i, !netif_queue_stopped(netdev->dev[i]), atomic_read(&priv->reset));
			continue;
		}

		/* Compute the limit where the Tx queue would have been saturated
		 * - In std mode, submitted is just before head
		 * - In autoloop, submitted is just before done (we don't care at all about head)
		 */
		if (priv->config->flags & MPPA_PCIE_ETH_CONFIG_RING_AUTOLOOP) {
			tx_limit = atomic_read(&priv->tx_done);
		} else {
			tx_limit = readl(priv->tx_head_addr);
			atomic_set(&priv->tx_head, tx_limit);
		}

		if ((atomic_read(&priv->tx_submitted) + 1) % priv->tx_size != tx_limit) {
			for(chanidx=0; chanidx <= MPPA_PCIE_NETDEV_NOC_CHAN_COUNT; chanidx ++) {
				mppa_pcie_dmaengine_set_channel_interrupt_mode(priv->tx_chan[chanidx],
									       _MPPA_PCIE_ENGINE_INTERRUPT_CHAN_DISABLED);
			}
			netdev_dbg(netdev->dev[i], "Wake netdev queue\n");
			netif_wake_queue(netdev->dev[i]);
		}
	}
	dev_dbg(&pdata->pdev->dev, "netdev interrupt OUT\n");
	return IRQ_HANDLED;
}


static int mppa_pcie_netdev_init(void)
{
	struct mppa_pcie_device *pdata = NULL;

	printk(KERN_INFO "Loading ethernet driver\n");

	list_for_each_entry(pdata, &mppa_device_list, link) {
		struct mppa_pcie_pdata_netdev *netdev;

		if (!pdata) {
			pr_warn("device data is NULL\n");
			return -1;
		}

		netdev = kzalloc(sizeof(*netdev), GFP_KERNEL);
		if (!netdev)
			continue;

		dev_dbg(&pdata->pdev->dev, "Attaching a netdev\n");
		netdev->pdata = pdata;
		pdata->netdev = netdev;

		atomic_set(&netdev->state, _MPPA_PCIE_NETDEV_STATE_DISABLED);
		netdev->if_count = 0;

		INIT_WORK(&netdev->enable, mppa_pcie_netdev_enable_task);
		pdata->netdev_reset = &mppa_pcie_netdev_disable;
		pdata->netdev_pre_reset = &mppa_pcie_netdev_pre_reset_all;
		pdata->netdev_interrupt = &mppa_pcie_netdev_interrupt;

		if (mppa_pcie_netdev_is_magic_set(pdata)){
			mppa_pcie_netdev_enable(pdata);
		}
	}

	return 0;
}

static void mppa_pcie_netdev_exit(void)
{
	struct mppa_pcie_device *pdata = NULL;

	list_for_each_entry(pdata, &mppa_device_list, link) {
		if (!pdata) {
			pr_warn("device data is NULL\n");
			return;
		}
		if (pdata->netdev) {
			mppa_pcie_netdev_disable(pdata);

			dev_dbg(&pdata->pdev->dev, "Removing the associated netdev\n");
			pdata->netdev_interrupt = NULL;
			pdata->netdev_reset = NULL;
			pdata->netdev_pre_reset = NULL;

			kfree(pdata->netdev);
			pdata->netdev = NULL;
		}
	}
}

module_init(mppa_pcie_netdev_init);
module_exit(mppa_pcie_netdev_exit);

MODULE_AUTHOR("Alexis Cellier <alexis.cellier@openwide.fr>");
MODULE_DESCRIPTION("MPPA PCIe Network Device Driver");
MODULE_LICENSE("GPL");
