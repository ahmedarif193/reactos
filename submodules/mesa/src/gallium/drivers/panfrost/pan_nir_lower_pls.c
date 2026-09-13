/*
 * Copyright © 2026 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "compiler/nir/nir_builder.h"
#include "pan_context.h"

static bool
lower_pls_intr(nir_builder *b, nir_intrinsic_instr *intr, void *data)
{
   struct panfrost_screen *screen = data;

   if (intr->intrinsic != nir_intrinsic_load_pixel_local &&
       intr->intrinsic != nir_intrinsic_store_pixel_local)
      return false;

   b->cursor = nir_before_instr(&intr->instr);

   enum pipe_format fmt = nir_intrinsic_format(intr);
   nir_io_semantics sem = nir_intrinsic_io_semantics(intr);
   const uint32_t base = (sem.location - FRAG_RESULT_DATA0);
   uint32_t conv = screen->vtbl.get_conv_desc(fmt, 0, 32, false);

   /* From the EXT_shader_pixel_local_storage spec:
    *
    *    "Pixel local storage variables declared inside pixel local storage
    *    blocks will be laid out in local storage in monotonically increasing
    *    order based on their location in the declaration. All pixel local
    *    storage variables consume exactly 4 bytes of storage."
    */
   nir_src *offset_src = nir_get_io_offset_src(intr);
   nir_def *offset = nir_imul_imm(b, offset_src->ssa, 4);
   offset = nir_iadd_imm(b, offset, base * 4);

   nir_def *pixel = nir_imm_int(b, 128); /* mega-sample mode */
   nir_def *coverage = nir_pack_32_2x16_split(b, nir_imm_intN_t(b, 0xffff, 16),
                                                 nir_u2u16(b, offset));

   if (intr->intrinsic == nir_intrinsic_load_pixel_local) {
      nir_def *val = nir_load_tile_pan(b,
         intr->def.num_components, intr->def.bit_size,
         pixel, coverage, nir_imm_int(b, conv),
         .dest_type = nir_intrinsic_dest_type(intr),
         .io_semantics = sem);
      nir_def_replace(&intr->def, val);
   } else {
      nir_store_tile_pan(b,
         intr->src[0].ssa, pixel, coverage, nir_imm_int(b, conv),
         .src_type = nir_intrinsic_src_type(intr),
         .io_semantics = sem);
      nir_instr_remove(&intr->instr);
   }

   return true;
}

bool
panfrost_nir_lower_pls(nir_shader *shader, struct panfrost_screen *screen)
{
   return nir_shader_intrinsics_pass(shader, lower_pls_intr,
                                     nir_metadata_control_flow, screen);
}
