/*
 * Copyright © 2026 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "brw_nir.h"
#include "compiler/nir/nir_builder.h"

static bool
block_is_in_loop(nir_block *block)
{
   nir_cf_node *cf_node = block->cf_node.parent;

   while (cf_node != NULL) {
      if (cf_node->type == nir_cf_node_loop)
         return true;

      cf_node = cf_node->parent;
   }

   return false;
}

bool
brw_nir_fence_shared_stores(nir_shader *shader)
{
   bool progress = false;

   nir_foreach_function_with_impl(function, impl, shader) {
      bool multiple_unfenced_write_blocks = false;
      nir_block *unfenced_write_block = NULL;
      nir_foreach_block(block, impl) {
         bool unfenced_writes = false;
         nir_foreach_instr(instr, block) {
            if (instr->type != nir_instr_type_intrinsic)
               continue;

            nir_intrinsic_instr *intrin = nir_instr_as_intrinsic(instr);
            switch (intrin->intrinsic) {
            case nir_intrinsic_store_shared:
            case nir_intrinsic_shared_atomic:
            case nir_intrinsic_shared_atomic_swap:
            case nir_intrinsic_store_shared_block_intel:
               unfenced_writes = true;
               break;

            case nir_intrinsic_barrier:
               if (nir_intrinsic_memory_modes(intrin) & nir_var_mem_shared)
                  unfenced_writes = false;
               break;

            default:
               break;
            }
         }

         if (unfenced_writes) {
            /* Consider we have multiple blocks if the unfenced write is
             * within a loop.
             */
            multiple_unfenced_write_blocks =
               unfenced_write_block != NULL ||
               block_is_in_loop(block);
            unfenced_write_block = block;
         }
      }

      if (multiple_unfenced_write_blocks || unfenced_write_block) {
         nir_builder b = nir_builder_at(
            nir_after_block_before_jump(
               multiple_unfenced_write_blocks ?
               nir_impl_last_block(impl) :
               unfenced_write_block));
         nir_barrier(&b,
                     .execution_scope=SCOPE_NONE,
                     .memory_scope=SCOPE_WORKGROUP,
                     .memory_semantics = NIR_MEMORY_RELEASE,
                     .memory_modes = nir_var_mem_shared);
         progress |= nir_progress(true, impl, nir_metadata_control_flow);
      }
   }

   return progress;
}
