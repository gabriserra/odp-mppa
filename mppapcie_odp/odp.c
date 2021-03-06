/*
 * odp.c: MPPA PCI Express device driver: Network Device for ODP Probe/setup/reset
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
#include <linux/vmalloc.h>

#include <mppa_pcie_api.h>
#include "mppapcie_odp.h"
#include "odp.h"

/**
 * List of per-netdev device (not netdev interfaces)
 */
LIST_HEAD(netdev_device_list);
DEFINE_MUTEX(netdev_device_list_mutex);

static int debug = -1;
module_param(debug, int, 0);
MODULE_PARM_DESC(debug, "Debug level (0=none,...,16=all)");

static uint32_t mpodp_get_eth_control_addr(struct mppa_pcie_device *pdata)
{
	uint32_t addr;
	int ret =
	    mppa_pcie_get_service_addr(pdata, PCIE_SERVICE_ODP, &addr);

	if (ret < 0)
		return 0;

	return addr;
}

static struct net_device_stats *mpodp_get_stats(struct net_device *netdev)
{
	/* TODO: get stats from the MPPA */

	return &netdev->stats;
}

static int mpodp_poll(struct napi_struct *napi, int budget)
{
	struct mpodp_if_priv *priv;
	int work_pending = 0, work = 0;
	int i;

	priv = container_of(napi, struct mpodp_if_priv, napi);
	if (netif_msg_rx_status(priv))
		netdev_info(priv->netdev, "Polling NAPI\n");

	/* Check for Rx transfer completion and send the SKBs to the
	 * network stack */
	for (i = 0; i < priv->n_rxqs; ++i) {
		if (budget > work)
			work += mpodp_clean_rx(priv, &priv->rxqs[i], budget - work);

		/* Start new Rx transfer if any */
		work_pending += mpodp_start_rx(priv, &priv->rxqs[i]);
	}

	if (work < budget && !work_pending) {
		if (!priv->interrupt_status) {
			priv->interrupt_status = 1;
			writel(1, priv->interrupt_status_addr);
			/* Force one more round in case we missed an IT */
			work = budget;
		} else
			napi_complete(napi);
	} else {
	  	work = budget;
		if (priv->interrupt_status) {
			priv->interrupt_status = 0;
			writel(0, priv->interrupt_status_addr);
		}
	}

	return work;
}

#ifdef CONFIG_NET_POLL_CONTROLLER
static void mpodp_poll_controller(struct net_device *netdev)
{
	struct mpodp_if_priv *priv = netdev_priv(netdev);
	napi_schedule(&priv->napi);
}
#endif


