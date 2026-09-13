/*
 * Copyright © 2026 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "compiler/nir/nir_builder.h"

#include "brw_nir.h"

/**
 * This pass tries to opportunistically optimize atomic operations with
 * divergent addresses/offsets by disabling lanes that will have the same
 * address/offset as the first active lane, letting the first lane do the work
 * instead.
 *
 * Non-returning atomic:
 *
 * atomic(addr, data)
 * ->
 *    first_lane_addr = read_first_invocation(addr)
 *    should_do_atomic = lane_id == first_lane_id || first_lane_addr != addr
 *    if (first_lane_addr == addr) {
 *       reduction = inclusive_scan(data, reduction_op)
 *       fused_data = read_last_invocation(reduction)
 *    }
 *    atomic_data = phi(fused_data, data)
 *    if (should_do_atomic)
 *       atomic(addr, atomic_data)
 *
 * Returning atomic:
 *
 * x = atomic(addr, data)
 * ->
 *    first_lane_addr = read_first_invocation(addr)
 *    should_do_atomic = lane_id == first_lane_id || first_lane_addr != addr
 *    empty_data = undef
 *    if (first_lane_addr == addr) {
 *       reduction = inclusive_scan(data, reduction_op)
 *       fused_data = read_last_invocation(reduction)
 *    }
 *    atomic_data = phi(fused_data, data)
 *    fused_data_per_lane = phi(reduction, empty_data)
 *    if (should_do_atomic)
 *       x' = atomic(addr, atomic_data)
 *    atomic_result = phi(x', empty_data)
 *    if (firstLane_addr == addr) {
 *       first_lane_result = read_first_invocation(reduction)
 *       fixed_result = first_lane_result + fused_data_per_lane - data
 *    }
 *    x = phi(fixed_result, atomic_result)
 */

static bool
supported_atomic_reduction(nir_atomic_op op)
{
   switch (op) {
   case nir_atomic_op_iadd:
   case nir_atomic_op_imin:
   case nir_atomic_op_umin:
   case nir_atomic_op_imax:
   case nir_atomic_op_umax:
   case nir_atomic_op_iand:
   case nir_atomic_op_ior:
   case nir_atomic_op_ixor:
   case nir_atomic_op_fmin:
   case nir_atomic_op_fmax:
      return true;

   default:
      return false;
   }
}

static void
opt_divergent_atomic_single_message(nir_builder *b, nir_intrinsic_instr *intrin)
{
   bool is_result_used = !nir_def_is_unused(&intrin->def);

   nir_def *addr = nir_get_io_offset_src(intrin)->ssa;
   nir_def *data = nir_get_io_data_src(intrin)->ssa;

   nir_op reduction_op = nir_atomic_op_to_alu(nir_intrinsic_atomic_op(intrin));

   /* Only iadd op is supported if a result is used */
   assert(!is_result_used || reduction_op == nir_op_iadd);

   b->cursor = nir_after_instr(&intrin->instr);

   nir_def *lane_id = nir_load_subgroup_invocation(b);

   nir_def *first_lane_id = nir_read_first_invocation(b, lane_id);
   nir_def *first_lane_addr = nir_read_first_invocation(b, addr);
   nir_def *is_first_lane = nir_ieq(b, lane_id, first_lane_id);
   nir_def *fusing_cond = nir_ball_iequal(b, first_lane_addr, addr);
   nir_def *should_do_atomic = nir_ior(b,
                                       is_first_lane,
                                       nir_inot(b, fusing_cond));

   nir_def *undef_phi_value = nir_undef(b, 1, intrin->def.bit_size);

   nir_def *fused_data, *reduction;
   nir_push_if(b, fusing_cond);
   {
      /* each lane has a value of 1+2+3+data */
      reduction = nir_inclusive_scan(b, data, .reduction_op = reduction_op);
      /* last lane data in each lane (esp in first) */
      fused_data = nir_read_invocation(b, reduction, nir_last_invocation(b));
   }
   nir_pop_if(b, NULL);

   /* Put the fused data into the first lane */
   nir_def *atomic_data = nir_if_phi(b, fused_data, data);

   nir_def *fused_data_per_lane = is_result_used ? nir_if_phi(b, reduction, undef_phi_value) : NULL;

   nir_intrinsic_instr *atomic_clone;
   nir_push_if(b, should_do_atomic);
   {
      atomic_clone = nir_instr_as_intrinsic(nir_instr_clone(b->shader, &intrin->instr));
      nir_instr_insert(b->cursor, &atomic_clone->instr);
      nir_src_rewrite(nir_get_io_data_src(atomic_clone), atomic_data);
   }
   nir_pop_if(b, NULL);

   if (is_result_used) {
      nir_def *atomic_result = nir_if_phi(b, &atomic_clone->def, undef_phi_value);

      nir_def *fixed_result;
      nir_push_if(b, fusing_cond);
      {
         nir_def *first_lane_result = nir_read_invocation(b, atomic_result, first_lane_id);
         fixed_result = nir_iadd(b, first_lane_result, nir_isub(b, fused_data_per_lane, data));
      }
      nir_pop_if(b, NULL);

      nir_def *fixed_result_phi = nir_if_phi(b, fixed_result, atomic_result);
      fixed_result_phi->divergent = intrin->def.divergent;
      nir_def_rewrite_uses(&intrin->def, fixed_result_phi);
   }

   nir_instr_remove(&intrin->instr);
}

static bool
opt_divergent_atomic_instr(nir_builder *b, nir_intrinsic_instr *intrin, void *cb_data)
{
   enum brw_divergent_atomics_flags flags =
      *((enum brw_divergent_atomics_flags *)cb_data);

   switch (intrin->intrinsic) {
   case nir_intrinsic_ssbo_atomic:
   case nir_intrinsic_shared_atomic:
   case nir_intrinsic_global_atomic:
      if (flags & BRW_OPT_DIVERGENT_ATOMICS_BUFFER)
         break;
      return false;
   case nir_intrinsic_image_atomic:
   case nir_intrinsic_bindless_image_atomic:
      if (flags & BRW_OPT_DIVERGENT_ATOMICS_IMAGE)
         break;
      return false;
   default:
      return false;
   }

   /* The address/offset should be non-uniform */
   if (!nir_src_is_divergent(nir_get_io_offset_src(intrin)))
      return false;

   /* For iadd op partial returning values can be restored, otherwise only
    * handle the intrinsic if it's return value is unused.
    */
   nir_atomic_op atomic_op = nir_intrinsic_atomic_op(intrin);
   if ((atomic_op == nir_atomic_op_iadd) ||
       (nir_def_is_unused(&intrin->def) && supported_atomic_reduction(atomic_op)))
   {
      opt_divergent_atomic_single_message(b, intrin);
      return true;
   }

   return false;
}

bool brw_nir_opt_divergent_atomics(nir_shader *shader,
                                   enum brw_divergent_atomics_flags flags)
{
   nir_foreach_function_impl(impl, shader) {
      nir_metadata_require(impl, nir_metadata_block_index | nir_metadata_divergence);
   }

   return nir_shader_intrinsics_pass(shader, opt_divergent_atomic_instr,
                                     nir_metadata_none, &flags);
}
