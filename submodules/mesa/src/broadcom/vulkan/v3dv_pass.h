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
#ifndef V3DV_PASS_H
#define V3DV_PASS_H

#include "v3dv_common.h"
#include "v3dv_limits.h"

struct v3dv_device;
struct v3dv_image_view;

struct v3dv_subpass_attachment {
   uint32_t attachment;
   VkImageLayout layout;
};

struct v3dv_subpass {
   uint32_t input_count;
   struct v3dv_subpass_attachment *input_attachments;

   uint32_t color_count;
   struct v3dv_subpass_attachment *color_attachments;
   struct v3dv_subpass_attachment *resolve_attachments;

   struct v3dv_subpass_attachment ds_attachment;
   struct v3dv_subpass_attachment ds_resolve_attachment;
   bool resolve_depth, resolve_stencil;

   /* If we need to emit the clear of the depth/stencil attachment using a
    * a draw call instead of using the TLB (GFXH-1461).
    */
   bool do_depth_clear_with_draw;
   bool do_stencil_clear_with_draw;

   /* Multiview */
   uint32_t view_mask;
};

struct v3dv_render_pass_attachment {
   VkAttachmentDescription2 desc;

   uint32_t first_subpass;
   uint32_t last_subpass;

   /* When multiview is enabled, we no longer care about when a particular
    * attachment is first or last used in a render pass, since not all views
    * in the attachment will meet that criteria. Instead, we need to track
    * each individual view (layer) in each attachment and emit our stores,
    * loads and clears accordingly.
    */
   struct {
      uint32_t first_subpass;
      uint32_t last_subpass;
   } views[MAX_MULTIVIEW_VIEW_COUNT];

   /* If this is a multisampled attachment that is going to be resolved,
    * whether we may be able to use the TLB hardware resolve based on the
    * attachment format.
    */
   bool try_tlb_resolve;
};

struct v3dv_render_pass {
   struct vk_object_base base;

   bool multiview_enabled;

   uint32_t attachment_count;
   struct v3dv_render_pass_attachment *attachments;

   uint32_t subpass_count;
   struct v3dv_subpass *subpasses;

   struct v3dv_subpass_attachment *subpass_attachments;
};

struct v3dv_framebuffer {
   struct vk_object_base base;

   uint32_t width;
   uint32_t height;
   uint32_t layers;

   /* Typically, edge tiles in the framebuffer have padding depending on the
    * underlying tiling layout. One consequence of this is that when the
    * framebuffer dimensions are not aligned to tile boundaries, tile stores
    * would still write full tiles on the edges and write to the padded area.
    * If the framebuffer is aliasing a smaller region of a larger image, then
    * we need to be careful with this though, as we won't have padding on the
    * edge tiles (which typically means that we need to load the tile buffer
    * before we store).
    */
   bool has_edge_padding;

   uint32_t attachment_count;
   uint32_t color_attachment_count;

   /* Notice that elements in 'attachments' will be NULL if the framebuffer
    * was created imageless. The driver is expected to access attachment info
    * from the command buffer state instead.
    */
   struct v3dv_image_view *attachments[0];
};

struct v3dv_frame_tiling {
   uint32_t width;
   uint32_t height;
   uint32_t layers;
   uint32_t render_target_count;
   uint32_t internal_bpp;
   uint32_t total_color_bpp;
   bool     msaa;
   bool     double_buffer;
   uint32_t tile_width;
   uint32_t tile_height;
   uint32_t draw_tiles_x;
   uint32_t draw_tiles_y;
   uint32_t supertile_width;
   uint32_t supertile_height;
   uint32_t frame_width_in_supertiles;
   uint32_t frame_height_in_supertiles;
};

bool v3dv_subpass_area_is_tile_aligned(struct v3dv_device *device,
                                       const VkRect2D *area,
                                       struct v3dv_framebuffer *fb,
                                       struct v3dv_render_pass *pass,
                                       uint32_t subpass_idx);

/* Checks if we need to emit 2 initial tile clears for double buffer mode.
 * This happens when we render at least 2 tiles, because in this mode each
 * tile uses a different half of the tile buffer memory so we can have 2 tiles
 * in flight (one being stored to memory and the next being rendered). In this
 * scenario, if we emit a single initial tile clear we would only clear the
 * first half of the tile buffer.
 */
static inline bool
v3dv_do_double_initial_tile_clear(const struct v3dv_frame_tiling *tiling)
{
   return tiling->double_buffer &&
          (tiling->draw_tiles_x > 1 || tiling->draw_tiles_y > 1 ||
           tiling->layers > 1);
}

VK_DEFINE_NONDISP_HANDLE_CASTS(v3dv_framebuffer, base, VkFramebuffer,
                               VK_OBJECT_TYPE_FRAMEBUFFER)
VK_DEFINE_NONDISP_HANDLE_CASTS(v3dv_render_pass, base, VkRenderPass,
                               VK_OBJECT_TYPE_RENDER_PASS)

#endif /* V3DV_PASS_H */
