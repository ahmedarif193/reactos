/*
 * Copyright © 2020 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

/* These passes enable converting uniforms to literals when it's profitable,
 * effectively inlining uniform values in the IR. The main benefit is register
 * usage decrease leading to better SMT (hyperthreading). It's accomplished
 * by targetting uniforms that determine whether a conditional branch is
 * taken or a loop can be unrolled.
 *
 * Only uniforms used in these places are analyzed:
 *   1. if condition
 *   2. loop terminator
 *   3. init and update value of induction variable used in loop terminator
 *
 * nir_find_inlinable_uniforms finds uniforms that can be inlined and stores
 * that information in shader_info.
 *
 * nir_inline_uniforms inlines uniform values.
 *
 * (uniforms must be lowered to load_ubo before calling this)
 */

#include "util/hash_table.h"
#include "nir_builder.h"
#include "nir_loop_analyze.h"

/* Maximum value in shader_info::inlinable_uniform_dw_offsets[] */
#define MAX_OFFSET (UINT16_MAX * 4)

#define MAX_NUM_BO 32

/* Flags used in the instr->pass_flags field for previously visted srcs */
enum {
   INSTR_UNVISITED = 0,
   INSTR_SRCS_INLINABLE = (1 << 0),
   INSTR_SRCS_NOT_INLINABLE = (1 << 1),
};

static uint8_t
get_instr_flag(nir_instr *instr, unsigned component)
{
   /* We can't store flags for more than 4 components. */
   if (component > 4)
      return INSTR_UNVISITED;

   /* pass_flags store 2 bits of state per component. */
   return (instr->pass_flags >> (component * 2)) & 0x3;
}

static void
set_instr_flag(nir_instr *instr, unsigned component, uint8_t value)
{
   /* We can't store flags for more than 4 components. */
   if (component > 4)
      return;

   /* pass_flags store 2 bits of state per component. */
   instr->pass_flags =
      (instr->pass_flags & ~BITFIELD_RANGE(component * 2, 2)) |
      ((value & 0x3) << (component * 2));
}

/**
 * Collect uniforms used in a source
 *
 * Recursively collects all of the UBO loads with constant UBO index and
 * constant offset (per the restrictions of \c max_num_bo and \c
 * max_offset). If any values are discovered that are non-constant, uniforms
 * that don't meet the restrictions, or if more than \c
 * MAX_INLINEABLE_UNIFORMS are discoverd for any one UBO, false is returned.
 *
 * When false is returned, the state of \c uni_offsets and \c num_uniforms is
 * undefined.
 *
 * \param max_num_bo Maximum number of uniform buffer objects
 * \param max_offset Maximum offset within a UBO
 * \param uni_offset Array of \c max_num_bo * \c MAX_INLINABLE_UNIFORMS values
 *                   used to store offsets of discovered uniform loads.
 * \param num_uniforms Array of \c max_num_bo values used to store the number
 *                     of uniforms collected from each UBO.
 */
