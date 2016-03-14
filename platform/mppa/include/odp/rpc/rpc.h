#ifndef __ODP_RPC_RPC_H__
#define __ODP_RPC_RPC_H__

#include <odp/rpc/defines.h>
#include <odp/rpc/helpers.h>

typedef struct {
	uint64_t data[4];
} odp_rpc_inl_data_t;

/* Command modules */
#include <odp/rpc/bas.h>
#include <odp/rpc/eth.h>
#include <odp/rpc/pcie.h>
#include <odp/rpc/c2c.h>
#include <odp/rpc/rnd.h>

typedef enum {
	ODP_RPC_ERR_NONE = 0,
	ODP_RPC_ERR_BAD_COS = 1,
	ODP_RPC_ERR_BAD_SUBTYPE = 2,
	ODP_RPC_ERR_VERSION_MISMATCH = 3,
	ODP_RPC_ERR_INTERNAL_ERROR = 4,
	ODP_RPC_ERR_BAD_CMD = 5,
	ODP_RPC_ERR_TIMEOUT = 6,
} odp_rpc_cmd_err_e;

typedef struct odp_rpc {
	uint8_t  pkt_class;      /* Class of Service */
	uint8_t  pkt_subtype;    /* Type of the pkt within the class of service */
	uint16_t cos_version;    /* Version of the CoS used. Used to ensure coherency between
				  * server and client */
	uint16_t data_len;       /* Packet is data len * 8B long. data_len < RPC_MAX_PAYLOAD / 8 */
	uint8_t  dma_id;         /* Source cluster ID */
	uint8_t  dnoc_tag;       /* Source Rx tag for reply */
	union {
		struct {
			uint8_t ack     : 1;
			uint8_t rpc_err : 4;
		};
		uint16_t flags;
	};
	odp_rpc_inl_data_t inl_data;
} odp_rpc_t;


/** Class of Services for RPC commands */
typedef enum {
	ODP_RPC_CLASS_BAS,
	ODP_RPC_CLASS_ETH,
	ODP_RPC_CLASS_PCIE,
	ODP_RPC_CLASS_C2C,
	ODP_RPC_CLASS_RND,
	ODP_RPC_N_CLASS
} odp_rpc_class_e;


typedef union {
	struct {
		uint8_t status;
		union {
			uint8_t foo;                    /* Dummy entry for init */
			ODP_RPC_ACK_LIST_BAS
			ODP_RPC_ACK_LIST_ETH
			ODP_RPC_ACK_LIST_PCIE
			ODP_RPC_ACK_LIST_C2C
			ODP_RPC_ACK_LIST_RND
		} cmd;
	};
	odp_rpc_inl_data_t inl_data;
} odp_rpc_ack_t;
ODP_RPC_CHECK_STRUCT_SIZE(odp_rpc_ack_t);

#define ODP_RPC_CMD_ACK_INITIALIZER { .inl_data = { .data = { 0 }}, .cmd = { 0 }, .status = -1}

/** RPC client status */
extern int g_rpc_init;

#endif /* __ODP_RPC_RPC_H__ */
