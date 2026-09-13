/*
 * Copyright © 2022 Imagination Technologies Ltd.
 *
 * SPDX-License-Identifier: MIT
 */

#include "pvr_image.h"

#include <assert.h>
#include <stdint.h>

#include "vk_log.h"
#include "vk_ycbcr_conversion.h"

#include "pvr_buffer.h"
#include "pvr_csb.h"
#include "pvr_device.h"
#include "pvr_entrypoints.h"
#include "pvr_macros.h"
#include "pvr_physical_device.h"
#include "pvr_tex_state.h"
#include "pvr_ycbcr.h"

static void pvr_adjust_non_compressed_view(const struct pvr_image *image,
                                           const struct pvr_image_plane *plane,
                                           struct pvr_texture_state_info *info)
{
   const uint32_t base_level = info->base_level;

   if (!vk_format_is_compressed(image->vk.format) ||
       vk_format_is_compressed(info->format)) {
      return;
   }

   /* Cannot use the image state, as the miplevel sizes for an
    * uncompressed chain view may not decrease by 2 each time compared to the
    * compressed one e.g. (22x22,11x11,5x5) -> (6x6,3x3,2x2)
    * Instead manually apply an offset and patch the size
    */
   info->extent.width = u_minify(info->extent.width, base_level);
   info->extent.height = u_minify(info->extent.height, base_level);
   info->extent.depth = u_minify(info->extent.depth, base_level);
   info->extent = vk_image_extent_to_elements(&image->vk, info->extent);
   info->offset += plane->mip_levels[base_level].offset;
   info->base_level = 0;
}

static unsigned int pvr_ycbcr_csc_index(struct vk_ycbcr_conversion *conversion)
{
#define MAPPING(vk, slot_full, slot_narrow)     \
   [VK_SAMPLER_YCBCR_MODEL_CONVERSION_##vk] = { \
      PVR_YCBCR_SLOT_##slot_full,               \
      PVR_YCBCR_SLOT_##slot_narrow              \
   }

   static unsigned int mapping[][2] = {
      MAPPING(RGB_IDENTITY, RGB_IDENTITY, RGB_IDENTITY),
      MAPPING(YCBCR_IDENTITY, YCBCR_IDENTITY_FULL, YCBCR_IDENTITY),
      MAPPING(YCBCR_709, YCBCR_709_FULL, YCBCR_709),
      MAPPING(YCBCR_601, YCBCR_601_FULL, YCBCR_601),
      MAPPING(YCBCR_2020, YCBCR_2020_FULL, YCBCR_2020),
   };
   return mapping[conversion->state.ycbcr_model][conversion->state.ycbcr_range];

#undef MAPPING
}

