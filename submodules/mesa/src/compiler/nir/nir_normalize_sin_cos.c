/*
 * Copyright © 2021 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "nir.h"
#include "nir_builder.h"

static bool
normalize_sin_cos(struct nir_builder *b, nir_alu_instr *sincos, UNUSED void *_)
{
   if (sincos->op != nir_op_fsin && sincos->op != nir_op_fcos)
      return false;

   b->cursor = nir_before_instr(&sincos->instr);
   b->fp_math_ctrl = sincos->fp_math_ctrl;

   nir_def *src = nir_fmul_imm(b, nir_ssa_for_alu_src(b, sincos, 0), 0.15915493667125702);
   nir_def *replace = sincos->op == nir_op_fsin ? nir_fsin_normalized_2_pi(b, src) : nir_fcos_normalized_2_pi(b, src);
   nir_def_replace(&sincos->def, replace);

   return true;
}

bool
nir_normalize_sin_cos(nir_shader *shader)
{
   return nir_shader_alu_pass(shader, normalize_sin_cos, nir_metadata_control_flow, NULL);
}
