/*
 * Copyright (C) 2021 Collabora, Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "compiler/nir/nir_builder.h"
#include "pan_nir.h"

/* Sample positions are supplied in a packed 8:8 fixed-point vec2 format in GPU
 * memory indexed by the sample. We lower in NIR to take advantage of possible
 * ALU optimizations at the end. This is convenient for Bifrost, since the
 * sample positions are passed in this format and it saves the driver from any
 * system value handling. For Midgard, it's a bit suboptimal (fp16 positions
 * could be supplied directly), but this lets us unify the implementation, and
 * it's a pretty trivial difference */

static bool
pan_lower_sample_pos_impl(struct nir_builder *b, nir_intrinsic_instr *intr,
                          UNUSED void *data)
{
   if (intr->intrinsic != nir_intrinsic_load_sample_pos &&
       intr->intrinsic != nir_intrinsic_load_sample_pos_or_center)
      return false;

   b->cursor = nir_before_instr(&intr->instr);

   if (!b->shader->info.fs.uses_sample_shading) {
      assert(intr->intrinsic == nir_intrinsic_load_sample_pos_or_center);

      /* When sample shading is disabled, lower to a constant (0.5,0.5).
       *
       * In Vulkan, sample shading state is always known statically. In
       * OpenGL, it's possible to enable sample shading dynamically. The only
       * thing that currently emits load_sample_pos_or_center is
       * nir_lower_wpos_center, which is only used for Vulkan, so this is
       * okay.
       *
       * In the case where multisample is disabled but sample shading is
       * enabled , we would skip this branch and load (0.5,0.5) from index 0
       * in the sample pos table.
       *
       * In theory we should get r61[13:23]=32 on Bifrost when sample shading
       * is disabled, and can load (0.5,0.5) from sample_positions[32] with
       * the same code we use for loading normal sample positions. This would
       * allow dynamic sample shading state, but would require passing the raw
       * sample ID register through to NIR. */
      nir_def_replace(&intr->def, nir_imm_vec2(b, 0.5, 0.5));

      return true;
   }

   /* Elements are 4 bytes */
   nir_def *addr =
      nir_iadd(b, nir_load_sample_positions_pan(b),
               nir_u2u64(b, nir_imul_imm(b, nir_load_sample_id(b), 4)));

   /* Decode 8:8 fixed-point */
   nir_def *raw = nir_load_global(b, 2, 16, addr);
   nir_def *decoded = nir_fmul_imm(b, nir_i2f16(b, raw), 1.0 / 256.0);

   /* Make NIR validator happy */
   if (decoded->bit_size != intr->def.bit_size)
      decoded = nir_f2fN(b, decoded, intr->def.bit_size);

   nir_def_rewrite_uses(&intr->def, decoded);
   return true;
}

bool
pan_nir_lower_sample_pos(nir_shader *shader)
{
   if (shader->info.stage != MESA_SHADER_FRAGMENT)
      return false;

   return nir_shader_intrinsics_pass(
      shader, pan_lower_sample_pos_impl,
      nir_metadata_control_flow, NULL);
}
