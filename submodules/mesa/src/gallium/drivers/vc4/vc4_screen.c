/*
 * Copyright © 2014 Broadcom
 * Copyright (C) 2012 Rob Clark <robclark@freedesktop.org>
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
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "util/os_misc.h"
#include "pipe/p_defines.h"
#include "pipe/p_screen.h"
#include "pipe/p_state.h"

#include "util/os_misc.h"
#include "util/u_debug.h"
#include "util/u_memory.h"
#include "util/format/u_format.h"
#include "util/u_hash_table.h"
#include "util/u_screen.h"
#include "util/u_transfer_helper.h"
#include "util/perf/cpu_trace.h"
#include "util/ralloc.h"

#ifdef USE_VC4_D3DKMT
#include <windows.h>
#include "vc4/d3dkmt/vc4_d3dkmt_public.h"
#else
#include <xf86drm.h>
#endif
#include "drm-uapi/drm_fourcc.h"
#include "drm-uapi/vc4_drm.h"
#include "vc4_screen.h"
#include "vc4_context.h"
#include "vc4_resource.h"

static const struct debug_named_value vc4_debug_options[] = {
        { "cl",       VC4_DEBUG_CL,
          "Dump command list during creation" },
        { "surf",       VC4_DEBUG_SURFACE,
          "Dump surface layouts" },
        { "qpu",      VC4_DEBUG_QPU,
          "Dump generated QPU instructions" },
        { "qir",      VC4_DEBUG_QIR,
          "Dump QPU IR during program compile" },
        { "nir",      VC4_DEBUG_NIR,
          "Dump NIR during program compile" },
        { "tgsi",     VC4_DEBUG_TGSI,
          "Dump TGSI during program compile" },
        { "shaderdb", VC4_DEBUG_SHADERDB,
          "Dump program compile information for shader-db analysis" },
        { "perf",     VC4_DEBUG_PERF,
          "Print during performance-related events" },
        { "norast",   VC4_DEBUG_NORAST,
          "Skip actual hardware execution of commands" },
        { "always_flush", VC4_DEBUG_ALWAYS_FLUSH,
          "Flush after each draw call" },
        { "always_sync", VC4_DEBUG_ALWAYS_SYNC,
          "Wait for finish after each flush" },
#ifdef USE_VC4_SIMULATOR
        { "dump", VC4_DEBUG_DUMP,
          "Write a GPU command stream trace file" },
#endif
        DEBUG_NAMED_VALUE_END
};

DEBUG_GET_ONCE_FLAGS_OPTION(vc4_debug, "VC4_DEBUG", vc4_debug_options, 0)
uint32_t vc4_mesa_debug;

static const char *
vc4_screen_get_name(struct pipe_screen *pscreen)
{
        struct vc4_screen *screen = vc4_screen(pscreen);

        if (!screen->name) {
                screen->name = ralloc_asprintf(screen,
                                               "VC4 V3D %d.%d",
                                               screen->v3d_ver / 10,
                                               screen->v3d_ver % 10);
        }

        return screen->name;
}

static const char *
vc4_screen_get_vendor(struct pipe_screen *pscreen)
{
        return "Broadcom";
}

static void
vc4_screen_destroy(struct pipe_screen *pscreen)
{
        struct vc4_screen *screen = vc4_screen(pscreen);

#ifdef USE_VC4_D3DKMT
        pipe_resource_reference(&screen->primary_resource, NULL);
#endif
        _mesa_hash_table_destroy(screen->bo_handles, NULL);
        vc4_bufmgr_destroy(pscreen);
        mtx_destroy(&screen->bo_cache.lock);
        slab_destroy_parent(&screen->transfer_pool);
        if (screen->ro)
                screen->ro->destroy(screen->ro);

#ifdef USE_VC4_SIMULATOR
        vc4_simulator_destroy(screen->sim_file);
#endif

        u_transfer_helper_destroy(pscreen->transfer_helper);

#ifdef USE_VC4_D3DKMT
        vc4_d3dkmt_close(screen->fd);
#else
        close(screen->fd);
#endif
        ralloc_free(pscreen);
}

#ifdef USE_VC4_D3DKMT
static bool
vc4_screen_is_gpu_output(HWND window)
{
        return window && GetPropW(window, L"ReactOS.Dwm.GpuOutput") != NULL;
}

static void
vc4_screen_gpu_failure(const char *stage, struct pipe_resource *resource)
{
        static unsigned failures;
        char message[192];

        failures++;
        if (failures > 16 && (failures & (failures - 1)) != 0)
                return;
        _snprintf(message, sizeof(message) - 1,
                  "RPI3VC4_GPU_PRESENT_FAIL stage=%s format=%u size=%ux%u\n",
                  stage,
                  resource ? resource->format : 0,
                  resource ? resource->width0 : 0,
                  resource ? resource->height0 : 0);
        message[sizeof(message) - 1] = '\0';
        OutputDebugStringA(message);
}

static void
vc4_screen_drop_primary(struct vc4_screen *screen)
{
        screen->primary_present_valid = false;
        pipe_resource_reference(&screen->primary_resource, NULL);
        screen->primary_global_share = 0;
        screen->primary_width = 0;
        screen->primary_height = 0;
        screen->primary_pitch = 0;
}

static bool
vc4_screen_import_primary(struct vc4_screen *screen,
                          enum pipe_format format,
                          uintptr_t global_share, uint32_t width,
                          uint32_t height, uint32_t pitch)
{
        struct pipe_screen *pscreen = &screen->base;
        struct pipe_resource templ = { 0 };
        struct winsys_handle whandle = { 0 };

        if (screen->primary_resource &&
            screen->primary_global_share == global_share &&
            screen->primary_width == width &&
            screen->primary_height == height &&
            screen->primary_pitch == pitch &&
            screen->primary_resource->format == format)
                return true;

        vc4_screen_drop_primary(screen);

        templ.target = PIPE_TEXTURE_2D;
        templ.format = format;
        templ.width0 = width;
        templ.height0 = height;
        templ.depth0 = 1;
        templ.array_size = 1;
        templ.bind = PIPE_BIND_DISPLAY_TARGET | PIPE_BIND_RENDER_TARGET;

        whandle.type = WINSYS_HANDLE_TYPE_SHARED;
        whandle.handle = (HANDLE)(uintptr_t)global_share;
        whandle.stride = pitch;
        whandle.modifier = DRM_FORMAT_MOD_LINEAR;

        screen->primary_resource = pscreen->resource_from_handle(
                pscreen, &templ, &whandle,
                PIPE_HANDLE_USAGE_FRAMEBUFFER_WRITE);
        if (!screen->primary_resource)
                return false;

        screen->primary_global_share = global_share;
        screen->primary_width = width;
        screen->primary_height = height;
        screen->primary_pitch = pitch;
        return true;
}

static bool
vc4_screen_present_gpu_impl(struct vc4_screen *screen,
                       struct pipe_context *ctx,
                       struct pipe_resource *resource,
                       unsigned level, unsigned layer, HDC hdc,
                       const struct pipe_box *damage)
{
        struct pipe_blit_info blit = { 0 };
        struct vc4_resource *primary;
        uintptr_t global_share;
        uint32_t primary_width, primary_height, primary_pitch;
        unsigned width = u_minify(resource->width0, level);
        unsigned height = u_minify(resource->height0, level);
        HWND window = WindowFromDC(hdc);
        RECT client;
        RECT dirty;
        POINT origin = { 0, 0 };
        struct pipe_box copy = {0, 0, 0, width, height, 1};

        if (!window || !GetClientRect(window, &client) ||
            !ClientToScreen(window, &origin) ||
            client.right - client.left != (LONG)width ||
            client.bottom - client.top != (LONG)height ||
            origin.x < 0 || origin.y < 0) {
                vc4_screen_gpu_failure("geometry", resource);
                return false;
        }

        if (!vc4_d3dkmt_primary_info(screen->fd, &global_share,
                                     &primary_width, &primary_height,
                                     &primary_pitch) ||
            !global_share || primary_pitch < primary_width * 4 ||
            (uint64_t)(unsigned)origin.x + width > primary_width ||
            (uint64_t)(unsigned)origin.y + height > primary_height) {
                vc4_screen_gpu_failure("primary_info", resource);
                return false;
        }

        if (!vc4_screen_import_primary(screen, resource->format, global_share,
                                       primary_width, primary_height,
                                       primary_pitch)) {
                vc4_screen_gpu_failure("primary_import", resource);
                return false;
        }

        if (screen->primary_resource->width0 != primary_width ||
            screen->primary_resource->height0 != primary_height) {
                vc4_screen_gpu_failure("primary_size", resource);
                return false;
        }

        /* Only the registered, full-size compositor output owns the complete
         * primary image. First import/recreation/error repairs it in full.
         * Expand hints to whole tiles so LOAD/STORE uses the existing RCL
         * path, including the permitted partial tile at the surface edge. */
        if (screen->primary_present_valid && damage && level == 0 && layer == 0 &&
            resource->nr_samples <= 1 && origin.x == 0 && origin.y == 0 &&
            width == primary_width && height == primary_height &&
            damage->x >= 0 && damage->y >= 0 && damage->z == 0 &&
            damage->width > 0 && damage->height > 0 && damage->depth == 1 &&
            (uint64_t)damage->x + damage->width <= width &&
            (uint64_t)damage->y + damage->height <= height) {
                copy.x = damage->x & ~63;
                copy.y = damage->y & ~63;
                copy.width = MIN2(align(damage->x + damage->width, 64), width) - copy.x;
                copy.height = MIN2(align(damage->y + damage->height, 64), height) - copy.y;
        }

        blit.src.resource = resource;
        blit.src.level = level;
        blit.src.box.x = copy.x;
        blit.src.box.y = copy.y;
        blit.src.box.z = layer;
        blit.src.box.width = copy.width;
        blit.src.box.height = copy.height;
        blit.src.box.depth = 1;
        blit.src.format = resource->format;
        blit.dst.resource = screen->primary_resource;
        blit.dst.level = 0;
        blit.dst.box.x = origin.x + copy.x;
        blit.dst.box.y = origin.y + copy.y;
        blit.dst.box.z = 0;
        blit.dst.box.width = copy.width;
        blit.dst.box.height = copy.height;
        blit.dst.box.depth = 1;
        blit.dst.format = screen->primary_resource->format;
        blit.mask = PIPE_MASK_RGBA;
        blit.filter = PIPE_TEX_FILTER_NEAREST;

        DPT_SCOPE stage_trace = DptBegin(&vc4_present_trace, DPT_BLIT);
        bool blit_ok = vc4_render_blit_for_present(ctx, &blit);
        DptEnd(&vc4_present_trace, stage_trace, blit_ok, (ULONGLONG)copy.width * copy.height * 4);
        if (!blit_ok) {
                vc4_screen_gpu_failure("render_blit", resource);
                return false;
        }

        stage_trace = DptBegin(&vc4_present_trace, DPT_FLUSH);
        ctx->flush(ctx, NULL, PIPE_FLUSH_END_OF_FRAME);
        DptEnd(&vc4_present_trace, stage_trace, TRUE, 0);

        dirty.left = origin.x;
        dirty.top = origin.y;
        dirty.right = origin.x + width;
        dirty.bottom = origin.y + height;
        primary = vc4_resource(screen->primary_resource);
        if (!vc4_d3dkmt_present_primary(screen->fd, primary->bo->handle,
                                        window, &dirty)) {
                vc4_screen_gpu_failure("flip", resource);
                return false;
        }
        screen->primary_present_valid = true;

        return true;
}

