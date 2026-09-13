/*
 * Copyright (C) 2021 Collabora Ltd.
 * SPDX-License-Identifier: MIT
 */

#ifndef __VALHALL_COMPILER_H
#define __VALHALL_COMPILER_H

#include "compiler.h"
#include "valhall.h"

#ifdef __cplusplus
extern "C" {
#endif

bool va_validate_fau(bi_instr *I);
void va_validate(FILE *fp, bi_context *ctx);
void va_repair_fau(bi_builder *b, bi_instr *I);
void va_fuse_add_imm(bi_instr *I);
void va_lower_constants(bi_context *ctx, bi_instr *I, struct hash_table_u64 *counts, uint32_t min_fau_count);
void va_count_constants(bi_context *ctx, bi_instr *I, struct hash_table_u64 *counts);
void va_lower_isel(bi_context *ctx);
void va_assign_slots(bi_context *ctx);
void va_insert_flow_control_nops(bi_context *ctx);
void va_merge_flow(bi_context *ctx);
void va_mark_last(bi_context *ctx);
void va_gather_hsr_info(bi_context *ctx, struct pan_shader_info *info);
uint64_t va_pack_instr(const bi_instr *I, unsigned arch);

static inline unsigned
va_fau_page(enum bir_fau value)
{
   /* Uniform slots of FAU have a 7-bit index. The top 2-bits are the page; the
    * bottom 5-bits are specified in the source.
    */
   if (value & BIR_FAU_UNIFORM) {
      unsigned slot = value & ~BIR_FAU_UNIFORM;
      unsigned page = slot >> 5;

      assert(page <= 3);
      return page;
   }

   /* Special indices are also paginated */
   switch (value) {
   case BIR_FAU_TLS_PTR:
   case BIR_FAU_WLS_PTR:
      return 1;
   case BIR_FAU_LANE_ID:
   case BIR_FAU_CORE_ID:
   case BIR_FAU_SHADER_OUTPUT:
   case BIR_FAU_PROGRAM_COUNTER:
      return 3;
   default:
      return 0;
   }
}

static inline unsigned
va_select_fau_page(const bi_instr *I)
{
   bi_foreach_src(I, s) {
      if (I->src[s].type == BI_INDEX_FAU)
         return va_fau_page((enum bir_fau)I->src[s].value);
   }

   return 0;
}

/** Cycle model for Valhall. Results need to be normalized */
struct va_stats {
   /** Counts per pipe */
   unsigned fma, cvt, sfu, v, ls, t;
   /** Mask of registers used */
   uint64_t reg_mask;
   /** number of uniform registers used */
   unsigned nr_fau_uniforms;
};

void va_count_instr_stats(bi_instr *I, struct va_stats *stats);

#ifdef __cplusplus
} /* extern C */
#endif

#endif
