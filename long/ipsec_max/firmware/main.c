#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <assert.h>
#include <HAL/hal/hal.h>
#include <unistd.h>

#include "rpc-server.h"
#include "boot.h"

int main(int argc, char **argv)
{
    int status;
	int ret;

    // default values
    static char _z[] = "0.50"; // target pps at 500Mhz: z * 1e6
    static char _k[] = "100"; // target pkt count perf cluster : k * 1e6

    if (argc < 2 || argc > 3) {
		fprintf(stderr, "Usage : %s <ref_perf (*1e6 pps, @ 500Mhz)> <pkt_cnt (*1e6 pkts, per cluster)>\n", argv[0]);
		exit(EXIT_FAILURE);
    }

    snprintf(_z, sizeof(_z), "%s", argv[1]);
    snprintf(_k, sizeof(_k), "%s", argv[2]);

	ret = odp_rpc_server_start();
	if (ret) {
		fprintf(stderr, "[RPC] Error: Failed to start server\n");
		exit(EXIT_FAILURE);
	}

	if ( __k1_get_cluster_id() == 128 ) {
		printf("Spawning clusters\n");
		{
			static char const * _argv[] = {
				"odp_ipsec.kelf",
                "-z", _z,
                "-k", _k,
#if 1
				"-i", "e0:loop,e1:loop",
#else
				"-i", "e0,e1",
#endif
				"-r", "192.168.111.2/32:e0:00.07.43.30.4a.70",
				"-r", "192.168.222.2/32:e1:00.07.43.30.4a.78",
				"-p", "192.168.111.0/24:192.168.222.0/24:out:both",
				"-e", "192.168.111.2:192.168.222.2:aesgcm:201:656c8523255ccc23a66c1917aa0cf309",
				"-a", "192.168.111.2:192.168.222.2:aesgcm:200:656c8523255ccc23a66c1917aa0cf309",
				"-p", "192.168.222.0/24:192.168.111.0/24:in:both",
				"-e", "192.168.222.2:192.168.111.2:aesgcm:301:656c8523255ccc23a66c1917aa0cf309",
				"-a", "192.168.222.2:192.168.111.2:aesgcm:300:656c8523255ccc23a66c1917aa0cf309",
				//"-c", "14", "-m", "ASYNC_IN_PLACE", NULL
				"-c", "14", "-m", "2", NULL//"ASYNC_IN_PLACE", NULL
			};

			for (int i = 0; i < 16; i++) {
                boot_cluster(i, _argv[0], _argv);
			}
		}
		//printf("All clusters booted\n");
	}

	if ((ret = join_clusters(&status)) != 0) {
		fprintf(stderr, "Failed to join clusters\n");
		return ret;
	}
	if (status){
		fprintf(stderr, "Some clusters returned with errors, status = %d\n", status);
		fflush(stderr);
		return status;
	}

    fprintf(stdout, "All clusters returned without error, status = %d\n", status);

	return 0;
}
