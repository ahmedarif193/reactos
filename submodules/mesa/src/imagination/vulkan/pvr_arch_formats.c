/*
 * Copyright © 2022 Imagination Technologies Ltd.
 *
 * SPDX-License-Identifier: MIT
 */

#include "pvr_formats.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <vulkan/vulkan.h>

#include "hwdef/rogue_hw_utils.h"
#include "pvr_common.h"
#include "pvr_csb.h"
#include "pvr_device.h"
#include "pvr_entrypoints.h"
#include "pvr_formats.h"
#include "pvr_macros.h"
#include "pvr_physical_device.h"
#include "util/bitpack_helpers.h"
#include "util/compiler.h"
#include "util/format/format_utils.h"
#include "util/format/u_formats.h"
#include "util/half_float.h"
#include "util/log.h"
#include "util/macros.h"
#include "util/u_math.h"
#include "vk_enum_defines.h"
#include "vk_enum_to_str.h"
#include "vk_format.h"
#include "vk_log.h"
#include "vk_util.h"

/* Convenience */

#define _V PVR_BIND_VERTEX_BUFFER
#define _T PVR_BIND_SAMPLER_VIEW
#define _R PVR_BIND_RENDER_TARGET
#define _Z PVR_BIND_DEPTH_STENCIL
#define _I PVR_BIND_STORAGE_IMAGE

#define FLAGS_V___ (_V)
#define FLAGS__T__ (_T)
#define FLAGS__TR_ (_T | _R)
#define FLAGS__TRI (_T | _R | _I)
#define FLAGS_VT__ (_V | _T)
#define FLAGS_VTR_ (_V | _T | _R)
#define FLAGS_VTRI (_V | _T | _R | _I)
#define FLAGS__T_Z (_T | _Z)

#define FORMAT(vk, tex_fmt, bind_)                         \
   [PIPE_FORMAT_##vk] = {                                  \
      .tex_format = ROGUE_TEXSTATE_FORMAT_##tex_fmt,       \
      .depth_tex_format = ROGUE_TEXSTATE_FORMAT_INVALID,   \
      .stencil_tex_format = ROGUE_TEXSTATE_FORMAT_INVALID, \
      .bind = FLAGS_##bind_,                               \
   }

#define FORMAT_YCBCR(vk, tex_fmt)                          \
   [PIPE_FORMAT_##vk] = {                                  \
      .tex_format = ROGUE_TEXSTATE_FORMAT_##tex_fmt,       \
      .depth_tex_format = ROGUE_TEXSTATE_FORMAT_INVALID,   \
      .stencil_tex_format = ROGUE_TEXSTATE_FORMAT_INVALID, \
      .bind = FLAGS__T__,                                  \
   }

#define FORMAT_COMPRESSED(vk, tex_fmt)                          \
   [PIPE_FORMAT_##vk] = {                                       \
      .tex_format = ROGUE_TEXSTATE_FORMAT_COMPRESSED_##tex_fmt, \
      .depth_tex_format = ROGUE_TEXSTATE_FORMAT_INVALID,        \
      .stencil_tex_format = ROGUE_TEXSTATE_FORMAT_INVALID,      \
      .bind = FLAGS__T__,                                       \
   }

#define FORMAT_DEPTH_STENCIL(vk, combined_fmt, d_fmt, s_fmt) \
   [PIPE_FORMAT_##vk] = {                                    \
      .tex_format = ROGUE_TEXSTATE_FORMAT_##combined_fmt,    \
      .depth_tex_format = ROGUE_TEXSTATE_FORMAT_##d_fmt,     \
      .stencil_tex_format = ROGUE_TEXSTATE_FORMAT_##s_fmt,   \
      .bind = FLAGS__T_Z,                                    \
   }