static bool
collect_src_uniforms(const nir_src *src, int component,
                     uint32_t *uni_offsets, uint8_t *num_uniforms,
                     unsigned max_num_bo, unsigned max_offset)
{
   assert(max_num_bo > 0 && max_num_bo <= MAX_NUM_BO);
   assert(component < src->ssa->num_components);

   nir_instr *instr = nir_def_instr(src->ssa);

   /* If we have already processed this src before skip it */
   if (get_instr_flag(instr, component) != INSTR_UNVISITED) {
      if (get_instr_flag(instr, component) & INSTR_SRCS_INLINABLE)
         return true;
      else
         return false;
   }

   switch (instr->type) {
   case nir_instr_type_alu: {
      nir_alu_instr *alu = nir_instr_as_alu(instr);

      /* Vector ops only need to check the corresponding component. */
      if (alu->op == nir_op_mov) {
         bool inlinable = collect_src_uniforms(&alu->src[0].src,
                                               alu->src[0].swizzle[component],
                                               uni_offsets, num_uniforms,
                                               max_num_bo, max_offset);
         set_instr_flag(instr, component,
                        inlinable ? INSTR_SRCS_INLINABLE : INSTR_SRCS_NOT_INLINABLE);
         return inlinable;
      } else if (nir_op_is_vec(alu->op)) {
         nir_alu_src *alu_src = alu->src + component;
         bool inlinable = collect_src_uniforms(&alu_src->src, alu_src->swizzle[0],
                                               uni_offsets, num_uniforms,
                                               max_num_bo, max_offset);

         set_instr_flag(instr, component,
                        inlinable ? INSTR_SRCS_INLINABLE : INSTR_SRCS_NOT_INLINABLE);
         return inlinable;
      }

      /* Return true if all sources return true. */
      for (unsigned i = 0; i < nir_op_infos[alu->op].num_inputs; i++) {
         nir_alu_src *alu_src = alu->src + i;
         int input_sizes = nir_op_infos[alu->op].input_sizes[i];

         if (input_sizes == 0) {
            /* For ops which has no input size, each component of dest is
             * only determined by the same component of srcs.
             */
            if (!collect_src_uniforms(&alu_src->src, alu_src->swizzle[component],
                                      uni_offsets, num_uniforms,
                                      max_num_bo, max_offset)) {
               set_instr_flag(instr, component, INSTR_SRCS_NOT_INLINABLE);
               return false;
            }
         } else {
            /* For ops which has input size, all components of dest are
             * determined by all components of srcs (except vec ops).
             */
            for (unsigned j = 0; j < input_sizes; j++) {
               if (!collect_src_uniforms(&alu_src->src, alu_src->swizzle[j],
                                         uni_offsets, num_uniforms,
                                         max_num_bo, max_offset)) {
                  set_instr_flag(instr, component, INSTR_SRCS_NOT_INLINABLE);
                  return false;
               }
            }
         }
      }

      set_instr_flag(instr, component, INSTR_SRCS_INLINABLE);
      return true;
   }

   case nir_instr_type_intrinsic: {
      nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
      /* Return true if the intrinsic loads from UBO 0 with a constant
       * offset.
       */
      if (intr->intrinsic == nir_intrinsic_load_ubo &&
          nir_src_is_const(intr->src[0]) && nir_src_num_components(intr->src[0]) == 1 &&
          nir_src_as_uint(intr->src[0]) < max_num_bo &&
          nir_src_is_const(intr->src[1]) &&
          nir_src_as_uint(intr->src[1]) <= max_offset &&
          /* TODO: Can't handle other bit sizes for now. */
          intr->def.bit_size == 32) {
         assert((num_uniforms != NULL) && (uni_offsets != NULL));

         uint32_t offset = nir_src_as_uint(intr->src[1]) + component * 4;
         assert(offset < MAX_OFFSET);

         const unsigned ubo = nir_src_as_uint(intr->src[0]);

         /* Already recorded by other one */
         for (int i = 0; i < num_uniforms[ubo]; i++) {
            if (uni_offsets[ubo * MAX_INLINABLE_UNIFORMS + i] == offset) {
               set_instr_flag(instr, component, INSTR_SRCS_INLINABLE);
               return true;
            }
         }

         /* Exceed uniform number limit */
         if (num_uniforms[ubo] == MAX_INLINABLE_UNIFORMS) {
            set_instr_flag(instr, component, INSTR_SRCS_NOT_INLINABLE);
            return false;
         }

         /* Record the uniform offset. */
         uni_offsets[ubo * MAX_INLINABLE_UNIFORMS + num_uniforms[ubo]++] = offset;
         set_instr_flag(instr, component, INSTR_SRCS_INLINABLE);
         return true;
      }

      set_instr_flag(instr, component, INSTR_SRCS_NOT_INLINABLE);
      return false;
   }

   case nir_instr_type_load_const:
      /* Always return true for constants. */
      set_instr_flag(instr, component, INSTR_SRCS_INLINABLE);
      return true;

   default:
      set_instr_flag(instr, component, INSTR_SRCS_NOT_INLINABLE);
      return false;
   }
}

static bool
is_induction_variable(const nir_src *src, int component, nir_loop_info *info,
                      uint32_t *uni_offsets, uint8_t *num_uniforms,
                      unsigned max_num_bo, unsigned max_offset)
{
   assert(component < src->ssa->num_components);

   /* Return true for induction variable (ie. i in for loop) */
   struct hash_entry *entry = _mesa_hash_table_search(info->induction_vars, src->ssa);
   if (!entry)
      return false;

   nir_loop_induction_variable *var = entry->data;
   /* Induction variable should have constant initial value (ie. i = 0),
    * constant update value (ie. i++) and constant end condition
    * (ie. i < 10), so that we know the exact loop count for unrolling
    * the loop.
    *
    * Add uniforms need to be inlined for this induction variable's
    * initial and update value to be constant, for example:
    *
    *     for (i = init; i < count; i += step)
    *
    * We collect uniform "init" and "step" here.
    */
   if (var->init_src) {
      if (!collect_src_uniforms(var->init_src, component,
                                uni_offsets, num_uniforms,
                                max_num_bo, max_offset))
         return false;
   }

   if (var->update_src) {
      nir_alu_src *alu_src = var->update_src;
      if (!collect_src_uniforms(&alu_src->src,
                                alu_src->swizzle[component],
                                uni_offsets, num_uniforms,
                                max_num_bo, max_offset))
         return false;
   }

   return true;
}

