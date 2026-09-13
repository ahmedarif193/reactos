/*
 * Copyright © 2021 Collabora Ltd.
 *
 * Derived from tu_shader.c which is:
 * Copyright © 2019 Google LLC
 *
 * Also derived from anv_pipeline.c which is
 * Copyright © 2015 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "panvk_device.h"
#include "panvk_shader.h"

#include "vk_graphics_state.h"

#include "nir.h"
#include "nir_builder.h"
#include "pan_nir.h"

struct panvk_lower_input_attachment_load_ctx {
   uint32_t ro_color_mask;
   uint32_t input_attachment_read;
};

static bool
collect_frag_writes(nir_builder *b, nir_intrinsic_instr *intr, void *data)
{
   if (intr->intrinsic != nir_intrinsic_store_deref)
      return false;

   nir_deref_instr *deref = nir_src_as_deref(intr->src[0]);

   if (deref->modes != nir_var_shader_out)
      return false;

   nir_variable *var = nir_deref_instr_get_variable(deref);
   assert(var);

   if (var->data.location < FRAG_RESULT_DATA0 ||
       var->data.location > FRAG_RESULT_DATA7)
      return false;

   uint32_t *written_mask = data;

   *written_mask |= BITFIELD_BIT(var->data.location - FRAG_RESULT_DATA0);
   return true;
}

static uint32_t
readonly_color_mask(nir_shader *nir,
                    const struct vk_graphics_pipeline_state *state)
{
   if (!state || !state->ial || !state->cal)
      return 0;

   uint32_t in_mask = 0, out_mask = 0;

   for (uint32_t i = 0; i < ARRAY_SIZE(state->ial->color_map); i++) {
      if (i >= state->ial->color_attachment_count)
         break;

      if (state->ial->color_map[i] != MESA_VK_ATTACHMENT_UNUSED)
         in_mask |= BITFIELD_BIT(i);
   }

   NIR_PASS(_, nir, nir_shader_intrinsics_pass, collect_frag_writes,
            nir_metadata_all, &out_mask);

   for (uint32_t i = 0; i < ARRAY_SIZE(state->cal->color_map); i++) {
      if (state->ial->color_map[i] == MESA_VK_ATTACHMENT_UNUSED)
         out_mask &= ~BITFIELD_BIT(i);
   }

   return in_mask & ~out_mask;
}

static bool
lower_input_attachment_load(nir_builder *b, nir_intrinsic_instr *intr,
                            void *data)
{
   struct panvk_lower_input_attachment_load_ctx *ctx = data;

   if (intr->intrinsic != nir_intrinsic_image_deref_load &&
       intr->intrinsic != nir_intrinsic_image_deref_sparse_load)
      return false;

   nir_deref_instr *deref = nir_src_as_deref(intr->src[0]);
   enum glsl_sampler_dim image_dim = glsl_get_sampler_dim(deref->type);
   if (image_dim != GLSL_SAMPLER_DIM_SUBPASS &&
       image_dim != GLSL_SAMPLER_DIM_SUBPASS_MS)
      return false;

   b->cursor = nir_before_instr(&intr->instr);

   uint32_t index_imm = 0, range = 1;
   nir_def *index_ssa = NULL;
   if (deref->deref_type == nir_deref_type_array) {
      nir_deref_instr *parent = nir_deref_instr_parent(deref);
      if (nir_src_is_const(deref->arr.index)) {
         index_imm = nir_src_as_uint(deref->arr.index);
      } else {
         index_ssa = deref->arr.index.ssa;
         range = glsl_array_size(parent->type);
      }
      deref = parent;
   }

   assert(deref->deref_type == nir_deref_type_var);
   nir_variable *var = deref->var;

   const unsigned base_idx =
      var->data.index != NIR_VARIABLE_NO_INDEX ? var->data.index + 1 : 0;
   index_imm += base_idx;
   index_ssa = index_ssa ?
      nir_iadd_imm(b, index_ssa, base_idx) : nir_imm_int(b, index_imm);

   nir_alu_type dest_type = nir_intrinsic_dest_type(intr);

   /* Zero means variable array. */
   range = range == 0 ? 9 - index_imm : range;
   ctx->input_attachment_read |= BITFIELD_RANGE(index_imm, range);

   nir_def *target = nir_load_input_attachment_target_pan(b, index_ssa);
   nir_def *load_img, *load_output;

   nir_push_if(b, nir_ine_imm(b, target, ~0));
   {
      nir_def *is_color_att = nir_ilt_imm(b, target, 8);
      nir_def *load_color, *load_zs;
      nir_io_semantics iosem = {0};
      iosem.fb_fetch_output = true;
      iosem.fb_fetch_output_coherent = !!(nir_intrinsic_access(intr) & ACCESS_COHERENT);

      nir_push_if(b, is_color_att);
      {
         nir_def *conversion =
            nir_load_input_attachment_conv_pan(b, index_ssa);
         nir_def *is_read_only =
            nir_i2b(b, nir_iand_imm(b, nir_ishl(b, nir_imm_int(b, 1), target),
                                    ctx->ro_color_mask));
         nir_def *load_ro_color, *load_rw_color;

         iosem.location = FRAG_RESULT_DATA0;
         nir_push_if(b, is_read_only);
	 {
            load_ro_color = nir_load_tile_res_pan(
               b, intr->def.num_components, intr->def.bit_size,
               pan_nir_tile_rt_sample(b, target, intr->src[2].ssa),
               pan_nir_tile_default_coverage(b),
               conversion, .dest_type = dest_type,
               .access = nir_intrinsic_access(intr), .io_semantics = iosem);
         }
         nir_push_else(b, NULL);
         {
            load_rw_color = nir_load_tile_pan(
               b, intr->def.num_components, intr->def.bit_size,
               pan_nir_tile_rt_sample(b, target, intr->src[2].ssa),
               pan_nir_tile_default_coverage(b),
               conversion, .dest_type = dest_type,
               .access = nir_intrinsic_access(intr), .io_semantics = iosem);
         }
         nir_pop_if(b, NULL);
         load_color = nir_if_phi(b, load_ro_color, load_rw_color);
      }
      nir_push_else(b, NULL);
      {
#if PAN_ARCH < 9
         /* On v7, we need to pass the depth format around. If we use a
          * conversion of zero, like we do on v9+, the GPU reports an
          * INVALID_INSTR_ENC. */
         struct mali_internal_conversion_packed stencil_conv;

         pan_pack(&stencil_conv, INTERNAL_CONVERSION, cfg) {
            cfg.register_format = MALI_REGISTER_FILE_FORMAT_U32;
            cfg.memory_format = GENX(pan_dithered_format_from_pipe_format)(
               PIPE_FORMAT_S8_UINT, false);
         }

         nir_def *conversion =
            dest_type == nir_type_uint32
               ? nir_imm_int(b, stencil_conv.opaque[0])
               : nir_load_input_attachment_conv_pan(b, index_ssa);
#else
         nir_def *conversion = nir_imm_int(b, 0);
#endif

         iosem.location = dest_type == nir_type_float32 ? FRAG_RESULT_DEPTH
                                                        : FRAG_RESULT_STENCIL;
         target = nir_imm_int(b, 0);
         load_zs = nir_load_tile_pan(
            b, intr->def.num_components, intr->def.bit_size,
            pan_nir_tile_location_sample(b, iosem.location, intr->src[2].ssa),
            pan_nir_tile_default_coverage(b),
            conversion, .dest_type = dest_type,
            .access = nir_intrinsic_access(intr), .io_semantics = iosem);

         /* If we loaded the stencil value, the upper 24 bits might contain
	  * garbage, hence the masking done here. */
         if (iosem.location == FRAG_RESULT_STENCIL)
            load_zs = nir_iand_imm(b, load_zs, BITFIELD_MASK(8));
      }
      nir_pop_if(b, NULL);

      load_output = nir_if_phi(b, load_color, load_zs);
   }
   nir_push_else(b, NULL);
   {
      nir_instr *load_clone = nir_instr_clone(b->shader, &intr->instr);
      nir_builder_instr_insert(b, load_clone);
      load_img = &nir_instr_as_intrinsic(load_clone)->def;
   }
   nir_pop_if(b, NULL);

   nir_def_replace(&intr->def, nir_if_phi(b, load_output, load_img));

   return true;
}

bool
panvk_per_arch(nir_lower_input_attachment_loads)(
   nir_shader *nir,
   const struct vk_graphics_pipeline_state *state,
   uint32_t *input_attachment_read_out)
{
   bool progress = false;
   struct panvk_lower_input_attachment_load_ctx ia_load_ctx = {
      .ro_color_mask = readonly_color_mask(nir, state),
   };

   NIR_PASS(progress, nir, nir_shader_intrinsics_pass,
            lower_input_attachment_load, nir_metadata_none,
            &ia_load_ctx);

   if (input_attachment_read_out)
      *input_attachment_read_out = ia_load_ctx.input_attachment_read;

   /* Lower the remaining input attachment loads. */
   struct nir_input_attachment_options lower_input_attach_opts = { };
   NIR_PASS(progress, nir, nir_lower_input_attachments,
            &lower_input_attach_opts);

   return progress;
}
