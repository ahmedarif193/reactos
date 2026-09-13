
/*
 * Copyright (C) 2021 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "bi_builder.h"
#include "va_compiler.h"
#include "valhall.h"

void
va_count_instr_stats(bi_instr *I, struct va_stats *stats)
{
   /* Adjusted for 64-bit arithmetic */
   unsigned words = bi_count_write_registers(I, 0);

   bi_foreach_dest(I, d) {
      if (I->dest[d].type == BI_INDEX_REGISTER)
         stats->reg_mask |= (uint64_t)bi_writemask(I, d) << I->dest[d].value;
   }
   bi_foreach_src(I, s) {
      if (I->src[s].type == BI_INDEX_REGISTER) {
         unsigned pos = I->src[s].offset + I->src[s].value;
         unsigned count = bi_count_read_registers(I, s);
         stats->reg_mask |= ((uint64_t)BITFIELD_MASK(count)) << pos;
      }
      if (I->src[s].type == BI_INDEX_FAU) {
         bi_index index = I->src[s];
         unsigned val = index.value;
         if (val >= BIR_FAU_UNIFORM) {
            val = val & ~BIR_FAU_UNIFORM;
            if (val < BIR_FAU_UNIFORM) {
               stats->nr_fau_uniforms = MAX2(stats->nr_fau_uniforms, val+1);
            }
         }
      }
   }
   switch (valhall_opcodes[I->op].unit) {
   /* Arithmetic is 2x slower for 64-bit than 32-bit */
   case VA_UNIT_FMA:
      stats->fma += words;
      return;

   case VA_UNIT_CVT:
      stats->cvt += words;
      return;

   case VA_UNIT_SFU:
      stats->sfu += words;
      return;

   /* Varying is scaled by 16-bit components interpolated */
   case VA_UNIT_V:
      stats->v +=
         (I->vecsize + 1) * (bi_is_regfmt_16(I->register_format) ? 1 : 2);
      return;

   /* We just count load/store and texturing for now */
   case VA_UNIT_LS:
      stats->ls++;
      return;

   case VA_UNIT_T:
      stats->t++;
      return;

   /* Fused varying+texture loads 2 FP32 components of varying for texture
    * coordinates and then textures */
   case VA_UNIT_VT:
      stats->ls += (2 * 2);
      stats->t++;
      return;

   /* Nothing to do here */
   case VA_UNIT_NONE:
      return;
   }

   UNREACHABLE("Invalid unit");
}
