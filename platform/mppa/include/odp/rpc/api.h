#ifndef __ODP_RPC_API_H__
#define __ODP_RPC_API_H__

#include <odp/rpc/rpc.h>

/**
 * Initialize the RPC client.
 * Allocate recption buffer and Rx
 */
int odp_rpc_client_init(void);

/**
 * Close a RPC client
 * Free resource allocated by #odp_rpc_client_init
 */
int odp_rpc_client_term(void);

/**
 * Returns the dma address of the default server to sync with (for C2C)
 */
int odp_rpc_client_get_default_server(void);

/**
 * Print the content of a RPC command
 */
void odp_rpc_print_msg(const struct odp_rpc * cmd);

/**
 * Send a RPC command
 * @param[in] local_interface Local ID of the DMA to use to send the packet
 * @param[in] dest_id DMA address of the target cluster
 * @param[in] dest_tag Rx Tag on the destination cluster
 * @param[in] cmd RPC command to send
 * @param[in] payload Associated payload. The payload length must be stored in cmd->data_len
 */
int odp_rpc_send_msg(uint16_t local_interface, uint16_t dest_id, uint16_t dest_tag,
		     const struct odp_rpc * cmd, const void * payload);

/**
 * Auto fill a RPC command headers with reply informations and
 * send it through #odp_rpc_send_msg
 */
int odp_rpc_do_query(uint16_t dest_id, uint16_t dest_tag,
		     struct odp_rpc * cmd, void * payload);

/**
 * Wait for a ACK.
 * This must be called after a call to odp_rpc_send_msg to wait for a reply
 * @param[out] cmd Address where to store Ack message address.
 * Ack message is only valid until a new RPC command is sent
 * @param[out] payload Address where to store Ack message payload address.
 * payload is only valid until a new RPC command is sent
 * @param[in] timeout Time out in cycles.
 * @retval -1 Error
 * @retval 0 Timeout
 * @retval 1 OK
 */
odp_rpc_cmd_err_e odp_rpc_wait_ack(struct odp_rpc ** cmd, void ** payload, uint64_t timeout);

#endif /* __ODP_RPC_API_H__ */
