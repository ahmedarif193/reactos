/*
 * Copyright © 2024,2026 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "compiler/nir/nir_builder.h"
#include "bi_opcodes.h"
#include "pan_nir.h"

/* Lowers nir_load_frag_coord_zw and point_coord to nir_load_var_special_pan */

static bool
lower_var_special_pan(nir_builder *b, nir_intrinsic_instr *intrin, void *data)
{
   enum bi_varying_name var_name;

   switch (intrin->intrinsic) {
   case nir_intrinsic_load_frag_coord_z:
      var_name = BI_VARYING_NAME_FRAG_Z;
      break;
   case nir_intrinsic_load_frag_coord_w:
      var_name = BI_VARYING_NAME_FRAG_W;
      break;
   case nir_intrinsic_load_point_coord:
      var_name = BI_VARYING_NAME_POINT;
      break;
   default:
      return false;
   }
   b->cursor = nir_before_instr(&intrin->instr);

   nir_def *bary = nir_load_barycentric_pixel(b, 32,
      .interp_mode = INTERP_MODE_NOPERSPECTIVE
   );

   nir_def *new = nir_load_var_special_pan(b, intrin->def.num_components, bary,
                                           .flags = var_name);
   nir_def_replace(&intrin->def, new);

   return true;
}

bool
pan_nir_lower_var_special_pan(nir_shader *shader)
{
   return nir_shader_intrinsics_pass(shader, lower_var_special_pan,
                                     nir_metadata_control_flow, NULL);
}
