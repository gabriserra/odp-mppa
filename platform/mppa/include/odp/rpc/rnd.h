#ifndef __MPPA_RPC_ODP_RND_H__
#define __MPPA_RPC_ODP_RND_H__

#include <odp/rpc/defines.h>

/** Version of the RND CoS */
#define MPPA_RPC_ODP_RND_VERSION 0x2

typedef enum {
	MPPA_RPC_ODP_CMD_RND_GET      /**< RND: Get a buffer with random data generated on IO cluster */,
	MPPA_RPC_ODP_CMD_RND_N_CMD
} mppa_rpc_odp_cmd_rnd_e;

#define MPPA_RPC_ODP_CMD_NAMES_RND			\
	"RANDOM GET"

#define MPPA_RPC_ODP_ACK_LIST_RND

/**
 * Command for MPPA_RPC_ODP_CMD_RND_GET
 */
typedef union {
	struct {
		uint8_t rnd_data[31]; /* Filled with data in response packet */
		uint8_t rnd_len;  /* lenght of random data to send back */
	};
	mppa_rpc_odp_inl_data_t inl_data;
} mppa_rpc_odp_cmd_rnd_t;
MPPA_RPC_ODP_CHECK_STRUCT_SIZE(mppa_rpc_odp_cmd_rnd_t);

#endif /* __MPPA_RPC_ODP_RND_H__ */