static void
add_inlinable_uniforms(const nir_src *cond, nir_loop_info *info,
                       uint32_t *uni_offsets, uint8_t *num_uniforms,
                       unsigned max_num_bo, unsigned max_offset)
{
   uint8_t new_num_uniforms[MAX_NUM_BO];
   memcpy(new_num_uniforms, num_uniforms, sizeof(new_num_uniforms));

   /* If condition SSA is always scalar, so component is 0. */
   unsigned component = 0;

   /* Allow induction variable which means a loop terminator. */
   if (info) {
      nir_scalar cond_scalar = { cond->ssa, 0 };

      /* Limit terminator condition to loop unroll support case which is a simple
       * comparison (ie. "i < count" is supported, but "i + 1 < count" is not).
       */
      if (nir_is_supported_terminator_condition(cond_scalar)) {
         if (nir_scalar_alu_op(cond_scalar) == nir_op_inot)
            cond_scalar = nir_scalar_chase_alu_src(cond_scalar, 0);

         nir_alu_instr *alu = nir_def_as_alu(cond_scalar.def);

         /* One side of comparison is induction variable, the other side is
          * only uniform.
          */
         for (int i = 0; i < 2; i++) {
            if (is_induction_variable(&alu->src[i].src, alu->src[i].swizzle[0],
                                      info, uni_offsets, new_num_uniforms,
                                      max_num_bo, max_offset)) {
               cond = &alu->src[1 - i].src;
               component = alu->src[1 - i].swizzle[0];
               break;
            }
         }
      }
   }

   /* Only update the number of uniforms if the whole expression becomes
    * a constant expression after inlining and the number of uniforms is small
    * enough that it fits within the unused elements of uni_offset, which is:
    *    MAX_INLINABLE_UNIFORMS - num_uniforms[ubo]
    *
    * The uniforms added by is_induction_variable are also not added if this
    * isn't successful.
    */
   if (collect_src_uniforms(cond, component, uni_offsets, new_num_uniforms,
                            max_num_bo, max_offset))
      memcpy(num_uniforms, new_num_uniforms,
             sizeof(new_num_uniforms[0]) * max_num_bo);
}

static void
process_node(nir_cf_node *node, nir_loop_info *info,
             uint32_t *uni_offsets, uint8_t *num_uniforms)
{
   switch (node->type) {
   case nir_cf_node_if: {
      nir_if *if_node = nir_cf_node_as_if(node);
      const nir_src *cond = &if_node->condition;
      add_inlinable_uniforms(cond, info, uni_offsets, num_uniforms, 1, MAX_OFFSET);

      /* Do not pass loop info down so only alow induction variable
       * in loop terminator "if":
       *
       *     for (i = 0; true; i++)
       *         if (i == count)
       *             if (i == num)
       *                 <no break>
       *             break
       *
       * so "num" won't be inlined due to the "if" is not a
       * terminator.
       */
      info = NULL;

      foreach_list_typed(nir_cf_node, nested_node, node, &if_node->then_list)
         process_node(nested_node, info, uni_offsets, num_uniforms);
      foreach_list_typed(nir_cf_node, nested_node, node, &if_node->else_list)
         process_node(nested_node, info, uni_offsets, num_uniforms);
      break;
   }

   case nir_cf_node_loop: {
      nir_loop *loop = nir_cf_node_as_loop(node);
      assert(!nir_loop_has_continue_construct(loop));

      /* Replace loop info, no nested loop info currently:
       *
       *     for (i = 0; i < count0; i++)
       *         for (j = 0; j < count1; j++)
       *             if (i == num)
       *
       * so "num" won't be inlined due to "i" is an induction
       * variable of upper loop.
       */
      info = loop->info;

      foreach_list_typed(nir_cf_node, nested_node, node, &loop->body) {
         bool is_terminator = false;
         list_for_each_entry(nir_loop_terminator, terminator,
                             &info->loop_terminator_list,
                             loop_terminator_link) {
            if (nested_node == &terminator->nif->cf_node) {
               is_terminator = true;
               break;
            }
         }

         /* Allow induction variables for terminator "if" only:
          *
          *     for (i = 0; i < count; i++)
          *         if (i == num)
          *             <no break>
          *
          * so "num" won't be inlined due to the "if" is not a
          * terminator.
          */
         nir_loop_info *use_info = is_terminator ? info : NULL;
         process_node(nested_node, use_info, uni_offsets, num_uniforms);
      }
      break;
   }

   default:
      break;
   }
}

