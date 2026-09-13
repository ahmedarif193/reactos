/* Copyright © 2026 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "anv_private.h"
#include "anv_nir.h"

#include "nir/nir_builder.h"

/**
 * This file implements an analysis pass to detect shaders we assume are
 * clearing/initializing some memory.
 *
 * The criteria for such shader is that all memory store operations are
 * writing constant values.
 */

struct clear_state {
   uint32_t n_image_store;
   uint32_t n_image_store_const;

   uint32_t n_ssbo_store;
   uint32_t n_ssbo_store_const;

   uint32_t n_global_store;
   uint32_t n_global_store_const;
};

static bool
intrin_analysis(nir_builder *b, nir_intrinsic_instr *intrin, void *data)
{
   struct clear_state *state = data;
   switch (intrin->intrinsic) {
   case nir_intrinsic_image_store:
   case nir_intrinsic_bindless_image_store:
      state->n_image_store++;
      if (nir_src_is_const(intrin->src[nir_get_io_data_src_number(intrin)]))
         state->n_image_store_const++;
      return true;

   case nir_intrinsic_image_atomic:
   case nir_intrinsic_image_atomic_swap:
   case nir_intrinsic_bindless_image_atomic:
   case nir_intrinsic_bindless_image_atomic_swap:
      state->n_image_store++;
      return true;

   case nir_intrinsic_store_ssbo:
      state->n_ssbo_store++;
      if (nir_src_is_const(intrin->src[nir_get_io_data_src_number(intrin)]))
         state->n_ssbo_store_const++;
      return true;

   case nir_intrinsic_ssbo_atomic:
      state->n_ssbo_store++;
      return true;

   case nir_intrinsic_store_global:
      state->n_global_store++;
      if (nir_src_is_const(intrin->src[nir_get_io_data_src_number(intrin)]))
         state->n_global_store_const++;
      return true;

   case nir_intrinsic_global_atomic:
      state->n_global_store++;
      return true;

   default:
      return false;
   }
}

enum anv_pipeline_behavior
anv_nir_clear_shader_analysis(nir_shader *shader)
{
   struct clear_state state = {};

   nir_shader_intrinsics_pass(shader, intrin_analysis, nir_metadata_all, &state);

   /* If something doesn't write a constant, assume no behavior. */
   if (state.n_image_store != state.n_image_store_const ||
       state.n_ssbo_store != state.n_ssbo_store_const ||
       state.n_global_store != state.n_global_store_const)
      return 0;

   enum anv_pipeline_behavior behavior = 0;
   if (state.n_image_store > 0 &&
       state.n_image_store == state.n_image_store_const)
      behavior |= ANV_PIPELINE_BEHAVIOR_CLEAR_TYPED;
   if (state.n_ssbo_store > 0 &&
       state.n_ssbo_store == state.n_ssbo_store_const)
      behavior |= ANV_PIPELINE_BEHAVIOR_CLEAR_UNTYPED;
   if (state.n_global_store > 0 &&
       state.n_global_store == state.n_global_store_const)
      behavior |= ANV_PIPELINE_BEHAVIOR_CLEAR_UNTYPED;

   return behavior;
}
