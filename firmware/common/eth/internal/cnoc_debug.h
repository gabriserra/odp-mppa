#ifndef ETH__INTERNAL__CNOC_DEBUG_H
#define ETH__INTERNAL__CNOC_DEBUG_H

/* DSU registers */
static const uint8_t __K1_DSU_REG_INVALIDATE = 0 << 4;
static const uint8_t __K1_DSU_REG_DEBUG_ENABLE = 1 << 4;
static const uint8_t __K1_DSU_REG_BREAK = 2 << 4;
static const uint8_t __K1_DSU_REG_FETCH_DISABLE = 3 << 4;
static const uint8_t __K1_DSU_REG_RESET = 4 << 4;
static const uint8_t __K1_DSU_REG_EVENT = 5 << 4;
static const uint8_t __K1_DSU_REG_STEP = 6 << 4;
static const uint8_t __K1_DSU_REG_BREAK_MASK = 7 << 4;
static const uint8_t __K1_DSU_REG_BYTE_DISABLE = 8 << 4;
static const uint8_t __K1_DSU_REG_DATA0 = 12 << 4; /* LSB */
static const uint8_t __K1_DSU_REG_DATA1 = 13 << 4; /* MSB */
static const uint8_t __K1_DSU_REG_ADDR = 14 << 4;
static const uint8_t __K1_DSU_REG_ADDR_EXT = 8 << 4;
static const uint8_t __K1_DSU_REG_EXCEPT_VB = 15 << 4;

/* DSU diagnostic mode commands */
static const uint8_t __K1_RESET_ACK_CMD = 0xff;
static const uint8_t __K1_REG_PULSE_CMD_BASE = 0x0e;
static const uint8_t __K1_REG_READ_CMD_BASE = 0x04;
static const uint8_t __K1_REG_WRITE_CMD_BASE = 0x02;
static const uint8_t __K1_REG_SET_CMD_BASE = 0x06;
static const uint8_t __K1_REG_CLEAR_CMD_BASE = 0x0a;
static const uint8_t __K1_PEEK = 0x01;
static const uint8_t __K1_PEEK_AND_ADDR = 0x21;
static const uint8_t __K1_PEEK_AND_DATA0 = 0x41;
static const uint8_t __K1_PEEK_AND_DATA1 = 0x61;
static const uint8_t __K1_POKE = 0x11;
static const uint8_t __K1_POKE_AND_ADDR = 0x31;
static const uint8_t __K1_POKE_AND_DATA0 = 0x51;
static const uint8_t __K1_POKE_AND_DATA1 = 0x71;
static const uint8_t __K1_PEEK_POKE_INCR_BIT = 0x08;
static const uint8_t __K1_NOP = 0;

/* DSU output messages */
static const uint8_t __K1_RESET_MESS = 0xff;
static const uint8_t __K1_READ = 0x01;
static const uint8_t __K1_POKED = 0x04;
static const uint8_t __K1_PEEKED_LOWER = 0x02;
static const uint8_t __K1_PEEKED_UPPER = 0x03;
static const uint8_t __K1_MESSAGE_LOWER = 0x10;
static const uint8_t __K1_MESSAGE_UPPER = 0x11;
static const uint8_t __K1_ERROR = 0x08;
static const uint8_t __K1_EVENT = 0x20;
static const uint8_t __K1_STATUS = 0x00;


static inline void __k1_cnoc_debug_send_set_return_route_first_dir (int return_first_dir)
{
	mppa_syscnoc[0]->control.address.dword = (unsigned long long)0xC00FF001;
	mppa_syscnoc[0]->control.data.reg    = (unsigned long long)return_first_dir;
	mppa_syscnoc[0]->control.push.reg    = 0;
}

static inline void __k1_cnoc_debug_send_set_return_route (int return_route)
{
	mppa_syscnoc[0]->control.address.dword = 0xC00FF000;
	mppa_syscnoc[0]->control.data.reg    = 0xC0000000 | return_route;
	mppa_syscnoc[0]->control.push.reg    = 0;
}


static inline void __k1_cnoc_debug_send_set_return_route64 (unsigned long long return_route, unsigned long long return_route2)
{
	mppa_syscnoc[0]->control.address.dword = 0xC00FF020;
	mppa_syscnoc[0]->control.data.reg    = ((unsigned long long)3 << 62ull)  | (return_route & (((unsigned long long)1<<44ull)-1));
	mppa_syscnoc[0]->control.push.reg    = 0;

	if (return_route2!=0 || (return_route>>44ull) != 0) {
		mppa_syscnoc[0]->control.address.dword = 0xC00FF021;
		mppa_syscnoc[0]->control.data.reg    = return_route>> 44ull;
		mppa_syscnoc[0]->control.data.reg    |= return_route2 << (64ull-44ull);
		mppa_syscnoc[0]->control.push.reg    = 0;
	}
}

static inline void __k1_cnoc_debug_send_enable_async_messages ()
{
	mppa_syscnoc[0]->control.address.dword = 0xC00FF002;
	mppa_syscnoc[0]->control.data.reg    = 1;
	mppa_syscnoc[0]->control.push.reg    = 0;
}

