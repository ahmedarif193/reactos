/*
 * Copyright © 2016 Rob Clark <robclark@freedesktop.org>
 * Copyright © 2018 Google, Inc.
 * SPDX-License-Identifier: MIT
 *
 * Authors:
 *    Rob Clark <robclark@freedesktop.org>
 */

#include "drm-uapi/drm_fourcc.h"
#include "pipe/p_screen.h"
#include "util/format/u_format.h"

#include "fd6_blitter.h"
#include "fd6_context.h"
#include "fd6_emit.h"
#include "fd6_resource.h"
#include "fd6_screen.h"

#include "ir3/ir3_compiler.h"

static bool
valid_sample_count(unsigned sample_count, bool is_suboptimal)
{
   /* NPoT formats do not support MSAA: */
   if (is_suboptimal && (sample_count > 1))
      return false;

   switch (sample_count) {
   case 0:
   case 1:
   case 2:
   case 4:
      // TODO seems 8x works, but increases lrz width or height.. but the
      // blob I have doesn't seem to expose any egl configs w/ 8x, so
      // just hide it for now and revisit later.
      //	case 8:
      return true;
   default:
      return false;
   }
}

static bool
is_cl_supported(const struct fd_dev_info *info, enum pipe_format format)
{
   if (info->chip >= A8XX) {
      unsigned b = util_format_get_component_bits(
         format, UTIL_FORMAT_COLORSPACE_RGB, PIPE_SWIZZLE_W);
      if (b == 2)
         return false;
   }
   return true;
}

static bool
fd6_screen_is_format_supported(struct pipe_screen *pscreen,
                               enum pipe_format format,
                               enum pipe_texture_target target,
                               unsigned sample_count,
                               unsigned storage_sample_count, unsigned usage)
{
   struct fd_screen *screen = fd_screen(pscreen);
   bool allow_suboptimal = usage & PIPE_BIND_SAMPLER_VIEW_SUBOPTIMAL;
   bool is_suboptimal = !util_is_power_of_two_or_zero(util_format_get_blocksize(format));
   unsigned retval = 0;

   usage &= ~PIPE_BIND_SAMPLER_VIEW_SUBOPTIMAL;

   if (usage & PIPE_BIND_OPENCL) {
      if (!is_cl_supported(screen->info, format))
         return false;
      usage &= ~PIPE_BIND_OPENCL;
   }

   if ((target >= PIPE_MAX_TEXTURE_TYPES) ||
       !valid_sample_count(sample_count, is_suboptimal)) {
      DBG("not supported: format=%s, target=%d, sample_count=%d, usage=%x",
          util_format_name(format), target, sample_count, usage);
      return false;
   }

   if (MAX2(1, sample_count) != MAX2(1, storage_sample_count))
      return false;

   if ((usage & PIPE_BIND_VERTEX_BUFFER) &&
       (fd6_vertex_format(format) != FMT6_NONE)) {
      retval |= PIPE_BIND_VERTEX_BUFFER;
   }

   bool has_color = fd6_color_format_supported(screen->info, format, TILE6_LINEAR);
   bool has_tex = fd6_texture_format_supported(screen->info, format, TILE6_LINEAR, false);

   if ((usage & (PIPE_BIND_SAMPLER_VIEW | PIPE_BIND_SHADER_IMAGE)) &&
       has_tex &&
       (target == PIPE_BUFFER || allow_suboptimal || !is_suboptimal)) {
      retval |= usage & PIPE_BIND_SAMPLER_VIEW;
      if (has_color)
         retval |= usage & PIPE_BIND_SHADER_IMAGE;
   }

   /* Images can't do the requisite swizzling to make these formats work,
    * without lowering in the shader:
    */
   if (util_format_is_luminance_alpha(format) ||
       util_format_is_luminance(format) ||
       util_format_is_alpha(format) ||
       util_format_is_intensity(format))
      retval &= ~PIPE_BIND_SHADER_IMAGE;

   if (usage & PIPE_BIND_SHADER_IMAGE) {
      if (sample_count > 0)
         return false;

      /* So, this only matters for image writes but 'usage' doesn't
       * differentiate.  See f1c1b96
       */
      const struct util_format_description *desc = util_format_description(format);
      if ((desc->nr_channels > 2) && (desc->block.bits == 16))
         return false;
   }

   if ((usage &
        (PIPE_BIND_RENDER_TARGET | PIPE_BIND_DISPLAY_TARGET |
         PIPE_BIND_SCANOUT | PIPE_BIND_SHARED)) &&
       has_color && has_tex) {
      retval |= usage & (PIPE_BIND_RENDER_TARGET | PIPE_BIND_DISPLAY_TARGET |
                         PIPE_BIND_SCANOUT | PIPE_BIND_SHARED);
   }

   /* For ARB_framebuffer_no_attachments: */
   if ((usage & PIPE_BIND_RENDER_TARGET) && (format == PIPE_FORMAT_NONE)) {
      retval |= usage & PIPE_BIND_RENDER_TARGET;
   }

   if ((usage & PIPE_BIND_DEPTH_STENCIL) &&
       (fd6_pipe2depth(format) != (enum a6xx_depth_format) ~0) && has_tex) {
      retval |= PIPE_BIND_DEPTH_STENCIL;
   }

   if ((usage & PIPE_BIND_INDEX_BUFFER) &&
       (fd_pipe2index(format) != (enum pc_di_index_size) ~0)) {
      retval |= PIPE_BIND_INDEX_BUFFER;
   }

   if ((usage & PIPE_BIND_BLENDABLE) && has_color &&
       !util_format_is_pure_integer(format)) {
      retval |= PIPE_BIND_BLENDABLE;
   }

   if (retval != usage) {
      DBG("not supported: format=%s, target=%d, sample_count=%d, "
          "usage=%x, retval=%x",
          util_format_name(format), target, sample_count, usage, retval);
   }

   return retval == usage;
}