/* clang-format off */
static const struct pvr_format pvr_format_table[] = {
   FORMAT(A4R4G4B4_UNORM,      A4R4G4B4,     VTR_),
   FORMAT(B5G6R5_UNORM,        R5G6B5,       VTR_),
   FORMAT(B5G5R5A1_UNORM,      A1R5G5B5,     VTR_),
   FORMAT(R8_UNORM,            U8,           VTRI),
   FORMAT(R8_SNORM,            S8,           VTRI),
   FORMAT(R8_UINT,             U8,           VTRI),
   FORMAT(R8_SINT,             S8,           VTRI),
   FORMAT(R8G8_UNORM,          U8U8,         VTRI),
   FORMAT(R8G8_SNORM,          S8S8,         VTRI),
   FORMAT(R8G8_SSCALED,        S8S8,         V___),
   FORMAT(R8G8_UINT,           U8U8,         VTRI),
   FORMAT(R8G8_SINT,           S8S8,         VTRI),
   FORMAT(R8G8B8_UINT,         U8U8U8,       VTR_),
   FORMAT(R8G8B8A8_UNORM,      U8U8U8U8,     VTRI),
   FORMAT(R8G8B8A8_SNORM,      S8S8S8S8,     VTRI),
   FORMAT(R8G8B8A8_UINT,       U8U8U8U8,     VTRI),
   FORMAT(R8G8B8A8_SINT,       S8S8S8S8,     VTRI),
   FORMAT(R8G8B8A8_SRGB,       U8U8U8U8,     _TR_),
   FORMAT(B8G8R8A8_UNORM,      U8U8U8U8,     VTR_),
   FORMAT(B8G8R8A8_SRGB,       U8U8U8U8,     _TR_),
   FORMAT(A8B8G8R8_UNORM,      U8U8U8U8,     VTR_),
   FORMAT(A8B8G8R8_SNORM,      S8S8S8S8,     VTR_),
   FORMAT(A8B8G8R8_UINT,       U8U8U8U8,     VTR_),
   FORMAT(A8B8G8R8_SINT,       S8S8S8S8,     VTR_),
   FORMAT(A8B8G8R8_SRGB,       U8U8U8U8,     _TR_),
   FORMAT(B10G10R10A2_USCALED, INVALID,      V___),
   FORMAT(B10G10R10A2_SSCALED, INVALID,      V___),
   FORMAT(R10G10B10A2_UNORM,   A2R10B10G10,  VTRI),
   FORMAT(R10G10B10A2_SNORM,   A2R10B10G10,  V___),
   FORMAT(R10G10B10A2_USCALED, INVALID,      V___),
   FORMAT(R10G10B10A2_SSCALED, INVALID,      V___),
   FORMAT(R10G10B10A2_UINT,    A2R10B10G10,  VTRI),
   FORMAT(R16_UNORM,           U16,          VTRI),
   FORMAT(R16_SNORM,           S16,          VTRI),
   FORMAT(R16_UINT,            U16,          VTRI),
   FORMAT(R16_SINT,            S16,          VTRI),
   FORMAT(R16_FLOAT,           F16,          VTRI),
   FORMAT(R16G16_UNORM,        U16U16,       VTRI),
   FORMAT(R16G16_SNORM,        S16S16,       VTRI),
   FORMAT(R16G16_UINT,         U16U16,       VTRI),
   FORMAT(R16G16_SINT,         S16S16,       VTRI),
   FORMAT(R16G16_FLOAT,        F16F16,       VTRI),
   FORMAT(R16G16B16_SNORM,     S16S16S16,    VTR_),
   FORMAT(R16G16B16_UINT,      U16U16U16,    VTR_),
   FORMAT(R16G16B16_SINT,      S16S16S16,    VTR_),
   FORMAT(R16G16B16A16_UNORM,  U16U16U16U16, VTRI),
   FORMAT(R16G16B16A16_SNORM,  S16S16S16S16, VTRI),
   FORMAT(R16G16B16A16_UINT,   U16U16U16U16, VTRI),
   FORMAT(R16G16B16A16_SINT,   S16S16S16S16, VTRI),
   FORMAT(R16G16B16A16_FLOAT,  F16F16F16F16, VTRI),
   FORMAT(R32_UINT,            U32,          VTRI),
   FORMAT(R32_SINT,            S32,          VTRI),
   FORMAT(R32_FLOAT,           F32,          VTRI),
   FORMAT(R32G32_UINT,         U32U32,       VTRI),
   FORMAT(R32G32_SINT,         S32S32,       VTRI),
   FORMAT(R32G32_FLOAT,        F32F32,       VTRI),
   FORMAT(R32G32B32_UINT,      U32U32U32,    VTR_),
   FORMAT(R32G32B32_SINT,      S32S32S32,    VTR_),
   FORMAT(R32G32B32_FLOAT,     F32F32F32,    VTR_),
   FORMAT(R32G32B32A32_UINT,   U32U32U32U32, VTRI),
   FORMAT(R32G32B32A32_SINT,   S32S32S32S32, VTRI),
   FORMAT(R32G32B32A32_FLOAT,  F32F32F32F32, VTRI),
   FORMAT(R11G11B10_FLOAT,     F10F11F11,    _TRI),
   FORMAT(R9G9B9E5_FLOAT,      SE9995,       VT__),
   FORMAT_DEPTH_STENCIL(Z16_UNORM, U16, U16, INVALID),
   FORMAT_DEPTH_STENCIL(Z24X8_UNORM, X8U24, X8U24, INVALID),
   FORMAT_DEPTH_STENCIL(Z32_FLOAT, F32, F32, INVALID),
   FORMAT_DEPTH_STENCIL(S8_UINT, U8, INVALID, U8),
   FORMAT_DEPTH_STENCIL(Z24_UNORM_S8_UINT, ST8U24, X8U24, U8X24),
   FORMAT_DEPTH_STENCIL(Z32_FLOAT_S8X24_UINT, X24U8F32, X24X8F32, X24G8X32),
   FORMAT_COMPRESSED(ETC2_RGB8, ETC2_RGB),
   FORMAT_COMPRESSED(ETC2_SRGB8, ETC2_RGB),
   FORMAT_COMPRESSED(ETC2_RGB8A1, ETC2_PUNCHTHROUGHA),
   FORMAT_COMPRESSED(ETC2_SRGB8A1, ETC2_PUNCHTHROUGHA),
   FORMAT_COMPRESSED(ETC2_RGBA8, ETC2A_RGBA),
   FORMAT_COMPRESSED(ETC2_SRGBA8, ETC2A_RGBA),
   FORMAT_COMPRESSED(ETC2_R11_UNORM, EAC_R11_UNSIGNED),
   FORMAT_COMPRESSED(ETC2_R11_SNORM, EAC_R11_SIGNED),
   FORMAT_COMPRESSED(ETC2_RG11_UNORM, EAC_RG11_UNSIGNED),
   FORMAT_COMPRESSED(ETC2_RG11_SNORM, EAC_RG11_SIGNED),
   FORMAT_YCBCR(G8_B8R8_420_UNORM, YVU420_2PLANE),
   FORMAT_YCBCR(G8_B8_R8_420_UNORM, YUV420_3PLANE),
};
/* clang-format on */

