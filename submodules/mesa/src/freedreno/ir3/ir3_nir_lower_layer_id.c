/*
 * Copyright 2023 Igalia S.L.
 * SPDX-License-Identifier: MIT
 */

#include "compiler/nir/nir_builder.h"
#include "ir3_nir.h"

struct state {
   unsigned layer_location;
};

static bool
nir_lower_layer_id(nir_builder *b, nir_intrinsic_instr *intr, void *cb_data)
{
   struct state *state = cb_data;

   if (intr->intrinsic != nir_intrinsic_load_layer_id)
      return false;
   b->cursor = nir_before_instr(&intr->instr);

   if (state->layer_location == ~0)
      state->layer_location = b->shader->num_inputs++;

   nir_intrinsic_instr *load_input = nir_intrinsic_instr_create(b->shader, nir_intrinsic_load_input);
   nir_intrinsic_set_base(load_input, state->layer_location);
   nir_intrinsic_set_component(load_input, 0);
   load_input->num_components = 1;
   load_input->src[0] = nir_src_for_ssa(nir_imm_int(b, 0));
   nir_intrinsic_set_dest_type(load_input, nir_type_int);
   nir_io_semantics semantics = {
      .location = VARYING_SLOT_LAYER,
      .num_slots = 1,
   };
   nir_intrinsic_set_io_semantics(load_input, semantics);
   nir_def_init(&load_input->instr, &load_input->def, 1, 32);
   nir_builder_instr_insert(b, &load_input->instr);
   nir_def_rewrite_uses(&intr->def, &load_input->def);
   return true;
}

bool ir3_nir_lower_layer_id(nir_shader *shader)
{
   assert(shader->info.stage == MESA_SHADER_FRAGMENT);

   struct state state = {
      .layer_location = ~0,
   };

   if (shader->info.inputs_read & BITFIELD64_BIT(VARYING_SLOT_LAYER)) {
      nir_foreach_function_impl (impl, shader) {
         nir_foreach_block (block, impl) {
            nir_foreach_instr (instr, block) {
               if (instr->type != nir_instr_type_intrinsic)
                  continue;
               struct nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
               if (intr->intrinsic != nir_intrinsic_load_input ||
                   nir_intrinsic_io_semantics(intr).location != VARYING_SLOT_LAYER)
                  continue;
               state.layer_location = nir_intrinsic_base(intr);
               goto finish;
            }
         }
      }
   }

finish:
   return nir_shader_intrinsics_pass(shader, nir_lower_layer_id,
                nir_metadata_control_flow, &state);
}
