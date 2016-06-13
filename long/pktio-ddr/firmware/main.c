#include <stdlib.h>
#include <stdio.h>
#include <inttypes.h>
#include <assert.h>
#include <unistd.h>
#include <string.h>
#include <HAL/hal/hal.h>
#include <mppa_routing.h>
#include <mppa_noc.h>

#include "rpc-server.h"
#include "boot.h"

/* If you change this, make sure to change the nfragments paremeter of the pktio */
#define DATA_SIZE 2048
#define PKTIO_MTU 512

char data[DATA_SIZE];


union mppa_ethernet_header_info_t {
	mppa_uint64 dword;
	mppa_uint32 word[2];
	mppa_uint16 hword[4];
	mppa_uint8 bword[8];
	struct {
		mppa_uint32 pkt_size : 16;
		mppa_uint32 hash_key : 16;
		mppa_uint32 lane_id  : 2;
		mppa_uint32 io_id    : 1;
		mppa_uint32 rule_id  : 4;
		mppa_uint32 pkt_id   : 25;
	} _;
};

typedef struct mppa_ethernet_header_s {
	mppa_uint64 timestamp;
	union mppa_ethernet_header_info_t info;
} mppa_ethernet_header_t;


int main()
{

	int ret, status;
	unsigned nocTx;
	mppa_dnoc_header_t header = { 0 };
	mppa_dnoc_channel_config_t config = { 0 };
	mppa_ethernet_header_t eth_header;

	memset(&eth_header, 0, sizeof(eth_header));

	ret = odp_rpc_server_start();
	if (ret) {
		fprintf(stderr, "[RPC] Error: Failed to start server\n");
		exit(EXIT_FAILURE);
	}


	printf("Spawning clusters\n");
	{
		static char const * _argv[] = {
			"pktio-ddr",
			"-i", "ioddr0:min_rx=127:max_rx=142:nfragments=4,drop",
			"-m", "0",
			"-s", "0",
			"-t", "15",
			"-c", "2", NULL
		};

		boot_cluster(0, _argv[0], _argv);
	}
	printf("Cluster booted\n");


	for (int i = 0; i < DATA_SIZE; ++i)
		data[i] = i;
	ret = mppa_routing_get_dnoc_unicast_route(128, 0, &config, &header);
	if (ret != MPPA_ROUTING_RET_SUCCESS) {
		fprintf(stderr, "Failed to route to cluster 0\n");
		return -1;
	}

	config._.loopback_multicast = 0;
	config._.cfg_pe_en = 1;
	config._.cfg_user_en = 1;
	config._.write_pe_en = 1;
	config._.write_user_en = 1;
	config._.decounter_id = 0;
	config._.decounted = 0;
	config._.payload_min = 0;
	config._.payload_max = 32;
	config._.bw_current_credit = 0xff;
	config._.bw_max_credit     = 0xff;
	config._.bw_fast_delay     = 0x00;
	config._.bw_slow_delay     = 0x00;

	header._.multicast = 0;
	header._.valid = 1;

	ret = mppa_noc_dnoc_tx_alloc_auto(0, &nocTx, MPPA_NOC_BLOCKING);
	if (ret != MPPA_NOC_RET_SUCCESS) {
		fprintf(stderr, "Failed to find an available Tx on DMA 0\n");
		return -1;
	}

	sleep(10);
	header._.tag = 127;

	for( int i = 0; i < DATA_SIZE / PKTIO_MTU; i ++) {
		printf("Start sending seg %d\n", i);
		header._.tag = 127 + i;

		ret = mppa_noc_dnoc_tx_configure(0, nocTx, header, config);
		if (ret != MPPA_NOC_RET_SUCCESS) {
			fprintf(stderr, "Failed to configure Tx\n");
			return -1;
		}

		eth_header.timestamp = i;
		eth_header.info._.pkt_id = i;
		eth_header.info._.pkt_size = PKTIO_MTU + sizeof(eth_header);
		mppa_noc_dnoc_tx_send_data(0, nocTx, sizeof(eth_header), &eth_header);
		mppa_noc_dnoc_tx_send_data_eot(0, nocTx, PKTIO_MTU, &data[i * PKTIO_MTU]);
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
