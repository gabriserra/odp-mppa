#ifndef __FIRMWARE__INTERNAL__DEBUG_H__
#define __FIRMWARE__INTERNAL__DEBUG_H__

#ifdef __cplusplus
extern "C" {
#endif

#if defined(__GNUC__) && !defined(__clang__)


#if __GNUC__ < 4 || (__GNUC__ == 4 && (__GNUC_MINOR__ < 6))

/**
 * @internal _Static_assert was only added in GCC 4.6. Provide a weak replacement
 * for previous versions.
 */
#define _Static_assert(e, s) (extern int (*static_assert_checker(void)) \
	[sizeof(struct { unsigned int error_if_negative:(e) ? 1 : -1; })])

#endif



#endif


/**
 * @internal Compile time assertion-macro - fail compilation if cond is false.
 * This macro has zero runtime overhead
 */
#define _ODP_STATIC_ASSERT(cond, msg)  _Static_assert(cond, msg)


#ifdef __cplusplus
}
#endif

#endif /* __FIRMWARE__INTERNAL__DEBUG_H__ */
