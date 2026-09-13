/*
 * Copyright 2022 Alyssa Rosenzweig
 * SPDX-License-Identifier: MIT
 */

#include "compiler/nir/nir_builder.h"
#include "agx_compiler.h"

/* Local memory instructions require 16-bit offsets, so we add conversions. */
static bool
pass(struct nir_builder *b, nir_intrinsic_instr *intr, UNUSED void *data)
{
   nir_src *offset = nir_get_io_offset_src(intr);
   if (!nir_is_shared_access(intr) || nir_src_bit_size(*offset) == 16)
      return false;

   b->cursor = nir_before_instr(&intr->instr);
   nir_src_rewrite(offset, nir_u2u16(b, offset->ssa));
   return true;
}

bool
agx_nir_lower_shared_bitsize(nir_shader *shader)
{
   return nir_shader_intrinsics_pass(shader, pass, nir_metadata_control_flow,
                                     NULL);
}
