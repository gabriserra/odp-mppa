#include <stdio.h>
#include <getopt.h>
#include <string.h>
#include <mppa_power.h>
#include <mppa_routing.h>
#include <mppa_noc.h>
#include <mppa_bsp.h>
#include <mppa/osconfig.h>

#include "odp_rpc_internal.h"
#include "rpc-server.h"
#include "rnd_generator.h"
#include "pcie.h"
#include "boot.h"

#define MAX_ARGS                       10
#define MAX_CLUS_NAME                  256

int main (int argc, char *argv[])
{
	int ret;

	if (argc < 2) {
		printf("Missing arguments\n");
		exit(1);
	}

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

	printf("Cluster booted\n");

	join_clusters();

	return 0;
}
