#ifndef RPC_SERVER_INTERNAL__H
#define RPC_SERVER_INTERNAL__H

#define MAX_RPC_HANDLERS 32
#define RPC_MAX_CLIENTS (BSP_NB_CLUSTER_MAX + BSP_NB_IOCLUSTER_MAX * 4)

typedef int (*odp_rpc_handler_t)(unsigned remoteClus, odp_rpc_t *msg, uint8_t *payload);

typedef struct {
	odp_rpc_t *msg;
	odp_rpc_ack_t ack;
	uint8_t  payload[RPC_MAX_PAYLOAD] __attribute__((aligned(8)));
	uint16_t payload_len;
} odp_rpc_answer_t;

#define ODP_RPC_ANSWER_INITIALIZER(m)  { .msg = m, .ack = ODP_RPC_CMD_ACK_INITIALIZER, .payload_len = 0 }

#ifdef VERBOSE
#define RPC_VERBOSE_PRINT(x...) do { printf(x);}while(0)
#else
#define RPC_VERBOSE_PRINT(x...) do { if(0) printf(x);}while(0)
#endif

#define RPC_ERROR(answer, mod, x...)										\
	do {													\
		if ((uintptr_t) NULL != (uintptr_t)answer) {							\
			(answer)->ack.status = 1;								\
			(answer)->msg->err_str = 1;								\
			(answer)->payload_len = snprintf((char*)(answer)->payload,				\
							 RPC_MAX_PAYLOAD, mod " Error:" x);			\
			RPC_VERBOSE_PRINT(mod " Error:" x);							\
		}												\
	} while(0)

int odp_rpc_server_ack(odp_rpc_answer_t *answer);

/** Global structure for modules to register their handlers */
extern odp_rpc_handler_t __rpc_handlers[ODP_RPC_N_CLASS];

static inline int get_rpc_tag_id(unsigned cluster_id)
{
	return odp_rpc_get_io_tag_id(cluster_id);
}

static inline int get_rpc_local_dma_id(unsigned cluster_id)
{
	int if_id = odp_rpc_get_io_dma_id(0, cluster_id) - 160;
	/* On K1B, DMA 0-3 belong to IODDR */
	if_id += 4;

	return if_id;
}


#endif /* RPC_SERVER_INTERNAL__H */
