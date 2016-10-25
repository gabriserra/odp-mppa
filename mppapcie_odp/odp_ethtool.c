/*
 * odp_ethtool.c: MPPA Ethtool support
 *
 * (C) Copyright 2016 Kalray
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; version 2
 * of the License, or (at your option) any later version.
 */
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/version.h>
#include <linux/ethtool.h>
#include <linux/pci.h>
#include <mppa_pcie_api.h>
#include "mppapcie_odp.h"
#include "odp.h"

static int mpodp_get_settings(struct net_device *netdev,
			      struct ethtool_cmd *ecmd)
{
	ecmd->supported = SUPPORTED_40000baseCR4_Full | SUPPORTED_Autoneg | SUPPORTED_Backplane;
	ecmd->advertising = ADVERTISED_40000baseCR4_Full | ADVERTISED_Backplane | ADVERTISED_Autoneg;
	ecmd->speed = SPEED_40000;
	ecmd->duplex = DUPLEX_FULL;
	ecmd->transceiver = XCVR_INTERNAL;
	ecmd->port = PORT_OTHER;
	ecmd->phy_address = 0;
	ecmd->autoneg = AUTONEG_ENABLE;
	return 0;
}

static void mpodp_get_drvinfo(struct net_device *netdev,
			      struct ethtool_drvinfo *drvinfo)
{
	struct mpodp_if_priv *priv = netdev_priv(netdev);

	strlcpy(drvinfo->driver,  "mppapcie_odp",
		sizeof(drvinfo->driver));
	strlcpy(drvinfo->version, MPODP_VERSION_STR,
		sizeof(drvinfo->version));
	strlcpy(drvinfo->fw_version, priv->pdata_priv->control.fw_version,
		sizeof(drvinfo->fw_version));
	strlcpy(drvinfo->bus_info, pci_name(priv->pdev),
		sizeof(drvinfo->bus_info));
}

static void mpodp_get_wol(struct net_device *netdev,
			  struct ethtool_wolinfo *wol)
{
	wol->supported = 0;
	wol->wolopts = 0;
}

static u32 mpodp_get_msglevel(struct net_device *netdev)
{
	struct mpodp_if_priv *priv = netdev_priv(netdev);

	return priv->msg_enable;
}

static void mpodp_set_msglevel(struct net_device *netdev, u32 data)
{
	struct mpodp_if_priv *priv = netdev_priv(netdev);

	priv->msg_enable = data;
}

static u32 mpodp_get_link(struct net_device *netdev)
{
	if (!netif_carrier_ok(netdev))
		return 0;
	return 1;
}

static void mpodp_get_ringparam(struct net_device *netdev,
				struct ethtool_ringparam *ring)
{
	struct mpodp_if_priv *priv = netdev_priv(netdev);
	ring->rx_pending =
		ring->rx_max_pending = priv->rxqs[0].size;
	ring->tx_pending = priv->txqs[0].size;
	ring->tx_max_pending = MPODP_AUTOLOOP_DESC_COUNT;
}

static const struct ethtool_ops mpodp_ethtool_ops = {
	.get_settings		= mpodp_get_settings,
	.get_drvinfo		= mpodp_get_drvinfo,
	.get_wol		= mpodp_get_wol,

	.get_msglevel		= mpodp_get_msglevel,
	.set_msglevel		= mpodp_set_msglevel,
	.get_link               = mpodp_get_link,
	.get_ringparam		= mpodp_get_ringparam,
};

void mpodp_set_ethtool_ops(struct net_device *netdev)
{
	netdev->ethtool_ops = &mpodp_ethtool_ops;
}
