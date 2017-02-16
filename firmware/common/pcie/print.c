#include <stdio.h>
#include <stdint.h>
#include <odp/rpc/api.h>

int mppa_odp_rpc_pcie_print_msg(const mppa_rpc_odp_t * cmd, const uint8_t *payload)
{
	switch(cmd->pkt_subtype) {
	case MPPA_RPC_ODP_CMD_PCIE_OPEN:
		if (!cmd->ack){
			mppa_rpc_odp_cmd_pcie_open_t open = { .inl_data = cmd->inl_data };
			printf("\t\tpcie_eth_id: %d\n"
			       "\t\tRx(s)      : [%d:%d]\n",
			       open.pcie_eth_if_id,
			       open.min_rx, open.max_rx);
		} else {
			mppa_rpc_odp_ack_pcie_t ack = { .inl_data = cmd->inl_data };
			printf("\t\ttx_if     : %d\n"
			       "\t\tmin_tx_tag: %d\n"
			       "\t\tmax_tx_tag: %d\n"
			       "\t\tMAC       : %02x%02x%02x%02x%02x%02x\n"
			       "\t\tMTU       : %d\n",
			       ack.cmd.pcie_open.tx_if,
			       ack.cmd.pcie_open.min_tx_tag,
			       ack.cmd.pcie_open.max_tx_tag,
			       ack.cmd.pcie_open.mac[0],
			       ack.cmd.pcie_open.mac[1],
			       ack.cmd.pcie_open.mac[2],
			       ack.cmd.pcie_open.mac[3],
			       ack.cmd.pcie_open.mac[4],
			       ack.cmd.pcie_open.mac[5],
			       ack.cmd.pcie_open.mtu);
		}
		break;
	case MPPA_RPC_ODP_CMD_PCIE_CLOS:
		{
			mppa_rpc_odp_cmd_eth_clos_t clos = { .inl_data = cmd->inl_data };
			printf("\t\tifId: %d\n", clos.ifId);
		}
		break;
	default:
		break;
	}
	(void)payload;
	return 0;
}