#undef FORMAT_YCBCR
#undef FORMAT
#undef FORMAT_DEPTH_STENCIL
#undef FORMAT_COMPRESSED

#define FORMAT(vk, pack_mode_, accum_format_)               \
   [PIPE_FORMAT_##vk] = {                                   \
      .packmode = ROGUE_PBESTATE_PACKMODE_##pack_mode_,     \
      .accum_format = PVR_PBE_ACCUM_FORMAT_##accum_format_, \
   }

#define FORMAT_DEPTH_STENCIL(vk, combined_fmt)            \
   [PIPE_FORMAT_##vk] = {                                 \
      .packmode = ROGUE_PBESTATE_PACKMODE_##combined_fmt, \
      .accum_format = PVR_PBE_ACCUM_FORMAT_INVALID,       \
   }

struct pvr_pbe_format {
   enum ROGUE_PBESTATE_PACKMODE packmode;
   enum pvr_pbe_accum_format accum_format;
};

static const struct pvr_pbe_format pvr_pbe_format_table[] = {
   FORMAT(A4R4G4B4_UNORM, A4R4G4B4, U8),
   FORMAT(B5G6R5_UNORM, R5G6B5, U8),
   FORMAT(B5G5R5A1_UNORM, A1R5G5B5, U8),
   FORMAT(R8_UNORM, U8, U8),
   FORMAT(R8_SNORM, S8, S8),
   FORMAT(R8_UINT, U8, UINT8),
   FORMAT(R8_SINT, S8, SINT8),
   FORMAT(R8G8_UNORM, U8U8, U8),
   FORMAT(R8G8_SNORM, S8S8, S8),
   FORMAT(R8G8_SSCALED, S8S8, INVALID),
   FORMAT(R8G8_UINT, U8U8, UINT8),
   FORMAT(R8G8_SINT, S8S8, SINT8),
   FORMAT(R8G8B8_UINT, U8U8U8, UINT8),
   FORMAT(R8G8B8A8_UNORM, U8U8U8U8, U8),
   FORMAT(R8G8B8A8_SNORM, S8S8S8S8, S8),
   FORMAT(R8G8B8A8_UINT, U8U8U8U8, UINT8),
   FORMAT(R8G8B8A8_SINT, S8S8S8S8, SINT8),
   FORMAT(R8G8B8A8_SRGB, U8U8U8U8, F16),
   FORMAT(B8G8R8A8_UNORM, U8U8U8U8, U8),
   FORMAT(B8G8R8A8_SRGB, U8U8U8U8, F16),
   FORMAT(A8B8G8R8_UNORM, U8U8U8U8, U8),
   FORMAT(A8B8G8R8_SNORM, S8S8S8S8, S8),
   FORMAT(A8B8G8R8_UINT, U8U8U8U8, UINT8),
   FORMAT(A8B8G8R8_SINT, S8S8S8S8, SINT8),
   FORMAT(A8B8G8R8_SRGB, U8U8U8U8, F16),
   FORMAT(B10G10R10A2_USCALED, INVALID, INVALID),
   FORMAT(B10G10R10A2_SSCALED, INVALID, INVALID),
   FORMAT(R10G10B10A2_UNORM, A2R10B10G10, F16),
   FORMAT(R10G10B10A2_SNORM, A2R10B10G10, F16),
   FORMAT(R10G10B10A2_USCALED, INVALID, INVALID),
   FORMAT(R10G10B10A2_SSCALED, INVALID, INVALID),
   FORMAT(R10G10B10A2_UINT, U32, U1010102),
   FORMAT(R16_UNORM, U16, U16),
   FORMAT(R16_SNORM, S16, S16),
   FORMAT(R16_UINT, U16, UINT16),
   FORMAT(R16_SINT, S16, SINT16),
   FORMAT(R16_FLOAT, F16, F16),
   FORMAT(R16G16_UNORM, U16U16, U16),
   FORMAT(R16G16_SNORM, S16S16, S16),
   FORMAT(R16G16_UINT, U16U16, UINT16),
   FORMAT(R16G16_SINT, S16S16, SINT16),
   FORMAT(R16G16_FLOAT, F16F16, F16),
   FORMAT(R16G16B16_SNORM, S16S16S16, S16),
   FORMAT(R16G16B16_UINT, U16U16U16, UINT16),
   FORMAT(R16G16B16_SINT, S16S16S16, SINT16),
   FORMAT(R16G16B16A16_UNORM, U16U16U16U16, U16),
   FORMAT(R16G16B16A16_SNORM, S16S16S16S16, S16),
   FORMAT(R16G16B16A16_UINT, U16U16U16U16, UINT16),
   FORMAT(R16G16B16A16_SINT, S16S16S16S16, SINT16),
   FORMAT(R16G16B16A16_FLOAT, F16F16F16F16, F16),
   FORMAT(R32_UINT, U32, UINT32),
   FORMAT(R32_SINT, S32, SINT32),
   FORMAT(R32_FLOAT, F32, F32),
   FORMAT(R32G32_UINT, U32U32, UINT32),
   FORMAT(R32G32_SINT, S32S32, SINT32),
   FORMAT(R32G32_FLOAT, F32F32, F32),
   FORMAT(R32G32B32_UINT, U32U32U32, UINT32),
   FORMAT(R32G32B32_SINT, S32S32S32, SINT32),
   FORMAT(R32G32B32_FLOAT, F32F32F32, F32),
   FORMAT(R32G32B32A32_UINT, U32U32U32U32, UINT32),
   FORMAT(R32G32B32A32_SINT, S32S32S32S32, SINT32),
   FORMAT(R32G32B32A32_FLOAT, F32F32F32F32, F32),
   FORMAT(R11G11B10_FLOAT, F10F11F11, F16),
   FORMAT(R9G9B9E5_FLOAT, SE9995, INVALID),
   FORMAT_DEPTH_STENCIL(Z16_UNORM, U16),
   FORMAT_DEPTH_STENCIL(Z24X8_UNORM, X8U24),
   FORMAT_DEPTH_STENCIL(Z32_FLOAT, F32),
   FORMAT_DEPTH_STENCIL(S8_UINT, U8),
   FORMAT_DEPTH_STENCIL(Z24_UNORM_S8_UINT, ST8U24),
   FORMAT_DEPTH_STENCIL(Z32_FLOAT_S8X24_UINT, X24U8F32),
};

