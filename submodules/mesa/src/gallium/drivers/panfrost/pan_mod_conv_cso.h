/*
 * Copyright (C) 2023 Amazon.com, Inc. or its affiliates
 * SPDX-License-Identifier: MIT
 */

#ifndef __PAN_MOD_CONV_CSO_H__
#define __PAN_MOD_CONV_CSO_H__

#include "util/hash_table.h"

#include "panfrost/compiler/pan_compiler.h"

#include "drm-uapi/drm_fourcc.h"

struct panfrost_context;
struct panfrost_resource;
struct panfrost_screen;

struct pan_mod_convert_shader_key {
   uint64_t mod;
   union {
      struct {
         unsigned uncompressed_size;
         unsigned align;
      } afbc;
      struct {
         unsigned has_y  : 1;
         unsigned has_uv : 1;
         unsigned unused : 30;
      } mtk_tiled;
   };
};

struct pan_mod_convert_shader_data {
   struct pan_mod_convert_shader_key key;
   union {
      struct {
         void *size_cso;
         void *pack_cso;
      } afbc;
      struct {
         void *detile_cso;
      } mtk_tiled;
   };
};

struct pan_mod_convert_shaders {
   struct hash_table *shaders;
   pthread_mutex_t lock;
};

struct panfrost_afbc_size_info {
   uint64_t src;
   uint64_t layout;
} PACKED;

struct panfrost_afbc_pack_info {
   uint64_t src;
   uint64_t dst;
   uint64_t layout;
   uint32_t header_size;
   uint32_t src_stride;
   uint32_t dst_stride;
   uint32_t padding[3]; // FIXME
} PACKED;

struct panfrost_mtk_detile_info {
   uint32_t src_y_row_stride_tl;
   uint32_t src_uv_row_stride_tl;
   uint32_t width;
   uint32_t height;
} PACKED;

void panfrost_afbc_context_init(struct panfrost_context *ctx);
void panfrost_afbc_context_destroy(struct panfrost_context *ctx);

struct pan_mod_convert_shader_data *
panfrost_get_afbc_pack_shaders(struct panfrost_context *ctx,
                               struct panfrost_resource *rsrc, unsigned align);

struct pan_mod_convert_shader_data *
panfrost_get_mtk_detile_shader(struct panfrost_context *ctx, bool has_y,
                               bool has_uv);

#define drm_is_mtk_tiled(mod)                                                  \
   ((mod >> 52) == (0 | (DRM_FORMAT_MOD_VENDOR_MTK << 4)))

/* check for whether a format can be used with MTK_16L32S format */

static inline bool
panfrost_format_supports_mtk_tiled(enum pipe_format format)
{
   switch (format) {
   case PIPE_FORMAT_NV12:
   case PIPE_FORMAT_R8_G8B8_420_UNORM:
   case PIPE_FORMAT_R8_UNORM:
   case PIPE_FORMAT_R8G8_UNORM:
      return true;
   default:
      return false;
   }
}

#define PANFROST_EMULATED_MODIFIERS(__name)                                    \
   static const uint64_t __name[] = {                                          \
      DRM_FORMAT_MOD_MTK_16L_32S_TILE,                                         \
   }

static inline bool panfrost_is_emulated_mod(uint64_t mod)
{
   PANFROST_EMULATED_MODIFIERS(emulated_mods);

   for (unsigned i = 0; i < ARRAY_SIZE(emulated_mods); i++) {
      if (emulated_mods[i] == mod)
         return true;
   }

   return false;
}

#endif
