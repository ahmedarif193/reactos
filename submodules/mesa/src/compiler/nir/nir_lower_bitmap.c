/*
 * Copyright © 2015 Red Hat
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "nir.h"
#include "nir_builder.h"

/* Lower glBitmap().
 *
 * This is based on the logic in st_get_bitmap_shader() in TGSI compiler.
 * From st_cb_bitmap.c:
 *
 *    glBitmaps are drawn as textured quads.  The user's bitmap pattern
 *    is stored in a texture image.  An alpha8 texture format is used.
 *    The fragment shader samples a bit (texel) from the texture, then
 *    discards the fragment if the bit is off.
 *
 *    Note that we actually store the inverse image of the bitmap to
 *    simplify the fragment program.  An "on" bit gets stored as texel=0x0
 *    and an "off" bit is stored as texel=0xff.  Then we kill the
 *    fragment if the negated texel value is less than zero.
 *
 * Note that the texture format will be, according to what driver supports,
 * in order of preference (with swizzle):
 *
 *    I8_UNORM - .xxxx
 *    A8_UNORM - .000x
 *    L8_UNORM - .xxx1
 *
 * If L8_UNORM, options->swizzle_xxxx is true.  Otherwise we can just use
 * the .w comp.
 */
bool
nir_lower_bitmap(nir_shader *shader, const nir_lower_bitmap_options *options)
{
   assert(shader->info.stage == MESA_SHADER_FRAGMENT);
   assert(shader->info.io_lowered);

   nir_function_impl *impl = nir_shader_get_entrypoint(shader);
   nir_builder b_ = nir_builder_at(nir_before_impl(impl));
   nir_builder *b = &b_;

   nir_def *baryc =
      nir_load_barycentric_pixel(b, 32, .interp_mode = INTERP_MODE_SMOOTH);

   nir_def *texcoord =
      nir_load_interpolated_input(b, 2, 32, baryc, nir_imm_int(b, 0),
                                  .io_semantics.location = VARYING_SLOT_TEX0);

   const struct glsl_type *sampler2D =
      glsl_sampler_type(GLSL_SAMPLER_DIM_2D, false, false, GLSL_TYPE_FLOAT);

   nir_variable *tex_var =
      nir_variable_create(b->shader, nir_var_uniform, sampler2D, "bitmap_tex");
   tex_var->data.binding = options->sampler;
   tex_var->data.explicit_binding = true;
   tex_var->data.how_declared = nir_var_hidden;

   nir_deref_instr *tex_deref = nir_build_deref_var(b, tex_var);

   nir_def *tex = nir_tex(b, texcoord, .texture_deref = tex_deref,
                          .sampler_deref = tex_deref,
                          .can_speculate = true);

   /* kill if tex != 0.0.. take .x or .w channel according to format: */
   nir_def *channel = nir_channel(b, tex, options->swizzle_xxxx ? 0 : 3);
   nir_discard_if(b, nir_fneu_imm(b, channel, 0.0));

   b->shader->info.fs.uses_discard = true;
   return nir_progress(true, impl, nir_metadata_control_flow);
}
