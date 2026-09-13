/*
 * Copyright © 2025 Raspberry Pi Ltd
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

/* This file generates the per-v3d-version function prototypes.  It must only
 * be included from v3dv_format_table.h.
 */

#include "broadcom/common/v3d_macros.h"
#include "broadcom/cle/v3dx_pack.h"

bool v3dX(tfu_supports_tex_format)(enum V3DX(Texture_Data_Formats) tex_format);

void v3dX(get_internal_type_bpp_for_output_format)(enum V3DX(Output_Image_Format) format,
                                                    uint32_t *type, uint32_t *bpp);

void
v3dX(meta_emit_copy_buffer)(struct v3dv_job *job,
                            struct v3dv_bo *dst,
                            struct v3dv_bo *src,
                            uint32_t dst_offset,
                            uint32_t src_offset,
                            struct v3dv_meta_framebuffer *framebuffer,
                            enum V3DX(Output_Image_Format)  format,
                            uint32_t item_size);

void
v3dX(meta_emit_copy_buffer_rcl)(struct v3dv_job *job,
                                struct v3dv_bo *dst,
                                struct v3dv_bo *src,
                                uint32_t dst_offset,
                                uint32_t src_offset,
                                struct v3dv_meta_framebuffer *framebuffer,
                                enum V3DX(Output_Image_Format)  format,
                                uint32_t item_size);

enum V3DX(Internal_Depth_Type)
v3dX(get_internal_depth_type)(VkFormat format);

#if V3D_VERSION == 42
enum V3DX(Render_Target_Clamp)
v3dX(clamp_for_format_and_type)(enum V3DX(Internal_Type) rt_type,
                                VkFormat vk_format);
#endif
#if V3D_VERSION >= 71
enum V3DX(Render_Target_Type_Clamp)
v3dX(clamp_for_format_and_type)(enum V3DX(Internal_Type) rt_type,
                                VkFormat vk_format);
#endif

enum V3DX(Stencil_Op)
v3dX(translate_stencil_op)(VkStencilOp op);
