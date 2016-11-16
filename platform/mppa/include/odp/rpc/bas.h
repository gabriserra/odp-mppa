#ifndef __MPPA_RPC_ODP_BAS_H__
#define __MPPA_RPC_ODP_BAS_H__

#include <odp/rpc/defines.h>

/** Version of the BAS CoS */
#define MPPA_RPC_ODP_BAS_VERSION 0x2

typedef enum {
	MPPA_RPC_ODP_CMD_BAS_INVL = 0 /**< BASE: Invalid command. Skip */,
	MPPA_RPC_ODP_CMD_BAS_PING     /**< BASE: Ping command. server sends back ack = 0 */,
	MPPA_RPC_ODP_CMD_BAS_N_CMD
} mppa_rpc_odp_cmd_bas_e;

#define MPPA_RPC_ODP_CMD_NAMES_BAS			\
	"INVALID",							\
		"PING"

#endif /* __MPPA_RPC_ODP_BAS_H__ */