#undef FORMAT
#undef FORMAT_DEPTH_STENCIL

struct pvr_format_table pvr_arch_get_format_table(void)
{
   return (struct pvr_format_table){
      .formats = pvr_format_table,
      .count = ARRAY_SIZE(pvr_format_table),
   };
}

static inline const struct pvr_format *get_format(VkFormat vk_format)
{
   enum pipe_format format = vk_format_to_pipe_format(vk_format);
   if (format < ARRAY_SIZE(pvr_format_table) &&
       pvr_format_table[format].bind != 0) {
      return &pvr_format_table[format];
   }

   mesa_logd("Format %s(%d) not supported\n",
             vk_Format_to_str(vk_format),
             vk_format);

   return NULL;
}

static inline const struct pvr_pbe_format *
pvr_get_pbe_format(VkFormat vk_format)
{
   enum pipe_format format = vk_format_to_pipe_format(vk_format);
   assert(format < ARRAY_SIZE(pvr_pbe_format_table));
   return &pvr_pbe_format_table[format];
}

uint32_t pvr_arch_get_tex_format(VkFormat vk_format)
{
   const struct pvr_format *pvr_format = get_format(vk_format);
   if (pvr_format) {
      return pvr_format->tex_format;
   }

   return ROGUE_TEXSTATE_FORMAT_INVALID;
}

