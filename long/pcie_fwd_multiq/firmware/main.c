#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <assert.h>
#include <HAL/hal/core/mp.h>
#include <unistd.h>
#include <string.h>


#include "pcie.h"
#include "netdev.h"
#include "rpc-server.h"
#include "boot.h"

#define N_PCIE_IF 1

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


	/* Setup MultiQ PCIe */
	eth_if_cfg_t if_cfgs[N_PCIE_IF];
	for (int i = 0; i < N_PCIE_IF; ++i){
		if_cfgs[i].mtu = MPODP_DEFAULT_MTU;
		if_cfgs[i].n_c2h_entries = 32;
		if_cfgs[i].n_h2c_entries = 32;
		if_cfgs[i].flags = 0;
		if_cfgs[i].if_id = i;
		if_cfgs[i].n_c2h_q = 1;
		if_cfgs[i].n_h2c_q = 2;
		memcpy(if_cfgs[i].mac_addr, "\x02\xde\xad\xbe\xef", 5);
		if_cfgs[i].mac_addr[MAC_ADDR_LEN - 1] = i + ((__k1_get_cluster_id() - 128) << 1);
	}

	netdev_init(N_PCIE_IF, if_cfgs);
	ret = pcie_start();
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
				"pcie_fwd_multiq",
				"-i", "p0p0:tags=60,p1p0:tags=60",
				"-m", "0",
				"-s", "0",
				"-c", "8", NULL
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
