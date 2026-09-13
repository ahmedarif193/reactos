/*
 * Copyright 2024 Collabora, Ltd.
 * SPDX-License-Identifier: MIT
 */

#include "intel_virtio_priv.h"

void *intel_virtio_bo_mmap(int fd, uint32_t handle, size_t size,
                           void *placed_addr)
{
   struct intel_virtio_device *dev = fd_to_intel_virtio_device(fd);

   if (!dev)
      return MAP_FAILED;

   void *map = vdrm_bo_map(dev->vdrm, handle, size, placed_addr);
   if (!map) {
      mesa_loge("failed to map bo");
      return MAP_FAILED;
   }

   return map;
}
