/*
 * Copyright © 2016 Red Hat.
 * Copyright © 2016 Bas Nieuwenhuizen
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

#include <stdlib.h>
#include <string.h>
#include "vk_descriptors.h"
#include "vk_common_entrypoints.h"
#include "util/macros.h"

struct binding_with_flags {
   VkDescriptorSetLayoutBinding binding;
   VkDescriptorBindingFlags flags;
};

static int
binding_compare(const void* av, const void *bv)
{
   const struct binding_with_flags *a = (const struct binding_with_flags*)av;
   const struct binding_with_flags *b = (const struct binding_with_flags*)bv;
 
   return (a->binding.binding < b->binding.binding) ? -1 :
          (a->binding.binding > b->binding.binding) ? 1 : 0;
}
 
VkResult
vk_create_sorted_bindings(const VkDescriptorSetLayoutBinding *bindings, unsigned count,
                          VkDescriptorSetLayoutBinding **sorted_bindings,
                          const VkDescriptorSetLayoutBindingFlagsCreateInfo *binding_flags_info,
                          VkDescriptorBindingFlags **sorted_binding_flags)
{
   struct binding_with_flags *bwfs;
   unsigned index;

   if (!count) {
      *sorted_bindings = NULL;
      if (sorted_binding_flags)
         *sorted_binding_flags = NULL;
      return VK_SUCCESS;
   }

   *sorted_bindings = malloc(count * sizeof(VkDescriptorSetLayoutBinding));
   if (!*sorted_bindings)
      return VK_ERROR_OUT_OF_HOST_MEMORY;

   if (binding_flags_info && !binding_flags_info->bindingCount)
      binding_flags_info = NULL;

   if (binding_flags_info) {
      assert(sorted_binding_flags);
      assert(binding_flags_info->bindingCount == count);

      *sorted_binding_flags = malloc(count * sizeof(VkDescriptorBindingFlags));
      if (!*sorted_binding_flags) {
         free(*sorted_bindings);
         return VK_ERROR_OUT_OF_HOST_MEMORY;
      }
   }

   bwfs = malloc(count * sizeof(struct binding_with_flags));
   if (!bwfs) {
      if (binding_flags_info)
         free(*sorted_binding_flags);
      free(*sorted_bindings);
      return VK_ERROR_OUT_OF_HOST_MEMORY;
   }

   for (index = 0; index < count; index++) {
      memcpy(&bwfs[index].binding, &bindings[index],
             sizeof(VkDescriptorSetLayoutBinding));
      if (binding_flags_info)
         bwfs[index].flags = binding_flags_info->pBindingFlags[index];
      else
         bwfs[index].flags = 0;
   }

   qsort(bwfs, count, sizeof(struct binding_with_flags), binding_compare);

   for (index = 0; index < count; index++) {
      memcpy(&(*sorted_bindings)[index], &bwfs[index].binding,
             sizeof(VkDescriptorSetLayoutBinding));
      if (binding_flags_info)
         (*sorted_binding_flags)[index] = bwfs[index].flags;
   }

   free(bwfs);
 
   return VK_SUCCESS;
}

/*
 * For drivers that don't have mutable state in buffers, images, image views, or
 * samplers, there's no need to save/restore anything to get the same
 * descriptor back as long as the user uses the same GPU virtual address. In
 * this case, the following EXT_descriptor_buffer functions are trivial.
 */
VKAPI_ATTR VkResult VKAPI_CALL
vk_common_GetBufferOpaqueCaptureDescriptorDataEXT(VkDevice device,
                                                  const VkBufferCaptureDescriptorDataInfoEXT *pInfo,
                                                  void *pData)
{
   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_common_GetImageOpaqueCaptureDescriptorDataEXT(VkDevice device,
                                                 const VkImageCaptureDescriptorDataInfoEXT *pInfo,
                                                 void *pData)
{
   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_common_GetImageViewOpaqueCaptureDescriptorDataEXT(VkDevice device,
                                                     const VkImageViewCaptureDescriptorDataInfoEXT *pInfo,
                                                     void *pData)
{
   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_common_GetSamplerOpaqueCaptureDescriptorDataEXT(VkDevice _device,
                                                   const VkSamplerCaptureDescriptorDataInfoEXT *pInfo,
                                                   void *pData)
{
   return VK_SUCCESS;
}

VKAPI_ATTR VkResult VKAPI_CALL
vk_common_GetAccelerationStructureOpaqueCaptureDescriptorDataEXT(VkDevice device,
                                                                 const VkAccelerationStructureCaptureDescriptorDataInfoEXT *pInfo,
                                                                 void *pData)
{
   return VK_SUCCESS;
}
