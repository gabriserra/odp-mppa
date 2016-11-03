#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <assert.h>

#include "pcie.h"
#include "rpc-server.h"

int main (int argc __attribute__((unused)), char *argv[] __attribute__((unused)))
{

	int ret;

	ret = pcie_init(MPPA_PCIE_ETH_IF_MAX, 0);
	if (ret != 0) {
		fprintf(stderr, "Failed to initialize PCIe eth interface\n");
		exit(1);
	}

	ret = odp_rpc_server_start();
	if (ret) {
		fprintf(stderr, "Failed to start server\n");
		exit(EXIT_FAILURE);
	}

	return odp_rpc_server_thread();
}