static bool
vc4_screen_present_gpu(struct vc4_screen *screen,
                       struct pipe_context *ctx,
                       struct pipe_resource *resource,
                       unsigned level, unsigned layer, HDC hdc,
                       const struct pipe_box *damage)
{
    DPT_SCOPE Trace = DptBegin(&vc4_present_trace, DPT_MESA_PRESENT);
    bool Result = vc4_screen_present_gpu_impl(screen, ctx, resource, level, layer, hdc, damage);
    if (!Result)
        screen->primary_present_valid = false;
    DptEnd(&vc4_present_trace, Trace, Result, 0);
    return Result;
}

static void
vc4_screen_flush_frontbuffer(struct pipe_screen *pscreen,
                             struct pipe_context *ctx,
                             struct pipe_resource *resource,
                             unsigned level, unsigned layer,
                             void *winsys_drawable_handle,
                             unsigned nboxes, struct pipe_box *subbox)
{
        struct vc4_screen *screen = vc4_screen(pscreen);
        struct vc4_resource *rsc = vc4_resource(resource);
        BITMAPV5HEADER bitmap = { 0 };
        unsigned width = u_minify(resource->width0, level);
        unsigned height = u_minify(resource->height0, level);
        const uint8_t *map;
        const uint8_t *pixels;
        HDC hdc = winsys_drawable_handle;
        HWND window;

        if (!winsys_drawable_handle || level >= VC4_MAX_MIP_LEVELS ||
            layer >= resource->array_size || rsc->cpp != 4)
                return;

        window = WindowFromDC(hdc);
        if (vc4_screen_is_gpu_output(window)) {
                vc4_screen_present_gpu(screen, ctx, resource, level,
                                        layer, hdc, nboxes == 1 ? subbox : NULL);
                return;
        }

        if (rsc->tiled)
                return;

        switch (resource->format) {
        case PIPE_FORMAT_B8G8R8X8_UNORM:
        case PIPE_FORMAT_B8G8R8A8_UNORM:
                bitmap.bV5Compression = BI_RGB;
                break;
        case PIPE_FORMAT_R8G8B8X8_UNORM:
        case PIPE_FORMAT_R8G8B8A8_UNORM:
                bitmap.bV5Compression = BI_BITFIELDS;
                bitmap.bV5RedMask = 0x000000ff;
                bitmap.bV5GreenMask = 0x0000ff00;
                bitmap.bV5BlueMask = 0x00ff0000;
                break;
        default:
                return;
        }

        map = vc4_bo_map(rsc->bo);
        if (!map)
                return;
        pixels = map + rsc->slices[level].offset +
                 layer * rsc->cube_map_stride;

        bitmap.bV5Size = sizeof(bitmap);
        bitmap.bV5Width = rsc->slices[level].stride / rsc->cpp;
        bitmap.bV5Height = -(LONG)height;
        bitmap.bV5Planes = 1;
        bitmap.bV5BitCount = 32;
        bitmap.bV5SizeImage = rsc->slices[level].stride * height;

        if (!SetDIBitsToDevice(winsys_drawable_handle,
                               0, 0, width, height,
                               0, 0, 0, height,
                              pixels, (const BITMAPINFO *)&bitmap,
                              DIB_RGB_COLORS)) {
                StretchDIBits(winsys_drawable_handle,
                              0, 0, width, height,
                              0, 0, width, height,
                              pixels, (const BITMAPINFO *)&bitmap,
                              DIB_RGB_COLORS, SRCCOPY);
        }
}
#endif

