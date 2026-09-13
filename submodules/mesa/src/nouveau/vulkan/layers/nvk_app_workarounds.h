/*
 * Copyright © 2026 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef NVK_APP_WORKAROUNDS_H
#define NVK_APP_WORKAROUNDS_H

#include <vulkan/vulkan.h>

VKAPI_ATTR VkResult VKAPI_CALL metro_exodus_GetSemaphoreCounterValue(VkDevice _device, VkSemaphore _semaphore,
                                                                     uint64_t *pValue);

#endif /* NVK_APP_WORKAROUNDS_H */
