#ifndef PCIE_H__
#define PCIE_H__

#define MPPA_PCIE_ETH_IF_MAX 1

/**
 * Create and start if_count PCI interfaces with default settings
 * For custom settings, use netdev_init and then call pcie_start.
 * If mtu is 0, default MTU is used
 */
int pcie_init(int if_count, int mtu);

/**
 * Start the PCI/netdev handler.
 * netdev settings must have been configured beforehand using netdev_init.
 * If pci was configured using pcie_init, this function must NOT be called
 */
int pcie_start();

#endif