void
nir_find_inlinable_uniforms(nir_shader *shader)
{
   uint32_t uni_offsets[MAX_INLINABLE_UNIFORMS];
   uint8_t num_uniforms[MAX_NUM_BO] = { 0 }; /* per UBO */

   nir_shader_clear_pass_flags(shader);

   nir_foreach_function_impl(impl, shader) {
      nir_metadata_require(impl, nir_metadata_loop_analysis,
                           nir_var_all, false);

      foreach_list_typed(nir_cf_node, node, node, &impl->body)
         process_node(node, NULL, uni_offsets, num_uniforms);
   }

   for (int i = 0; i < num_uniforms[0]; i++)
      shader->info.inlinable_uniform_dw_offsets[i] = uni_offsets[i] / 4;
   shader->info.num_inlinable_uniforms = num_uniforms[0];
}

typedef struct {
   unsigned num_uniforms;
   const uint32_t *uniform_values;
   const uint16_t *uniform_dw_offsets;
} nir_inline_uniforms_state;

static bool
nir_inline_uniforms_impl(nir_builder *b, nir_intrinsic_instr *intr, void *state)
{
   const nir_inline_uniforms_state *s = state;

   /* Only replace UBO 0 with constant offsets. */
   if (intr->intrinsic == nir_intrinsic_load_ubo &&
       nir_src_is_const(intr->src[0]) &&
       nir_src_as_uint(intr->src[0]) == 0 &&
       nir_src_is_const(intr->src[1]) &&
       /* TODO: Can't handle other bit sizes for now. */
       intr->def.bit_size == 32) {
      int num_components = intr->def.num_components;
      uint32_t offset = nir_src_as_uint(intr->src[1]) / 4;

      if (num_components == 1) {
         /* Just replace the uniform load to constant load. */
         for (unsigned i = 0; i < s->num_uniforms; i++) {
            if (offset == s->uniform_dw_offsets[i]) {
               b->cursor = nir_before_instr(&intr->instr);
               nir_def *def = nir_imm_int(b, s->uniform_values[i]);
               nir_def_replace(&intr->def, def);
               return true;
            }
         }
      } else {
         /* Lower vector uniform load to scalar and replace each
          * found component load with constant load.
          */
         uint32_t max_offset = offset + num_components;
         nir_def *components[NIR_MAX_VEC_COMPONENTS] = { 0 };
         bool found = false;

         b->cursor = nir_before_instr(&intr->instr);

         /* Find component to replace. */
         for (unsigned i = 0; i < s->num_uniforms; i++) {
            uint32_t uni_offset = s->uniform_dw_offsets[i];
            if (uni_offset >= offset && uni_offset < max_offset) {
               int index = uni_offset - offset;
               components[index] = nir_imm_int(b, s->uniform_values[i]);
               found = true;
            }
         }

         if (!found)
            return false;

         /* Create per-component uniform load. */
         for (unsigned i = 0; i < num_components; i++) {
            if (!components[i]) {
               uint32_t scalar_offset = (offset + i) * 4;
               components[i] = nir_load_ubo(b, 1, intr->def.bit_size,
                                            intr->src[0].ssa,
                                            nir_imm_int(b, scalar_offset));
               nir_intrinsic_instr *load =
                  nir_def_as_intrinsic(components[i]);
               nir_intrinsic_set_align(load, NIR_ALIGN_MUL_MAX, scalar_offset);
               nir_intrinsic_set_range_base(load, scalar_offset);
               nir_intrinsic_set_range(load, 4);
            }
         }

         /* Replace the original uniform load. */
         nir_def_replace(&intr->def,
                         nir_vec(b, components, num_components));

         return true;
      }
   }

   return false;
}

bool
nir_inline_uniforms(nir_shader *shader, unsigned num_uniforms,
                    const uint32_t *uniform_values,
                    const uint16_t *uniform_dw_offsets)
{
   if (!num_uniforms)
      return false;

   nir_inline_uniforms_state state = {
      .num_uniforms = num_uniforms,
      .uniform_values = uniform_values,
      .uniform_dw_offsets = uniform_dw_offsets,
   };

   return nir_shader_intrinsics_pass(shader, nir_inline_uniforms_impl,
                                     nir_metadata_control_flow, &state);
}
