/*
 * Copyright 2024 Valve Corporation
 * Copyright 2024 Alyssa Rosenzweig
 * Copyright 2022-2023 Collabora Ltd. and Red Hat Inc.
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "agx_pack.h"
#include "hk_private.h"
#include "vk_image.h"

struct hk_device;

#define HK_MAX_PLANES      3
#define HK_MAX_IMAGE_DESCS (10 * HK_MAX_PLANES)

struct hk_image_view {
   struct vk_image_view vk;

   uint8_t plane_count;
   struct {
      uint8_t image_plane;

      struct agx_texture_packed ia, sampled, ro_storage, background,
         layered_background, emrt_texture;

      struct agx_pbe_packed storage, eot, layered_eot, emrt_pbe;
   } planes[3];
};

VK_DEFINE_NONDISP_HANDLE_CASTS(hk_image_view, vk.base, VkImageView,
                               VK_OBJECT_TYPE_IMAGE_VIEW)
