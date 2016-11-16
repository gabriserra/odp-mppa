#ifndef __MPPA_RPC_MPPA_RPC_ODP_H__
#define __MPPA_RPC_MPPA_RPC_ODP_H__

#include <odp/rpc/defines.h>
#include <odp/rpc/helpers.h>

typedef struct {
	uint64_t data[4];
} mppa_rpc_odp_inl_data_t;

/* Command modules */
#include <odp/rpc/bas.h>

typedef enum {
	MPPA_RPC_ODP_ERR_NONE = 0,
	MPPA_RPC_ODP_ERR_BAD_COS = 1,
	MPPA_RPC_ODP_ERR_BAD_SUBTYPE = 2,
	MPPA_RPC_ODP_ERR_VERSION_MISMATCH = 3,
	MPPA_RPC_ODP_ERR_INTERNAL_ERROR = 4,
	MPPA_RPC_ODP_ERR_TIMEOUT = 5,
} mppa_rpc_odp_cmd_err_e;

typedef struct mppa_rpc_odp {
	uint8_t  pkt_class;      /* Class of Service */
	uint8_t  pkt_subtype;    /* Type of the pkt within the class of service */
	uint16_t cos_version;    /* Version of the CoS used. Used to ensure coherency between
				  * server and client */
	uint16_t data_len;       /* Payload is data len bytes long. data_len < RPC_MAX_PAYLOAD */
	uint8_t  dma_id;         /* Source cluster ID */
	uint8_t  dnoc_tag;       /* Source Rx tag for reply */
	union {
		struct {
			uint8_t ack     : 1; /* When set message is an Ack from a command */
			uint8_t err_str : 1; /* When set, payload is an error string.
						Only valid when message is a ack */
			uint8_t rpc_err : 4; /* RPC Service error. Frame was not understood or broken
					      * See mppa_rpc_odp_cmd_err_e for value */
		};
		uint16_t flags;
	};
	mppa_rpc_odp_inl_data_t inl_data;
} mppa_rpc_odp_t;


/** Class of Services for RPC commands */
typedef enum {
	MPPA_RPC_ODP_CLASS_BAS,
	MPPA_RPC_ODP_CLASS_ETH,
	MPPA_RPC_ODP_CLASS_PCIE,
	MPPA_RPC_ODP_CLASS_C2C,
	MPPA_RPC_ODP_CLASS_RND,
	MPPA_RPC_ODP_CLASS_FP,
	MPPA_RPC_ODP_N_CLASS
} mppa_rpc_odp_class_e;

#define _MPPA_RPC_ODP_DEFINE_ACK(name, cmds)		\
	typedef union {					\
		struct {				\
			uint8_t status;			\
			cmds;				\
		};					\
		mppa_rpc_odp_inl_data_t inl_data;	\
	} mppa_rpc_odp_ack##name##t;			\
	MPPA_RPC_ODP_CHECK_STRUCT_SIZE(mppa_rpc_odp_ack##name##t)

/* Default* Ack struct */
_MPPA_RPC_ODP_DEFINE_ACK(_, uint8_t data[31];);
#define MPPA_RPC_ODP_CMD_ACK_INITIALIZER { .inl_data = { .data = { 0 }}, .data = { 0 }, .status = 0}

/* Macro for each module to define its Ack. */
#define MPPA_RPC_ODP_DEFINE_ACK(name, cmds) _MPPA_RPC_ODP_DEFINE_ACK(_##name##_, union { cmds } cmd)

/** RPC client status */
extern int g_rpc_init;

#endif /* __MPPA_RPC_MPPA_RPC_ODP_H__ */
