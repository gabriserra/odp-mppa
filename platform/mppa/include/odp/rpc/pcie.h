#ifndef __MPPA_RPC_ODP_PCIE_H__
#define __MPPA_RPC_ODP_PCIE_H__

#include <odp/rpc/defines.h>

/** Mark BAS Rpc has available */
#define HAS_ODP_RPC_PCIE

/** Version of the PCIE CoS */
#define MPPA_RPC_ODP_PCIE_VERSION 0x2

#define PCIE_ALEN 6

typedef enum {
	MPPA_RPC_ODP_CMD_PCIE_OPEN    /**< PCIe: Forward Rx traffic to a cluster */,
	MPPA_RPC_ODP_CMD_PCIE_CLOS    /**< PCIe: Stop forwarding Rx trafic to a cluster */,
	MPPA_RPC_ODP_CMD_PCIE_N_CMD
} mppa_rpc_odp_cmd_pcie_e;

#define MPPA_RPC_ODP_CMD_NAMES_PCIE			\
	"PCIE OPEN",				\
		"PCIE CLOSE"

/**
 * Command for MPPA_RPC_ODP_CMD_PCIE_OPEN
 */
typedef union {
	struct {
		uint16_t pkt_size;
		uint8_t pcie_eth_if_id; /* PCIe eth interface number */
		uint8_t min_rx;
		uint8_t max_rx;
		uint8_t cnoc_rx;
	};
	mppa_rpc_odp_inl_data_t inl_data;
} mppa_rpc_odp_cmd_pcie_open_t;
MPPA_RPC_ODP_CHECK_STRUCT_SIZE(mppa_rpc_odp_cmd_pcie_open_t);

/**
 * Ack inline for MPPA_RPC_ODP_CMD_PCIE_OPEN
 */
typedef struct {
	uint16_t tx_if;	/* IO Cluster id */
	uint8_t  min_tx_tag;	/* Tag of the first IO Cluster rx */
	uint8_t  max_tx_tag;	/* Tag of the last IO Cluster rx */
	uint8_t  mac[PCIE_ALEN];
	uint16_t mtu;
} mppa_rpc_odp_ack_pcie_open_t;

/**
 * Command for MPPA_RPC_ODP_CMD_PCIE_CLOS
 */
typedef union {
	struct {
		uint8_t ifId : 3; /* 0-3, 4 for 40G */
	};
	mppa_rpc_odp_inl_data_t inl_data;
} mppa_rpc_odp_cmd_pcie_clos_t;
MPPA_RPC_ODP_CHECK_STRUCT_SIZE(mppa_rpc_odp_cmd_pcie_clos_t);

MPPA_RPC_ODP_DEFINE_ACK(pcie, mppa_rpc_odp_ack_pcie_open_t pcie_open;);


#endif /* __MPPA_RPC_ODP_PCIE_H__ */
