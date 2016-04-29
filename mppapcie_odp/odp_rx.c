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

int mpodp_clean_rx(struct mpodp_if_priv *priv, int budget)
{
	struct net_device *netdev = priv->netdev;
	struct mpodp_rx *rx;
	int worked = 0;

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
		napi_gro_receive(&priv->napi, rx->skb);

		/* update stats */
		netdev->stats.rx_bytes += rx->len;
		netdev->stats.rx_packets++;

	      pkt_skip:
		priv->rx_used = (priv->rx_used + 1) % priv->rx_size;

		worked++;
	}
	/* write new RX head */
	if (worked) {
		writel(priv->rx_used, priv->rx_head_addr);
	}


	return worked;
}

int mpodp_start_rx(struct mpodp_if_priv *priv)
{
	struct net_device *netdev = priv->netdev;
	struct dma_async_tx_descriptor *dma_txd;
	struct mpodp_rx *rx;
	int dma_len, limit;

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

	return 0;
}
