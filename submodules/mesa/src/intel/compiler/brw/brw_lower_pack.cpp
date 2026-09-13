/*
 * Copyright © 2015 Connor Abbott
 * SPDX-License-Identifier: MIT
 */

#include "util/half_float.h"
#include "brw_shader.h"
#include "brw_cfg.h"
#include "brw_builder.h"

bool
brw_lower_pack(brw_shader &s)
{
   bool progress = false;

   foreach_block_and_inst_safe(block, brw_inst, inst, s.cfg) {
      if (inst->opcode != FS_OPCODE_PACK &&
          inst->opcode != FS_OPCODE_PACK_HALF_2x16_SPLIT)
         continue;

      assert(inst->dst.file == VGRF);
      assert(inst->saturate == false);
      brw_reg dst = inst->dst;

      const brw_builder ibld(inst);
      /* The lowering generates 2 instructions for what was previously 1. This
       * can trick the IR to believe we're doing partial writes, but the
       * register is actually fully written. Mark it as undef to help the IR
       * reduce the liveness of the register.
       */
      if (!inst->is_partial_write())
         ibld.emit_undef_for_dst(inst);

      switch (inst->opcode) {
      case FS_OPCODE_PACK:
         for (unsigned i = 0; i < inst->sources; i++)
            ibld.MOV(subscript(dst, inst->src[i].type, i), inst->src[i]);
         break;
      case FS_OPCODE_PACK_HALF_2x16_SPLIT:
         assert(dst.type == BRW_TYPE_UD);

         for (unsigned i = 0; i < inst->sources; i++) {
            if (inst->src[i].file == IMM) {
               const uint32_t half = _mesa_float_to_half(inst->src[i].f);
               ibld.MOV(subscript(dst, BRW_TYPE_UW, i),
                        brw_imm_uw(half));
            } else {
               ibld.MOV(subscript(dst, BRW_TYPE_HF, i),
                        inst->src[i]);
            }
         }
         break;
      default:
         UNREACHABLE("skipped above");
      }

      inst->remove();
      progress = true;
   }

   if (progress)
      s.invalidate_analysis(BRW_DEPENDENCY_INSTRUCTIONS);

   return progress;
}
