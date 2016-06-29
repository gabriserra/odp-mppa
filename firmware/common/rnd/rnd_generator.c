#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>
#include <assert.h>
#include <string.h>
#include <mppa_bsp.h>
#include <odp/rpc/rpc.h>
#include <odp/rpc/api.h>

#include "internal/lib_trng.h"
#include "internal/rpc-server.h"

#define TRNG_AB01_PARAM_NOISE 12
#define TRNG_AB01_PARAM_DIV   4
#define TRNG_AB01_PARAM_CYCLE 8

#define TRNG_KONIC80_PARAM_NOISE 12
#define TRNG_KONIC80_PARAM_DIV   15
#define TRNG_KONIC80_PARAM_CYCLE 4

void
odp_rnd_gen_init() {
	switch (__bsp_flavour) {
	case BSP_KONIC80:
		mppa_trng_config_simple(TRNG_KONIC80_PARAM_NOISE,
					TRNG_KONIC80_PARAM_DIV,
					TRNG_KONIC80_PARAM_CYCLE);
		break;
	case BSP_DEVELOPER:
		mppa_trng_config_simple(TRNG_AB01_PARAM_NOISE,
					TRNG_AB01_PARAM_DIV,
					TRNG_AB01_PARAM_CYCLE);
		break;
	default:
		/* Not supported, do nothing */
		return;
	}
	mppa_trng_enable();
}


int
odp_rnd_gen_get(char *buf, unsigned len) {
	(void)buf;

	unsigned curr_len = 0;
	while ( curr_len < len ) {
		mppa_trng_rnd_data_t random_data = {{0}};
		if (mppa_trng_failure_event()) {
#ifdef VERBOSE
			printf("[TRNG] failure detected. Rearming TRNG\n");
#endif
			switch (__bsp_flavour) {
			case BSP_KONIC80:
				mppa_trng_repare_failure(TRNG_KONIC80_PARAM_NOISE,
							 TRNG_KONIC80_PARAM_DIV,
							 TRNG_KONIC80_PARAM_CYCLE);
				break;
			case BSP_DEVELOPER:
				mppa_trng_repare_failure(TRNG_AB01_PARAM_NOISE,
							 TRNG_AB01_PARAM_DIV,
							 TRNG_AB01_PARAM_CYCLE);
				break;
			default:
				/* Not supported, do nothing */
				return 0;
			}
		}
		while (!mppa_trng_data_ready()) {};

		int data_status = mppa_trng_read_data(&random_data);

		if (!data_status) {
#ifdef VERBOSE
			printf("[TRNG] invalid random data\n");
			printf("[TRNG] int status: %x\n", mppa_trng_read_status());
#endif
			return 0;
		}
		mppa_trng_ack_data();
		unsigned copy_len = len - curr_len;
		if (copy_len > sizeof(random_data.data))
			copy_len = sizeof(random_data.data);
		memcpy(buf, random_data.data, copy_len);
		buf += copy_len;
		curr_len += copy_len;
	}
	return len;
}

void
rnd_send_buffer(unsigned remoteClus, mppa_rpc_odp_t * msg) {
	unsigned interface = 0;
	mppa_rpc_odp_cmd_rnd_t rnd = {.inl_data = msg->inl_data};

	(void)remoteClus;
	assert( rnd.rnd_len <= sizeof(rnd.rnd_data));

	rnd.rnd_len = odp_rnd_gen_get((char*)rnd.rnd_data, rnd.rnd_len);
	mppa_rpc_odp_send_msg(interface, msg->dma_id, msg->dnoc_tag, msg, NULL);
}

static int rnd_rpc_handler(unsigned remoteClus, mppa_rpc_odp_t *msg, uint8_t *payload)
{
	if (msg->pkt_class != MPPA_RPC_ODP_CLASS_RND)
		return -MPPA_RPC_ODP_ERR_INTERNAL_ERROR;
	if (msg->cos_version != MPPA_RPC_ODP_RND_VERSION)
		return -MPPA_RPC_ODP_ERR_VERSION_MISMATCH;

	(void)payload;
	switch (msg->pkt_subtype){
	case MPPA_RPC_ODP_CMD_RND_GET:
		rnd_send_buffer(remoteClus, msg);
		break;
	default:
		return -MPPA_RPC_ODP_ERR_BAD_SUBTYPE;
	}

	return -MPPA_RPC_ODP_ERR_NONE;
}

void  __attribute__ ((constructor)) __rnd_rpc_constructor()
{
#if defined (K1B_EXPLORER) || defined(MAGIC_SCALL)
	/* No RND on explorer */
	return;
#endif
	odp_rnd_gen_init();
	__rpc_handlers[MPPA_RPC_ODP_CLASS_RND] = rnd_rpc_handler;
}
