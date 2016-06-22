#include <stdio.h>
#include <getopt.h>
#include <string.h>
#include <mppa_power.h>
#include <mppa_routing.h>
#include <mppa_noc.h>
#include <mppa_bsp.h>
#include <mppa/osconfig.h>

#include "rpc-server.h"
#include "pcie.h"
#include "boot.h"

#define MAX_ARGS                       10
#define MAX_CLUS_NAME                  256

int main (int argc, char *argv[])
{
	int ret, status;

	if (argc < 2) {
		printf("Missing arguments\n");
		exit(1);
	}

	ret = pcie_init(MPPA_PCIE_ETH_IF_MAX);
	if (ret != 0) {
		fprintf(stderr, "Failed to initialize PCIe eth interface\n");
		exit(1);
	}

	ret = odp_rpc_server_start();
	if (ret) {
		fprintf(stderr, "[RPC] Error: Failed to start server\n");
		exit(EXIT_FAILURE);
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
