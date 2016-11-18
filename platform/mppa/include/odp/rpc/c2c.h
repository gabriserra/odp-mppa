#ifndef __MPPA_RPC_ODP_C2C_H__
#define __MPPA_RPC_ODP_C2C_H__

#include <odp/rpc/defines.h>

/** Mark C2C Rpc has available */
#define HAS_ODP_RPC_C2C

/** Version of the C2C CoS */
#define MPPA_RPC_ODP_C2C_VERSION 0x2

typedef enum {
	MPPA_RPC_ODP_CMD_C2C_OPEN    /**< Cluster2Cluster: Declare as ready to receive message */,
	MPPA_RPC_ODP_CMD_C2C_CLOS    /**< Cluster2Cluster: Declare as not ready to receive message */,
	MPPA_RPC_ODP_CMD_C2C_QUERY   /**< Cluster2Cluster: Query the amount of creadit available for tx */,
	MPPA_RPC_ODP_CMD_C2C_N_CMD
} mppa_rpc_odp_cmd_c2c_e;

#define MPPA_RPC_ODP_CMD_NAMES_C2C			\
	"C2C OPEN",				\
		"C2C CLOSE",			\
		"C2C QUERY"

/**
 * Command for MPPA_RPC_ODP_CMD_C2C_OPEN
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
	mppa_rpc_odp_inl_data_t inl_data;
} mppa_rpc_odp_cmd_c2c_open_t;
MPPA_RPC_ODP_CHECK_STRUCT_SIZE(mppa_rpc_odp_cmd_c2c_open_t);

/**
 * Command for MPPA_RPC_ODP_CMD_C2C_CLOS
 */
typedef union {
	struct {
		uint8_t cluster_id : 8;
	};
	mppa_rpc_odp_inl_data_t inl_data;
} mppa_rpc_odp_cmd_c2c_clos_t;
MPPA_RPC_ODP_CHECK_STRUCT_SIZE(mppa_rpc_odp_cmd_c2c_clos_t);

/**
 * Command for MPPA_RPC_ODP_CMD_C2C_QUERY
 */
typedef mppa_rpc_odp_cmd_c2c_clos_t mppa_rpc_odp_cmd_c2c_query_t;
MPPA_RPC_ODP_CHECK_STRUCT_SIZE(mppa_rpc_odp_cmd_c2c_query_t);

/**
 * Ack inline for MPPA_RPC_ODP_CMD_C2C_QUERY
 */
typedef struct {
	uint8_t closed  : 1;
	uint8_t eacces  : 1;
	uint8_t min_rx  : 8;
	uint8_t max_rx  : 8;
	uint8_t cnoc_rx : 8;
	uint16_t mtu    : 16;
} mppa_rpc_odp_ack_c2c_query_t;

MPPA_RPC_ODP_DEFINE_ACK(c2c, mppa_rpc_odp_ack_c2c_query_t c2c_query;);


#endif /* __MPPA_RPC_ODP_C2C_H__ */
