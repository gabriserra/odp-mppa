#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <assert.h>
#include <unistd.h>
#include <HAL/hal/core/mp.h>


#include "pcie.h"
#include "rpc-server.h"
#include "boot.h"

int main()
{

	int ret;
	unsigned clusters = 0;

	clusters = 0xf;

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
	if ( __k1_get_cluster_id() == 128 ) {
		printf("Spawning clusters\n");
		{
			static char const * _argv[] = {
				"odp_l2fwd.kelf",
				"-i", "e0:tags=50,p0p0:tags=50,e1:tags=50,p1p0:tags=50",
				"-m", "0",
				"-s", "0",
				"-d", "0",
				"-c", "10", "--allow_fail", NULL
			};

			while(clusters) {
				int clus_id = __builtin_k1_ctz(clusters);
				clusters &= ~ (1 << clus_id);
				boot_cluster(clus_id, _argv[0], _argv);
			}
		}
		printf("Cluster booted\n");
	}

	join_clusters(NULL);

	return 0;
}
