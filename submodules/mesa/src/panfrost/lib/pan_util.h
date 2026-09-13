/*
 * Copyright 2019 Collabora, Ltd.
 * SPDX-License-Identifier: MIT
 */

#ifndef PAN_UTIL_H
#define PAN_UTIL_H

#include <stdbool.h>
#include <stdint.h>
#include "util/format/u_format.h"

#define PAN_DBG_PERF  0x0001
#define PAN_DBG_TRACE 0x0002
/* 0x4 unused */
#define PAN_DBG_DIRTY 0x0008
#define PAN_DBG_SYNC  0x0010
/* 0x20 unused */
#define PAN_DBG_NOFP16  0x0040
#define PAN_DBG_NO_CRC  0x0080
#define PAN_DBG_GL3     0x0100
#define PAN_DBG_NO_AFBC 0x0200
/* 0x400 unused */
#define PAN_DBG_STRICT_IMPORT 0x0800
#define PAN_DBG_LINEAR   0x1000
#define PAN_DBG_NO_CACHE 0x2000
#define PAN_DBG_DUMP     0x4000

#ifndef NDEBUG
#define PAN_DBG_OVERFLOW 0x8000
#endif

#define PAN_DBG_YUV        0x20000
#define PAN_DBG_FORCE_PACK 0x40000
#define PAN_DBG_CS         0x80000

struct pan_blendable_format;

unsigned pan_translate_swizzle_4(const unsigned char swizzle[4]);

void pan_invert_swizzle(const unsigned char *in, unsigned char *out);

void pan_pack_color(const struct pan_blendable_format *blendable_formats,
                    uint32_t *packed, const union pipe_color_union *color,
                    enum pipe_format format, bool dithered);

/* Get the last blend shader, for an erratum workaround on v5 */

static inline uint64_t
pan_last_nonnull(uint64_t *ptrs, unsigned count)
{
   for (signed i = ((signed)count - 1); i >= 0; --i) {
      if (ptrs[i])
         return ptrs[i];
   }

   return 0;
}

#endif /* PAN_UTIL_H */
