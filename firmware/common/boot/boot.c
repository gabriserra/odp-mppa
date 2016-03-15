#include <stdio.h>
#include <getopt.h>
#include <string.h>
#include <mppa_power.h>
#include <mppa_routing.h>
#include <mppa_noc.h>
#include <mppa_bsp.h>
#include <mppa/osconfig.h>
#include <errno.h>

#include "boot.h"

#define MAX_ARGS                       256
#define MAX_CLUS_NAME                  256

#define PCIE_ETH_INTERFACE_COUNT	1

enum state {
	STATE_OFF = 0,
	STATE_ON,
};

struct clus_bin_boot {
	int id;
	char *bin;
	const char *argv[MAX_ARGS];
	int argc;
	enum state status;
	mppa_power_pid_t pid;
};

static unsigned has_booted;
static struct clus_bin_boot clus_bin_boots[BSP_NB_CLUSTER_MAX];

int join_cluster(int clus_id, int *status)
{
	mppa_power_pid_t pid;

	if (clus_id < 0 || clus_id >= BSP_NB_CLUSTER_MAX)
		return -1;

	struct clus_bin_boot *clus = &clus_bin_boots[clus_id];

	if (clus_bin_boots[clus_id].status != STATE_ON)
		return -1;
#ifdef VERBOSE
	printf("[BOOT] Joining cluster %d\n", clus_id);
#endif

	pid = mppa_power_base_waitpid(clus->pid, status, 0);

	if (pid < 0) {
		fprintf(stderr, "Failed to join cluster %d\n", clus_id);
		return -1;
	}
#ifdef VERBOSE
	printf("[BOOT] Joined cluster %d. Status=%d\n", clus_id, *status);
#endif
	clus->status = STATE_OFF;
	free(clus->bin);
	clus->bin = NULL;
	return pid;
}

int join_clusters(void)
{
	int i, ret, status;
	if (!has_booted)
		while(1);

	for (i = 0; i < BSP_NB_CLUSTER_MAX; ++i) {
		if (clus_bin_boots[i].status != STATE_ON)
			continue;

		ret = join_cluster(i, &status);
		if (ret < 0)
			return ret;
	}
	return 0;
}

int boot_cluster(int clus_id, const char bin_file[], const char * argv[] ) {
	if (__k1_get_cluster_id() != 128)
		return -1;

	struct clus_bin_boot *clus = &clus_bin_boots[clus_id];
	has_booted = 1;

	if (clus->status != STATE_OFF)
		return -1;

#ifdef VERBOSE
	printf("[BOOT] Spawning cluster %d with binary %s\n",
	       clus_id, bin_file);
#endif
	clus->bin = strdup(bin_file);
	clus->id = clus_id;
	clus->argv[0] = clus->bin;
	clus->argc = 1;
	clus->pid =
	  mppa_power_base_spawn(clus->id,
				  bin_file,
				  argv,
				  NULL,
				  MPPA_POWER_SHUFFLING_DISABLED);
	if (clus->pid < 0) {
		fprintf(stderr, "Failed to spawn cluster %d\n", clus_id);
		return -1;
	}
	clus->status = STATE_ON;

	return 0;
}

int boot_clusters(int argc, char * const argv[])
{
	unsigned int i;
	int opt;
	if (__k1_get_cluster_id() != 128)
		return -1;

	unsigned clus_count = 0;
	while ((opt = getopt(argc, argv, "c:a:")) != -1) {
		switch (opt) {
		case 'c':
			{
				struct clus_bin_boot *clus =
					&clus_bin_boots[clus_count];
				clus->bin = strdup(optarg);
				clus->id = clus_count;
				clus->argv[0] = clus->bin;
				clus->argc = 1;
				clus_count++;
			}
			break;
		case 'a':
			{
				char *pch = strtok(strdup(optarg), " ");
				while ( pch != NULL ) {
					struct clus_bin_boot *clus =
					  &clus_bin_boots[clus_count - 1];
					clus->argv[clus->argc] = pch;
					clus->argc++;
					pch = strtok(NULL, " ");
				}
			}
			break;
		default: /* '?' */
			fprintf(stderr, "Wrong arguments for boot\n");
			return -1;
		}
	}

	for (i = 0; i < clus_count; i++) {
		struct clus_bin_boot *clus = &clus_bin_boots[i];

		clus->argv[clus->argc] = NULL;
		if (boot_cluster(i, clus->argv[0], clus->argv))
			return -1;
	}
	return 0;
}
