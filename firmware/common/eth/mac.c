#include <stdint.h>
#include <stdio.h>
#include <HAL/hal/hal.h>
#include "internal/cnoc_debug.h"

static int debug_cnoc_init = 0;
void init_debug_cnoc()  {

  //may be change by a real route finding function
  unsigned int route = 0x2 | 0x2 << 3;
  unsigned int return_route = 0x2 | 0x2 << 3;

  mppa_syscnoc[0]->control.header.dword = 0xC0000000 | (route>>3);
  mppa_syscnoc[0]->control.config.dword = route & 0x7; //fdir
  // Set return route
  __k1_cnoc_debug_send_set_return_route_first_dir (return_route & 0x7);
  __k1_cnoc_debug_send_set_return_route (0xC0000000 | (return_route>>3));
  __k1_cnoc_debug_send_set_ctrl_reg (1);
}
int mppa_ethernet_generate_mac(unsigned int ioeth_id, unsigned int ifce_id, uint8_t *buffer)
{
	uint64_t serial, mac;
	uint8_t ifce_value = 0;
	int i;
	const int clus_id = __k1_get_cluster_id();

	if (ifce_id >= 4) {
		fprintf(stderr, "[ETH] Error: Wrong interface id %d\n", ifce_id);
		return -1;
	}

	/* create the interface number for the MAC address */
	if (ioeth_id == 160) {
		ifce_value = ifce_id;
	} else if (ioeth_id == 224) {
		ifce_value = ifce_id + 4;
	} else {
		fprintf(stderr, "[ETH] Error: Wrong ethernet cluster id %d\n", ioeth_id);
		return -1;
	}

	if ((clus_id >= 128 && clus_id <= 131) ||
	    (clus_id >= 160 && clus_id <= 163)) {
		mppa_fuse_init();
		serial = mppa_fuse_get_serial();
	} else if ((clus_id >= 192 && clus_id <= 195) ||
		   (clus_id >= 224 && clus_id <= 227)) {

		if (!debug_cnoc_init) {
			init_debug_cnoc();
			debug_cnoc_init = 1;
		}
		serial = __k1_cnoc_debug_peek((unsigned int) &mppa_ftu[0]->fuse_box[1][0],
					      sizeof(unsigned long long));

	} else {
		/* no fuses on this cluser */
		fprintf(stderr, "[ETH] Warning: Could not access fuse\n");
		serial = 0ULL;
	}

	if (serial == 0) {
		fprintf(stderr, "[ETH] Warning: Fuse returns bad serial\n");
	}
	{
		uint64_t timestamp;
		unsigned year = (serial >> 36) & 0xff;
		unsigned month = (serial >> 32) & 0x0f;
		unsigned day = (serial >> 27) & 0x1f;
		unsigned hour = (serial >> 22) & 0x1f;
		unsigned minute = (serial >> 16) & 0x3f;
		/* unsigned second = (serial >> 10) & 0x3f; */

		timestamp =
			year * 12 * 31 * 24 * 60 +
			month * 31 * 24 * 60 +
			day * 24 * 60 +
			hour * 60 +
			minute;
		mac = (0x02ULL << 40) | ((timestamp << 4) & 0xfffffffff0) | (ifce_value << 1);
	}
	/* Generate the MAC */
	for (i = 0; i < 6; i++) {
		uint8_t v = (mac >> (i * 8)) & 0xff;
		buffer[5-i] = v;
	}
	return 0;
}