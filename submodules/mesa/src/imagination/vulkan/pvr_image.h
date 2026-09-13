/*
 * Copyright © 2022 Imagination Technologies Ltd.
 *
 * based in part on anv driver which is:
 * Copyright © 2015 Intel Corporation
 *
 * based in part on radv driver which is:
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef PVR_IMAGE_H
#define PVR_IMAGE_H

#include <stdint.h>

#include "vk_image.h"

#include "pvr_common.h"
#include "pvr_types.h"

#define PVR_MAX_PLANE_COUNT 3

struct pvr_device;

struct pvr_mip_level {
   /* Offset of the mip level in bytes */
   uint32_t offset;

   /* Aligned mip level size in bytes */
   uint32_t size;

   /* Aligned row length in bytes */
   uint32_t pitch;

   /* Aligned height in bytes */
   uint32_t height_pitch;
};

struct pvr_image_plane {
   /* Derived and other state */
   VkExtent3D physical_extent;

   VkDeviceSize layer_size;
   VkDeviceSize size;
   VkDeviceSize offset;

   struct pvr_mip_level mip_levels[14];
};

struct pvr_image {
   struct vk_image vk;

   /* vma this image is bound to */
   struct pvr_winsys_vma *vma;

   /* Device address the image is mapped to in device virtual address space */
   pvr_dev_addr_t dev_addr;

   enum pvr_memlayout memlayout;

   VkDeviceSize alignment;
   VkDeviceSize total_size;

   uint8_t plane_count;
   struct pvr_image_plane planes[PVR_MAX_PLANE_COUNT];
};

/* Gets the first plane and asserts we only have one plane. For use in areas
 * where we never want to deal with multiplanar images.
 */
static inline struct pvr_image_plane *pvr_single_plane(struct pvr_image *image)
{
   assert(image->plane_count == 1);
   return &image->planes[0];
}

static inline const struct pvr_image_plane *
pvr_single_plane_const(const struct pvr_image *image)
{
   assert(image->plane_count == 1);
   return &image->planes[0];
}

static inline struct pvr_image_plane *
pvr_plane_from_aspect(struct pvr_image *image, VkImageAspectFlags aspect)
{
   switch (aspect) {
   case VK_IMAGE_ASPECT_COLOR_BIT:
   case VK_IMAGE_ASPECT_DEPTH_BIT:
   case VK_IMAGE_ASPECT_STENCIL_BIT:
   case VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT:
      return pvr_single_plane(image);
   case VK_IMAGE_ASPECT_PLANE_0_BIT:
   case VK_IMAGE_ASPECT_MEMORY_PLANE_0_BIT_EXT:
      return &image->planes[0];
   case VK_IMAGE_ASPECT_PLANE_1_BIT:
   case VK_IMAGE_ASPECT_MEMORY_PLANE_1_BIT_EXT:
      return &image->planes[1];
   case VK_IMAGE_ASPECT_PLANE_2_BIT:
   case VK_IMAGE_ASPECT_MEMORY_PLANE_2_BIT_EXT:
      return &image->planes[2];
   default:
      UNREACHABLE("invalid image aspect");
   }
}

static inline const struct pvr_image_plane *
pvr_plane_from_aspect_const(const struct pvr_image *image,
                            VkImageAspectFlags aspect)
{
   switch (aspect) {
   case VK_IMAGE_ASPECT_COLOR_BIT:
   case VK_IMAGE_ASPECT_DEPTH_BIT:
   case VK_IMAGE_ASPECT_STENCIL_BIT:
   case VK_IMAGE_ASPECT_DEPTH_BIT | VK_IMAGE_ASPECT_STENCIL_BIT:
      return pvr_single_plane_const(image);
   case VK_IMAGE_ASPECT_PLANE_0_BIT:
   case VK_IMAGE_ASPECT_MEMORY_PLANE_0_BIT_EXT:
      return &image->planes[0];
   case VK_IMAGE_ASPECT_PLANE_1_BIT:
   case VK_IMAGE_ASPECT_MEMORY_PLANE_1_BIT_EXT:
      return &image->planes[1];
   case VK_IMAGE_ASPECT_PLANE_2_BIT:
   case VK_IMAGE_ASPECT_MEMORY_PLANE_2_BIT_EXT:
      return &image->planes[2];
   default:
      UNREACHABLE("invalid image aspect");
   }
}

struct pvr_image_view {
   struct vk_image_view vk;

   /* Prepacked Texture Image dword 0 and 1. It will be copied to the
    * descriptor info during pvr_UpdateDescriptorSets().
    *
    * We create separate texture states for sampling, storage and input
    * attachment cases.
    */
   struct pvr_image_descriptor image_state[PVR_TEXTURE_STATE_MAX_ENUM];

   /* Prepacked Sampler Words with YCbCr plane addresses */
   uint64_t sampler_words[ROGUE_NUM_TEXSTATE_SAMPLER_WORDS];
};

VK_DEFINE_NONDISP_HANDLE_CASTS(pvr_image, vk.base, VkImage, VK_OBJECT_TYPE_IMAGE)

VK_DEFINE_NONDISP_HANDLE_CASTS(pvr_image_view,
                               vk.base,
                               VkImageView,
                               VK_OBJECT_TYPE_IMAGE_VIEW)

static inline const struct pvr_image *
vk_to_pvr_image(const struct vk_image *image)
{
   return container_of(image, const struct pvr_image, vk);
}

static inline const struct pvr_image *
pvr_image_view_get_image(const struct pvr_image_view *const iview)
{
   return vk_to_pvr_image(iview->vk.image);
}

void pvr_get_image_subresource_layout(const struct pvr_image *image,
                                      const VkImageSubresource *subresource,
                                      VkSubresourceLayout *layout);

void pvr_image_init(struct pvr_device *device,
                    const VkImageCreateInfo *pCreateInfo,
                    struct pvr_image *image);
void pvr_image_fini(struct pvr_device *device, struct pvr_image *image);

#endif /* PVR_IMAGE_H */
