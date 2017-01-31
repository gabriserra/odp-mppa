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
	unsigned n_clusters = 1;
	int opt;

	while ((opt = getopt(argc, argv, "c:h")) != -1) {
		switch (opt) {
			case 'c':
				n_clusters =  atoi(optarg);
				break;
			case 'h':
				printf("Usage: %s [ -c <n_clusters> (number of l2fwd clusters, 1..14) ]", argv[0]);
				exit(0);
				break;
			default: /* '?' */
				fprintf(stderr, "Wrong arguments\n");
				return -1;
		}
	}
	if (!n_clusters) n_clusters = 1;
	if (n_clusters > 14) n_clusters = 14;

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
				"-i", "e0:tags=120:min_payload=48:max_payload=48,e1:tags=120:min_payload=48:max_payload=48",
				"-m", "0",
				"-s", "0",
				"-d", "0",
				"-a", "2",
				//"-S",
				//"-t", "30",
				"-c", "8", NULL
			};

			for (unsigned i = 0; i < n_clusters; i++) 
				boot_cluster(i, _argv[0], _argv);
		}
		if (1)
		{
			static char const * _argv[] = {
				"odp_generator.kelf",
				"-I", "e0:nofree", // generates traffic on eth0,
				"--srcmac", "08:00:27:76:b5:e0",
				"--dstmac", "00:00:00:00:80:01",
				"--srcip",  "192.168.111.2",
				"--dstip",  "192.168.222.2",
				"-m", "u",   // UDP mode
				"-i", "0",   // interval between sends
				"-w", "1",   // worker generating traffic per cluster
				"-P", "64",  // total packet length 64B
				NULL
			};
			boot_cluster(14, _argv[0], _argv);
		}
		if (1)
		{
			static char const * _argv[] = {
				"odp_generator.kelf",
				"-I", "e1:nofree", // generates traffic on eth1,
				"--srcmac", "08:00:27:76:b5:e1",
				"--dstmac", "00:00:00:00:80:01",
				"--srcip",  "192.168.111.2",
				"--dstip",  "192.168.222.2",
				"-m", "u",   // UDP mode
				"-i", "0",   // interval between sends
				"-w", "1",   // worker generating traffic per cluster
				"-P", "64",  // total packet length 64B
				NULL
			};
			boot_cluster(15, _argv[0], _argv);
		}
		printf("Clusters booted\n");
	}

	join_clusters(NULL);

	return 0;
}
