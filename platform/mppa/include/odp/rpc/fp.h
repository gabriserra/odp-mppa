#ifndef __ODP_RPC_FP_H__
#define __ODP_RPC_FP_H__

#include <odp/rpc/defines.h>

/** Version of the FP CoS */
#define MPPA_RPC_ODP_FP_VERSION 0x2 /* Entirely arbitrary */

typedef enum {
	MPPA_RPC_ODP_CMD_FP_CLI,
	MPPA_RPC_ODP_CMD_FP_N_CMD
} mppa_rpc_odp_cmd_fp_e;

#define MPPA_RPC_ODP_CMD_NAMES_FP \
	"CLI",

#endif /* __ODP_RPC_FP_H__ */