static bool
vc4_has_feature(struct vc4_screen *screen, uint32_t feature)
{
        struct drm_vc4_get_param p = {
                .param = feature,
        };
        int ret = vc4_ioctl(screen->fd, DRM_IOCTL_VC4_GET_PARAM, &p);

        if (ret != 0)
                return false;

        return p.value;
}

static void
vc4_init_shader_caps(struct vc4_screen *screen)
{
        for (unsigned i = 0; i <= MESA_SHADER_FRAGMENT; i++) {
                struct pipe_shader_caps *caps =
                        (struct pipe_shader_caps *)&screen->base.shader_caps[i];

                if (i != MESA_SHADER_VERTEX && i != MESA_SHADER_FRAGMENT)
                        continue;

                caps->max_instructions =
                caps->max_alu_instructions =
                caps->max_tex_instructions =
                caps->max_tex_indirections = 16384;

                caps->max_control_flow_depth = screen->has_control_flow;
                caps->max_inputs = 8;
                caps->max_outputs = i == MESA_SHADER_FRAGMENT ? 1 : 8;
                caps->max_temps = 256; /* GL_MAX_PROGRAM_TEMPORARIES_ARB */
                caps->max_const_buffer0_size = 16 * 1024 * sizeof(float);
                caps->max_const_buffers = 1;
                caps->indirect_const_addr = true;
                caps->integers = true;
                caps->max_texture_samplers =
                caps->max_sampler_views = VC4_MAX_TEXTURE_SAMPLERS;
                caps->supported_irs = 1 << PIPE_SHADER_IR_NIR;
        }
}

