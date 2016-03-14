#ifndef __ODP_RPC_RND_H__
#define __ODP_RPC_RND_H__

#include <odp/rpc/defines.h>

/** Version of the RND CoS */
#define ODP_RPC_RND_VERSION 0x1

typedef enum {
	ODP_RPC_CMD_RND_GET      /**< RND: Get a buffer with random data generated on IO cluster */,
	ODP_RPC_CMD_RND_N_CMD
} odp_rpc_cmd_rnd_e;

#define ODP_RPC_CMD_NAMES_RND			\
	"RANDOM GET"

#define ODP_RPC_ACK_LIST_RND

/**
 * Command for ODP_RPC_CMD_RND_GET
 */
typedef union {
	struct {
		uint8_t rnd_data[31]; /* Filled with data in response packet */
		uint8_t rnd_len;  /* lenght of random data to send back */
	};
	odp_rpc_inl_data_t inl_data;
} odp_rpc_cmd_rnd_t;
ODP_RPC_CHECK_STRUCT_SIZE(odp_rpc_cmd_rnd_t);

#endif /* __ODP_RPC_RND_H__ */
