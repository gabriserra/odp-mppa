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


static void mpodp_dma_callback_rx(void *param)
{
	struct mpodp_if_priv *priv = param;

	napi_schedule(&priv->napi);
}

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

	/* RX: 2nd step: give packet to kernel and update RX head */
	while (budget-- && rxq->used != rxq->avail) {
		if (!mpodp_rx_is_done(priv, rxq, rxq->used)) {
			/* DMA transfer not completed */
			break;
		}

		netdev_dbg(netdev, "rxq[%d] rx[%d]: transfer done\n",
			   rxq->id, rxq->used);

		/* get rx slot */
		rx = &(rxq->ring[rxq->used]);

		if (rx->len == 0) {
			/* error packet, skip it */
			goto pkt_skip;
		}

		dma_unmap_sg(&priv->pdev->dev, rx->sg, 1, DMA_FROM_DEVICE);

		/* fill skb field */
		skb_put(rx->skb, rx->len);
		skb_record_rx_queue(rx->skb, rxq->id);
		rx->skb->protocol = eth_type_trans(rx->skb, netdev);
		napi_gro_receive(&priv->napi, rx->skb);

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

int mpodp_start_rx(struct mpodp_if_priv *priv, struct mpodp_rxq *rxq)
{
	struct net_device *netdev = priv->netdev;
	struct dma_async_tx_descriptor *dma_txd;
	struct mpodp_rx *rx;
	int dma_len, limit;
	int work_done = 0;
	int add_it;

	if (atomic_read(&priv->reset) == 1) {
		/* Interface is reseting, do not start new transfers */
		return 0;
	}

	/* RX: 1st step: start transfer */
	/* read RX tail */
	rxq->tail = readl(rxq->tail_addr);

	if (rxq->avail > rxq->tail) {
		/* make a first loop to the end of the ring */
		limit = rxq->size;
	} else {
		limit = rxq->tail;
	}
 loop:
	/* get mppa entries */
	memcpy_fromio(rxq->mppa_entries + rxq->avail,
		      rxq->ring[rxq->avail].entry_addr,
		      sizeof(struct mpodp_c2h_entry) *
		      (limit - rxq->avail));
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
		dma_len =
		    dma_map_sg(&priv->pdev->dev, rx->sg, 1,
			       DMA_FROM_DEVICE);
		if (dma_len == 0) {
			goto map_failed;
		}

		/* configure channel */
		priv->rx_config.cfg.src_addr =
		    rxq->mppa_entries[rxq->avail].pkt_addr;
		if (dmaengine_slave_config
		    (priv->rx_chan, &priv->rx_config.cfg)) {
			/* board has reset, wait for reset of netdev */
			netif_carrier_off(netdev);
			netdev_err(netdev,
				   "rxq[%d] rx[%d]: cannot configure channel\n",
				   rxq->id, rxq->avail);
			break;
		}

		/* get transfer descriptor */
		add_it = ((rxq->avail + 1)  == limit);
		dma_txd =
		    dmaengine_prep_slave_sg(priv->rx_chan, rx->sg, dma_len,
					    DMA_DEV_TO_MEM,
					    add_it ? DMA_PREP_INTERRUPT : 0);
		if (dma_txd == NULL) {
			netdev_err(netdev,
				   "rxq[%d] rx[%d]: cannot get dma descriptor",
				   rxq->id, rxq->avail);
			goto dma_failed;
		}

		if (add_it) {
			dma_txd->callback = mpodp_dma_callback_rx;
			dma_txd->callback_param = priv;
		}
		netdev_dbg(netdev, "rxq[%d] rx[%d]: transfer start\n",
			   rxq->id, rxq->avail);

		/* submit and issue descriptor */
		rx->cookie = dmaengine_submit(dma_txd);
		work_done++;

	      pkt_error:
		rxq->avail++;

		continue;

	      dma_failed:
		dma_unmap_sg(&priv->pdev->dev, rx->sg, 1, DMA_FROM_DEVICE);
	      map_failed:
		dev_kfree_skb_any(rx->skb);
	      skb_failed:
		/* napi will be rescheduled */
		break;
	}

	if (limit != rxq->tail) {
		/* make the second part of the ring */
		limit = rxq->tail;
		rxq->avail = 0;
		goto loop;
	}
	if (work_done)
		dma_async_issue_pending(priv->rx_chan);

	return 0;
}
