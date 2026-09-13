/*
 * Copyright © 2026 Raspberry Pi Ltd
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */
#ifndef V3DV_QUERY_H
#define V3DV_QUERY_H

#include "v3dv_common.h"
#include "v3dv_limits.h"
#include "util/list.h"

struct vk_sync;
struct v3dv_bo;
struct v3dv_device;

struct v3dv_perf_query {
   uint32_t kperfmon_ids[V3DV_MAX_PERFMONS];

   /* A DRM syncobj to wait on the GPU jobs for which we are collecting
    * performance data.
    */
   struct vk_sync *last_job_sync;
};

struct v3dv_query {
   /* Used by queries where we implement result copying in the CPU so we can
    * tell if the relevant jobs have been submitted for execution. Currently
    * these are all but occlusion queries.
    */
   bool maybe_available;

   union {
      /* Used by occlusion queries */
      struct {
         /* Offset of this query in the occlusion query counter BO */
         uint32_t offset;
      } occlusion;

      /* Used by timestamp queries */
      struct {
         /* Offset of this query in the timestamp BO for its value */
         uint32_t offset;

         /* Syncobj to signal timestamp query availability */
         struct vk_sync *sync;
      } timestamp;

      /* Used by performance queries */
      struct v3dv_perf_query perf;
   };
};

struct v3dv_query_pool {
   struct vk_object_base base;

   /* Per-pool Vulkan resources required to implement GPU-side query
    * functions (only occlusion queries for now).
    */
   struct {
      /* Buffer to access the BO with the occlusion query results and
       * availability info.
       */
      VkBuffer buf;
      VkDeviceMemory mem;

      /* Descriptor set for accessing the buffer from a pipeline. */
      VkDescriptorPool descriptor_pool;
      VkDescriptorSet descriptor_set;
   } meta;

   /* Only used with occlusion queries */
   struct {
      /* BO with the occlusion counters and query availability */
      struct v3dv_bo *bo;
      /* Offset of the availability info in the BO */
      uint32_t avail_offset;
   } occlusion;

   /* Only used with timestamp queries */
   struct {
      /* BO with the query timestamp values */
      struct v3dv_bo *bo;
   } timestamp;

   /* Only used with performance queries */
   struct {
      uint32_t ncounters;
      uint8_t counters[V3D_MAX_PERFCNT];

      /* V3D has a limit on the number of counters we can track in a
       * single performance monitor, so if too many counters are requested
       * we need to create multiple monitors to record all of them. This
       * field represents the number of monitors required for the number
       * of counters requested.
       */
      uint8_t nperfmons;
   } perfmon;

   VkQueryType query_type;
   uint32_t query_count;
   struct v3dv_query *queries;
};

struct v3dv_event {
   struct vk_object_base base;

   /* Link in the device list of pre-allocated free events */
   struct list_head link;

   /* Each event gets a different index, which we use to compute the offset
    * in the BO we use to track their state (signaled vs reset).
    */
   uint32_t index;
};

VkResult
v3dv_event_allocate_resources(struct v3dv_device *device);

void
v3dv_event_free_resources(struct v3dv_device *device);

VK_DEFINE_NONDISP_HANDLE_CASTS(v3dv_event, base, VkEvent, VK_OBJECT_TYPE_EVENT)
VK_DEFINE_NONDISP_HANDLE_CASTS(v3dv_query_pool, base, VkQueryPool,
                               VK_OBJECT_TYPE_QUERY_POOL)

#endif /* V3DV_QUERY_H */
