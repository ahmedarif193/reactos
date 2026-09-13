/*
 * Copyright 2024 Valve Corporation
 * Copyright 2024 Alyssa Rosenzweig
 * Copyright 2022-2023 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */
#pragma once

#include "agx_pack.h"
#include "hk_private.h"

#include "vk_buffer_view.h"

struct hk_physical_device;

VkFormatFeatureFlags2
hk_get_buffer_format_features(struct hk_physical_device *pdevice,
                              VkFormat format);

struct hk_buffer_view {
   struct vk_buffer_view vk;
   struct agx_texture_packed tex;
   struct agx_pbe_packed pbe;
};

VK_DEFINE_NONDISP_HANDLE_CASTS(hk_buffer_view, vk.base, VkBufferView,
                               VK_OBJECT_TYPE_BUFFER_VIEW)
