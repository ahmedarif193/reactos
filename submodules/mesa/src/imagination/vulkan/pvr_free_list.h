/*
 * Copyright © 2022 Imagination Technologies Ltd.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef PVR_FREE_LIST_H
#define PVR_FREE_LIST_H

#include <vulkan/vulkan_core.h>
#include <stdint.h>

struct pvr_bo;
struct pvr_device;
struct pvr_winsys_free_list;

struct pvr_free_list {
   struct pvr_device *device;

   uint64_t size;

   struct pvr_bo *bo;

   struct pvr_winsys_free_list *ws_free_list;
};

VkResult pvr_free_list_create(struct pvr_device *device,
                              uint32_t initial_size,
                              uint32_t max_size,
                              uint32_t grow_size,
                              uint32_t grow_threshold,
                              struct pvr_free_list *parent_free_list,
                              struct pvr_free_list **const free_list_out);

void pvr_free_list_destroy(struct pvr_free_list *free_list);

#endif /* PVR_FREE_LIST */
