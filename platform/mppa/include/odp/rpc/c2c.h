#ifndef __ODP_RPC_C2C_H__
#define __ODP_RPC_C2C_H__

#include <odp/rpc/defines.h>

/** Version of the C2C CoS */
#define ODP_RPC_C2C_VERSION 0x1

typedef enum {
	ODP_RPC_CMD_C2C_OPEN    /**< Cluster2Cluster: Declare as ready to receive message */,
	ODP_RPC_CMD_C2C_CLOS    /**< Cluster2Cluster: Declare as not ready to receive message */,
	ODP_RPC_CMD_C2C_QUERY   /**< Cluster2Cluster: Query the amount of creadit available for tx */,
	ODP_RPC_CMD_C2C_N_CMD
} odp_rpc_cmd_c2c_e;

#define ODP_RPC_CMD_NAMES_C2C			\
	"C2C OPEN",				\
		"C2C CLOSE",			\
		"C2C QUERY"

#define ODP_RPC_ACK_LIST_C2C odp_rpc_ack_c2c_query_t c2c_query;

/**
 * Command for ODP_RPC_CMD_C2C_OPEN
 */
typedef union {
	struct {
		uint8_t cluster_id : 8;
		uint8_t min_rx     : 8;
		uint8_t max_rx     : 8;
		uint8_t rx_enabled : 1;
		uint8_t tx_enabled : 1;
		uint8_t cnoc_rx    : 8;
		uint16_t mtu       :16;
	};
	odp_rpc_inl_data_t inl_data;
} odp_rpc_cmd_c2c_open_t;
ODP_RPC_CHECK_STRUCT_SIZE(odp_rpc_cmd_c2c_open_t);

/**
 * Command for ODP_RPC_CMD_C2C_CLOS
 */
typedef union {
	struct {
		uint8_t cluster_id : 8;
	};
	odp_rpc_inl_data_t inl_data;
} odp_rpc_cmd_c2c_clos_t;
ODP_RPC_CHECK_STRUCT_SIZE(odp_rpc_cmd_c2c_clos_t);

/**
 * Command for ODP_RPC_CMD_C2C_QUERY
 */
typedef odp_rpc_cmd_c2c_clos_t odp_rpc_cmd_c2c_query_t;
ODP_RPC_CHECK_STRUCT_SIZE(odp_rpc_cmd_c2c_query_t);

/**
 * Ack inline for ODP_RPC_CMD_C2C_QUERY
 */
typedef struct {
	uint8_t closed  : 1;
	uint8_t eacces  : 1;
	uint8_t min_rx  : 8;
	uint8_t max_rx  : 8;
	uint8_t cnoc_rx : 8;
	uint16_t mtu    : 16;
} odp_rpc_ack_c2c_query_t;

#endif /* __ODP_RPC_C2C_H__ */