uint32_t pvr_arch_get_tex_format_aspect(VkFormat vk_format,
                                        VkImageAspectFlags aspect_mask)
{
   const struct pvr_format *pvr_format = get_format(vk_format);
   if (pvr_format) {
      if (aspect_mask == VK_IMAGE_ASPECT_DEPTH_BIT)
         return pvr_format->depth_tex_format;
      else if (aspect_mask == VK_IMAGE_ASPECT_STENCIL_BIT)
         return pvr_format->stencil_tex_format;

      return pvr_format->tex_format;
   }

   return ROGUE_TEXSTATE_FORMAT_INVALID;
}

uint32_t pvr_arch_get_pbe_packmode(VkFormat vk_format)
{
   if (vk_format_is_block_compressed(vk_format))
      return ROGUE_PBESTATE_PACKMODE_INVALID;

   return pvr_get_pbe_format(vk_format)->packmode;
}

uint32_t pvr_arch_get_pbe_accum_format(VkFormat vk_format)
{
   if (vk_format_is_block_compressed(vk_format))
      return PVR_PBE_ACCUM_FORMAT_INVALID;

   return pvr_get_pbe_format(vk_format)->accum_format;
}

bool pvr_arch_format_is_pbe_downscalable(const struct pvr_device_info *dev_info,
                                         VkFormat vk_format)
{
   if (vk_format_is_int(vk_format)) {
      /* PBE downscale behavior for integer formats does not match Vulkan
       * spec. Vulkan requires a single sample to be chosen instead of
       * taking the average sample color.
       */
      return false;
   }

   switch (pvr_arch_get_pbe_packmode(vk_format)) {
   default:
      return true;
   case ROGUE_PBESTATE_PACKMODE_F16:
      return PVR_HAS_FEATURE(dev_info, pbe_filterable_f16);
   case ROGUE_PBESTATE_PACKMODE_U16U16U16U16:
   case ROGUE_PBESTATE_PACKMODE_S16S16S16S16:
   case ROGUE_PBESTATE_PACKMODE_U32U32U32U32:
   case ROGUE_PBESTATE_PACKMODE_S32S32S32S32:
   case ROGUE_PBESTATE_PACKMODE_F32F32F32F32:
   case ROGUE_PBESTATE_PACKMODE_U16U16U16:
   case ROGUE_PBESTATE_PACKMODE_S16S16S16:
   case ROGUE_PBESTATE_PACKMODE_U32U32U32:
   case ROGUE_PBESTATE_PACKMODE_S32S32S32:
   case ROGUE_PBESTATE_PACKMODE_F32F32F32:
   case ROGUE_PBESTATE_PACKMODE_U16U16:
   case ROGUE_PBESTATE_PACKMODE_S16S16:
   case ROGUE_PBESTATE_PACKMODE_U32U32:
   case ROGUE_PBESTATE_PACKMODE_S32S32:
   case ROGUE_PBESTATE_PACKMODE_F32F32:
   case ROGUE_PBESTATE_PACKMODE_U24ST8:
   case ROGUE_PBESTATE_PACKMODE_ST8U24:
   case ROGUE_PBESTATE_PACKMODE_U16:
   case ROGUE_PBESTATE_PACKMODE_S16:
   case ROGUE_PBESTATE_PACKMODE_U32:
   case ROGUE_PBESTATE_PACKMODE_S32:
   case ROGUE_PBESTATE_PACKMODE_F32:
   case ROGUE_PBESTATE_PACKMODE_X24U8F32:
   case ROGUE_PBESTATE_PACKMODE_X24X8F32:
   case ROGUE_PBESTATE_PACKMODE_X24G8X32:
   case ROGUE_PBESTATE_PACKMODE_X8U24:
   case ROGUE_PBESTATE_PACKMODE_U8X24:
   case ROGUE_PBESTATE_PACKMODE_PBYTE:
   case ROGUE_PBESTATE_PACKMODE_PWORD:
   case ROGUE_PBESTATE_PACKMODE_INVALID:
      return false;
   }
}
