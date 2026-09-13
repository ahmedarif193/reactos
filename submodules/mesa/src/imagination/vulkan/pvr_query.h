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

#ifndef PVR_QUERY_H
#define PVR_QUERY_H

#include "vk_object.h"

#include "pvr_common.h"

struct pvr_device;

struct pvr_query_pool {
   struct vk_object_base base;

   /* Stride of result_buffer to get to the start of the results for the next
    * Phantom.
    */
   uint32_t result_stride;

   uint32_t query_count;

   struct pvr_suballoc_bo *result_buffer;
   struct pvr_suballoc_bo *availability_buffer;
};

struct pvr_query_info {
   enum pvr_query_type type;

   struct {
      bool is_dynamic_render : 1;
      bool is_suspend : 1;
   };

   union {
      struct {
         uint32_t num_query_indices;
         struct pvr_suballoc_bo *index_bo;
         uint32_t num_queries;
         struct pvr_suballoc_bo *availability_bo;
      } availability_write;

      struct {
         VkQueryPool query_pool;
         uint32_t first_query;
         uint32_t query_count;
      } reset_query_pool;

      struct {
         VkQueryPool query_pool;
         uint32_t first_query;
         uint32_t query_count;
         VkBuffer dst_buffer;
         VkDeviceSize dst_offset;
         VkDeviceSize stride;
         VkQueryResultFlags flags;
      } copy_query_results;
   };
};

VK_DEFINE_NONDISP_HANDLE_CASTS(pvr_query_pool,
                               base,
                               VkQueryPool,
                               VK_OBJECT_TYPE_QUERY_POOL)

#ifdef PVR_PER_ARCH

VkResult PVR_PER_ARCH(device_create_compute_query_programs)(
   struct pvr_device *device);

#   define pvr_arch_device_create_compute_query_programs \
      PVR_PER_ARCH(device_create_compute_query_programs)

void PVR_PER_ARCH(device_destroy_compute_query_programs)(
   struct pvr_device *device);

#   define pvr_arch_device_destroy_compute_query_programs \
      PVR_PER_ARCH(device_destroy_compute_query_programs)

#endif

#endif /* PVR_QUERY_H*/
