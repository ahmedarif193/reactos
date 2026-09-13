/*
 * Copyright (C) 2023 Collabora, Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "nir_builder.h"
#include "pan_context.h"

static bool
pass(nir_builder *b, nir_intrinsic_instr *intr, void *data)
{
   if (intr->intrinsic != nir_intrinsic_store_output)
      return false;

   unsigned *nr_cbufs = data;
   unsigned location = nir_intrinsic_io_semantics(intr).location;

   if (location >= FRAG_RESULT_DATA0 &&
       (location - FRAG_RESULT_DATA0) >= (*nr_cbufs)) {
      nir_instr_remove(&intr->instr);
      return true;
   } else {
      return false;
   }
}

bool
panfrost_nir_remove_fragcolor_stores(nir_shader *s, unsigned nr_cbufs)
{
   return nir_shader_intrinsics_pass(
      s, pass, nir_metadata_control_flow, &nr_cbufs);
}
