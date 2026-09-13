/*
 * Copyright © 2018 Intel Corporation
 * SPDX-License-Identifier: MIT
 */
#ifndef IRIS_DEFINES_H
#define IRIS_DEFINES_H

/**
 * @file iris_defines.h
 *
 * Random hardware #defines that we're not using GENXML for.
 */

#define MI_PREDICATE                         (0xC << 23)
# define MI_PREDICATE_LOADOP_KEEP            (0 << 6)
# define MI_PREDICATE_LOADOP_LOAD            (2 << 6)
# define MI_PREDICATE_LOADOP_LOADINV         (3 << 6)
# define MI_PREDICATE_COMBINEOP_SET          (0 << 3)
# define MI_PREDICATE_COMBINEOP_AND          (1 << 3)
# define MI_PREDICATE_COMBINEOP_OR           (2 << 3)
# define MI_PREDICATE_COMBINEOP_XOR          (3 << 3)
# define MI_PREDICATE_COMPAREOP_TRUE         (0 << 0)
# define MI_PREDICATE_COMPAREOP_FALSE        (1 << 0)
# define MI_PREDICATE_COMPAREOP_SRCS_EQUAL   (2 << 0)
# define MI_PREDICATE_COMPAREOP_DELTAS_EQUAL (3 << 0)

/* Predicate registers */
#define MI_PREDICATE_SRC0                    0x2400
#define MI_PREDICATE_SRC1                    0x2408
#define MI_PREDICATE_DATA                    0x2410
#define MI_PREDICATE_RESULT                  0x2418
#define MI_PREDICATE_RESULT_1                0x241C
#define MI_PREDICATE_RESULT_2                0x2214

#define CS_GPR(n) (0x2600 + (n) * 8)

/* For gfx12 we set the streamout buffers using 4 separate commands
 * (3DSTATE_SO_BUFFER_INDEX_*) instead of 3DSTATE_SO_BUFFER. However the layout
 * of the 3DSTATE_SO_BUFFER_INDEX_* commands is identical to that of
 * 3DSTATE_SO_BUFFER apart from the SOBufferIndex field, so for now we use the
 * 3DSTATE_SO_BUFFER command, but change the 3DCommandSubOpcode.
 * SO_BUFFER_INDEX_0_CMD is actually the 3DCommandSubOpcode for
 * 3DSTATE_SO_BUFFER_INDEX_0.
 */
#define SO_BUFFER_INDEX_0_CMD 0x60

#endif
