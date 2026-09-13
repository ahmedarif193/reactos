/*
 * Copyright © 2006-2015 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "elk_vec4.h"

namespace elk {

class vec4_vs_visitor : public vec4_visitor
{
public:
   vec4_vs_visitor(const struct elk_compiler *compiler,
                   const struct elk_compile_params *params,
                   const struct elk_vs_prog_key *key,
                   struct elk_vs_prog_data *vs_prog_data,
                   const nir_shader *shader,
                   bool debug_enabled);

protected:
   virtual void setup_payload();
   virtual void emit_prolog();
   virtual void emit_thread_end();
   virtual void emit_urb_write_header(int mrf);
   virtual void emit_urb_slot(dst_reg reg, int varying);
   virtual vec4_instruction *emit_urb_write_opcode(bool complete);

private:
   int setup_attributes(int payload_reg);

   const struct elk_vs_prog_key *const key;
   struct elk_vs_prog_data * const vs_prog_data;
};

} /* namespace elk */
