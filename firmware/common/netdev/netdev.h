#ifndef NETDEV__H
#define NETDEV__H

#include <mppa_pcie_netdev.h>

typedef struct {
	uint8_t if_id;
	uint8_t mac_addr[MAC_ADDR_LEN];	/*< Mac address */
	uint16_t mtu;
	uint32_t flags;

	struct {
		uint8_t noalloc :1;
	};

	uint32_t n_c2h_entries;
	uint32_t c2h_flags;

	uint32_t n_h2c_entries;
	uint32_t h2c_flags;
} eth_if_cfg_t;

int netdev_init(uint8_t n_if, const eth_if_cfg_t cfg[n_if]);
int netdev_init_interface(const eth_if_cfg_t *cfg);
int netdev_start();

#endif /* NETDEV__H */
