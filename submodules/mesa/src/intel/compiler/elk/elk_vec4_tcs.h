/*
 * Copyright © 2013 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

/**
 * \file elk_vec4_tcs.h
 *
 * The vec4-mode tessellation control shader compiler backend.
 */

#pragma once

#include "elk_compiler.h"
#include "elk_eu.h"
#include "elk_vec4.h"

#ifdef __cplusplus
namespace elk {

class vec4_tcs_visitor : public vec4_visitor
{
public:
   vec4_tcs_visitor(const struct elk_compiler *compiler,
                    const struct elk_compile_params *params,
                    const struct elk_tcs_prog_key *key,
                    struct elk_tcs_prog_data *prog_data,
                    const nir_shader *nir,
                    bool debug_enabled);

protected:
   virtual void setup_payload();
   virtual void emit_prolog();
   virtual void emit_thread_end();

   virtual void nir_emit_intrinsic(nir_intrinsic_instr *instr);

   void emit_input_urb_read(const dst_reg &dst,
                            const src_reg &vertex_index,
                            unsigned base_offset,
                            unsigned first_component,
                            const src_reg &indirect_offset);
   void emit_output_urb_read(const dst_reg &dst,
                             unsigned base_offset,
                             unsigned first_component,
                             const src_reg &indirect_offset);

   void emit_urb_write(const src_reg &value, unsigned writemask,
                       unsigned base_offset, const src_reg &indirect_offset);

   /* we do not use the normal end-of-shader URB write mechanism -- but every
    * vec4 stage must provide implementations of these:
    */
   virtual void emit_urb_write_header(int /* mrf */) {}
   virtual vec4_instruction *emit_urb_write_opcode(bool /* complete */) { return NULL; }

   const struct elk_tcs_prog_key *key;
   src_reg invocation_id;
};

} /* namespace elk */
#endif /* __cplusplus */