static void
vc4_init_screen_caps(struct vc4_screen *screen)
{
        struct pipe_caps *caps = (struct pipe_caps *)&screen->base.caps;

        u_init_pipe_screen_caps(&screen->base, 1);

        /* Supported features (boolean caps). */
        caps->vertex_color_unclamped = true;
        caps->fragment_color_clamped = true;
        caps->npot_textures = true;
        caps->blend_equation_separate = true;
        caps->texture_multisample = true;
        caps->texture_swizzle = true;
        caps->texture_barrier = true;
        caps->tgsi_texcoord = true;

        caps->native_fence_fd = screen->has_syncobj;

        caps->tile_raster_order =
                vc4_has_feature(screen, DRM_VC4_PARAM_SUPPORTS_FIXED_RCL_ORDER);

        caps->fs_coord_origin_upper_left = true;
        caps->fs_coord_pixel_center_half_integer = true;
        caps->fs_face_is_integer_sysval = true;

        caps->mixed_framebuffer_sizes = true;
        caps->mixed_color_depth_bits = true;

        /* Texturing. */
        caps->max_texture_2d_size = 2048;
        caps->max_texture_cube_levels = VC4_MAX_MIP_LEVELS;
        caps->max_texture_3d_levels = 0;

        caps->max_varyings = 8;

        caps->vendor_id = 0x14E4;

        caps->video_memory = os_get_gpu_heap_size(1.0f, NULL) >> 20;

        caps->uma = true;

        caps->alpha_test = false;
        caps->vertex_color_clamped = false;
        caps->two_sided_color = false;
        caps->texrect = false;
        caps->image_store_formatted = false;
        caps->clip_planes = 0;

        caps->supported_prim_modes = screen->prim_types;

        caps->min_line_width =
        caps->min_line_width_aa =
        caps->min_point_size =
        caps->min_point_size_aa = 1;

        caps->point_size_granularity =
        caps->line_width_granularity = 0.1;

        caps->max_line_width =
        caps->max_line_width_aa = 32;

        caps->max_point_size =
        caps->max_point_size_aa = 512.0f;
}

