/*
 * PROJECT:     ReactOS Vulkan loader
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Private definitions of the Vulkan loader
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#include <windows.h>

#define VK_USE_PLATFORM_WIN32_KHR
#include <vulkan/vulkan.h>
#include <vulkan/vk_icd.h>

struct vk_export
{
    const char *Name;
    PFN_vkVoidFunction Function;
};

struct vk_icd
{
    HMODULE Module;
    PFN_vkGetInstanceProcAddr GetInstanceProcAddr;
};

struct vk_table
{
    struct vk_icd *Icd;
    PFN_vkGetDeviceProcAddr GetDeviceProcAddr;
    PFN_vkVoidFunction Functions[1];
};

#include "loader_gen.h"

static inline struct vk_table *vk_instance_table(const void *Handle)
{
    return *(struct vk_table *const *)Handle;
}

static inline struct vk_table *vk_device_table(const void *Handle)
{
    return *(struct vk_table *const *)Handle;
}
