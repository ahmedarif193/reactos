/*
 * Copyright © 2019 Google, Inc.
 * SPDX-License-Identifier: MIT
 */

#include "compiler/nir/nir_builder.h"
#include "ir3_nir.h"

/**
 * This pass lowers load_sample_pos to load_sample_pos_from_id, and adjusts
 * load_sample_pos_from_id for our 0.0-center RGETPOS instruction.  It needs to
 * happen early, before wpos_ytransform.
 */

static bool
ir3_nir_lower_load_sample_pos_instr(nir_builder *b, nir_intrinsic_instr *intr,
                                    void *data)
{
   b->cursor = nir_after_instr(&intr->instr);
   if (intr->intrinsic == nir_intrinsic_load_sample_pos_from_id) {
      /* Our RGETPOS returns a position with a 0-based center, rather than NIR's
       * 0.5 center.
       */
      nir_def_rewrite_uses_after(&intr->def, nir_fadd_imm(b, &intr->def, 0.5));
      return true;
   } else if (intr->intrinsic == nir_intrinsic_load_sample_pos) {
      /* Note that the safe iterator will skip the load_sample_pos_from_id that
       * we generate.
       */
      nir_def_replace(
         &intr->def,
         nir_fadd_imm(
            b, nir_load_sample_pos_from_id(b, 32, nir_load_sample_id(b)), 0.5));

      return true;
   } else {
      return false;
   }
}

bool
ir3_nir_lower_load_sample_pos(nir_shader *shader)
{
   assert(shader->info.stage == MESA_SHADER_FRAGMENT);

   return nir_shader_intrinsics_pass(shader,
                                     ir3_nir_lower_load_sample_pos_instr,
                                     nir_metadata_control_flow, NULL);
}
