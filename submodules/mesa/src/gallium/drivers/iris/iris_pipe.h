/*
 * Copyright © 2017 Intel Corporation
 * SPDX-License-Identifier: MIT
 */
#ifndef IRIS_PIPE_H
#define IRIS_PIPE_H

#include "pipe/p_defines.h"

/**
 * Convert an swizzle enumeration (i.e. PIPE_SWIZZLE_X) to one of the HW's
 * "Shader Channel Select" enumerations (i.e. SCS_RED).  The mappings are
 *
 * SWIZZLE_X, SWIZZLE_Y, SWIZZLE_Z, SWIZZLE_W, SWIZZLE_ZERO, SWIZZLE_ONE
 *         0          1          2          3             4            5
 *         4          5          6          7             0            1
 *   SCS_RED, SCS_GREEN,  SCS_BLUE, SCS_ALPHA,     SCS_ZERO,     SCS_ONE
 *
 * which is simply adding 4 then modding by 8 (or anding with 7).
 */
static inline enum isl_channel_select
pipe_swizzle_to_isl_channel(enum pipe_swizzle swizzle)
{
   return (swizzle + 4) & 7;
}

#endif
