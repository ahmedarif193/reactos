/*
 * Copyright (C) 2021 Collabora, Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "compiler/nir/nir_builder.h"
#include "pan_nir.h"

/* Lower gl_HelperInvocation to (gl_SampleMaskIn == 0), this depends on
 * architectural details but is more efficient than NIR's lowering.
 */
static bool
pan_lower_helper_invocation_instr(nir_builder *b, nir_intrinsic_instr *intr,
                                  void *data)
{
   if (intr->intrinsic != nir_intrinsic_load_helper_invocation)
      return false;

   b->cursor = nir_before_instr(&intr->instr);

   nir_def *mask = nir_load_sample_mask_in(b);
   nir_def_replace(&intr->def, nir_ieq_imm(b, mask, 0));
   return true;
}

bool
pan_nir_lower_helper_invocation(nir_shader *shader)
{
   return nir_shader_intrinsics_pass(shader, pan_lower_helper_invocation_instr,
                                     nir_metadata_control_flow, NULL);
}
