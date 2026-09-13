/*
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include <vulkan/vulkan_core.h>

#include "anv_slab_bo.h"
#include "test_common.h"
#include "util/list.h"
#include "util/pb_slab.h"

/*
 * Mockers for anv_device_* functions to detangle allocation details.
 */
VkResult
__wrap_anv_device_alloc_bo(struct anv_device *device,
                    const char *name,
                    const uint64_t base_size,
                    enum anv_bo_alloc_flags alloc_flags,
                    uint64_t explicit_address,
                    struct anv_bo **bo_out);

VkResult
__wrap_anv_device_alloc_bo(struct anv_device *device,
                    const char *name,
                    const uint64_t base_size,
                    enum anv_bo_alloc_flags alloc_flags,
                    uint64_t explicit_address,
                    struct anv_bo **bo_out)
{
    *bo_out = calloc(1, sizeof(struct anv_bo));
    (*bo_out)->actual_size = base_size;
    return VK_SUCCESS;
}


void
__wrap_anv_device_release_bo(struct anv_device *device,
                      struct anv_bo *bo);

void
__wrap_anv_device_release_bo(struct anv_device *device,
                      struct anv_bo *bo)
{
    free(bo);
}

/*
 * ====================== Tests ======================
 */

static int find_first_non_empty_reclaim_slabs(struct pb_slabs* slabs, int num_slabs) {
   for (int i = 0; i < num_slabs; ++i) {
      if (!list_is_empty(&slabs[i].reclaim)) {
         return i;
      }
   }
   return -1;
}

static bool is_all_reclaim_list_empty_except_one(struct pb_slabs* slabs, int num_slabs, int target_index) {
   for (int i = 0; i < num_slabs; ++i) {
      if (i == target_index) {
         if (list_is_empty(&slabs[i].reclaim)) {
            return false;
         }
      } else {
         if (!list_is_empty(&slabs[i].reclaim)) {
            return false;
         }
      }
   }
   return true;
}

static bool is_reclaim_list_all_empty(struct pb_slabs* slabs, int num_slabs) {
   return is_all_reclaim_list_empty_except_one(slabs, num_slabs, -1);
}

void anv_slab_bo_alloc_with_alignment(void);

void anv_slab_bo_alloc_with_alignment(void) {
   const int kRequestedSize = 4;
   const int kAlignmentOrder = 16;
   const int kAlignment = 1 << kAlignmentOrder;

   struct anv_instance instance = {};
   struct anv_physical_device physical_device = {
      .instance = &instance,
      .info = {
         .has_mmap_offset = true,
         .has_partial_mmap_offset = true,
      }
   };
   struct anv_device device = {};

   test_device_info_init(&physical_device.info);
   anv_device_set_physical(&device, &physical_device);
   device.kmd_backend = anv_kmd_backend_get(INTEL_KMD_TYPE_STUB);
   pthread_mutex_init(&device.mutex, NULL);
   anv_bo_cache_init(&device.bo_cache, &device);

   ASSERT(anv_slab_bo_init(&device));
   struct anv_bo* bo = anv_slab_bo_alloc(&device, /*name=*/"test_user", kRequestedSize, kAlignment, /*alloc_flags=*/0);

   // Validate after allocation.
   const int num_entries = bo->slab_entry.slab->num_entries;
   ASSERT(bo != NULL);
   ASSERT(bo->refcount == 1);
   ASSERT(bo->size == kRequestedSize);
   ASSERT(bo->actual_size == kAlignment);
   // One entry is requested by anv_slab_bo_alloc for the first time will always get from the free list since no any slab is recycled yet.
   ASSERT(bo->slab_entry.slab->num_free == num_entries - 1);
   ASSERT(bo->slab_entry.slab->entry_size == kAlignment);
   ASSERT(bo->slab_entry.slab->num_free ==
             list_length(&bo->slab_entry.slab->free));
   const int kNumSlabs = ARRAY_SIZE(device.bo_slabs);
   ASSERT(is_reclaim_list_all_empty(device.bo_slabs, kNumSlabs));

   // Free the bo.
   anv_slab_bo_free(&device, bo);

   // Validate that the bo is moved to the correct relcaim list.
   // Find the first non-empty slabs and reuse the index over the whole test.
   ASSERT(bo->slab_entry.slab->num_free == num_entries - 1);
   const int kSlabsIndex = find_first_non_empty_reclaim_slabs(device.bo_slabs, kNumSlabs);
   ASSERT(kSlabsIndex >= 0 && kSlabsIndex < ARRAY_SIZE(device.bo_slabs));

   // Validate the slab manager order is correct.
   ASSERT(device.bo_slabs[kSlabsIndex].min_order <= kAlignmentOrder);
   ASSERT(kAlignmentOrder <=
           device.bo_slabs[kSlabsIndex].min_order + device.bo_slabs[kSlabsIndex].num_orders - 1);

   // Validate that no slab is accidentally appended to other slab managers.
   ASSERT(is_all_reclaim_list_empty_except_one(device.bo_slabs, kNumSlabs, kSlabsIndex));

   // Request all the remaining bo's.
   struct anv_bo** bo_list = calloc(num_entries - 1, sizeof(struct anv_bo *));
   for (int i = 0; i < num_entries - 1; ++i) {
      bo_list[i] = anv_slab_bo_alloc(&device, /*name=*/"test_user", kRequestedSize, kAlignment, /*alloc_flags=*/0);
      ASSERT(bo_list[i] != NULL);
      ASSERT(bo_list[i]->refcount == 1);
      ASSERT(bo_list[i]->size == kRequestedSize);
      ASSERT(bo_list[i]->actual_size == kAlignment);
      ASSERT(bo_list[i]->slab_entry.slab->entry_size == kAlignment);
      ASSERT(bo_list[i]->slab_entry.slab->num_free ==
                list_length(&bo->slab_entry.slab->free));
      // All allocated entries sit on the same slab.
      ASSERT(bo_list[i]->slab_entry.slab == bo->slab_entry.slab);
   }

   // Free the remaining bos.
   for (int i = 0; i < num_entries - 1; ++i) {
      anv_slab_bo_free(&device, bo_list[i]);
   }
   // Validate that the correct slab manager is appended.
   ASSERT(find_first_non_empty_reclaim_slabs(device.bo_slabs, kNumSlabs) == kSlabsIndex);
   // Validate that no slab is accidentally appended to other slab managers.
   ASSERT(is_all_reclaim_list_empty_except_one(device.bo_slabs, kNumSlabs, kSlabsIndex));

   // Denint the slab manager.
   anv_slab_bo_deinit(&device);
   free(bo_list);
   anv_bo_cache_finish(&device.bo_cache);
   pthread_mutex_destroy(&device.mutex);
}
