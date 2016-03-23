#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <assert.h>
#include <HAL/hal/hal.h>

#include "rpc-server.h"
#include "pcie.h"
#include "boot.h"

int main (int argc, char *argv[])
{

	int ret, status;

	ret = odp_rpc_server_start();
	if (ret) {
		fprintf(stderr, "[RPC] Error: Failed to start server\n");
		exit(EXIT_FAILURE);
	}

	ret = pcie_init(MPPA_PCIE_ETH_IF_MAX);
	if (ret != 0) {
		fprintf(stderr, "Failed to initialize PCIe eth interface\n");
		exit(1);
	}
	boot_clusters(argc, argv);

	if ((ret = join_clusters(&status)) != 0) {
		fprintf(stderr, "Failed to joined clusters\n");
		return ret;
	}
	if (status){
		fprintf(stderr, "Clusters returned with errors: %d\n", status);
		fflush(stderr);
		return status;
	}

	return 0;
}
