/*
 * Copyright 2024 Valve Corporation
 * SPDX-License-Identifier: MIT
 */

#include <stdlib.h>
#include "compiler.h"

void
bi_reindex_ssa(bi_context *ctx)
{
   unsigned *remap = calloc(ctx->ssa_alloc, sizeof(*remap));

   ctx->ssa_alloc = 0;

   bi_foreach_instr_global(ctx, I) {
      bi_foreach_ssa_dest(I, d) {
         assert(!remap[I->dest[d].value] && "input is SSA");
         remap[I->dest[d].value] = ctx->ssa_alloc++;
         I->dest[d].value = remap[I->dest[d].value];
      }
   }

   bi_foreach_instr_global(ctx, I) {
      bi_foreach_ssa_src(I, s) {
         I->src[s].value = remap[I->src[s].value];
      }
   }

   free(remap);
}
