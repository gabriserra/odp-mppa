#ifndef __ODP_RPC_BAS_H__
#define __ODP_RPC_BAS_H__

#include <odp/rpc/defines.h>

/** Version of the BAS CoS */
#define ODP_RPC_BAS_VERSION 0x1

typedef enum {
	ODP_RPC_CMD_BAS_INVL = 0 /**< BASE: Invalid command. Skip */,
	ODP_RPC_CMD_BAS_PING     /**< BASE: Ping command. server sends back ack = 0 */,
	ODP_RPC_CMD_BAS_N_CMD
} odp_rpc_cmd_bas_e;

#define ODP_RPC_CMD_NAMES_BAS			\
	"INVALID",							\
		"PING"

#define ODP_RPC_ACK_LIST_BAS

#endif /* __ODP_RPC_BAS_H__ */
