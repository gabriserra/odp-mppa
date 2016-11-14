#ifndef ODP_MACROS_INTERNAL_H__
#define ODP_MACROS_INTERNAL_H__

/**
 * Concat the name of the two parameters
 * @param[in] x x
 * @param[in] y y
 * @return x ## y
 */
#define CONCAT(x,y)x ## y

/**
 * Internal macro to compute min of 2 values. Use #MIN
 * @param[in] x x
 * @param[in] y y
 * @param[in] c Magic CPP auto numbering value to avoid variable shadowing
 * @return MIN(x,y)
 */
#define _MIN(x, y, c) ({ typeof(x)CONCAT(_x, c) = (x); typeof(y)CONCAT(_y, c) = (y); CONCAT(_x, c) < CONCAT(_y, c) ? CONCAT(_x, c) : CONCAT(_y, c); })  /**< Compute min of 2 elements */

/**
 * Internal macro to compute max of 2 values. Use #MAX
 * @param[in] x x
 * @param[in] y y
 * @param[in] c Magic CPP auto numbering value to avoid variable shadowing
 * @return MAX(x,y)
 */
#define _MAX(x, y, c) ({ typeof(x)CONCAT(_x, c) = (x); typeof(y)CONCAT(_y, c) = (y); CONCAT(_x, c) > CONCAT(_y, c) ? CONCAT(_x, c) : CONCAT(_y, c); })  /**< Compute max of 2 elements */

/**
 * Safe macro to compute min of 2 values
 * @param[in] x x
 * @param[in] y y
 * @return MIN(x,y)
 */
#define MIN(x,y) _MIN(x,y, __COUNTER__)
/**
 * Safe macro to compute max of 2 values
 * @param[in] x x
 * @param[in] y y
 * @return MAX(x,y)
 */
#define MAX(x,y) _MAX(x,y, __COUNTER__)

/**
 * Macro to convert a DEFINE label to a boolean.
 * @return Macro value.
 * @retval 1 The macro is set to 1.
 * @retval 0 The macro is not set to 1 or undefined.
 */
#define IS_SET(macro) is_set_(macro)

#define EXPAND(x) #x
#define macrotest_1 ,
#define is_set_(value) is_set__(macrotest_ ## value)
#define is_set__(comma) is_set___(comma 1, 0)
#define is_set___(_, v, ...) v

#endif /* ODP_MACROS_INTERNAL_H__ */
