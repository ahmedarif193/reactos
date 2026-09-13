/*
 * Copyright (C) 2020 Collabora, Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "compiler/nir/nir.h"
#include "compiler/nir/nir_builder.h"
#include "midgard_nir.h"

static bool
nir_lower_image_bitsize(nir_builder *b, nir_intrinsic_instr *intr,
                        UNUSED void *data)
{
   switch (intr->intrinsic) {
   case nir_intrinsic_image_load:
   case nir_intrinsic_image_store:
   case nir_intrinsic_image_texel_address:
      break;
   default:
      return false;
   }

   if (nir_src_bit_size(intr->src[1]) == 16)
      return false;

   b->cursor = nir_before_instr(&intr->instr);

   nir_def *coord = intr->src[1].ssa;

   nir_def *coord16 = nir_u2u16(b, coord);

   nir_src_rewrite(&intr->src[1], coord16);

   return true;
}

bool
midgard_nir_lower_image_bitsize(nir_shader *shader)
{
   return nir_shader_intrinsics_pass(
      shader, nir_lower_image_bitsize,
      nir_metadata_control_flow, NULL);
}