VkResult PVR_PER_ARCH(CreateImageView)(VkDevice _device,
                                       const VkImageViewCreateInfo *pCreateInfo,
                                       const VkAllocationCallbacks *pAllocator,
                                       VkImageView *pView)
{
   VK_FROM_HANDLE(pvr_device, device, _device);
   struct pvr_texture_state_info info = { 0 };
   unsigned char input_swizzle[4];
   const uint8_t *format_swizzle;
   const struct pvr_image *image;
   struct pvr_image_view *iview;
   VkResult result;

   iview = vk_image_view_create(&device->vk,
                                pCreateInfo,
                                pAllocator,
                                sizeof(*iview));
   if (!iview)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   image = pvr_image_view_get_image(iview);
   const struct pvr_image_plane *plane;

   const struct vk_format_ycbcr_info *ycbcr_image =
      vk_format_get_ycbcr_info(image->vk.format);
   const struct vk_format_ycbcr_info *ycbcr_iview =
      vk_format_get_ycbcr_info(iview->vk.format);
   if (ycbcr_image && !ycbcr_iview) {
      plane = pvr_plane_from_aspect_const(image, iview->vk.aspects);
   } else {
      plane = &image->planes[0];
   }

   if (image->vk.image_type == VK_IMAGE_TYPE_3D &&
       (iview->vk.view_type == VK_IMAGE_VIEW_TYPE_2D_ARRAY ||
        iview->vk.view_type == VK_IMAGE_VIEW_TYPE_2D)) {
      iview->vk.layer_count = image->vk.extent.depth;
   }

   info.type = iview->vk.view_type;
   info.base_level = iview->vk.base_mip_level;
   info.mip_levels = iview->vk.level_count;
   info.extent = image->vk.extent;
   info.aspect_mask = iview->vk.aspects;
   info.is_cube = (info.type == VK_IMAGE_VIEW_TYPE_CUBE ||
                   info.type == VK_IMAGE_VIEW_TYPE_CUBE_ARRAY);
   info.array_size = iview->vk.layer_count;
   info.offset = iview->vk.base_array_layer * plane->layer_size;
   info.mipmaps_present = (image->vk.mip_levels > 1) ? true : false;
   info.stride = plane->physical_extent.width;
   info.tex_state_type = PVR_TEXTURE_STATE_SAMPLE;
   info.mem_layout = image->memlayout;
   info.flags = 0;
   info.sample_count = image->vk.samples;
   info.addr = image->dev_addr;

   info.format = pCreateInfo->format;
   info.layer_size = plane->layer_size;

   if (image->vk.create_flags & VK_IMAGE_CREATE_2D_VIEW_COMPATIBLE_BIT_EXT) {
      info.offset = 0;
      info.z_slice = iview->vk.base_array_layer;
   }

   pvr_adjust_non_compressed_view(image, plane, &info);

   if (ycbcr_iview) {
      const VkSamplerYcbcrConversionInfo *pConversion =
         vk_find_struct_const(pCreateInfo->pNext,
                              SAMPLER_YCBCR_CONVERSION_INFO);
      VK_FROM_HANDLE(vk_ycbcr_conversion, conversion, pConversion->conversion);
      assert(conversion);

      /* UV swizzle is handled by chroma swap instead */
      input_swizzle[0] = PIPE_SWIZZLE_X;
      input_swizzle[1] = PIPE_SWIZZLE_Y;
      input_swizzle[2] = PIPE_SWIZZLE_Z;
      input_swizzle[3] = PIPE_SWIZZLE_1;

      if (conversion->state.mapping[3] == VK_COMPONENT_SWIZZLE_ZERO) {
         input_swizzle[3] = PIPE_SWIZZLE_0;
      }

      info.swap_chroma = conversion->state.mapping[0] == VK_COMPONENT_SWIZZLE_B;

      info.csc_coeff_index = pvr_ycbcr_csc_index(conversion);

      pvr_csb_pack (&iview->sampler_words[0], TEXSTATE_SAMPLER_WORD0, cfg) {
         cfg.texaddr_plane2_lo =
            PVR_DEV_ADDR_OFFSET(image->dev_addr, image->planes[1].offset);
      }
      pvr_csb_pack (&iview->sampler_words[1], TEXSTATE_SAMPLER_WORD1, cfg) {
         cfg.texaddr_plane2_hi =
            PVR_DEV_ADDR_OFFSET(image->dev_addr, image->planes[1].offset);
         cfg.texaddr_plane3 =
            PVR_DEV_ADDR_OFFSET(image->dev_addr, image->planes[2].offset);
      }
   } else {
      vk_component_mapping_to_pipe_swizzle(iview->vk.swizzle, input_swizzle);
   }

   enum pipe_format pipe_format =
      vk_format_to_pipe_format(iview->vk.view_format);
   if (util_format_is_depth_or_stencil(pipe_format)) {
      switch (pipe_format) {
      case PIPE_FORMAT_S8_UINT:
         pipe_format = PIPE_FORMAT_R8_UINT;
         break;

      case PIPE_FORMAT_Z16_UNORM:
         pipe_format = PIPE_FORMAT_R16_UINT;
         break;

      case PIPE_FORMAT_Z32_FLOAT:
         pipe_format = PIPE_FORMAT_R32_FLOAT;
         break;

      default:
         break;
      }
   }
   format_swizzle = util_format_description(pipe_format)->swizzle;

   util_format_compose_swizzles(format_swizzle, input_swizzle, info.swizzle);

   result = pvr_arch_pack_tex_state(device,
                                    &info,
                                    &iview->image_state[info.tex_state_type]);
   if (result != VK_SUCCESS)
      goto err_vk_image_view_destroy;

   /* Create an additional texture state for cube type if storage
    * usage flag is set.
    */
   if (info.is_cube && image->vk.usage & VK_IMAGE_USAGE_STORAGE_BIT) {
      info.tex_state_type = PVR_TEXTURE_STATE_STORAGE;

      result =
         pvr_arch_pack_tex_state(device,
                                 &info,
                                 &iview->image_state[info.tex_state_type]);
      if (result != VK_SUCCESS)
         goto err_vk_image_view_destroy;
   }

   if (image->vk.usage & VK_IMAGE_USAGE_INPUT_ATTACHMENT_BIT) {
      /* Attachment state is created as if the mipmaps are not supported, so the
       * baselevel is set to zero and num_mip_levels is set to 1. Which gives an
       * impression that this is the only level in the image. This also requires
       * that width, height and depth be adjusted as well. Given
       * iview->vk.extent is already adjusted for base mip map level we use it
       * here.
       */
      /* TODO: Investigate and document the reason for above approach. */
      info.extent = iview->vk.extent;

      info.mip_levels = 1;
      info.mipmaps_present = false;
      info.stride = u_minify(plane->physical_extent.width, info.base_level);
      info.base_level = 0;
      info.tex_state_type = PVR_TEXTURE_STATE_ATTACHMENT;

      if (image->vk.image_type == VK_IMAGE_TYPE_3D &&
          iview->vk.view_type == VK_IMAGE_VIEW_TYPE_2D) {
         info.type = VK_IMAGE_VIEW_TYPE_3D;
      } else {
         info.type = iview->vk.view_type;
      }

      result =
         pvr_arch_pack_tex_state(device,
                                 &info,
                                 &iview->image_state[info.tex_state_type]);
      if (result != VK_SUCCESS)
         goto err_vk_image_view_destroy;
   }

   *pView = pvr_image_view_to_handle(iview);

   return VK_SUCCESS;

err_vk_image_view_destroy:
   vk_image_view_destroy(&device->vk, pAllocator, &iview->vk);

   return result;
}

