/*
 * Copyright © 2023 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

/*
 * Lower non uniform at sample messages to the interpolator.
 *
 * This is pretty much identical to what nir_lower_non_uniform_access() does.
 * We do it here because otherwise GCM would undo this optimization. Also we
 * can assume divergence analysis here.
 */

#include "intel_nir.h"
#include "compiler/nir/nir_builder.h"

static bool
intel_nir_lower_non_uniform_barycentric_at_sample_instr(nir_builder *b,
                                                        nir_instr *instr,
                                                        void *cb_data)
{
   if (instr->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
   if (intrin->intrinsic != nir_intrinsic_load_barycentric_at_sample)
      return false;

   if (nir_src_is_always_uniform(intrin->src[0]) ||
       !nir_src_is_divergent(&intrin->src[0]))
      return false;

   if (nir_def_instr(&intrin->def)->pass_flags != 0)
      return false;

   nir_def *sample_id = intrin->src[0].ssa;

   b->cursor = nir_instr_remove(&intrin->instr);

   nir_push_loop(b);
   {
      nir_def *first_sample_id = nir_read_first_invocation(b, sample_id);

      nir_push_if(b, nir_ieq(b, sample_id, first_sample_id));
      {
         nir_builder_instr_insert(b, &intrin->instr);
         nir_def_instr(&intrin->def)->pass_flags = 1;

         nir_src_rewrite(&intrin->src[0], first_sample_id);

         nir_jump(b, nir_jump_break);
      }
   }

   return true;
}

static bool
intel_nir_lower_non_uniform_interpolated_input_instr(nir_builder *b,
                                                     nir_instr *instr,
                                                     void *cb_data)
{
   if (instr->type != nir_instr_type_intrinsic)
      return false;

   nir_intrinsic_instr *load_ii = nir_instr_as_intrinsic(instr);
   if (load_ii->intrinsic != nir_intrinsic_load_interpolated_input)
      return false;

   assert(nir_src_is_intrinsic(load_ii->src[0]));

   nir_intrinsic_instr *bary =
      nir_def_as_intrinsic(load_ii->src[0].ssa);
   if (bary->intrinsic != nir_intrinsic_load_barycentric_at_sample)
      return false;

   if (nir_src_is_always_uniform(bary->src[0]) ||
       !nir_src_is_divergent(&bary->src[0]))
      return false;

   nir_def *sample_id = bary->src[0].ssa;

   b->cursor = nir_instr_remove(&load_ii->instr);

   nir_push_loop(b);
   {
      nir_def *first_sample_id = nir_read_first_invocation(b, sample_id);

      nir_push_if(b, nir_ieq(b, sample_id, first_sample_id));
      {
         nir_def *new_bary = nir_load_barycentric_at_sample(
            b, bary->def.bit_size, first_sample_id,
            .interp_mode = nir_intrinsic_interp_mode(bary));

         /* Set pass_flags so that the other lowering pass won't try to also
          * lower this new load_barycentric_at_sample.
          */
         nir_def_instr(new_bary)->pass_flags = 1;

         nir_builder_instr_insert(b, &load_ii->instr);

         nir_src_rewrite(&load_ii->src[0], new_bary);

         nir_jump(b, nir_jump_break);
      }
   }

   return true;
}

bool
intel_nir_lower_non_uniform_barycentric_at_sample(nir_shader *nir)
{
   bool progress;

   nir_divergence_analysis(nir);
   nir_shader_clear_pass_flags(nir);

   progress = nir_shader_instructions_pass(
      nir,
      intel_nir_lower_non_uniform_interpolated_input_instr,
      nir_metadata_none,
      NULL);

   progress = nir_shader_instructions_pass(
      nir,
      intel_nir_lower_non_uniform_barycentric_at_sample_instr,
      nir_metadata_none,
      NULL) || progress;

   return progress;
}