static bool
vc4_screen_is_format_supported(struct pipe_screen *pscreen,
                               enum pipe_format format,
                               enum pipe_texture_target target,
                               unsigned sample_count,
                               unsigned storage_sample_count,
                               unsigned usage)
{
        struct vc4_screen *screen = vc4_screen(pscreen);

        if (MAX2(1, sample_count) != MAX2(1, storage_sample_count))
                return false;

        if (sample_count > 1 && sample_count != VC4_MAX_SAMPLES)
                return false;

        if (target >= PIPE_MAX_TEXTURE_TYPES) {
                return false;
        }

        if (usage & PIPE_BIND_VERTEX_BUFFER) {
                switch (format) {
                case PIPE_FORMAT_R32G32B32A32_FLOAT:
                case PIPE_FORMAT_R32G32B32_FLOAT:
                case PIPE_FORMAT_R32G32_FLOAT:
                case PIPE_FORMAT_R32_FLOAT:
                case PIPE_FORMAT_R32G32B32A32_SNORM:
                case PIPE_FORMAT_R32G32B32_SNORM:
                case PIPE_FORMAT_R32G32_SNORM:
                case PIPE_FORMAT_R32_SNORM:
                case PIPE_FORMAT_R32G32B32A32_SSCALED:
                case PIPE_FORMAT_R32G32B32_SSCALED:
                case PIPE_FORMAT_R32G32_SSCALED:
                case PIPE_FORMAT_R32_SSCALED:
                case PIPE_FORMAT_R16G16B16A16_UNORM:
                case PIPE_FORMAT_R16G16B16_UNORM:
                case PIPE_FORMAT_R16G16_UNORM:
                case PIPE_FORMAT_R16_UNORM:
                case PIPE_FORMAT_R16G16B16A16_SNORM:
                case PIPE_FORMAT_R16G16B16_SNORM:
                case PIPE_FORMAT_R16G16_SNORM:
                case PIPE_FORMAT_R16_SNORM:
                case PIPE_FORMAT_R16G16B16A16_USCALED:
                case PIPE_FORMAT_R16G16B16_USCALED:
                case PIPE_FORMAT_R16G16_USCALED:
                case PIPE_FORMAT_R16_USCALED:
                case PIPE_FORMAT_R16G16B16A16_SSCALED:
                case PIPE_FORMAT_R16G16B16_SSCALED:
                case PIPE_FORMAT_R16G16_SSCALED:
                case PIPE_FORMAT_R16_SSCALED:
                case PIPE_FORMAT_R8G8B8A8_UNORM:
                case PIPE_FORMAT_R8G8B8_UNORM:
                case PIPE_FORMAT_R8G8_UNORM:
                case PIPE_FORMAT_R8_UNORM:
                case PIPE_FORMAT_R8G8B8A8_SNORM:
                case PIPE_FORMAT_R8G8B8_SNORM:
                case PIPE_FORMAT_R8G8_SNORM:
                case PIPE_FORMAT_R8_SNORM:
                case PIPE_FORMAT_R8G8B8A8_USCALED:
                case PIPE_FORMAT_R8G8B8_USCALED:
                case PIPE_FORMAT_R8G8_USCALED:
                case PIPE_FORMAT_R8_USCALED:
                case PIPE_FORMAT_R8G8B8A8_SSCALED:
                case PIPE_FORMAT_R8G8B8_SSCALED:
                case PIPE_FORMAT_R8G8_SSCALED:
                case PIPE_FORMAT_R8_SSCALED:
                        break;
                default:
                        return false;
                }
        }

        if ((usage & PIPE_BIND_RENDER_TARGET) &&
            !vc4_rt_format_supported(format)) {
                return false;
        }

        if ((usage & PIPE_BIND_SAMPLER_VIEW) &&
            (!vc4_tex_format_supported(format) ||
             (format == PIPE_FORMAT_ETC1_RGB8 && !screen->has_etc1))) {
                return false;
        }

        if ((usage & PIPE_BIND_DEPTH_STENCIL) &&
            format != PIPE_FORMAT_S8_UINT_Z24_UNORM &&
            format != PIPE_FORMAT_X8Z24_UNORM) {
                return false;
        }

        if ((usage & PIPE_BIND_INDEX_BUFFER) &&
            format != PIPE_FORMAT_R8_UINT &&
            format != PIPE_FORMAT_R16_UINT) {
                return false;
        }

        return true;
}

