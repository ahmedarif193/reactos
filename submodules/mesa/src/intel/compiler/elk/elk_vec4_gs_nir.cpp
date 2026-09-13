/*
 * Copyright © 2015 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "elk_vec4_gs_visitor.h"

namespace elk {

void
vec4_gs_visitor::nir_emit_intrinsic(nir_intrinsic_instr *instr)
{
   dst_reg dest;
   src_reg src;

   switch (instr->intrinsic) {
   case nir_intrinsic_load_per_vertex_input: {
      assert(instr->def.bit_size == 32);
      /* The EmitNoIndirectInput flag guarantees our vertex index will
       * be constant.  We should handle indirects someday.
       */
      const unsigned vertex = nir_src_as_uint(instr->src[0]);
      const unsigned offset_reg = nir_src_as_uint(instr->src[1]);

      const unsigned input_array_stride = prog_data->urb_read_length * 2;

      /* Make up a type...we have no way of knowing... */
      const glsl_type *const type = glsl_ivec_type(instr->num_components);

      src = src_reg(ATTR, input_array_stride * vertex +
                    nir_intrinsic_base(instr) + offset_reg,
                    type);
      src.swizzle = ELK_SWZ_COMP_INPUT(nir_intrinsic_component(instr));

      dest = get_nir_def(instr->def, src.type);
      dest.writemask = elk_writemask_for_size(instr->num_components);
      emit(MOV(dest, src));
      break;
   }

   case nir_intrinsic_load_input:
      UNREACHABLE("nir_lower_io should have produced per_vertex intrinsics");

   case nir_intrinsic_emit_vertex_with_counter:
      this->vertex_count =
         retype(get_nir_src(instr->src[0], 1), ELK_REGISTER_TYPE_UD);
      gs_emit_vertex(nir_intrinsic_stream_id(instr));
      break;

   case nir_intrinsic_end_primitive_with_counter:
      this->vertex_count =
         retype(get_nir_src(instr->src[0], 1), ELK_REGISTER_TYPE_UD);
      gs_end_primitive();
      break;

   case nir_intrinsic_set_vertex_and_primitive_count:
      this->vertex_count =
         retype(get_nir_src(instr->src[0], 1), ELK_REGISTER_TYPE_UD);
      break;

   case nir_intrinsic_load_primitive_id:
      assert(gs_prog_data->include_primitive_id);
      dest = get_nir_def(instr->def, ELK_REGISTER_TYPE_D);
      emit(MOV(dest, retype(elk_vec4_grf(1, 0), ELK_REGISTER_TYPE_D)));
      break;

   case nir_intrinsic_load_invocation_id: {
      dest = get_nir_def(instr->def, ELK_REGISTER_TYPE_D);
      if (gs_prog_data->invocations > 1)
         emit(ELK_GS_OPCODE_GET_INSTANCE_ID, dest);
      else
         emit(MOV(dest, elk_imm_ud(0)));
      break;
   }

   default:
      vec4_visitor::nir_emit_intrinsic(instr);
   }
}
}
