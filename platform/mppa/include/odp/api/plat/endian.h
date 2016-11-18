/* Copyright (c) 2014, Linaro Limited
 * All rights reserved.
 *
 * SPDX-License-Identifier:     BSD-3-Clause
 */

/**
 * @file
 *
 * ODP byteorder
 */

#ifndef ODP_PLAT_ENDIAN_H_
#define ODP_PLAT_ENDIAN_H_

#include <machine/endian.h>

#ifndef __LITTLE_ENDIAN
#define __LITTLE_ENDIAN LITTLE_ENDIAN
#endif
#ifndef __LITTLE_ENDIAN_BITFIELD
#define __LITTLE_ENDIAN_BITFIELD
#endif
#ifndef __BIG_ENDIAN
#define __BIG_ENDIAN BIG_ENDIAN
#endif
#ifndef __BIG_ENDIAN_BITFIELD
#define __BIG_ENDIAN_BITFIELD
#endif
#ifndef __BYTE_ORDER
#define __BYTE_ORDER __BYTE_ORDER__
#endif

#endif
