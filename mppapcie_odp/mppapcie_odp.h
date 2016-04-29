#ifndef MPPAPCIE_ODP_H
#define MPPAPCIE_ODP_H

/**
 * Definition of communication structures between MPPA and HOST
 * Since the MPPA is seen as a network card, TX means Host to MPPA and RX means MPPA To Host
 *
 * Basically, the MPPA prepares the mppa_pcie_eth_control struct.
 * Once it is ready, it write the magic and the host knows the device is "available"
 *
 * The host driver then polls the ring buffers descriptors
 */

/**
 * Count of interfaces for one PCIe device
 */
#define MPPA_PCIE_ETH_MAX_INTERFACE_COUNT	16

/**
 * Mac address length
 */
#define MAC_ADDR_LEN				6

/**
 * Default MTU
 */
#define MPPA_PCIE_ETH_DEFAULT_MTU		1500


#define MPPA_PCIE_ETH_CONTROL_STRUCT_MAGIC	0xCAFEBABE

/**
 * Flags for config flags
 */
#define MPPA_PCIE_ETH_CONFIG_RING_AUTOLOOP	(1 << 0)
#define MPPA_PCIE_ETH_CONFIG_SEND_IT_TO_RM	(1 << 1)
#define MPPA_PCIE_ETH_CONFIG_DISABLED		(1 << 2)


/**
 * Flags for tx flags
 */
#define MPPA_PCIE_ETH_NEED_PKT_HDR	(1 << 0)

/**
 * Per interface configuration (Read from host)
 */
struct mppa_pcie_eth_if_config {
	uint64_t c2h_ring_buf_desc_addr; /*< MPPA2Host ring buffer address (`mppa_pcie_eth_ring_buff_desc`) */
	uint64_t h2c_ring_buf_desc_addr; /*< Host2MPPA ring buffer address (`mppa_pcie_eth_ring_buff_desc`) */
	uint16_t mtu;			 /*< MTU */
	uint8_t  mac_addr[MAC_ADDR_LEN]; /*< Mac address */
	uint32_t interrupt_status;       /*< interrupt status (set by host) */
	uint32_t flags;			 /*< Flags for config (checksum offload, etc) */
	uint32_t link_status;		 /*< Link status (activity, speed, duplex, etc) */
} __attribute__ ((packed));

/**
 * Control structure to exchange control data between host and MPPA
 * This structure is placed at `MPPA_PCIE_ETH_CONTROL_STRUCT_ADDR`
 */
struct mppa_pcie_eth_control {
	uint32_t magic;			/*< Magic to test presence of control structure */
	uint32_t if_count;		/*< Count of interfaces for this PCIe device */
	struct mppa_pcie_eth_if_config configs[MPPA_PCIE_ETH_MAX_INTERFACE_COUNT];
} __attribute__ ((packed));

/**
 * TX (Host2MPPA) single entry descriptor (Updated by Host)
 */
struct mppa_pcie_eth_h2c_ring_buff_entry {
	uint32_t len;		/*< Packet length */
	uint32_t flags;		/*< Flags to control offloading features */
	uint64_t pkt_addr;	/*< Packet Address */
} __attribute__ ((packed));

/**
 * RX (MPPA2Host) single entry descriptor (Updated by MPPA)
 */
struct mppa_pcie_eth_c2h_ring_buff_entry {
	uint16_t len;		/*< Packet length */
	uint16_t status;	/*< Packet status (errors, etc) */
	uint32_t checksum;	/*< Packet checksum (computed by MPPA) */
	uint64_t pkt_addr;	/*< Packet Address */
	uint64_t data;	        /*< Data for MPPA use */
} __attribute__ ((packed));

/**
 * Ring buffer descriptors
 * `ring_buffer_addr` point either to RX ring entries (`mppa_pcie_eth_rx_ring_buff_entry`)
 * or TX ring entries (`mppa_pcie_eth_tx_ring_buff_entry`) depending on ring buffertype
 *
 * For a TX ring buffer, the MPPA writes the head pointer to signal that previous
 * packet has been sent and host write the tail pointer to indicate there is new
 * packets to send. Host must be careful to always let at least one free ring buffer entry
 * in order to avoid stalling the ring buffer. For that, it must read the head pointer
 * before writing the tail one. Every descriptor located between head and tail belongs to the
 * MPPA in order to send them.
 *
 * When used as RX ring buffer, the reverse process is done.
 * The host write the head pointer to indicates it read the packet and the MPPA
 * writes the tail to indicates there is incoming packets. Every descriptor between
 * the head and tail belongs to the Host in order to receive them.
 */
struct mppa_pcie_eth_ring_buff_desc {
	uint32_t head;				/*< Index of head */
	uint32_t tail;				/*< Index of tail */
	uint32_t ring_buffer_entries_count;	/*< Count of ring buffer entries */
	uint64_t ring_buffer_entries_addr;	/*< Pointer to ring buffer entries depending on RX or TX*/
} __attribute__ ((packed));

/**
 * Header added to packet when needed (fifo mode for instance)
 */
union mppa_pcie_eth_pkt_hdr_info {
		uint64_t dword;
		uint32_t word[2];
		uint16_t hword[4];
		uint8_t bword[8];
		struct {
			uint32_t pkt_size : 16;
			uint32_t hash_key : 16;
			uint32_t lane_id  : 2;
			uint32_t io_id    : 1;
			uint32_t rule_id  : 4;
			uint32_t pkt_id   : 25;
		} _;
};

struct mppa_pcie_eth_pkt_hdr {
	uint64_t timestamp;
	union mppa_pcie_eth_pkt_hdr_info info;
} __attribute__ ((packed));;

#endif
