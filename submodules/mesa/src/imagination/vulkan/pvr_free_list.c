/*
 * Copyright © 2022 Imagination Technologies Ltd.
 *
 * SPDX-License-Identifier: MIT
 */

#include "pvr_free_list.h"

#include "vk_log.h"

#include "hwdef/pvr_hw_utils.h"

#include "pvr_device.h"
#include "pvr_physical_device.h"

#define ROGUE_BIF_PM_FREELIST_BASE_ADDR_ALIGNSIZE 16U

/* FIXME: Is there a hardware define we can use instead? */
/* 1 DWord per PM physical page stored in the free list */
#define ROGUE_FREE_LIST_ENTRY_SIZE ((uint32_t)sizeof(uint32_t))

VkResult pvr_free_list_create(struct pvr_device *device,
                              uint32_t initial_size,
                              uint32_t max_size,
                              uint32_t grow_size,
                              uint32_t grow_threshold,
                              struct pvr_free_list *parent_free_list,
                              struct pvr_free_list **const free_list_out)
{
   const struct pvr_device_runtime_info *runtime_info =
      &device->pdevice->dev_runtime_info;
   struct pvr_winsys_free_list *parent_ws_free_list =
      parent_free_list ? parent_free_list->ws_free_list : NULL;
   const uint64_t bo_flags = PVR_BO_ALLOC_FLAG_GPU_UNCACHED |
                             PVR_BO_ALLOC_FLAG_PM_FW_PROTECT;
   struct pvr_free_list *free_list;
   uint32_t cache_line_size;
   uint32_t initial_num_pages;
   uint32_t grow_num_pages;
   uint32_t max_num_pages;
   uint64_t addr_alignment;
   uint64_t size_alignment;
   uint64_t size;
   VkResult result;

   assert((initial_size + grow_size) <= max_size);
   assert(max_size != 0);
   assert(grow_threshold <= 100);

   /* Make sure the free list is created with at least a single page. */
   if (initial_size == 0)
      initial_size = ROGUE_BIF_PM_PHYSICAL_PAGE_SIZE;

   /* The freelists sizes must respect the PM freelist base address alignment
    * requirement. As the freelist entries are cached by the SLC, it's also
    * necessary to ensure the sizes respect the SLC cache line size to avoid
    * invalid entries appearing in the cache, which would be problematic after
    * a grow operation, as the SLC entries aren't invalidated. We do this by
    * making sure the freelist values are appropriately aligned.
    *
    * To calculate the alignment, we first take the largest of the freelist
    * base address alignment and the SLC cache line size. We then divide this
    * by the freelist entry size to determine the number of freelist entries
    * required by the PM. Finally, as each entry holds a single PM physical
    * page, we multiple the number of entries by the page size.
    *
    * As an example, if the base address alignment is 16 bytes, the SLC cache
    * line size is 64 bytes and the freelist entry size is 4 bytes then 16
    * entries are required, as we take the SLC cacheline size (being the larger
    * of the two values) and divide this by 4. If the PM page size is 4096
    * bytes then we end up with an alignment of 65536 bytes.
    */
   cache_line_size = pvr_get_slc_cache_line_size(&device->pdevice->dev_info);

   addr_alignment =
      MAX2(ROGUE_BIF_PM_FREELIST_BASE_ADDR_ALIGNSIZE, cache_line_size);
   size_alignment = (addr_alignment / ROGUE_FREE_LIST_ENTRY_SIZE) *
                    ROGUE_BIF_PM_PHYSICAL_PAGE_SIZE;

   assert(util_is_power_of_two_nonzero64(size_alignment));

   initial_size = align64(initial_size, size_alignment);
   max_size = align64(max_size, size_alignment);
   grow_size = align64(grow_size, size_alignment);

   /* Make sure the 'max' size doesn't exceed what the firmware supports and
    * adjust the other sizes accordingly.
    */
   if (max_size > runtime_info->max_free_list_size) {
      max_size = runtime_info->max_free_list_size;
      assert(align64(max_size, size_alignment) == max_size);
   }

   if (initial_size > max_size)
      initial_size = max_size;

   if (initial_size == max_size)
      grow_size = 0;

   initial_num_pages = initial_size >> ROGUE_BIF_PM_PHYSICAL_PAGE_SHIFT;
   max_num_pages = max_size >> ROGUE_BIF_PM_PHYSICAL_PAGE_SHIFT;
   grow_num_pages = grow_size >> ROGUE_BIF_PM_PHYSICAL_PAGE_SHIFT;

   /* Calculate the size of the buffer needed to store the free list entries
    * based on the maximum number of pages we can have.
    */
   size = max_num_pages * ROGUE_FREE_LIST_ENTRY_SIZE;
   assert(align64(size, addr_alignment) == size);

   free_list = vk_alloc(&device->vk.alloc,
                        sizeof(*free_list),
                        8,
                        VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
   if (!free_list)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   /* FIXME: The memory is mapped GPU uncached, but this seems to contradict
    * the comment above about aligning to the SLC cache line size.
    */
   result = pvr_bo_alloc(device,
                         device->heaps.general_heap,
                         size,
                         addr_alignment,
                         bo_flags,
                         &free_list->bo);
   if (result != VK_SUCCESS)
      goto err_vk_free_free_list;

   result = device->ws->ops->free_list_create(device->ws,
                                              free_list->bo->vma,
                                              initial_num_pages,
                                              max_num_pages,
                                              grow_num_pages,
                                              grow_threshold,
                                              parent_ws_free_list,
                                              &free_list->ws_free_list);
   if (result != VK_SUCCESS)
      goto err_pvr_bo_free_bo;

   free_list->device = device;
   free_list->size = size;

   *free_list_out = free_list;

   return VK_SUCCESS;

err_pvr_bo_free_bo:
   pvr_bo_free(device, free_list->bo);

err_vk_free_free_list:
   vk_free(&device->vk.alloc, free_list);

   return result;
}

void pvr_free_list_destroy(struct pvr_free_list *free_list)
{
   struct pvr_device *device = free_list->device;

   device->ws->ops->free_list_destroy(free_list->ws_free_list);
   pvr_bo_free(device, free_list->bo);
   vk_free(&device->vk.alloc, free_list);
}
