/*
 * Copyright © 2025 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "nir/nir.h"
#include "nir/nir_builder.h"
#include "radv_nir.h"
#include "radv_shader.h"

typedef struct {
   const struct radv_ps_epilog_key *epilog_key;
   bool mrt0_alpha_is_dead;
} trim_fs_color_exports_state;

static bool
trim_fs_color_exports(nir_builder *b, nir_intrinsic_instr *intrin, void *_state)
{
   const trim_fs_color_exports_state *state = (const trim_fs_color_exports_state *)_state;

   if (intrin->intrinsic != nir_intrinsic_store_output)
      return false;

   nir_io_semantics io_sem = nir_intrinsic_io_semantics(intrin);

   int index = mesa_frag_result_get_color_index(io_sem.location);

   if (index < 0)
      return false;

   bool progress = false;

   if (state->epilog_key->no_signed_zero & BITFIELD_BIT(index)) {
      nir_io_semantics sem = nir_intrinsic_io_semantics(intrin);

      if (!sem.no_signed_zero) {
         sem.no_signed_zero = 1;
         nir_intrinsic_set_io_semantics(intrin, sem);
         progress = true;
      }
   }

   uint8_t channels_needed = state->epilog_key->colors_needed >> (index * 4) & 0xf;

   if (index == 0 && state->mrt0_alpha_is_dead)
      channels_needed &= ~BITFIELD_BIT(3);

   const uint8_t write_mask = nir_intrinsic_write_mask(intrin);
   const uint8_t new_write_mask = write_mask & (channels_needed >> nir_intrinsic_component(intrin));

   if (new_write_mask == write_mask)
      return progress;

   if (!new_write_mask)
      nir_instr_remove(&intrin->instr);
   else
      nir_intrinsic_set_write_mask(intrin, new_write_mask);

   return true;
}

bool
radv_nir_trim_fs_color_exports(nir_shader *shader, const struct radv_ps_epilog_key *epilog_key, bool mrt0_alpha_is_dead)
{
   trim_fs_color_exports_state state = {
      .epilog_key = epilog_key,
      .mrt0_alpha_is_dead = mrt0_alpha_is_dead,
   };

   return nir_shader_intrinsics_pass(shader, trim_fs_color_exports, nir_metadata_control_flow, &state);
}
