/*
 * Copyright (C) 2024 Collabora, Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "compiler/nir/nir.h"
#include "compiler/nir/nir_builder.h"
#include "pan_nir.h"

static bool
nir_lower_image_ms(nir_builder *b, nir_intrinsic_instr *intr,
                        UNUSED void *data)
{
   bool img_deref = false;

   switch (intr->intrinsic) {
   case nir_intrinsic_image_deref_load:
   case nir_intrinsic_image_deref_store:
      img_deref = true;
      break;
   case nir_intrinsic_image_texel_address:
   case nir_intrinsic_image_load:
   case nir_intrinsic_image_store:
      break;
   default:
      return false;
   }

   if (nir_intrinsic_image_dim(intr) != GLSL_SAMPLER_DIM_MS)
      return false;

   b->cursor = nir_before_instr(&intr->instr);

   nir_def *coord = intr->src[1].ssa;
   nir_def *sample = nir_channel(b, intr->src[2].ssa, 0);
   bool is_array = nir_intrinsic_image_array(intr);

   nir_def *img_samples =
      img_deref ?
      nir_image_deref_samples(b, 32, intr->src[0].ssa, .image_array = is_array,
                              .image_dim = GLSL_SAMPLER_DIM_MS) :
      nir_image_samples(b, 32, intr->src[0].ssa, .image_array = is_array,
                        .image_dim = GLSL_SAMPLER_DIM_MS);

   nir_def *z_coord = nir_channel(b, coord, 2);

   /* image2DMS is treated by panfrost as if it were a 3D image, so
    * the sample index is in src[2]. We need to put this into the coordinates
    * in the Z component. If there already was a Z component (i.e. an
    * array index) then scale that by the number of samples and add it to
    * the sample number. We've lowered image2DMSArray images to be 3D images
    * with a larger Z.
    */
   z_coord = nir_iadd(b, sample, nir_imul(b, z_coord, img_samples));
   nir_src_rewrite(&intr->src[1],
                   nir_vector_insert_imm(b, coord, z_coord, 2));

   nir_intrinsic_set_image_dim(intr, GLSL_SAMPLER_DIM_3D);
   nir_intrinsic_set_image_array(intr, false);
   return true;
}

bool
pan_nir_lower_image_ms(nir_shader *shader)
{
   return nir_shader_intrinsics_pass(
      shader, nir_lower_image_ms,
      nir_metadata_control_flow, NULL);
}