static const uint64_t *vc4_get_modifiers(struct pipe_screen *pscreen, int *num)
{
        struct vc4_screen *screen = vc4_screen(pscreen);
        static const uint64_t all_modifiers[] = {
                DRM_FORMAT_MOD_BROADCOM_VC4_T_TILED,
                DRM_FORMAT_MOD_LINEAR,
        };
        int m;

        /* We support both modifiers (tiled and linear) for all sampler
         * formats, but if we don't have the DRM_VC4_GET_TILING ioctl
         * we shouldn't advertise the tiled formats.
         */
        if (screen->has_tiling_ioctl) {
                m = 0;
                *num = 2;
        } else{
                m = 1;
                *num = 1;
        }

        return &all_modifiers[m];
}

static void
vc4_screen_query_dmabuf_modifiers(struct pipe_screen *pscreen,
                                  enum pipe_format format, int max,
                                  uint64_t *modifiers,
                                  unsigned int *external_only,
                                  int *count)
{
        const uint64_t *available_modifiers;
        int i;
        bool tex_will_lower;
        int num_modifiers;

        available_modifiers = vc4_get_modifiers(pscreen, &num_modifiers);

        if (!modifiers) {
                *count = num_modifiers;
                return;
        }

        *count = MIN2(max, num_modifiers);
        tex_will_lower = !vc4_tex_format_supported(format);
        for (i = 0; i < *count; i++) {
                modifiers[i] = available_modifiers[i];
                if (external_only)
                        external_only[i] = tex_will_lower;
       }
}

