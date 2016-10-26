#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <assert.h>
#include <HAL/hal/hal.h>
#include <unistd.h>

#include "rpc-server.h"
#include "boot.h"
#include "pcie.h"

int main(int argc,
	 char *const argv[] )
{
	int status;
	int ret;

	ret = pcie_init(MPPA_PCIE_ETH_IF_MAX, 0);
	if (ret != 0) {
		fprintf(stderr, "Failed to initialize PCIe eth interface\n");
		exit(1);
	}

	ret = odp_rpc_server_start();
	if (ret) {
		fprintf(stderr, "[RPC] Error: Failed to start server\n");
		exit(EXIT_FAILURE);
	}

	/* Only spawn from IODDR0, not IODDR1 */
	if ( __k1_get_cluster_id() == 128 ) {

		printf("Spawning clusters\n");
		{
			boot_clusters(argc, argv);
		}
		printf("Cluster booted\n");
	}

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
