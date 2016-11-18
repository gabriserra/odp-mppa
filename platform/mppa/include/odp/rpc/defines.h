#ifndef __MPPA_RPC_ODP_DEFINES_H__
#define __MPPA_RPC_ODP_DEFINES_H__

#define _MPPA_RPC_ODP_STRINGIFY(x) #x
#define _MPPA_RPC_ODP_TOSTRING(x) _MPPA_RPC_ODP_STRINGIFY(x)

#define MPPA_RPC_ODP_CHECK_STRUCT_SIZE(sname) _Static_assert(sizeof(sname) == sizeof(mppa_rpc_odp_inl_data_t),		\
							"MPPA_RPC_ODP_CMD_" _MPPA_RPC_ODP_TOSTRING(sname) "__SIZE_ERROR")
#define MPPA_RPC_ODP_TIMEOUT_1S ((uint64_t)__bsp_frequency)


#define RPC_BASE_RX 192
#define RPC_MAX_PAYLOAD 168 * 8 /* max payload in bytes */

#ifndef MPPA_RPC_ODP_PRINT
#define MPPA_RPC_ODP_PRINT(x...) printf(##x)
#endif

#ifndef MPPA_RPC_ODP_PREFIX
#define MPPA_RPC_ODP_PREFIX mppa_rpc_odp
#endif

#define _MPPA_RPC_ODP_CONCAT(x, y) x ## _ ## y
#define _MPPA_RPC_ODP_FUNCTION(x, y) _MPPA_RPC_ODP_CONCAT(x, y)
#define MPPA_RPC_ODP_FUNCTION(x) _MPPA_RPC_ODP_FUNCTION(MPPA_RPC_ODP_PREFIX, x)
#endif /* __MPPA_RPC_ODP_DEFINES_H__ */
