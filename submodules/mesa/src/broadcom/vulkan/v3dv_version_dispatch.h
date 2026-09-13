/*
 * Copyright © 2026 Raspberry Pi Ltd
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */
#ifndef V3DV_VERSION_DISPATCH_H
#define V3DV_VERSION_DISPATCH_H

#include "common/v3d_tiling.h"
#include "common/v3d_util.h"

#ifdef V3D_VERSION
#include "common/v3d_macros.h"
#endif

struct v3dv_bo;
struct v3dv_buffer;
struct v3dv_buffer_view;
struct v3dv_cmd_buffer;
struct v3dv_cmd_buffer_attachment_state;
struct v3dv_device;
struct v3dv_draw_info;
struct v3dv_format;
struct v3dv_format_plane;
struct v3dv_frame_tiling;
struct v3dv_framebuffer;
struct v3dv_image;
struct v3dv_image_view;
struct v3dv_job;
struct v3dv_meta_framebuffer;
struct v3dv_pipeline;
struct v3dv_sampler;
struct v3dv_subpass;
union v3dv_clear_value;

#ifdef v3dX
#  include "v3dvx_private.h"
#else
#  define v3dX(x) v3d42_##x
#  include "v3dvx_private.h"
#  undef v3dX

#  define v3dX(x) v3d71_##x
#  include "v3dvx_private.h"
#  undef v3dX
#endif

#endif /* V3DV_VERSION_DISPATCH_H */
