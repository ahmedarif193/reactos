/*
 * Copyright © 2021 Collabora Ltd.
 *
 * Derived from tu_image.c which is:
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
 * Copyright © 2015 Intel Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "genxml/gen_macros.h"

#include "panvk_buffer.h"
#include "panvk_buffer_view.h"
#include "panvk_device.h"
#include "panvk_entrypoints.h"
#include "panvk_priv_bo.h"

#include "pan_afbc.h"
#include "pan_buffer.h"
#include "pan_desc.h"
#include "pan_props.h"

#include "vk_format.h"
#include "vk_log.h"

VKAPI_ATTR VkResult VKAPI_CALL
panvk_per_arch(CreateBufferView)(VkDevice _device,
                                 const VkBufferViewCreateInfo *pCreateInfo,
                                 const VkAllocationCallbacks *pAllocator,
                                 VkBufferView *pView)
{
   VK_FROM_HANDLE(panvk_device, device, _device);
   VK_FROM_HANDLE(panvk_buffer, buffer, pCreateInfo->buffer);

   struct panvk_buffer_view *view = vk_object_zalloc(
      &device->vk, pAllocator, sizeof(*view), VK_OBJECT_TYPE_BUFFER_VIEW);

   if (!view)
      return panvk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   vk_buffer_view_init(&device->vk, &view->vk, pCreateInfo);

   enum pipe_format pfmt = vk_format_to_pipe_format(view->vk.format);

   VkBufferUsageFlags tex_usage_mask =
      VK_BUFFER_USAGE_UNIFORM_TEXEL_BUFFER_BIT |
      VK_BUFFER_USAGE_STORAGE_TEXEL_BUFFER_BIT;

   if (buffer->vk.usage & tex_usage_mask) {
      struct pan_buffer_view bview = {
         .format = pfmt,
         .width_el = view->vk.elements,
         .base = panvk_buffer_gpu_ptr(buffer, pCreateInfo->offset),
      };
#if PAN_ARCH >= 9
      GENX(pan_buffer_texture_emit)(&bview, &view->descs.buf);
#else
      GENX(pan_buffer_texture_emit)(&bview, &view->descs.attrib_buf,
                                    &view->descs.attrib);
#endif
   }

   *pView = panvk_buffer_view_to_handle(view);
   return VK_SUCCESS;
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(DestroyBufferView)(VkDevice _device, VkBufferView bufferView,
                                  const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(panvk_device, device, _device);
   VK_FROM_HANDLE(panvk_buffer_view, view, bufferView);

   if (!view)
      return;

   vk_buffer_view_destroy(&device->vk, pAllocator, &view->vk);
}
