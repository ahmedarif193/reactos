/*
 * Copyright © 2013 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

/**
 * \file elk_vec4_tes.h
 *
 * The vec4 mode tessellation evaluation shader compiler backend.
 */

#pragma once

#include "elk_vec4.h"

#ifdef __cplusplus
namespace elk {

class vec4_tes_visitor : public vec4_visitor
{
public:
   vec4_tes_visitor(const struct elk_compiler *compiler,
                    const struct elk_compile_params *params,
                   const struct elk_tes_prog_key *key,
                   struct elk_tes_prog_data *prog_data,
                   const nir_shader *nir,
                   bool debug_enabled);

protected:
   virtual void nir_emit_intrinsic(nir_intrinsic_instr *instr);

   virtual void setup_payload();
   virtual void emit_prolog();
   virtual void emit_thread_end();

   virtual void emit_urb_write_header(int mrf);
   virtual vec4_instruction *emit_urb_write_opcode(bool complete);

private:
   src_reg input_read_header;
};

} /* namespace elk */
#endif /* __cplusplus */
