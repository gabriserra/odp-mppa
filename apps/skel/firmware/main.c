#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <assert.h>
#include <unistd.h>
#include <HAL/hal/core/mp.h>

#include "rpc-server.h"
#include "boot.h"

int main(int argc __attribute__((unused)),
	 char *const argv[] __attribute__((unused)))
{
	int status;
	int ret;

	ret = odp_rpc_server_start();
	if (ret) {
		fprintf(stderr, "[RPC] Error: Failed to start server\n");
		exit(EXIT_FAILURE);
	}

	/* Only spawn from IODDR0, not IODDR1 */
	if ( __k1_get_cluster_id() == 128 ) {

		printf("Spawning clusters\n");
		{
			static char const * _argv[] = {
				"odp_example.kelf",
				"-v", NULL
			};
			boot_cluster(0, _argv[0], _argv);
			boot_cluster(4, _argv[0], _argv);
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
