/*
 * Copyright © 2013 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "elk_vec4_vs.h"
#include "dev/intel_debug.h"

namespace elk {

void
vec4_vs_visitor::emit_prolog()
{
}


void
vec4_vs_visitor::emit_urb_write_header(int mrf)
{
   /* No need to do anything for VS; an implied write to this MRF will be
    * performed by ELK_VEC4_VS_OPCODE_URB_WRITE.
    */
   (void) mrf;
}


vec4_instruction *
vec4_vs_visitor::emit_urb_write_opcode(bool complete)
{
   vec4_instruction *inst = emit(ELK_VEC4_VS_OPCODE_URB_WRITE);
   inst->urb_write_flags = complete ?
      ELK_URB_WRITE_EOT_COMPLETE : ELK_URB_WRITE_NO_FLAGS;

   return inst;
}


void
vec4_vs_visitor::emit_urb_slot(dst_reg reg, int varying)
{
   reg.type = ELK_REGISTER_TYPE_F;
   output_reg[varying][0].type = reg.type;

   switch (varying) {
   case VARYING_SLOT_COL0:
   case VARYING_SLOT_COL1:
   case VARYING_SLOT_BFC0:
   case VARYING_SLOT_BFC1: {
      /* These built-in varyings are only supported in compatibility mode,
       * and we only support GS in core profile.  So, this must be a vertex
       * shader.
       */
      vec4_instruction *inst = emit_generic_urb_slot(reg, varying, 0);
      if (inst && key->clamp_vertex_color)
         inst->saturate = true;
      break;
   }
   default:
      return vec4_visitor::emit_urb_slot(reg, varying);
   }
}


void
vec4_vs_visitor::emit_thread_end()
{
   /* For VS, we always end the thread by emitting a single vertex.
    * emit_urb_write_opcode() will take care of setting the eot flag on the
    * SEND instruction.
    */
   emit_vertex();
}


vec4_vs_visitor::vec4_vs_visitor(const struct elk_compiler *compiler,
                                 const struct elk_compile_params *params,
                                 const struct elk_vs_prog_key *key,
                                 struct elk_vs_prog_data *vs_prog_data,
                                 const nir_shader *shader,
                                 bool debug_enabled)
   : vec4_visitor(compiler, params, &key->base.tex, &vs_prog_data->base,
                  shader, false /* no_spills */, debug_enabled),
     key(key),
     vs_prog_data(vs_prog_data)
{
}


} /* namespace elk */
