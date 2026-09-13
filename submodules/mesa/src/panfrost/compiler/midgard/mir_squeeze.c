/*
 * Copyright (C) 2019 Collabora, Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "compiler.h"

/* When we're 'squeezing down' the values in the IR, we maintain a hash
 * as such */

static unsigned
find_or_allocate_temp(compiler_context *ctx, struct hash_table_u64 *map,
                      unsigned hash)
{
   if (hash >= SSA_FIXED_MINIMUM)
      return hash;

   unsigned temp = (uintptr_t)_mesa_hash_table_u64_search(map, hash + 1);

   if (temp)
      return temp - 1;

   /* If no temp is find, allocate one */
   temp = ctx->temp_count++;
   ctx->max_hash = MAX2(ctx->max_hash, hash);

   _mesa_hash_table_u64_insert(map, hash + 1, (void *)((uintptr_t)temp + 1));

   return temp;
}

/* Reassigns numbering to get rid of gaps in the indices and to prioritize
 * smaller register classes */

void
mir_squeeze_index(compiler_context *ctx)
{
   struct hash_table_u64 *map = _mesa_hash_table_u64_create(NULL);

   /* Reset */
   ctx->temp_count = 0;

   /* We need to prioritize texture registers on older GPUs so we don't
    * fail RA trying to assign to work registers r0/r1 when a work
    * register is already there */

   mir_foreach_instr_global(ctx, ins) {
      if (ins->type == TAG_TEXTURE_4)
         ins->dest = find_or_allocate_temp(ctx, map, ins->dest);
   }

   mir_foreach_instr_global(ctx, ins) {
      if (ins->type != TAG_TEXTURE_4)
         ins->dest = find_or_allocate_temp(ctx, map, ins->dest);

      for (unsigned i = 0; i < ARRAY_SIZE(ins->src); ++i)
         ins->src[i] = find_or_allocate_temp(ctx, map, ins->src[i]);
   }

   ctx->blend_input = find_or_allocate_temp(ctx, map, ctx->blend_input);
   ctx->blend_src1 = find_or_allocate_temp(ctx, map, ctx->blend_src1);

   _mesa_hash_table_u64_destroy(map);
}
