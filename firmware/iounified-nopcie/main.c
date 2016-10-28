#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <assert.h>

#include "rpc-server.h"

int main()
{

	int ret;

	ret = odp_rpc_server_start();
	if (ret) {
		fprintf(stderr, "[RPC] Error: Failed to start server\n");
		exit(EXIT_FAILURE);
	}


	while (1);

	return 0;
}
