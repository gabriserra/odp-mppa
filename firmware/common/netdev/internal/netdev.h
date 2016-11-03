
#ifndef NETDEV_INTERNAL__H
#define NETDEV_INTERNAL__H

extern struct mpodp_control *eth_ctrl;

static inline struct mpodp_if_config *
netdev_get_eth_if_config(uint8_t if_id){
	return &eth_ctrl->configs[if_id];
}

static inline struct mpodp_ring_buff_desc *
netdev_get_c2h_ring_buffer(uint8_t if_id, uint32_t c2h_q){
	return (struct mpodp_ring_buff_desc *)(unsigned long)
		(eth_ctrl->configs[if_id].c2h_addr[c2h_q]);
}

static inline struct mpodp_ring_buff_desc *
netdev_get_h2c_ring_buffer(uint8_t if_id, uint32_t h2c_q){
	return (struct mpodp_ring_buff_desc *)(unsigned long)
		(eth_ctrl->configs[if_id].h2c_addr[h2c_q]);
}

/* C2H */
int netdev_c2h_is_full(struct mpodp_if_config *cfg, uint32_t c2h_q);

/* old_entry pkt_addr and data are filled from the RBE just replaced */
int netdev_c2h_enqueue_data(struct mpodp_if_config *cfg,
			    uint32_t c2h_q,
			    struct mpodp_c2h_entry *data,
			    struct mpodp_c2h_entry *old_entry, int it);

/* H2C */
int netdev_h2c_enqueue_buffer(struct mpodp_if_config *cfg,
			      uint32_t h2c_q,
			      struct mpodp_h2c_entry *buffer);
struct mpodp_h2c_entry *
netdev_h2c_peek_data(const struct mpodp_if_config *cfg,
		     uint32_t h2c_q);
int netdev_start();

#endif /* NETDEV_INTERNAL__H */
