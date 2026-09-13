/*
 * Copyright © 2026 Raspberry Pi Ltd
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

/**
 * @file v3d_opt_alu.c
 *
 * Identifies suboptimal ALU patterns and transforms them into optimal code
 * instead (for cases where NIR won't do what we want).
 */

#include "v3d_compiler.h"

static bool
has_unpack_l(struct qinst *inst, int chan)
{
        assert(chan == 0 || chan == 1);
        if (vir_is_add(inst)) {
                if (chan == 0)
                        return inst->qpu.alu.add.a.unpack == V3D_QPU_UNPACK_L;
                else
                        return inst->qpu.alu.add.b.unpack == V3D_QPU_UNPACK_L;
        } else {
                if (chan == 0)
                        return inst->qpu.alu.mul.a.unpack == V3D_QPU_UNPACK_L;
                else
                        return inst->qpu.alu.mul.b.unpack == V3D_QPU_UNPACK_L;
        }
}


/* Detect fmov(shr(x, 16).l) and convert it to fmov(x.h).
 *
 * This pattern can be produced by NIR for f16 extracts since it replaced
 * unpack_half_2x16_split_{xy} with f2f32:
 *
 *   32x4   %5 = (float32)txl %4 (coord), %3 (lod), 0 (texture), 2 (sampler)
 *   32    %10 = ushr %5.x, %9 (0x10)
 *   16    %14 = u2u16 %10
 *   32    %18 = f2f32 %14
 *
 * Copy propagation will clean up the MOVs we emit for u2u16 and this pass will
 * drop the UNPACK_L on the SHR result to do UNPACK_H on the original value
 * instead, which would allow us to DCE the SHR.
 */
static bool
try_opt_extract_h16(struct v3d_compile *c, struct qinst *inst)
{
        bool progress = false;
        for (int i = 0; i < vir_get_nsrc(inst); i++) {
                /* Check src has unpack L */
                if (inst->src[i].file != QFILE_TEMP)
                        continue;

                if (!has_unpack_l(inst, i))
                        continue;

                /* Find producer of src and check is SHR with constant 16 */
               struct qinst *shr_inst = c->defs[inst->src[i].index];
               if (!shr_inst || shr_inst->qpu.alu.add.op != V3D_QPU_A_SHR)
                        continue;

               if (shr_inst->src[1].file != QFILE_TEMP)
                        continue;

               struct qinst *unif_inst = c->defs[shr_inst->src[1].index];
               if (!unif_inst->qpu.sig.ldunif)
                        continue;

                if (c->uniform_data[unif_inst->uniform] != 0x10)
                        continue;

                /* Transform instruction so we can hopefully DCE the SHR  */
                inst->src[i] = shr_inst->src[0];
                vir_set_unpack(inst, i, V3D_QPU_UNPACK_H);
                progress = true;
        }

        return progress;
}

static bool
try_opt_alu(struct v3d_compile *c, struct qinst *inst)
{
        if(inst->qpu.type != V3D_QPU_INSTR_TYPE_ALU)
                return false;

        return try_opt_extract_h16(c, inst);
}

bool
vir_opt_alu(struct v3d_compile *c)
{
        bool progress = false;
        vir_for_each_block(block, c) {
                c->cur_block = block;
                vir_for_each_inst_safe(inst, block) {
                        progress = try_opt_alu(c, inst) || progress;
                }
        }

        return progress;
}