static inline void __k1_cnoc_debug_send_diag_command (uint8_t cmd, uint32_t data)
{
	mppa_syscnoc[0]->control.address.dword = 0xC00FF100 | cmd;
	mppa_syscnoc[0]->control.data.reg    = data; //data
	mppa_syscnoc[0]->control.push.reg    = 0;
}

static inline uint64_t __k1_cnoc_debug_read_status ()
{
	return mppa_syscnoc[0]->debug.status.rw.reg;
}

static inline uint64_t __k1_cnoc_debug_read_error ()
{
	return mppa_syscnoc[0]->debug.error.rw.reg;
}

static inline uint64_t __k1_cnoc_debug_read_mailbox (unsigned int mailbox_idx)
{
	return mppa_syscnoc[0]->debug.mailbox[mailbox_idx].reg;
}

static inline void __k1_cnoc_debug_write_rx_status (uint64_t val)
{
	mppa_syscnoc[0]->debug.status.rw.reg = val;
}

static inline void __k1_cnoc_debug_clear_rx_status (uint64_t val)
{
	mppa_syscnoc[0]->debug.status.clear_mask.reg = val;
}

static inline uint64_t __k1_cnoc_debug_auto_read_status ()
{
	return mppa_syscnoc[0]->debug_auto.status.rw.reg;
}

static inline uint64_t __k1_cnoc_debug_auto_read_error ()
{
	return mppa_syscnoc[0]->debug_auto.error.rw.reg;
}

static inline uint64_t __k1_cnoc_debug_auto_read_mailbox (unsigned int mailbox_idx)
{
	return mppa_syscnoc[0]->debug_auto.mailbox[mailbox_idx].reg;
}

static inline void __k1_cnoc_debug_auto_write_rx_status (uint64_t val)
{
	mppa_syscnoc[0]->debug_auto.status.rw.reg = val;
}

static inline
void __k1_cnoc_debug_auto_clear_rx_status (uint64_t val)
{
	mppa_syscnoc[0]->debug_auto.status.clear_mask.reg = val;
}

static inline
void __k1_cnoc_debug_send_set_ctrl_reg (int mask)
{
	int mailbox_id;
	mppa_syscnoc[0]->control.address.dword = 0xC00FF005;
	mppa_syscnoc[0]->control.data.reg    = (unsigned long long)mask << 32;
	mppa_syscnoc[0]->control.push.reg    = 0;

	while (__k1_cnoc_debug_read_status() ==0);
	mailbox_id = __builtin_ctz((unsigned int)__k1_cnoc_debug_read_status());
	__k1_cnoc_debug_clear_rx_status(1<<mailbox_id);
}

static inline
int __k1_cnoc_debug_poke(unsigned int address,unsigned long long data,  unsigned int size)
{
	unsigned int byte_mask = ((1<<size)-1) << (address & 0x7);
	data = data << ((unsigned long long)((address & 0x7)*8));
	int mailbox_id;

	/* Send poke */
	__k1_cnoc_debug_send_diag_command (__K1_REG_WRITE_CMD_BASE|__K1_DSU_REG_BYTE_DISABLE, ((unsigned int)(~(byte_mask))<<24));
	__k1_cnoc_debug_send_diag_command (__K1_REG_WRITE_CMD_BASE|__K1_DSU_REG_ADDR, address);
	__k1_cnoc_debug_send_diag_command (__K1_REG_WRITE_CMD_BASE|__K1_DSU_REG_DATA0, (unsigned int)((data)& 0xFFFFFFFF));
	__k1_cnoc_debug_send_diag_command (__K1_POKE_AND_DATA1, (unsigned int)((data)>> 32ull));

	/* Waiting for response */
	while (__k1_cnoc_debug_read_status() ==0);
	mailbox_id = __builtin_ctz((unsigned int)__k1_cnoc_debug_read_status());
	__k1_cnoc_debug_clear_rx_status(1<<mailbox_id);
	if (__k1_cnoc_debug_read_mailbox (mailbox_id) ==  __K1_POKED)
		return 0;
	else
		return 1;
}

static inline
unsigned long long __k1_cnoc_debug_peek(unsigned int address, unsigned int size)
{

	unsigned int byte_mask = ((1<<size)-1) << (address & 0x7);
	int mailbox_id;
	__k1_cnoc_debug_clear_rx_status(0xFFFFFFFFFFFFFFFF);

	/* Send peek */
	__k1_cnoc_debug_send_diag_command (__K1_REG_WRITE_CMD_BASE|__K1_DSU_REG_BYTE_DISABLE, ((unsigned int)(~(byte_mask))<<24));
	__k1_cnoc_debug_send_diag_command (__K1_REG_WRITE_CMD_BASE|__K1_DSU_REG_ADDR, address);
	__k1_cnoc_debug_send_diag_command (__K1_PEEK, 0);

	/* Waiting for response */
	while (__k1_cnoc_debug_read_status() ==0);
	mailbox_id = __builtin_ctz((unsigned int)__k1_cnoc_debug_read_status());
	__k1_cnoc_debug_clear_rx_status(1<<mailbox_id);
	return ((__k1_cnoc_debug_read_mailbox (mailbox_id)) >> ((unsigned long long)((address & 0x7)*8)));
}

#endif /* ETH__INTERNAL__CNOC_DEBUG_H */
