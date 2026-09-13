/*
 * Copyright © 2014 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "brw_shader.h"
#include "brw_analysis.h"
#include "brw_cfg.h"

/** @file
 *
 * Dataflow-aware dead code elimination.
 *
 * Walks the instruction list from the bottom, removing instructions that
 * have results that both aren't used in later blocks and haven't been read
 * yet in the tail end of this block.
 */

/**
 * Is it safe to eliminate the instruction?
 */
static bool
can_eliminate(const intel_device_info *devinfo, const brw_inst *inst,
              BITSET_WORD *flag_live)
{
    return !inst->is_control_flow() &&
           !inst->has_side_effects() &&
           !(flag_live[0] & inst->flags_written(devinfo)) &&
           !inst->writes_accumulator;
}

/**
 * Is it safe to omit the write, making the destination ARF null?
 */
static bool
can_omit_write(const brw_compiler *compiler, const brw_inst *inst, bool late)
{
   switch (inst->opcode) {
   case SHADER_OPCODE_MEMORY_ATOMIC_LOGICAL:
      return true;
   default:
      /* Dead code elimination that occurs once after brw_lower_3src_null_dest
       * should not generate more NULL destinations.
       */
      if (inst->is_3src(compiler) && late)
         return false;

      /* We can eliminate the destination write for ordinary instructions,
       * but not most SENDs.
       */
      const brw_send_inst *send = inst->as_send();
      if (inst->opcode < NUM_BRW_OPCODES &&
          (!send || send->mlen == 0))
         return true;

      /* It might not be safe for other virtual opcodes. */
      return false;
   }
}

static bool
can_eliminate_conditional_mod(const intel_device_info *devinfo,
                              const brw_inst *inst, BITSET_WORD *flag_live)
{
   /* CMP, CMPN, and CSEL must have a conditional modifier because the
    * modifier determines what the instruction does. SEL with a conditional
    * modifier has a special meaning (i.e., makes the instruction behave as
    * MIN or MAX), so those cannot be eliminated either.
    */
   if (inst->conditional_mod == BRW_CONDITIONAL_NONE ||
       inst->opcode == BRW_OPCODE_CMP ||
       inst->opcode == BRW_OPCODE_CMPN ||
       inst->opcode == BRW_OPCODE_SEL ||
       inst->opcode == BRW_OPCODE_CSEL) {
      return false;
   }

   /* The conditional modifier can be eliminated if none of the flags written
    * are read.
    */
   const BITSET_WORD flags_written = inst->flags_written(devinfo);

   assert(flags_written != 0);
   return (flag_live[0] & flags_written) == 0;
}

bool
brw_opt_dead_code_eliminate(brw_shader &s)
{
   const intel_device_info *devinfo = s.devinfo;
   const bool late = s.phase >= BRW_SHADER_PHASE_AFTER_LATE_LOWERING;

   bool progress = false;

   const brw_live_variables &live_vars = s.live_analysis.require();
   int num_vars = live_vars.num_vars;
   BITSET_WORD *live = rzalloc_array(NULL, BITSET_WORD, BITSET_WORDS(num_vars));
   BITSET_WORD *flag_live = rzalloc_array(NULL, BITSET_WORD, 1);

   foreach_block_reverse(block, s.cfg) {
      memcpy(live, live_vars.block_data[block->num].liveout,
             sizeof(BITSET_WORD) * BITSET_WORDS(num_vars));
      memcpy(flag_live, live_vars.block_data[block->num].flag_liveout,
             sizeof(BITSET_WORD));

      foreach_inst_in_block_reverse_safe(brw_inst, inst, block) {
         if (inst->dst.file == VGRF) {
            const unsigned var = live_vars.var_from_reg(inst->dst);
            bool result_live = false;

            for (unsigned i = 0; i < regs_written(inst); i++)
               result_live |= BITSET_TEST(live, var + i);

            if (!result_live &&
                (can_omit_write(s.compiler, inst, late) ||
                 can_eliminate(devinfo, inst, flag_live))) {
               inst->dst = brw_reg(spread(retype(brw_null_reg(), inst->dst.type),
                                          inst->dst.stride));
               progress = true;
            }
         }

         if (can_eliminate_conditional_mod(devinfo, inst, flag_live))
            inst->conditional_mod = BRW_CONDITIONAL_NONE;

         if (inst->dst.is_null() && can_eliminate(devinfo, inst, flag_live) &&
             !(inst->opcode == BRW_OPCODE_NOP &&
               brw_exec_list_is_singular(&block->instructions))) {
            inst = brw_transform_inst(s, inst, BRW_OPCODE_NOP);
            progress = true;
         }

         if (inst->dst.file == VGRF) {
            if (!inst->is_partial_write()) {
               const unsigned var = live_vars.var_from_reg(inst->dst);
               for (unsigned i = 0; i < regs_written(inst); i++) {
                  BITSET_CLEAR(live, var + i);
               }
            }
         }

         if (!inst->predicate && inst->exec_size >= 8)
            flag_live[0] &= ~inst->flags_written(devinfo);

         if (inst->opcode == BRW_OPCODE_NOP) {
            inst->remove();
            continue;
         }

         for (int i = 0; i < inst->sources; i++) {
            if (inst->src[i].file == VGRF) {
               int var = live_vars.var_from_reg(inst->src[i]);

               for (unsigned j = 0; j < regs_read(devinfo, inst, i); j++) {
                  BITSET_SET(live, var + j);
               }
            }
         }

         flag_live[0] |= inst->flags_read(devinfo);
      }
   }

   ralloc_free(live);
   ralloc_free(flag_live);

   if (progress)
      s.invalidate_analysis(BRW_DEPENDENCY_INSTRUCTIONS);

   return progress;
}