/* clang-format off */
static const enum pc_di_primtype primtypes[] = {
   [MESA_PRIM_POINTS]                      = DI_PT_POINTLIST,
   [MESA_PRIM_LINES]                       = DI_PT_LINELIST,
   [MESA_PRIM_LINE_LOOP]                   = DI_PT_LINELOOP,
   [MESA_PRIM_LINE_STRIP]                  = DI_PT_LINESTRIP,
   [MESA_PRIM_TRIANGLES]                   = DI_PT_TRILIST,
   [MESA_PRIM_TRIANGLE_STRIP]              = DI_PT_TRISTRIP,
   [MESA_PRIM_TRIANGLE_FAN]                = DI_PT_TRIFAN,
   [MESA_PRIM_QUADS]                       = DI_PT_NONE,   /* unsupported */
   [MESA_PRIM_QUAD_STRIP]                  = DI_PT_NONE,   /* unsupported */
   [MESA_PRIM_POLYGON]                     = DI_PT_NONE,   /* unsupported */
   [MESA_PRIM_LINES_ADJACENCY]             = DI_PT_LINE_ADJ,
   [MESA_PRIM_LINE_STRIP_ADJACENCY]        = DI_PT_LINESTRIP_ADJ,
   [MESA_PRIM_TRIANGLES_ADJACENCY]         = DI_PT_TRI_ADJ,
   [MESA_PRIM_TRIANGLE_STRIP_ADJACENCY]    = DI_PT_TRISTRIP_ADJ,
   [MESA_PRIM_PATCHES]                     = DI_PT_PATCHES0,
   [MESA_PRIM_COUNT]                       = DI_PT_RECTLIST,  /* internal clear blits */
};
/* clang-format on */

void
fd6_screen_init(struct pipe_screen *pscreen)
{
   struct fd_screen *screen = fd_screen(pscreen);

   screen->max_rts = A6XX_MAX_RENDER_TARGETS;

   screen->gmemsize_bytes =
      fd6_calc_gmem_cache_offsets(screen->info, screen->gmemsize_bytes,
                                  &screen->config_gmem,
                                  &screen->config_sysmem);

   /* Currently only FB_READ forces GMEM path, mostly because we'd have to
    * deal with cmdstream patching otherwise..
    */
   screen->gmem_reason_mask = (enum fd_gmem_reason)(
         FD_GMEM_CLEARS_DEPTH_STENCIL |
         FD_GMEM_DEPTH_ENABLED | FD_GMEM_STENCIL_ENABLED |
         FD_GMEM_BLEND_ENABLED | FD_GMEM_LOGICOP_ENABLED);

   pscreen->context_create = FD_CALLX(screen->info, fd6_context_create);
   pscreen->is_format_supported = fd6_screen_is_format_supported;

   screen->tile_mode = fd6_tile_mode;

   FD_CALLX(screen->info, fd6_resource_screen_init)(pscreen);
   fd6_emit_init_screen(pscreen);
   ir3_screen_init(pscreen);

   screen->primtypes = primtypes;
}
