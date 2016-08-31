#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <assert.h>
#include <unistd.h>
#include <HAL/hal/core/mp.h>

#include "rpc-server.h"
#include "boot.h"

int main()
{

	int ret;

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
				"-i", "e0:loop,e1:loop",
				"-r", "192.168.111.2/32:e0:00.07.43.30.4a.70",
				"-r", "192.168.222.2/32:e1:00.07.43.30.4a.78",
				"-p", "192.168.111.0/24:192.168.222.0/24:out:both",
				"-e", "192.168.111.2:192.168.222.2:aesgcm:201:656c8523255ccc23a66c1917aa0cf309",
				"-a", "192.168.111.2:192.168.222.2:aesgcm:200:656c8523255ccc23a66c1917aa0cf309",
				"-p", "192.168.222.0/24:192.168.111.0/24:in:both",
				"-e", "192.168.222.2:192.168.111.2:aesgcm:301:656c8523255ccc23a66c1917aa0cf309",
				"-a", "192.168.222.2:192.168.111.2:aesgcm:300:656c8523255ccc23a66c1917aa0cf309",
				"-c", "14", "-m", "ASYNC_IN_PLACE", NULL
			};

			for (int i = 0; i < 16; i++) {
				if ( i % 4 == 0 ) continue;
				if ( i % 4 == 3 ) continue;
				boot_cluster(i, _argv[0], _argv);
			}
		}
		{
			static char const * _argv[] = {
				"odp_generator.kelf",
				"-I", "e0:loop:nofree",
				"--srcmac", "08:00:27:76:b5:e0",
				"--dstmac", "00:00:00:00:80:01",
				"--srcip",  "192.168.111.2",
				"--dstip", "192.168.222.2",
				"-m", "u",
				"-i", "0", "-w", "2", "-p", "256", NULL
			};

			for (int i = 0; i < 16; i += 4) {
        if ( i % 4 == 1 ) continue;
				if ( i % 4 == 2 ) continue;
				if ( i % 4 == 3 ) continue;
				boot_cluster(i, _argv[0], _argv);
			}
		}
		printf("Cluster booted\n");
	}

	join_clusters(NULL);

	return 0;
}
