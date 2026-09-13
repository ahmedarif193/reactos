/*
 * Copyright © 2024 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "intel_nir.h"
#include "intel_shader_enums.h"
#include "compiler/nir/nir_builder.h"

static bool
lower_printf_intrinsics(nir_builder *b, nir_intrinsic_instr *intrin, void *_)
{
   b->cursor = nir_before_instr(&intrin->instr);

   switch (intrin->intrinsic) {
   case nir_intrinsic_load_printf_buffer_address:
      nir_def_replace(
         &intrin->def,
         nir_pack_64_2x32_split(
            b,
            nir_load_reloc_const_intel(b, BRW_SHADER_RELOC_PRINTF_BUFFER_ADDR_LOW),
            nir_load_reloc_const_intel(b, BRW_SHADER_RELOC_PRINTF_BUFFER_ADDR_HIGH)));
      return true;

   case nir_intrinsic_load_printf_buffer_size:
      nir_def_replace(
         &intrin->def,
         nir_load_reloc_const_intel(b, BRW_SHADER_RELOC_PRINTF_BUFFER_SIZE));
      return true;

   default:
      return false;
   }
}

bool
intel_nir_lower_printf(nir_shader *nir)
{
   return nir_shader_intrinsics_pass(nir, lower_printf_intrinsics,
                                     nir_metadata_control_flow, NULL);
}
