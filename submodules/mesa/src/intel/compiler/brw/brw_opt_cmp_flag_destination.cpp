/*
 * Copyright © 2022 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

/** \file
 * Change the flags written by some CMP and CMPN instructions.
 *
 * One frustrating thing about the CMP and CMPN instructions is that they
 * always write the flags. Sometimes, however, it is desirable to generate the
 * comparison result without modifying the flags. This would, theoretically,
 * reduce false dependencies that restrict the scheduler's ability to
 * rearrange code, create more opportunities for cmod propagation, save a
 * kitten from a tree, and make a rainbow.
 *
 * Consider this sequence:
 *
 *            cmp.ge.f0.0(8)  g103<1>F        g101<8,8,1>F    g39<8,8,1>F
 *            cmp.nz.f0.0(8)  null<1>D        g81<8,8,1>D     0D
 *    (+f0.0) if(8)   JIP:  LABEL19         UIP:  LABEL19
 *
 * It would be advantageous to put the first CMP between the second CMP and
 * the IF, but this cannot be done since the IF depends on the flags generated
 * by the second CMP.
 *
 * This pass enables this rescheduling by changing the first CMP to write to a
 * different flags register.
 *
 *            cmp.ge.f1.0(8)  g103<1>F        g101<8,8,1>F    g39<8,8,1>F
 *            cmp.nz.f0.0(8)  null<1>D        g81<8,8,1>D     0D
 *    (+f0.0) if(8)   JIP:  LABEL19         UIP:  LABEL19
 *
 * Sometimes this is also possible by using a different instruction.  For
 * example, consider
 *
 *            cmp.l.f0.0(8)   g103<1>D        g101<8,8,1>D    0D
 *
 * This produces 0xffffffff when g101 negative and zero otherwise. This
 * instruction, which does not modifiy the flag, also produces these results:
 *
 *            asr(8)          g103<1>D        g101<8,8,1>D    31D
 */
#include "brw_shader.h"
#include "brw_cfg.h"

static bool
opt_cmp_flag_destination_local(brw_shader &s, bblock_t *block,
                               bool uses_kill)
{
   bool progress = false;

   foreach_inst_in_block(brw_inst, inst, block) {
      if (inst->opcode != BRW_OPCODE_CMP && inst->opcode != BRW_OPCODE_CMPN)
         continue;

      if (inst->dst.is_null() || inst->predicate != BRW_PREDICATE_NONE)
         continue;

      if ((s.dispatch_width <= 16 && inst->flag_subreg == 1) ||
          inst->flag_subreg > 1) {
         continue;
      }

      unsigned flags_written = inst->flags_written(s.devinfo);

      foreach_inst_in_block_starting_from(brw_inst, scan_inst, inst) {
         if ((scan_inst->flags_read(s.devinfo) & flags_written) != 0)
            break;

         /* If scan_inst might not modify the flags, keep scanning. */
         if (scan_inst->predicate != BRW_PREDICATE_NONE)
            continue;

         flags_written &= ~scan_inst->flags_written(s.devinfo);
         if (flags_written == 0) {
            /* A common special case:
             *
             *    cmp.l.f0.0(8)   g103<1>D        g101<8,8,1>D    0D
             *
             * becomes
             *
             *    asr(8)          g103<1>D        g101<8,8,1>D    31D
             */
            if (inst->opcode == BRW_OPCODE_CMP &&
                brw_type_is_sint(inst->src[0].type) &&
                inst->src[1].is_zero() &&
                inst->conditional_mod == BRW_CONDITIONAL_L) {
               inst->opcode = BRW_OPCODE_ASR;
               inst->conditional_mod = BRW_CONDITIONAL_NONE;

               inst->src[1] =
                  brw_imm_w(brw_type_size_bits(inst->src[0].type) - 1);

               progress = true;
            }

            /* A common special case:
             *
             *    cmp.nz.f0.0(8)  g103<1>D    g101<8,8,1>D         0D
             *
             * or
             *
             *    cmp.nz.f0.0(8)  g103<1>UD   g101<8,8,1>UD        0UD
             *
             * becomes
             *
             *    asr(8)          g103<1>D    -(abs)g101<8,8,1>D   31D
             */
            if (inst->opcode == BRW_OPCODE_CMP &&
                brw_type_is_int(inst->src[0].type) &&
                inst->src[1].is_zero() &&
                inst->conditional_mod == BRW_CONDITIONAL_NZ) {
               inst->opcode = BRW_OPCODE_ASR;
               inst->conditional_mod = BRW_CONDITIONAL_NONE;

               inst->dst.type =
                  brw_type_with_size(BRW_TYPE_D,
                                     brw_type_size_bits(inst->dst.type));

               unsigned bits = brw_type_size_bits(inst->src[0].type);

               inst->src[0].type = brw_type_with_size(BRW_TYPE_D, bits);
               inst->src[0].negate = true;
               inst->src[0].abs = true;

               inst->src[1] = brw_imm_w(bits - 1);

               progress = true;
            }

            if (inst->conditional_mod != BRW_CONDITIONAL_NONE) {
               if (s.dispatch_width <= 16) {
                  inst->flag_subreg += 1;
                  progress = true;
               } else if (s.devinfo->ver >= 20) {
                  /* Xe2 (and a couple exotic Xe platforms) have 4 32-bit
                   * flags registers. Use f2.x.
                   */
                  inst->flag_subreg += 4;
                  progress = true;
               } else if (!uses_kill) {
                  inst->flag_subreg += 2;
                  progress = true;
               }

               assert(s.stage != MESA_SHADER_FRAGMENT ||
                      inst->flag_subreg != sample_mask_flag_subreg(s));
            }

            break;
         }
      }
   }

   return progress;
}

bool
brw_opt_cmp_flag_destination(brw_shader &s, bool uses_kill)
{
   bool progress = false;

   foreach_block (block, s.cfg) {
      if (opt_cmp_flag_destination_local(s, block, uses_kill))
         progress = true;
   }

   return progress;
}
