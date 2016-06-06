#ifndef MPPAPCIE_ODP_H
#define MPPAPCIE_ODP_H

/**
 * Definition of communication structures between MPPA and HOST
 * Since the MPPA is seen as a network card, TX means Host to MPPA and RX means MPPA To Host
 *
 * Basically, the MPPA prepares the odp_control struct.
 * Once it is ready, it write the magic and the host knows the device is "available"
 *
 * The host driver then polls the ring buffers descriptors
 */

/**
 * Count of interfaces for one PCIe device
 */
#define MPODP_MAX_IF_COUNT	                16

/**
 * Mac address length
 */
#define MAC_ADDR_LEN				6

/**
 * Default MTU
 */
#define MPODP_DEFAULT_MTU		1500


#define MPODP_CONTROL_STRUCT_MAGIC	0xCAFEBABE

/**
 * Flags for config flags
 */
#define MPODP_CONFIG_DISABLED		(1 << 0)

/**
 * Maximum number of Tx queues
 */
#define MPODP_MAX_TX_QUEUES           2

/**
 * Maximum number of Rx queues
 */
#define MPODP_MAX_RX_QUEUES           1

/**
 * Flags for tx flags
 */

/**
 * Per interface configuration (Read from host)
 */
struct mpodp_if_config {
	/** MPPA2Host ring buffer address (`mpodp_ring_buff_desc`) */
	uint64_t c2h_addr[MPODP_MAX_RX_QUEUES];
	/** Host2MPPA ring buffer address (`mpodp_ring_buff_desc`) */
	uint64_t h2c_addr[MPODP_MAX_TX_QUEUES];

	uint32_t interrupt_status;	/*< interrupt status (set by host) */
	uint32_t flags;		/*< Flags for config (checksum offload, etc) */
	uint32_t link_status;	/*< Link status (activity, speed, duplex, etc) */
	uint16_t mtu;		/*< MTU */
	uint16_t n_txqs;      /*< Number of TX queues */
	uint16_t n_rxqs;      /*< Number of RX queues */
	uint8_t mac_addr[MAC_ADDR_LEN];	/*< Mac address */
} __attribute__ ((packed, aligned(8)));

/**
 * Control structure to exchange control data between host and MPPA
 * This structure is placed at `MPODP_CONTROL_STRUCT_ADDR`
 */
struct mpodp_control {
	uint32_t magic;		/*< Magic to test presence of control structure */
	uint32_t if_count;	/*< Count of interfaces for this PCIe device */
	struct mpodp_if_config configs[MPODP_MAX_IF_COUNT];
} __attribute__ ((packed, aligned(8)));

/**
 * TX (Host2MPPA) single entry descriptor (Updated by Host)
 */
struct mpodp_h2c_entry {
	uint64_t pkt_addr;	/*< Packet Address */
} __attribute__ ((packed, aligned(8)));

/**
 * RX (MPPA2Host) single entry descriptor (Updated by MPPA)
 */
struct mpodp_c2h_entry {
	uint64_t pkt_addr;	/*< Packet Address */
	uint64_t data;		/*< Data for MPPA use */
	uint16_t len;		/*< Packet length */
	uint16_t status;	/*< Packet status (errors, etc) */
} __attribute__ ((packed, aligned(8)));

/**
 * Ring buffer descriptors
 * `ring_buffer_addr` point either to RX ring entries (`mpodp_rx_ring_buff_entry`)
 * or TX ring entries (`mpodp_tx_ring_buff_entry`) depending on ring buffertype
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
struct mpodp_ring_buff_desc {
	uint64_t addr;     	/*< Pointer to ring buffer entries depending on RX or TX */
	uint32_t head;		/*< Index of head */
	uint32_t tail;		/*< Index of tail */
	uint32_t count; 	/*< Count of ring buffer entries */
} __attribute__ ((packed, aligned(8)));

/**
 * Header added to packet when needed (fifo mode for instance)
 */
union mpodp_pkt_hdr_info {
	uint64_t dword;
	uint32_t word[2];
	uint16_t hword[4];
	uint8_t bword[8];
	struct {
		uint32_t pkt_size:16;
		uint32_t hash_key:16;
		uint32_t lane_id:2;
		uint32_t io_id:1;
		uint32_t rule_id:4;
		uint32_t pkt_id:25;
	} _;
};

struct mpodp_pkt_hdr {
	uint64_t timestamp;
	union mpodp_pkt_hdr_info info;
} __attribute__ ((packed));;

#endif
