/* Copyright (c) 2015, Linaro Limited
 * All rights reserved.
 *
 * SPDX-License-Identifier:     BSD-3-Clause
 */

#include <stdlib.h>
#include "pktio.h"

int main(int argc, char* argv[])
{
	int idx = 1;
	int ret = 0;

	while (idx < argc) {
		if(!strcmp(argv[idx], "--")){
			/* Empty argc. Skip */
			idx++;
		} else if (argc - idx == 1 || !strcmp(argv[idx + 1], "--")) {
			/* Last arg, or just one for this run */
			printf("Running with one pktio: '%s'\n", argv[idx]);
			setenv("ODP_PKTIO_IF0", argv[idx], 1);
			unsetenv("ODP_PKTIO_IF1");
			idx += 2;
			ret |= pktio_main(argc, argv);
		} else if (argc - idx == 2 || !strcmp(argv[idx + 2], "--")) {
			setenv("ODP_PKTIO_IF0", argv[idx], 1);
			setenv("ODP_PKTIO_IF1", argv[idx + 1], 1);
			printf("Running with 2 pktio: '%s' <=> '%s'\n", argv[idx], argv[idx + 1]);
			idx += 3;
			ret |= pktio_main(argc, argv);
		} else {
			fprintf(stderr, "Bag arguments from %d\n", idx);
			exit(1);
		}
	}
	return ret;
}
