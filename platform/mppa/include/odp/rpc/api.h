#ifndef __MPPA_RPC_ODP_API_H__
#define __MPPA_RPC_ODP_API_H__

#include <odp/rpc/rpc.h>

/**
 * Initialize the RPC client.
 * Allocate recption buffer and Rx
 */
int mppa_rpc_odp_client_init(void);

/**
 * Close a RPC client
 * Free resource allocated by #mppa_rpc_odp_client_init
 */
int mppa_rpc_odp_client_term(void);

/**
 * Returns the dma address of the default server to sync with (for C2C)
 */
int mppa_rpc_odp_client_get_default_server(void);

/**
 * Print the content of a RPC command
 */
void mppa_rpc_odp_print_msg(const struct mppa_rpc_odp * cmd, const uint8_t *payload);

/**
 * Send a RPC command
 * @param[in] local_interface Local ID of the DMA to use to send the packet
 * @param[in] dest_id DMA address of the target cluster
 * @param[in] dest_tag Rx Tag on the destination cluster
 * @param[in] cmd RPC command to send
 * @param[in] payload Associated payload. The payload length must be stored in cmd->data_len
 */
int mppa_rpc_odp_send_msg(uint16_t local_interface, uint16_t dest_id, uint16_t dest_tag,
		     const struct mppa_rpc_odp * cmd, const void * payload);

/**
 * Auto fill a RPC command headers with reply informations and
 * send it through #mppa_rpc_odp_send_msg
 */
int mppa_rpc_odp_do_query(uint16_t dest_id, uint16_t dest_tag,
		     struct mppa_rpc_odp * cmd, void * payload);

/**
 * Wait for a ACK.
 * This must be called after a call to mppa_rpc_odp_send_msg to wait for a reply
 * @param[out] cmd Address where to store Ack message address.
 * Ack message is only valid until a new RPC command is sent
 * @param[out] payload Address where to store Ack message payload address.
 * payload is only valid until a new RPC command is sent
 * @param[in] timeout Time out in cycles.
 * @param[in] mod Name of the module calling wait_ack to add to error messages
 * @retval -1 Error
 * @retval 0 Timeout
 * @retval 1 OK
 */
mppa_rpc_odp_cmd_err_e mppa_rpc_odp_wait_ack(struct mppa_rpc_odp ** cmd, void ** payload, uint64_t timeout,
				   const char* mod);

/**
 * Register a RPC class.
 *
 * Aallows mppa_rpc_odp_print_msg to
 * properly display the new class messages.
 * This is not needed for the default classes.
 *
 * @param[in] class_id Id of the class
 * @param[in] class_name Name of the class
 * @param[in] n_commands Number of commands supported by the class
 * @param[in] command_names Array of string containing the class command names
 * @param[in] print_msg Optional callback to be called by mppa_rpc_odp_print_msg
 * to display commands arguments/payloads
 */
int mppa_rpc_odp_register_class(int class_id, const char * class_name,
								int n_commands, const char * const * command_names,
								mppa_rpc_odp_print_t print_msg);

#endif /* __MPPA_RPC_ODP_API_H__ */
