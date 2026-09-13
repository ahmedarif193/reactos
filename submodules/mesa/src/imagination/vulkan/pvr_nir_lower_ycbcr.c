/*
 * Copyright © 2026 Imagination Technologies Ltd.
 * Copyright © 2017 Intel Corporation
 * SPDX-License-Identifier: MIT
 *
 * Based on vk_nir_convert_ycbcr.c.
 *
 * We need this to fixup YCBCR_IDENTITY conversion since the TPU clamps the
 * output from csc into the range 0.0 to 1.0.
 */
#include "pvr_nir_lower_ycbcr.h"

#include "nir_builder.h"
#include "vk_ycbcr_conversion.h"

struct lower_ycbcr_tex_state {
   nir_pvr_ycbcr_conversion_lookup_cb cb;
   const void *cb_data;
};

static bool
lower_ycbcr_tex_instr(nir_builder *b, nir_tex_instr *tex, void *_state)
{
   const struct lower_ycbcr_tex_state *state = _state;

   /* For the following instructions, we don't apply any change and let the
    * instruction apply to the first plane.
    */
   if (tex->op == nir_texop_txs || tex->op == nir_texop_query_levels ||
       tex->op == nir_texop_lod)
      return false;

   int deref_src_idx = nir_tex_instr_src_index(tex, nir_tex_src_texture_deref);
   assert(deref_src_idx >= 0);
   nir_deref_instr *deref = nir_src_as_deref(tex->src[deref_src_idx].src);

   nir_variable *var = nir_deref_instr_get_variable(deref);
   uint32_t set = var->data.descriptor_set;
   uint32_t binding = var->data.binding;

   assert(tex->texture_index == 0);
   unsigned array_index = 0;
   if (deref->deref_type != nir_deref_type_var) {
      assert(deref->deref_type == nir_deref_type_array);
      if (!nir_src_is_const(deref->arr.index))
         return false;
      array_index = nir_src_as_uint(deref->arr.index);
   }

   const struct vk_ycbcr_conversion_state *conversion =
      state->cb(state->cb_data, set, binding, array_index);
   if (conversion == NULL)
      return false;

   /* Range expansion fix only needed for YCbCr Identity */
   if (conversion->ycbcr_model !=
       VK_SAMPLER_YCBCR_MODEL_CONVERSION_YCBCR_IDENTITY)
      return false;

   b->cursor = nir_after_instr(&tex->instr);
   nir_def *result =
      nir_fsub(b, &tex->def, nir_imm_vec4(b, 0.5, 0.0, 0.5, 0.0));
   nir_def_rewrite_uses_after(&tex->def, result);
   return true;
}

bool nir_pvr_lower_ycbcr_tex(nir_shader *nir,
                             nir_pvr_ycbcr_conversion_lookup_cb cb,
                             const void *cb_data)
{
   struct lower_ycbcr_tex_state state = {
      .cb = cb,
      .cb_data = cb_data,
   };

   return nir_shader_tex_pass(nir,
                              lower_ycbcr_tex_instr,
                              nir_metadata_control_flow,
                              &state);
}
