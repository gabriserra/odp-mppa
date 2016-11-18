#include <stdlib.h>
#include <stdio.h>

#include <odp.h>

/* Example test program */
int main(int argc, char *argv[] ODP_UNUSED)
{
	odp_instance_t instance;
	/* Init ODP before calling anything else */
	/* This will sync all booted clusters */
	if (odp_init_global(&instance, NULL, NULL)) {
		fprintf(stderr, "Error: ODP global init failed.\n");
		exit(EXIT_FAILURE);
	}

	/* Init this thread */
	if (odp_init_local(instance, ODP_THREAD_CONTROL)) {
		fprintf(stderr, "Error: ODP local init failed.\n");
		exit(EXIT_FAILURE);
	}

	printf("Cluster is running with %d arguments\n", argc);

	/* Terminate this thread */
	if (odp_term_local()) {
		fprintf(stderr, "Error: ODP local term failed.\n");
		exit(EXIT_FAILURE);
	}
	/* Terminate ODP  */
	if (odp_term_global(instance)) {
		fprintf(stderr, "Error: ODP global term failed.\n");
		exit(EXIT_FAILURE);
	}

	return 0;
}
