/*
 * Copyright © 2022 Imagination Technologies Ltd.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#ifndef PVR_HW_PASS_H
#define PVR_HW_PASS_H

#include <stdbool.h>
#include <stdint.h>
#include <vulkan/vulkan.h>

#include "pvr_macros.h"

struct pvr_device;
struct pvr_render_pass;

#include "pvr_mrt.h"

enum pvr_resolve_type {
   PVR_RESOLVE_TYPE_INVALID = 0, /* explicitly treat 0 as invalid. */
   PVR_RESOLVE_TYPE_PBE,
   PVR_RESOLVE_TYPE_TRANSFER,
};

enum pvr_renderpass_hwsetup_input_access {
   /* The attachment must be loaded using a texture sample. */
   PVR_RENDERPASS_HWSETUP_INPUT_ACCESS_OFFCHIP,
   /* The attachment can be loaded from an output register or tile buffer. */
   PVR_RENDERPASS_HWSETUP_INPUT_ACCESS_ONCHIP,
   /* As _ONCHIP but the attachment is the result of a Z replicate in the same
    * subpass.
    */
   PVR_RENDERPASS_HWSETUP_INPUT_ACCESS_ONCHIP_ZREPLICATE,
};

struct pvr_renderpass_hwsetup_eot_surface {
   /* MRT index to store from. Also used to index into
    * usc_mrt_setup::mrt_resources.
    */
   uint32_t mrt_idx;

   /* Index of pvr_render_pass_info::attachments to store into. */
   uint32_t attachment_idx;

   /* True if the surface should be resolved. */
   bool need_resolve;

   /* How the surface should be resolved at the end of a render. Only valid if
    * pvr_renderpass_hwsetup_eot_surface::need_resolve is set to true.
    */
   enum pvr_resolve_type resolve_type;

   /* Index of pvr_render_pass_info::attachments to resolve from. Only valid if
    * pvr_renderpass_hwsetup_eot_surface::need_resolve is set to true.
    */
   uint32_t src_attachment_idx;
};

struct pvr_renderpass_hwsetup_subpass {
   /* Mapping from fragment stage pixel outputs to hardware storage for all
    * fragment programs in the subpass.
    */
   struct usc_mrt_setup setup;

   /* If >=0 then copy the depth into this pixel output for all fragment
    * programs in the subpass.
    */
   int32_t z_replicate;

   /* The operation to perform on the depth at the start of the subpass. Loads
    * are deferred to subpasses when depth has been replicated.
    */
   VkAttachmentLoadOp depth_initop;

   /* If true then clear the stencil at the start of the subpass. */
   bool stencil_clear;

   /* Subpass index from the input pvr_render_subpass structure. */
   uint32_t index;

   /* For each color attachment to the subpass the operation to perform at
    * the start of the subpass.
    */
   VkAttachmentLoadOp *color_initops;

   struct pvr_load_op *load_op;

   struct {
      enum pvr_renderpass_hwsetup_input_access type;
      uint32_t on_chip_rt;
   } *input_access;

   uint8_t output_register_mask;
   bool has_stencil_self_dep;
};

struct pvr_renderpass_colorinit {
   /* Source attachment for the operation. */
   uint32_t index;

   /* Type of operation either clear or load. */
   VkAttachmentLoadOp op;
};

struct pvr_renderpass_hwsetup_render {
   /* Number of pixel output registers to allocate for this render. */
   uint32_t output_regs_count;

   /* Number of tile buffers to allocate for this render. */
   uint32_t tile_buffers_count;

   /* Number of subpasses in this render. */
   uint32_t subpass_count;

   /* Description of each subpass. */
   struct pvr_renderpass_hwsetup_subpass *subpasses;

   /* The sample count of every color attachment (or depth attachment if
    * z-only) in this render.
    */
   uint32_t sample_count;

   /* Index of the attachment to use for depth/stencil load/store in this
    * render.
    */
   uint32_t ds_attach_idx;

   /* Index of the attachment to use for depth/stencil resolve load/store in
    * this render.
    */
   uint32_t ds_attach_resolve_idx;

   /* Resolve mode to perform for this render originally set for each subpass.
    */
   VkResolveModeFlagBits stencil_resolve_mode;
   VkResolveModeFlagBits depth_resolve_mode;

   /* Operation on the on-chip depth at the start of the render.
    * Either load from 'ds_attach_idx', clear using 'ds_attach_idx' or leave
    * uninitialized.
    */
   VkAttachmentLoadOp depth_init;

   /* Operation on the on-chip stencil at the start of the render. */
   VkAttachmentLoadOp stencil_init;

   /* Count of operations on on-chip color storage at the start of the render.
    */
   uint32_t color_init_count;

   /* For each operation: the destination in the on-chip color storage. */
   struct usc_mrt_setup init_setup;

   /* How to initialize render targets at the start of the render. */
   struct pvr_renderpass_colorinit *color_init;

   /* true to store depth to 'ds_attach_idx' at the end of the render. */
   bool depth_store;
   /* true to store stencil to 'ds_attach_idx' at the end of the render. */
   bool stencil_store;

   /* Describes the location of the source data for each stored surface. */
   struct usc_mrt_setup eot_setup;

   struct pvr_renderpass_hwsetup_eot_surface *eot_surfaces;
   uint32_t eot_surface_count;

   uint32_t pbe_emits;

   /* true if this HW render has lasting effects on its attachments. */
   bool has_side_effects;

   bool requires_frag_pr;

   bool multiview_enabled;

   /* View mask for multiview. */
   uint32_t view_mask;

   struct pvr_load_op_state *load_op_state;
};

struct pvr_renderpass_hw_map {
   uint32_t render;
   uint32_t subpass;
};

struct pvr_renderpass_hwsetup {
   /* Number of renders. */
   uint32_t render_count;

   /* Description of each render. */
   struct pvr_renderpass_hwsetup_render *renders;

   /* Maps indices from pvr_render_pass::subpasses to the
    * pvr_renderpass_hwsetup_render/pvr_renderpass_hwsetup_subpass relative to
    * that render where the subpass is scheduled.
    */
   struct pvr_renderpass_hw_map *subpass_map;

   bool *surface_allocate;
};

#ifdef PVR_PER_ARCH

VkResult PVR_PER_ARCH(create_renderpass_hwsetup)(
   struct pvr_device *device,
   const VkAllocationCallbacks *alloc,
   struct pvr_render_pass *pass,
   bool disable_merge,
   struct pvr_renderpass_hwsetup **const hw_setup_out);

#   define pvr_arch_create_renderpass_hwsetup \
      PVR_PER_ARCH(create_renderpass_hwsetup)

void PVR_PER_ARCH(destroy_renderpass_hwsetup)(
   const VkAllocationCallbacks *alloc,
   struct pvr_renderpass_hwsetup *hw_setup);

#   define pvr_arch_destroy_renderpass_hwsetup \
      PVR_PER_ARCH(destroy_renderpass_hwsetup)

#endif

#endif /* PVR_HW_PASS_H */
