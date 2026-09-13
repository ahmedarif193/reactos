/*
 * Copyright © 2026 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#include "vk_framebuffer.h"

#include "nvk_cmd_buffer.h"
#include "nvk_device.h"
#include "nvk_entrypoints.h"
#include "nvk_image_view.h"

#include "layers/nvk_app_workarounds.h"

/* This layer implements various application WAs that don't need to be in the core driver. */

/* Metro Exodus */
VKAPI_ATTR VkResult VKAPI_CALL
metro_exodus_GetSemaphoreCounterValue(VkDevice _device, VkSemaphore _semaphore, uint64_t *pValue)
{
   /* See https://gitlab.freedesktop.org/mesa/mesa/-/issues/5119. */
   if (_semaphore == VK_NULL_HANDLE) {
      fprintf(stderr, "NVK: Ignoring vkGetSemaphoreCounterValue() with NULL semaphore (game bug)!\n");
      return VK_SUCCESS;
   }

   VK_FROM_HANDLE(nvk_device, device, _device);
   return device->layer_dispatch.app.GetSemaphoreCounterValue(_device, _semaphore, pValue);
}
