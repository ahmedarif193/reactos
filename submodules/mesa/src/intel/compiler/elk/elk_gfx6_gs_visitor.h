/*
 * Copyright © 2014 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "elk_vec4.h"
#include "elk_vec4_gs_visitor.h"

#ifdef __cplusplus

namespace elk {

class gfx6_gs_visitor : public vec4_gs_visitor
{
public:
   gfx6_gs_visitor(const struct elk_compiler *comp,
                   const struct elk_compile_params *params,
                   struct elk_gs_compile *c,
                   struct elk_gs_prog_data *prog_data,
                   const nir_shader *shader,
                   bool no_spills,
                   bool debug_enabled) :
      vec4_gs_visitor(comp, params, c, prog_data, shader, no_spills, debug_enabled)
      {
      }

protected:
   virtual void emit_prolog();
   virtual void emit_thread_end();
   virtual void gs_emit_vertex(int stream_id);
   virtual void gs_end_primitive();
   virtual void emit_urb_write_header(int mrf);
   virtual void setup_payload();

private:
   void xfb_write();
   void xfb_program(unsigned vertex, unsigned num_verts);
   int get_vertex_output_offset_for_varying(int vertex, int varying);
   void emit_snb_gs_urb_write_opcode(bool complete,
                                     int base_mrf,
                                     int last_mrf,
                                     int urb_offset);

   src_reg vertex_output;
   src_reg vertex_output_offset;
   src_reg temp;
   src_reg first_vertex;
   src_reg prim_count;
   src_reg primitive_id;

   /* Transform Feedback members */
   src_reg sol_prim_written;
   src_reg svbi;
   src_reg max_svbi;
   src_reg destination_indices;
};

} /* namespace elk */

#endif /* __cplusplus */