static bool
vc4_screen_is_dmabuf_modifier_supported(struct pipe_screen *pscreen,
                                        uint64_t modifier,
                                        enum pipe_format format,
                                        bool *external_only)
{
        const uint64_t *available_modifiers;
        int i, num_modifiers;

        available_modifiers = vc4_get_modifiers(pscreen, &num_modifiers);

        for (i = 0; i < num_modifiers; i++) {
                if (modifier == available_modifiers[i]) {
                        if (external_only)
                                *external_only = !vc4_tex_format_supported(format);

                        return true;
                }
        }

        return false;
}

static bool
vc4_get_chip_info(struct vc4_screen *screen)
{
        struct drm_vc4_get_param ident0 = {
                .param = DRM_VC4_PARAM_V3D_IDENT0,
        };
        struct drm_vc4_get_param ident1 = {
                .param = DRM_VC4_PARAM_V3D_IDENT1,
        };
        int ret;

        ret = vc4_ioctl(screen->fd, DRM_IOCTL_VC4_GET_PARAM, &ident0);
        if (ret != 0) {
                if (errno == EINVAL) {
                        /* Backwards compatibility with 2835 kernels which
                         * only do V3D 2.1.
                         */
                        screen->v3d_ver = 21;
                        return true;
                } else {
                        mesa_loge("Couldn't get V3D IDENT0: %s",
                                  strerror(errno));
                        return false;
                }
        }
        ret = vc4_ioctl(screen->fd, DRM_IOCTL_VC4_GET_PARAM, &ident1);
        if (ret != 0) {
                mesa_loge("Couldn't get V3D IDENT1: %s",
                          strerror(errno));
                return false;
        }

        uint32_t major = (ident0.value >> 24) & 0xff;
        uint32_t minor = (ident1.value >> 0) & 0xf;
        screen->v3d_ver = major * 10 + minor;

        if (screen->v3d_ver != 21 && screen->v3d_ver != 26) {
                mesa_loge("V3D %d.%d not supported by this version of Mesa.",
                          screen->v3d_ver / 10,
                          screen->v3d_ver % 10);
                return false;
        }

        return true;
}

static int
vc4_screen_get_fd(struct pipe_screen *pscreen)
{
        struct vc4_screen *screen = vc4_screen(pscreen);

        return screen->fd;
}

struct pipe_screen *
vc4_screen_create(int fd, const struct pipe_screen_config *config,
                  struct renderonly *ro)
{
        struct vc4_screen *screen = rzalloc(NULL, struct vc4_screen);
#ifndef USE_VC4_D3DKMT
        uint64_t syncobj_cap = 0;
        int err;
#endif
        struct pipe_screen *pscreen;

        if (!screen)
                return NULL;

