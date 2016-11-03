#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <assert.h>
#include <unistd.h>
#include <HAL/hal/core/mp.h>


#include "pcie.h"
#include "rpc-server.h"
#include "boot.h"

int main(int argc, char *const argv[])
{

	int ret;
	unsigned clusters = 0;
	unsigned n_clusters = 0;
	int opt;

	while ((opt = getopt(argc, argv, "c:h")) != -1) {
		switch (opt) {
		case 'c':
			{
				unsigned mask = 1 << atoi(optarg);
				if ((clusters & mask) == 0)
					n_clusters ++;
				clusters |= mask;
			}
			break;
		case 'h':
			printf("Usage: %s [ -c <clus_id> -c <clus_id> -c ... ]", argv[0]);
			exit(0);
			break;
		default: /* '?' */
			fprintf(stderr, "Wrong arguments\n");
			return -1;
		}
	}
	if (clusters == 0) {
		clusters = 0xffff;
		n_clusters = 16;
	}


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
				"-i", "p0p0:tags=120,p1p0:tags=120",
				"-m", "0",
				"-s", "0",
				"-c", "10", NULL
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
