/*
 * odp_rx.c: MPPA PCI Express device driver: Network Device for ODP Packet Rx
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


static int mpodp_rx_is_done(struct mpodp_if_priv *priv, struct mpodp_rxq *rxq,
			    int index)
{
	if (rxq->ring[index].len == 0) {
		/* error packet, always done */
		return 1;
	}

	return (dmaengine_tx_status(priv->rx_chan, rxq->ring[index].cookie,
				    NULL) == DMA_SUCCESS);
}

int mpodp_clean_rx(struct mpodp_if_priv *priv, struct mpodp_rxq *rxq,
		   int budget)
{
	struct net_device *netdev = priv->netdev;
	struct mpodp_rx *rx;
	int worked = 0;
	ktime_t now = ktime_get_real();
	/* RX: 2nd step: give packet to kernel and update RX head */
	while (budget-- && rxq->used != rxq->avail) {
		if (!mpodp_rx_is_done(priv, rxq, rxq->used)) {
			/* DMA transfer not completed */
			break;
		}

		if (netif_msg_rx_status(priv))
			netdev_info(netdev, "rxq[%d] rx[%d]: transfer done\n",
				   rxq->id, rxq->used);

		/* get rx slot */
		rx = &(rxq->ring[rxq->used]);

		if (rx->len == 0) {
			/* error packet, skip it */
			goto pkt_skip;
		}

		dma_unmap_sg(&priv->pdev->dev, rx->sg,
			     rx->dma_len, DMA_FROM_DEVICE);

		/* fill skb field */
		skb_put(rx->skb, rx->len);

		/* Mark the packet as already checksummed if
		 * the NOCSUM config flag is set on the interface */
		if (priv->config->flags & MPODP_CONFIG_NOCSUM)
			rx->skb->ip_summed = CHECKSUM_UNNECESSARY; 

 		skb_record_rx_queue(rx->skb, rxq->id);
		rx->skb->tstamp = now;

		rx->skb->protocol = eth_type_trans(rx->skb, netdev);
		netif_receive_skb(rx->skb);

		/* update stats */
		netdev->stats.rx_bytes += rx->len;
		netdev->stats.rx_packets++;

	      pkt_skip:
		rxq->used = (rxq->used + 1) % rxq->size;

		worked++;
	}
	/* write new RX head */
	if (worked) {
		writel(rxq->used, rxq->head_addr);
	}


	return worked;
}

static int mpodp_flush_rx_trans(struct mpodp_if_priv *priv, struct mpodp_rxq *rxq,
				struct mpodp_rx *rx, uint32_t first_slot)
{
	struct net_device *netdev = priv->netdev;
	struct dma_async_tx_descriptor *dma_txd;

	rx->dma_len =
		dma_map_sg(&priv->pdev->dev, rx->sg, rx->sg_len,
			   DMA_FROM_DEVICE);
	if (rx->dma_len == 0)
		return -1;

	/* configure channel */
	priv->rx_config.cfg.src_addr =
		    rxq->mppa_entries[first_slot].pkt_addr;
	if (dmaengine_slave_config(priv->rx_chan, &priv->rx_config.cfg)) {
		/* board has reset, wait for reset of netdev */
		netif_carrier_off(netdev);
		if (netif_msg_rx_err(priv))
			netdev_err(netdev,
				   "rxq[%d] rx[%d]: cannot configure channel\n",
				   rxq->id, first_slot);
		goto dma_failed;
	}

	/* get transfer descriptor */
	dma_txd = dmaengine_prep_slave_sg(priv->rx_chan,
					  rx->sg, rx->dma_len,
					  DMA_DEV_TO_MEM, 0);
	if (dma_txd == NULL) {
		if (netif_msg_rx_err(priv))
			netdev_err(netdev,
				   "rxq[%d] rx[%d]: cannot get dma descriptor",
				   rxq->id, first_slot);
		goto dma_failed;
	}

	if (netif_msg_rx_status(priv))
		netdev_info(netdev, "rxq[%d] rx[%d]: transfer start (%d)\n",
			   rxq->id, rxq->avail, rx->sg_len);

	/* submit and issue descriptor */
	rx->cookie = dmaengine_submit(dma_txd);

	return 0;
 dma_failed:
	dma_unmap_sg(&priv->pdev->dev, rx->sg, rx->sg_len, DMA_FROM_DEVICE);
	return -1;
}

int mpodp_start_rx(struct mpodp_if_priv *priv, struct mpodp_rxq *rxq)
{
	struct mpodp_rx *rx;
	int limit, work_done = 0;
	int early_exit = 0;

	if (atomic_read(&priv->reset) == 1) {
		/* Interface is reseting, do not start new transfers */
		return 0;
	}

	/* RX: 1st step: start transfer */
	/* read RX tail */
	rxq->tail = *rxq->tail_host_addr;

	if (rxq->avail > rxq->tail) {
		/* make a first loop to the end of the ring */
		limit = rxq->size;
	} else {
		limit = rxq->tail;
	}
 loop:
	/* get mppa entries */
	while (rxq->avail != limit) {
		/* get rx slot */
		rx = &(rxq->ring[rxq->avail]);

		/* check rx status */
		if (rxq->mppa_entries[rxq->avail].status) {
			/* TODO: report correctly the error */
			rx->len = 0;	/* means this is an error packet */
			goto pkt_error;
		}

		/* read rx entry information */
		rx->len = rxq->mppa_entries[rxq->avail].len;

		/* get skb from kernel */
		rx->skb = netdev_alloc_skb_ip_align(priv->netdev, rx->len);
		if (rx->skb == NULL) {
			goto skb_failed;
		}

		/* prepare sg */
		sg_set_buf(rx->sg, rx->skb->data, rx->len);
		rx->sg_len = 1;

		if (mpodp_flush_rx_trans(priv, rxq, rx, rxq->avail))
			goto dma_failed;

		work_done++;

	      pkt_error:
		rxq->avail++;

		continue;

	      dma_failed:
		dev_kfree_skb_any(rx->skb);
	      skb_failed:
		/* napi will be rescheduled */
		early_exit = 1;
		break;
	}

	if (!early_exit && limit != rxq->tail) {
		/* make the second part of the ring */
		limit = rxq->tail;
		rxq->avail = 0;
		goto loop;
	}
	if (work_done)
		dma_async_issue_pending(priv->rx_chan);

	return rxq->avail >= rxq->used ? (rxq->avail - rxq->used):
		(rxq->avail + rxq->size - rxq->used);
}
