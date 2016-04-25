#ifndef __ODP_RPC_PCIE_H__
#define __ODP_RPC_PCIE_H__

#include <odp/rpc/defines.h>

/** Version of the PCIE CoS */
#define ODP_RPC_PCIE_VERSION 0x2

typedef enum {
	ODP_RPC_CMD_PCIE_OPEN    /**< PCIe: Forward Rx traffic to a cluster */,
	ODP_RPC_CMD_PCIE_CLOS    /**< PCIe: Stop forwarding Rx trafic to a cluster */,
	ODP_RPC_CMD_PCIE_N_CMD
} odp_rpc_cmd_pcie_e;

#define ODP_RPC_CMD_NAMES_PCIE			\
	"PCIE OPEN",				\
		"PCIE CLOSE"

#define ODP_RPC_ACK_LIST_PCIE odp_rpc_ack_pcie_open_t pcie_open;

/**
 * Command for ODP_RPC_CMD_PCIE_OPEN
 */
typedef union {
	struct {
		uint16_t pkt_size;
		uint8_t pcie_eth_if_id; /* PCIe eth interface number */
		uint8_t min_rx;
		uint8_t max_rx;
	};
	odp_rpc_inl_data_t inl_data;
} odp_rpc_cmd_pcie_open_t;
ODP_RPC_CHECK_STRUCT_SIZE(odp_rpc_cmd_pcie_open_t);

/**
 * Ack inline for ODP_RPC_CMD_PCIE_OPEN
 */
typedef struct {
	uint16_t tx_if;	/* IO Cluster id */
	uint8_t  min_tx_tag;	/* Tag of the first IO Cluster rx */
	uint8_t  max_tx_tag;	/* Tag of the last IO Cluster rx */
	uint8_t  mac[ETH_ALEN];
	uint16_t mtu;
} odp_rpc_ack_pcie_open_t;

/**
 * Command for ODP_RPC_CMD_PCIE_CLOS
 */
typedef odp_rpc_cmd_eth_clos_t odp_rpc_cmd_pcie_clos_t;
ODP_RPC_CHECK_STRUCT_SIZE(odp_rpc_cmd_pcie_clos_t);

#endif /* __ODP_RPC_PCIE_H__ */