void PVR_PER_ARCH(DestroyImageView)(VkDevice _device,
                                    VkImageView _iview,
                                    const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(pvr_device, device, _device);
   VK_FROM_HANDLE(pvr_image_view, iview, _iview);

   if (!iview)
      return;

   vk_image_view_destroy(&device->vk, pAllocator, &iview->vk);
}

VkResult
PVR_PER_ARCH(CreateBufferView)(VkDevice _device,
                               const VkBufferViewCreateInfo *pCreateInfo,
                               const VkAllocationCallbacks *pAllocator,
                               VkBufferView *pView)
{
   VK_FROM_HANDLE(pvr_buffer, buffer, pCreateInfo->buffer);
   VK_FROM_HANDLE(pvr_device, device, _device);
   struct pvr_texture_state_info info = { 0 };
   const uint8_t *format_swizzle;
   struct pvr_buffer_view *bview;
   VkResult result;

   bview = vk_buffer_view_create(&device->vk,
                                 pCreateInfo,
                                 pAllocator,
                                 sizeof(*bview));
   if (!bview)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   /* If the remaining size of the buffer is not a multiple of the element
    * size of the format, the nearest smaller multiple is used.
    */
   bview->vk.range -=
      bview->vk.range % vk_format_get_blocksize(bview->vk.format);

   /* The range of the buffer view shouldn't be smaller than one texel. */
   assert(bview->vk.range >= vk_format_get_blocksize(bview->vk.format));

   bview->num_rows = DIV_ROUND_UP(bview->vk.elements, PVR_BUFFER_VIEW_WIDTH);

   info.base_level = 0U;
   info.mip_levels = 1U;
   info.mipmaps_present = false;
   info.extent.width = PVR_BUFFER_VIEW_WIDTH;
   info.extent.height = bview->num_rows;
   info.extent.depth = 0U;
   info.sample_count = 1U;
   info.stride = PVR_BUFFER_VIEW_WIDTH;
   info.offset = 0U;
   info.addr = PVR_DEV_ADDR_OFFSET(buffer->dev_addr, pCreateInfo->offset);
   info.mem_layout = PVR_MEMLAYOUT_LINEAR;
   info.is_cube = false;
   info.type = VK_IMAGE_VIEW_TYPE_2D;
   info.tex_state_type = PVR_TEXTURE_STATE_SAMPLE;
   info.format = bview->vk.format;
   info.flags = PVR_TEXFLAGS_INDEX_LOOKUP;
   info.aspect_mask = VK_IMAGE_ASPECT_COLOR_BIT;
   info.buffer_elems = bview->vk.elements;

   if (PVR_HAS_FEATURE(&device->pdevice->dev_info, tpu_array_textures))
      info.array_size = 1U;

   format_swizzle = pvr_get_format_swizzle(info.format);
   memcpy(info.swizzle, format_swizzle, sizeof(info.swizzle));

   result = pvr_arch_pack_tex_state(device, &info, &bview->image_state);
   if (result != VK_SUCCESS)
      goto err_vk_buffer_view_destroy;

   *pView = pvr_buffer_view_to_handle(bview);

   return VK_SUCCESS;

err_vk_buffer_view_destroy:
   vk_object_free(&device->vk, pAllocator, bview);

   return result;
}

void PVR_PER_ARCH(DestroyBufferView)(VkDevice _device,
                                     VkBufferView bufferView,
                                     const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(pvr_buffer_view, bview, bufferView);
   VK_FROM_HANDLE(pvr_device, device, _device);

   if (!bview)
      return;

   vk_buffer_view_destroy(&device->vk, pAllocator, &bview->vk);
}
