/*
 * Copyright (c) 2026 Lima Project
 * SPDX-License-Identifier: MIT
 */

#include "nir.h"
#include "nir_builder.h"
#include "compiler/nir/nir_format_convert.h"
#include "lima_ir.h"

static nir_def *
linear_to_srgb(nir_builder *b, nir_def *linear)
{
   nir_def *rgb = nir_trim_vector(b, linear, 3);

   nir_def *srgb = nir_format_linear_to_srgb(b, rgb);

   nir_def *comp[4] = {
      nir_channel(b, srgb, 0),
      nir_channel(b, srgb, 1),
      nir_channel(b, srgb, 2),
      nir_channel(b, linear, 3),
   };

   return nir_vec(b, comp, 4);
}

static bool
lima_nir_lower_framebuffer_impl(nir_builder *b, nir_intrinsic_instr *intr, void *data)
{
   enum pipe_format color_format = *((enum pipe_format *)data);

   if (intr->intrinsic != nir_intrinsic_store_output)
      return false;

   unsigned loc = nir_intrinsic_io_semantics(intr).location;

   if (loc != FRAG_RESULT_COLOR)
      return false;

   if (!util_format_is_srgb(color_format))
      return false;

   nir_def *linear = intr->src[0].ssa;

   b->cursor = nir_before_instr(&intr->instr);

   nir_def *srgb = linear_to_srgb(b, linear);
   nir_src_rewrite(&intr->src[0], srgb);

   return true;
}

bool
lima_nir_lower_framebuffer(nir_shader *shader, enum pipe_format color_format)
{
   return nir_shader_intrinsics_pass(shader, lima_nir_lower_framebuffer_impl,
                                     nir_metadata_control_flow, &color_format);
}