static int mpodp_open(struct net_device *netdev)
{
	struct mpodp_if_priv *priv = netdev_priv(netdev);
	int ready;

	ready = mpodp_tx_update_cache(priv);

	netif_tx_start_all_queues(netdev);
	napi_enable(&priv->napi);

	priv->interrupt_status = 1;
	writel(priv->interrupt_status, priv->interrupt_status_addr);

	mod_timer(&priv->tx_timer, jiffies + MPODP_TX_RECLAIM_PERIOD);

	if (ready) {
		if (netif_msg_link(priv))
			netdev_info(priv->netdev, "Interface is ready\n");
		netif_carrier_on(netdev);
		napi_schedule(&priv->napi);
	} else {
		if (netif_msg_link(priv))
			netdev_info(priv->netdev, "Interface is not ready\n");
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
	netif_tx_stop_all_queues(netdev);

	mpodp_clean_tx(priv, -1);

	return 0;
}

static int mpodp_change_mtu(struct net_device *dev, int new_mtu)
{
	struct mpodp_if_priv *priv = netdev_priv(dev);
	if (new_mtu < 68 || new_mtu > priv->config->mtu)
		return -EINVAL;
	dev->mtu = new_mtu;
	return 0;
}

static const struct net_device_ops mpodp_ops = {
	.ndo_open = mpodp_open,
	.ndo_stop = mpodp_close,
	.ndo_start_xmit = mpodp_start_xmit,
	.ndo_select_queue = mpodp_select_queue,
	.ndo_get_stats = mpodp_get_stats,
	.ndo_tx_timeout = mpodp_tx_timeout,
	.ndo_change_mtu = mpodp_change_mtu,
#ifdef CONFIG_NET_POLL_CONTROLLER
	.ndo_poll_controller = mpodp_poll_controller,
#endif
};

static void mpodp_remove(struct net_device *netdev)
{
	struct mpodp_if_priv *priv = netdev_priv(netdev);
	int chanidx;
	int i;


	if (netif_msg_probe(priv))
		printk(KERN_INFO "Removing netdev %s\n", netdev->name);

	if (priv->tx_timer.function)
		del_timer_sync(&priv->tx_timer);

	/* unregister */
	unregister_netdev(netdev);

	/* clean */
	for (chanidx = 0; chanidx < MPODP_NOC_CHAN_COUNT; chanidx++) {
		dma_release_channel(priv->tx_chan[chanidx]);
	}

	for (i = 0; i < priv->n_txqs; ++i) {
		vfree(priv->txqs[i].cache);
		vfree(priv->txqs[i].ring);
	}

	dma_release_channel(priv->rx_chan);
	for (i = 0; i < priv->n_rxqs; ++i) {
		vfree(priv->rxqs[i].ring);
		pci_free_consistent(priv->pdev,
				    priv->rxqs[i].size * sizeof(struct mpodp_c2h_entry),
				    priv->rxqs[i].mppa_entries,
				    priv->rxqs[i].mppa_entries_handle);
		pci_free_consistent(priv->pdev,
				    sizeof(*priv->rxqs[i].tail_host_addr),
				    priv->rxqs[i].tail_host_addr,
				    priv->rxqs[i].tail_handle);
	}

	mppa_pcie_time_destroy(priv->tx_time);
	netif_napi_del(&priv->napi);

	free_netdev(netdev);
}

static struct net_device *mpodp_create(struct mppa_pcie_device *pdata,
				       const struct mpodp_pdata_priv *pdata_priv,
				       struct mpodp_if_config *config,
				       uint32_t eth_control_addr, int id)
{
	struct net_device *netdev;
	struct mpodp_if_priv *priv;
	dma_cap_mask_t mask;
	char name[64];
	int i, j, entries_addr;
	int chanidx;
	struct mppa_pcie_id mppa_id;
	struct pci_dev *pdev = mppa_pcie_get_pci_dev(pdata);
	u8 __iomem *smem_vaddr;
	struct mppa_pcie_dmae_filter filter;

	if (config->n_rxqs > MPODP_MAX_RX_QUEUES) {
		dev_err(&pdev->dev,
			"interface %d requires too many rx queues (%d, max=%d)\n", id, config->n_rxqs, MPODP_MAX_RX_QUEUES);
		return NULL;
	}

	if (config->n_txqs > MPODP_MAX_TX_QUEUES) {
		dev_err(&pdev->dev,
			"interface %d requires too many tx queues (%d, max=%d)\n", id, config->n_txqs, MPODP_MAX_TX_QUEUES);
		return NULL;
	}

	/* initialize mask for dma channel */
	dma_cap_zero(mask);
	dma_cap_set(DMA_SLAVE, mask);
	dma_cap_set(DMA_PRIVATE, mask);

	filter.wanted_device = mppa_pcie_get_dma_device(pdata);

	mppa_pcie_get_id(pdata, &mppa_id);
	snprintf(name, 64, "modp%d.%d.%d.%d", mppa_id.board_id,
		 mppa_id.chip_id, mppa_id.ioddr_id, id);

	/* alloc netdev */
	if (!(netdev = alloc_netdev_mqs(sizeof(struct mpodp_if_priv), name,
#if (LINUX_VERSION_CODE > KERNEL_VERSION (3, 16, 0))
				    NET_NAME_UNKNOWN,
#endif
					  ether_setup, config->n_txqs, config->n_rxqs))) {
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
	netdev->mtu = config->mtu < 1500 ? config->mtu : 1500;
	memcpy(netdev->dev_addr, &(config->mac_addr), 6);
	netdev->features |= NETIF_F_SG | NETIF_F_HIGHDMA;
	if (config->flags & MPODP_CONFIG_NOCSUM)
		netdev->features |= NETIF_F_HW_CSUM;
	mpodp_set_ethtool_ops(netdev);

	/* init priv */
	priv = netdev_priv(netdev);
	priv->pdata = pdata;
	priv->pdata_priv = pdata_priv;
	priv->pdev = pdev;
	priv->config = config;
	priv->netdev = netdev;
	priv->n_rxqs = config->n_rxqs;
	priv->n_txqs = config->n_txqs;
	priv->msg_enable = netif_msg_init(debug, MPODP_DEFAULT_MSG);
	atomic_set(&priv->reset, 0);

	smem_vaddr = mppa_pcie_get_smem_vaddr(pdata);
	priv->interrupt_status_addr = smem_vaddr
		+ eth_control_addr + offsetof(struct mpodp_control, configs)
		+ id * sizeof(struct mpodp_if_config)
		+ offsetof(struct mpodp_if_config, interrupt_status);
	netif_napi_add(netdev, &priv->napi, mpodp_poll, MPODP_NAPI_WEIGHT);

	/* init fs */
	snprintf(name, 64, "netdev%d-txtime", id);
	priv->tx_time =
		mppa_pcie_time_create(pdata, name, 25000, 25000,
				      40, MPPA_TIME_TYPE_NS);

	/* Init all RX Queues */
	for (i = 0; i < priv->n_rxqs; ++i) {
		struct mpodp_rxq *rxq = &priv->rxqs[i];

		rxq->id = i;
		rxq->size =
			readl(desc_info_addr(smem_vaddr, config->c2h_addr[i],
					     count));
		rxq->head_addr =
			desc_info_addr(smem_vaddr, config->c2h_addr[i], head);
		rxq->tail_host_addr = pci_alloc_consistent(pdev, sizeof(*rxq->tail_addr),
							   &rxq->tail_handle);
		if (rxq->tail_host_addr == NULL) {
			dev_err(&pdev->dev, "Failed to allocate consistent memory\n");
			goto rx_alloc_failed;
		}
		*rxq->tail_host_addr = 0;

		writel(rxq->tail_handle, desc_info_addr(smem_vaddr, config->c2h_addr[i], h_tail_addr));
		rxq->tail_addr = desc_info_addr(smem_vaddr, config->c2h_addr[i], tail);
		rxq->head = readl(rxq->head_addr);
		rxq->tail = readl(rxq->tail_addr);
		rxq->used = rxq->head;
		rxq->avail = rxq->tail;

		entries_addr =
			readl(desc_info_addr(smem_vaddr, config->c2h_addr[i],
					     addr));
		rxq->ring =vmalloc(rxq->size * sizeof(struct mpodp_rx));
		if (rxq->ring == NULL) {
			dev_err(&pdev->dev, "RX ring allocation failed\n");
			goto rx_alloc_failed;
		}

		for (j = 0; j < rxq->size; ++j) {
			/* initialize scatterlist to 1 as the RX skb is in one chunk */
			sg_init_table(rxq->ring[j].sg, 1);
			/* set the RX ring entry address */
			rxq->ring[j].entry_addr = smem_vaddr
				+ entries_addr
				+ j * sizeof(struct mpodp_c2h_entry);
		}
		rxq->mppa_entries = pci_alloc_consistent(pdev,
							 rxq->size * sizeof(struct mpodp_c2h_entry),
							 &rxq->mppa_entries_handle);
		if (rxq->mppa_entries == NULL) {
			dev_err(&pdev->dev, "RX mppa_entries allocation failed\n");
			goto rx_alloc_failed;
		}
		writel(rxq->mppa_entries_handle,
		       desc_info_addr(smem_vaddr, config->c2h_addr[i], host_addr));
	}

	/* Init Rx DMA chan */
	priv->rx_config.cfg.direction = DMA_DEV_TO_MEM;
	priv->rx_config.fifo_mode = _MPPA_PCIE_ENGINE_FIFO_MODE_DISABLED;
	priv->rx_config.requested_engine = -1;
	priv->rx_config.short_latency_load_threshold = INT_MAX;

	filter.direction = _MPPA_PCIE_ENGINE_DIRECTION_MPPA_TO_HOST;
	priv->rx_chan =
	    dma_request_channel(mask, mppa_pcie_dmae_chan_filter, &filter);
	if (priv->rx_chan == NULL) {
		dev_err(&pdev->dev, "RX chan request failed\n");
		goto rx_chan_failed;
	}

	/* Init all Tx Queues */
	for (i = 0; i < priv->n_txqs; ++i) {
		struct mpodp_txq *txq = &priv->txqs[i];

		txq->id = i;
		txq->txq = netdev_get_tx_queue(netdev, i);
		txq->mppa_size =
			readl(desc_info_addr(smem_vaddr, config->h2c_addr[i],
					     count));
		txq->size = MPODP_AUTOLOOP_DESC_COUNT;

		txq->head_addr =
			desc_info_addr(smem_vaddr, config->h2c_addr[i],
				       head);

		/* Setup Host TX Ring */
		txq->ring = vmalloc(txq->size * sizeof(struct mpodp_tx));
		if (txq->ring == NULL) {
			dev_err(&pdev->dev, "TX ring allocation failed\n");
			goto tx_alloc_failed;
		}
		for (j = 0; j < txq->size; ++j) {
			/* initialize scatterlist to the maximum size */
			sg_init_table(txq->ring[j].sg, MAX_SKB_FRAGS + 1);
		}

		/* Pre cache MPPA TX Ring */
		txq->cache = vmalloc(txq->mppa_size * sizeof(*txq->cache));
		if (txq->cache == NULL) {
			dev_err(&pdev->dev, "TX cache allocation failed\n");
			goto tx_alloc_failed;
		}
		entries_addr =
			readl(desc_info_addr(smem_vaddr, config->h2c_addr[i],
					     addr));
		for (j = 0; j < txq->mppa_size; ++j) {
			/* set the TX ring entry address */
			txq->cache[j].entry_addr = smem_vaddr
				+ entries_addr
				+ j * sizeof(struct mpodp_h2c_entry);
		}

		atomic_set(&txq->submitted, 0);
		atomic_set(&txq->done, 0);
		atomic_set(&txq->autoloop_cur, 0);

		txq->cached_head = 0;
	}

	/* Init Tx DMA chan */
	for (chanidx = 0; chanidx < MPODP_NOC_CHAN_COUNT; chanidx++) {
		spin_lock_init(&priv->tx_lock[chanidx]);
		priv->tx_config[chanidx].cfg.direction = DMA_MEM_TO_DEV;
		priv->tx_config[chanidx].fifo_mode =
		    _MPPA_PCIE_ENGINE_FIFO_MODE_ENABLED;
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
	}
	priv->packet_id = 0ULL;

	/* register netdev */
	if (register_netdev(netdev)) {
		if (netif_msg_probe(priv))
			dev_err(&pdev->dev, "failed to register netdev\n");
		goto register_failed;
	}

	/* Tx poll timer */
	setup_timer(&priv->tx_timer, mpodp_tx_timer_cb,
		    (unsigned long) priv);

	if (netif_msg_probe(priv))
		printk(KERN_INFO "Registered netdev for %s (txq:%d, rxq:%d csum:%d)\n",
		       netdev->name, priv->n_txqs, priv->n_rxqs,
		       !(priv->config->flags & MPODP_CONFIG_NOCSUM));

	return netdev;

      register_failed:
      tx_chan_failed:
	while (chanidx >= 0) {
		dma_release_channel(priv->tx_chan[chanidx]);
		chanidx--;
	}
      tx_alloc_failed:
	for (i = 0; i < priv->n_txqs; ++i){
		if(priv->txqs[i].cache)
			vfree(priv->txqs[i].cache);
		if(priv->txqs[i].ring)
			vfree(priv->txqs[i].ring);
	}
	dma_release_channel(priv->rx_chan);
      rx_chan_failed:
      rx_alloc_failed:
	for (i = 0; i < priv->n_rxqs; ++i){
		if(priv->rxqs[i].ring)
			vfree(priv->rxqs[i].ring);
		if(priv->rxqs[i].mppa_entries) {
			pci_free_consistent(pdev,
					    priv->rxqs[i].size * sizeof(struct mpodp_c2h_entry),
					    priv->rxqs[i].mppa_entries,
					    priv->rxqs[i].mppa_entries_handle);
		}
		if (priv->rxqs[i].tail_host_addr) {
			pci_free_consistent(pdev,
					    sizeof(*priv->rxqs[i].tail_host_addr),
					    priv->rxqs[i].tail_host_addr,
					    priv->rxqs[i].tail_handle);
		}
	}
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
		if (magic == MPODP_CONTROL_STRUCT_MAGIC)
			return 1;
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

	/* add net devices */
	for (i = 0; i < if_count; ++i) {
		netdev->dev[i] =
			mpodp_create(pdata, netdev,
				     netdev->control.configs + i,
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
	int now, i;

	/* There is no real need to lock these changes
	 * but it makes sure that there is no packet being sent
	 * when we release the lock so we just have to wai for the
	 * one being transmitted. */

	if (priv->tx_timer.function)
		del_timer_sync(&priv->tx_timer);

	netif_tx_lock(netdev);
	atomic_set(&priv->reset, 1);
	netif_tx_stop_all_queues(netdev);
	netif_carrier_off(netdev);
	netif_tx_unlock(netdev);

	for (i = 0; i < priv->n_txqs; ++i) {
		now = jiffies;
		while (atomic_read(&priv->txqs[i].done) !=
		       atomic_read(&priv->txqs[i].submitted)) {
			msleep(10);
			if (jiffies - now >= msecs_to_jiffies(1000)) {
				if (netif_msg_tx_err(priv))
					netdev_err(netdev,
						   "Txq %d stalled. Ignoring for reset\n", i);
				break;
			}
		}
	}
	for (i = 0; i < priv->n_rxqs; ++i) {
		now = jiffies;
		while(priv->rxqs[i].used != priv->rxqs[i].avail) {
			msleep(10);
			if (jiffies - now >= msecs_to_jiffies(1000)) {
				if (netif_msg_rx_err(priv))
					netdev_err(netdev,
						   "Rxq %d stalled. Ignoring for reset\n", i);
				break;
			}
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
			  struct mppa_pcie_device *pdata,
			  int netdev_status)
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
		switch(last_state) {
		case _MPODP_IF_STATE_ENABLING:
			/* Wait for interface to be fully enabled before breaking it down */
			msleep(10);
			break;
		case _MPODP_IF_STATE_REMOVING:
			/* Everything is already off */
			return;
		case _MPODP_IF_STATE_ENABLED:
			/* Get out of the loop to disable the interfaces */
			break;
		case _MPODP_IF_STATE_DISABLED:
			/* Change the status to REMOVING so interrupt can't
			 * enable interfaces during unload */
			atomic_set(&netdev->state, netdev_status);
			return;
		default:
			return;
		}
	} while (last_state == _MPODP_IF_STATE_ENABLING);

	eth_control_addr = mpodp_get_eth_control_addr(pdata);
	writel(0, mppa_pcie_get_smem_vaddr(pdata)
	       + eth_control_addr + offsetof(struct mpodp_control, magic));

	/* delete net devices */
	for (i = 0; i < netdev->if_count; ++i) {
		mpodp_remove(netdev->dev[i]);
	}

	netdev->if_count = 0;

	atomic_set(&netdev->state, netdev_status);
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

	last_state = atomic_read(&netdev->state);

	/* disabled : try to enable */
	if (last_state == _MPODP_IF_STATE_DISABLED) {
		if (mpodp_is_magic_set(netdev->pdata)) {
			schedule_work(&netdev->enable);
		}
	}
	/* not enabled, stop here */
	if (last_state != _MPODP_IF_STATE_ENABLED) {
		return IRQ_HANDLED;
	}

	/* schedule poll call */
	for (i = 0; i < netdev->if_count; ++i) {
		if (!netif_running(netdev->dev[i]) ||
		    !netif_carrier_ok(netdev->dev[i])) {
			/* Netdev not running */
			continue;
		}

		priv = netdev_priv(netdev->dev[i]);
		if (netif_msg_intr(priv))
			netdev_info(priv->netdev, "Interrupt\n");

		if (priv->interrupt_status) {
			priv->interrupt_status = 0;
			writel(0, priv->interrupt_status_addr);
		}
		/* Schedule NAPI */
		napi_schedule(&priv->napi);

		if (!netif_queue_stopped(netdev->dev[i])
		    || atomic_read(&priv->reset)) {
			/* Netdev is stopped or in reset */
			continue;
		}
	}
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
		mpodp_disable(netdev, pdata, _MPODP_IF_STATE_DISABLED);
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

	irq = mppa_pcie_get_irq(pdata, MPPA_PCIE_IRQ_USER_IT, 0);
	if (irq < 0)
		goto free_netdev;

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
	dev_warn(&netdev->pdev->dev, "Failed to attach a netdev\n");
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

	mpodp_disable(netdev, pdata, _MPODP_IF_STATE_REMOVING);

	cancel_work_sync(&netdev->enable);

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
	printk(KERN_INFO "mppapcie_odp: loading PCIe ethernet driver\n");
	mppa_pcie_register_notifier(&mpodp_notifier);
	return 0;
}

static void mpodp_exit(void)
{
	printk(KERN_INFO "mppapcie_odp: unloading PCIe ethernet driver\n");
	mppa_pcie_unregister_notifier(&mpodp_notifier);
}

module_init(mpodp_init);
module_exit(mpodp_exit);

MODULE_AUTHOR("Alexis Cellier <alexis.cellier@openwide.fr>");
MODULE_DESCRIPTION("MPPA PCIe Network Device Driver");
MODULE_LICENSE("GPL");
