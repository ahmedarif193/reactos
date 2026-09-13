/*
 * Copyright 2012 Advanced Micro Devices, Inc.
 * Copyright 2026 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef AC_GUARDBAND_H
#define AC_GUARDBAND_H

/*
 * The discard X/Y fields determine clip-space X and Y distance from (0, 0)
 * that defines the rectangle boundary of the visible viewport range area in
 * clip space for the purpose of culling primitives outside the viewport.
 * Normally, triangles would set this to 1, which means exactly the edge of
 * the viewport, while points and lines would set it to 1 + the half point
 * size or half line width because point and line culling is done against the
 * point or line center, respectively, which can be slightly outside the
 * visible range of the viewport while the edge of the point or line can be
 * visible. That prevents points and lines from popping suddenly into view
 * when their center enters the visible part of the viewport range. It's
 * possible to set the discard X/Y fields to values very far into the
 * non-visible range of the viewport (> 1) to essentially disable culling of
 * primitives outside the visible range, but that's never useful. The discard
 * X/Y fields only cause primitives completely outside the rectangle boundary
 * to be culled, but those primitives that are only partially outside that
 * area are kept, i.e. it only determines culling, not clipping.
 *
 * The clip X/Y fields determine clip-space X and Y distance from (0, 0) that
 * defines the rectangle boundary of the area in clip space where clipping
 * must absolutely occur. This should be set to the maximum area of the total
 * viewport range including all invisible space. The purpose of this boundary
 * is to prevent primitives that are partially outside the viewport range
 * (e.g.  [-32K, 32K]) from being forwarded to the rasterizer because the
 * rasterizer can't represent positions outside the viewport range since it
 * can (typically) only accept 16-bit integer positions in screen space, which
 * is what really determines the viewport range limits.
 *
 * Here is an example of how both rectangles should be set for an 8K
 * (8192x8192) viewport:
 *
 * -32K       clip X/Y area       +32K (ideally the same as the viewport range)
 *  --------------------------------
 *  |     G U A R D   B A N D      |
 *  |       discard X/Y area       |
 *  |         ------------         |
 *  |         | visible  |         |
 *  |         | viewport |         |
 *  |         |          |         |
 *  |         ------------         |
 *  |        -4K       +4K         |
 *  |                              |
 *  --------------------------------
 *
 *
 * Since clipping is slow because it uses floating-point math to shift vertices
 * and potentially generate extra primitives, the clipping optimization works
 * as follows:

 * If a primitive is fully outside the discard rectangle, it's culled.
 * ("discard" means cull everything outside)
 * If a primitive is partially inside and partially outside the discard X/Y
 * rectangle, but fully inside the clip X/Y rectangle, it's kept. This is
 * beneficial because the rasterizer can trivially skip pixels outside the
 * visible viewport, but it can only accept primitives inside the viewport range
 * (typically [-32K, 32K]).  If a primitive is partially inside the discard X/Y
 * rectangle (i.e. partially visible) and also partially outside the clip X/Y
 * rectangle, it must be clipped because the rasterizer can't accept it (it
 * overflows the 16-bit integer space). This is the only time when clipping must
 * occur (potentially generating new primitives). The goal of the driver is to
 * program the discard X/Y area as small as possible and the clip X/Y area as
 * large as possible to make sure that this is very unlikely to happen.

 * In this example, the discard X/Y fields are set to (1, 1), and the clip X/Y
 * fields are set to (8, 8).  The band outside the discard X/Y rectangle
 * boundary and inside the clip X/Y rectangle boundary is called the guard band,
 * and is used as a clipping optimization described above. In the example, the
 * 8K viewport is centered in the viewport range by setting
 * PA_SU_HARDWARE_SCREEN_OFFSET=(4K, 4K), which makes the size of the guard band
 * on all sides equal. Centering the viewport is part of the clipping
 * optimization because the discard X/Y and clip X/Y fields apply to both sides
 * (left and right, top and bottom) and we want to maximize the clip X/Y values.
 * If the viewport wasn't centered, we would have to program the fields to the
 * minimum values of both sides.
 */
struct radeon_info;

enum ac_quant_mode
{
   /* The small prim precision computation depends on the enum values to be like this. */
   AC_QUANT_MODE_16_8_FIXED_POINT_1_256TH,
   AC_QUANT_MODE_14_10_FIXED_POINT_1_1024TH,
   AC_QUANT_MODE_12_12_FIXED_POINT_1_4096TH,
};

struct ac_guardband {
   float clip_x;
   float clip_y;
   float discard_x;
   float discard_y;
   int hw_screen_offset_x;
   int hw_screen_offset_y;
};

void
ac_compute_guardband(const struct radeon_info *info, int minx, int miny,
                     int maxx, int maxy, enum ac_quant_mode quant_mode,
                     float clip_discard_distance, struct ac_guardband *guardband);

#endif /* AC_GUARDBAND_H */
