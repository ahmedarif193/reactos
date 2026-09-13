/*
 * Copyright © 2015 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

/** @file elk_fs_validate.cpp
 *
 * Implements a pass that validates various invariants of the IR.  The current
 * pass only validates that GRF's uses are sane.  More can be added later.
 */

#include "elk_fs.h"
#include "elk_cfg.h"

#define fsv_assert(assertion)                                           \
   {                                                                    \
      if (!(assertion)) {                                               \
         fprintf(stderr, "ASSERT: Scalar %s validation failed!\n",      \
                 _mesa_shader_stage_to_abbrev(stage));                  \
         dump_instruction(inst, stderr);                                \
         fprintf(stderr, "%s:%d: '%s' failed\n", __FILE__, __LINE__, #assertion);  \
         abort();                                                       \
      }                                                                 \
   }

#define fsv_assert_eq(first, second)                                    \
   {                                                                    \
      unsigned f = (first);                                             \
      unsigned s = (second);                                            \
      if (f != s) {                                                     \
         fprintf(stderr, "ASSERT: Scalar %s validation failed!\n",      \
                 _mesa_shader_stage_to_abbrev(stage));                  \
         dump_instruction(inst, stderr);                                \
         fprintf(stderr, "%s:%d: A == B failed\n", __FILE__, __LINE__); \
         fprintf(stderr, "  A = %s = %u\n", #first, f);                 \
         fprintf(stderr, "  B = %s = %u\n", #second, s);                \
         abort();                                                       \
      }                                                                 \
   }

#define fsv_assert_ne(first, second)                                    \
   {                                                                    \
      unsigned f = (first);                                             \
      unsigned s = (second);                                            \
      if (f == s) {                                                     \
         fprintf(stderr, "ASSERT: Scalar %s validation failed!\n",      \
                 _mesa_shader_stage_to_abbrev(stage));                  \
         dump_instruction(inst, stderr);                                \
         fprintf(stderr, "%s:%d: A != B failed\n", __FILE__, __LINE__); \
         fprintf(stderr, "  A = %s = %u\n", #first, f);                 \
         fprintf(stderr, "  B = %s = %u\n", #second, s);                \
         abort();                                                       \
      }                                                                 \
   }

#define fsv_assert_lte(first, second)                                   \
   {                                                                    \
      unsigned f = (first);                                             \
      unsigned s = (second);                                            \
      if (f > s) {                                                      \
         fprintf(stderr, "ASSERT: Scalar %s validation failed!\n",      \
                 _mesa_shader_stage_to_abbrev(stage));                  \
         dump_instruction(inst, stderr);                                \
         fprintf(stderr, "%s:%d: A <= B failed\n", __FILE__, __LINE__); \
         fprintf(stderr, "  A = %s = %u\n", #first, f);                 \
         fprintf(stderr, "  B = %s = %u\n", #second, s);                \
         abort();                                                       \
      }                                                                 \
   }

#ifndef NDEBUG
void
elk_fs_visitor::validate()
{
   cfg->validate(_mesa_shader_stage_to_abbrev(stage));

   foreach_block_and_inst (block, elk_fs_inst, inst, cfg) {
      switch (inst->opcode) {
      case ELK_SHADER_OPCODE_SEND:
         fsv_assert(is_uniform(inst->src[0]));
         break;

      case ELK_OPCODE_MOV:
         fsv_assert(inst->sources == 1);
         break;

      default:
         break;
      }

      if (inst->elk_is_3src(compiler)) {
         const unsigned integer_sources =
            elk_reg_type_is_integer(inst->src[0].type) +
            elk_reg_type_is_integer(inst->src[1].type) +
            elk_reg_type_is_integer(inst->src[2].type);
         const unsigned float_sources =
            elk_reg_type_is_floating_point(inst->src[0].type) +
            elk_reg_type_is_floating_point(inst->src[1].type) +
            elk_reg_type_is_floating_point(inst->src[2].type);

         fsv_assert((integer_sources == 3 && float_sources == 0) ||
                    (integer_sources == 0 && float_sources == 3));

         if (grf_used != 0) {
            /* Only perform the pre-Gfx10 checks after register allocation has
             * occured.
             *
             * Many passes (e.g., constant copy propagation) will genenerate
             * invalid 3-source instructions with the expectation that later
             * passes (e.g., combine constants) will fix them.
             */
            for (unsigned i = 0; i < 3; i++) {
               fsv_assert_ne(inst->src[i].file, ELK_IMMEDIATE_VALUE);

               /* A stride of 1 (the usual case) or 0, with a special
                * "repctrl" bit, is allowed. The repctrl bit doesn't work for
                * 64-bit datatypes, so if the source type is 64-bit then only
                * a stride of 1 is allowed. From the Broadwell PRM, Volume 7
                * "3D Media GPGPU", page 944:
                *
                *    This is applicable to 32b datatypes and 16b datatype. 64b
                *    datatypes cannot use the replicate control.
                */
               fsv_assert_lte(inst->src[i].vstride, 1);

               if (type_sz(inst->src[i].type) > 4)
                  fsv_assert_eq(inst->src[i].vstride, 1);
            }
         }
      }

      if (inst->dst.file == VGRF) {
         fsv_assert_lte(inst->dst.offset / REG_SIZE + regs_written(inst),
                        alloc.sizes[inst->dst.nr]);
      }

      for (unsigned i = 0; i < inst->sources; i++) {
         if (inst->src[i].file == VGRF) {
            fsv_assert_lte(inst->src[i].offset / REG_SIZE + regs_read(inst, i),
                           alloc.sizes[inst->src[i].nr]);
         }
      }
   }
}
#endif