        pscreen = &screen->base;

        pscreen->destroy = vc4_screen_destroy;
        pscreen->get_screen_fd = vc4_screen_get_fd;
        pscreen->context_create = vc4_context_create;
        pscreen->is_format_supported = vc4_screen_is_format_supported;
#ifdef USE_VC4_D3DKMT
        pscreen->flush_frontbuffer = vc4_screen_flush_frontbuffer;
#endif

        screen->fd = fd;
        screen->ro = ro;

        list_inithead(&screen->bo_cache.time_list);
        (void) mtx_init(&screen->bo_cache.lock, mtx_plain);
        (void) mtx_init(&screen->bo_handles_mutex, mtx_plain);
        screen->bo_handles = util_hash_table_create_ptr_keys();

        screen->has_control_flow =
                vc4_has_feature(screen, DRM_VC4_PARAM_SUPPORTS_BRANCHES);
        screen->has_etc1 =
                vc4_has_feature(screen, DRM_VC4_PARAM_SUPPORTS_ETC1);
        screen->has_threaded_fs =
                vc4_has_feature(screen, DRM_VC4_PARAM_SUPPORTS_THREADED_FS);
        screen->has_madvise =
                vc4_has_feature(screen, DRM_VC4_PARAM_SUPPORTS_MADVISE);
        screen->has_perfmon_ioctl =
                vc4_has_feature(screen, DRM_VC4_PARAM_SUPPORTS_PERFMON);

#ifdef USE_VC4_D3DKMT
        screen->has_syncobj = false;
#else
        err = drmGetCap(fd, DRM_CAP_SYNCOBJ, &syncobj_cap);
        if (err == 0 && syncobj_cap)
                screen->has_syncobj = true;
#endif

        if (!vc4_get_chip_info(screen))
                goto fail;

        slab_create_parent(&screen->transfer_pool, sizeof(struct vc4_transfer), 16);

        vc4_fence_screen_init(screen);

        vc4_mesa_debug = debug_get_option_vc4_debug();

#ifdef USE_VC4_SIMULATOR
        screen->sim_file = vc4_simulator_init(screen);
#endif

        vc4_resource_screen_init(pscreen);

        for (unsigned i = 0; i <= MESA_SHADER_COMPUTE; i++)
           pscreen->nir_options[i] = vc4_screen_get_compiler_options(pscreen, i);

        pscreen->get_name = vc4_screen_get_name;
        pscreen->get_vendor = vc4_screen_get_vendor;
        pscreen->get_device_vendor = vc4_screen_get_vendor;
        pscreen->query_dmabuf_modifiers = vc4_screen_query_dmabuf_modifiers;
        pscreen->is_dmabuf_modifier_supported = vc4_screen_is_dmabuf_modifier_supported;

        if (screen->has_perfmon_ioctl) {
                pscreen->get_driver_query_group_info = vc4_get_driver_query_group_info;
                pscreen->get_driver_query_info = vc4_get_driver_query_info;
        }

        /* Generate the bitmask of supported draw primitives. */
        screen->prim_types = BITFIELD_BIT(MESA_PRIM_POINTS) |
                             BITFIELD_BIT(MESA_PRIM_LINES) |
                             BITFIELD_BIT(MESA_PRIM_LINE_LOOP) |
                             BITFIELD_BIT(MESA_PRIM_LINE_STRIP) |
                             BITFIELD_BIT(MESA_PRIM_TRIANGLES) |
                             BITFIELD_BIT(MESA_PRIM_TRIANGLE_STRIP) |
                             BITFIELD_BIT(MESA_PRIM_TRIANGLE_FAN);

        vc4_init_shader_caps(screen);
        vc4_init_screen_caps(screen);

        return pscreen;

fail:
#ifndef USE_VC4_D3DKMT
        close(fd);
#endif
        /* The D3DKMT winsys retains fd ownership until creation succeeds. */
        ralloc_free(pscreen);
        return NULL;
}
