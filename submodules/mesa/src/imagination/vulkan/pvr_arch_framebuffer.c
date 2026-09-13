/*
 * Copyright © 2022 Imagination Technologies Ltd.
 *
 * based in part on anv driver which is:
 * Copyright © 2015 Intel Corporation
 *
 * based in part on v3dv driver which is:
 * Copyright © 2019 Raspberry Pi
 *
 * SPDX-License-Identifier: MIT
 */

#include "pvr_framebuffer.h"

#include "vk_util.h"

#include "hwdef/pvr_hw_utils.h"

#include "pvr_cmd_buffer.h"
#include "pvr_csb.h"
#include "pvr_device.h"
#include "pvr_entrypoints.h"
#include "pvr_hw_pass.h"
#include "pvr_image.h"
#include "pvr_pass.h"
#include "pvr_physical_device.h"
#include "pvr_rt_dataset.h"
#include "pvr_spm.h"

static VkResult
pvr_render_state_create_ppp_state(struct pvr_device *device,
                                  struct pvr_render_state *rstate)
{
   const uint32_t cache_line_size =
      pvr_get_slc_cache_line_size(&device->pdevice->dev_info);
   uint32_t ppp_state[3];
   VkResult result;

   pvr_csb_pack (&ppp_state[0], TA_STATE_HEADER, header) {
      header.pres_terminate = true;
   }

   pvr_csb_pack (&ppp_state[1], TA_STATE_TERMINATE0, term0) {
      term0.clip_right =
         DIV_ROUND_UP(
            rstate->width,
            ROGUE_TA_STATE_TERMINATE0_CLIP_RIGHT_BLOCK_SIZE_IN_PIXELS) -
         1;
      term0.clip_bottom =
         DIV_ROUND_UP(
            rstate->height,
            ROGUE_TA_STATE_TERMINATE0_CLIP_BOTTOM_BLOCK_SIZE_IN_PIXELS) -
         1;
   }

   pvr_csb_pack (&ppp_state[2], TA_STATE_TERMINATE1, term1) {
      term1.render_target = 0;
      term1.clip_left = 0;
   }

   result = pvr_gpu_upload(device,
                           device->heaps.general_heap,
                           ppp_state,
                           sizeof(ppp_state),
                           cache_line_size,
                           &rstate->ppp_state_bo);
   if (result != VK_SUCCESS)
      return result;

   /* Calculate the size of PPP state in dwords. */
   rstate->ppp_state_size = sizeof(ppp_state) / sizeof(uint32_t);

   return VK_SUCCESS;
}

static bool pvr_render_targets_init(struct pvr_render_target *render_targets,
                                    uint32_t render_targets_count)
{
   uint32_t i;

   for (i = 0; i < render_targets_count; i++) {
      if (pthread_mutex_init(&render_targets[i].mutex, NULL))
         goto err_mutex_destroy;
   }

   return true;

err_mutex_destroy:
   while (i--)
      pthread_mutex_destroy(&render_targets[i].mutex);

   return false;
}

VkResult
pvr_arch_render_state_setup(struct pvr_device *device,
                            const VkAllocationCallbacks *pAllocator,
                            struct pvr_render_state *rstate,
                            uint32_t render_count,
                            const struct pvr_renderpass_hwsetup_render *renders)
{
   struct pvr_spm_bgobj_state *spm_bgobj_state_per_render;
   struct pvr_spm_eot_state *spm_eot_state_per_render;
   struct pvr_render_target *render_targets;
   uint32_t render_targets_count;
   VkResult result;

   render_targets_count =
      PVR_RENDER_TARGETS_PER_FRAMEBUFFER(&device->pdevice->dev_info);

   VK_MULTIALLOC(ma);
   vk_multialloc_add(&ma,
                     &render_targets,
                     __typeof__(*render_targets),
                     render_targets_count);
   vk_multialloc_add(&ma,
                     &spm_eot_state_per_render,
                     __typeof__(*spm_eot_state_per_render),
                     render_count);
   vk_multialloc_add(&ma,
                     &spm_bgobj_state_per_render,
                     __typeof__(*spm_bgobj_state_per_render),
                     render_count);

   if (!vk_multialloc_zalloc2(&ma,
                              &device->vk.alloc,
                              pAllocator,
                              VK_SYSTEM_ALLOCATION_SCOPE_OBJECT)) {
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);
   }

   rstate->render_targets = render_targets;
   rstate->render_targets_count = render_targets_count;
   if (!pvr_render_targets_init(rstate->render_targets, render_targets_count)) {
      result = vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);
      goto err_free_render_targets;
   }

   assert(rstate->scratch_buffer_size);
   result = pvr_spm_scratch_buffer_get_buffer(device,
                                              rstate->scratch_buffer_size,
                                              &rstate->scratch_buffer);
   if (result != VK_SUCCESS)
      goto err_finish_render_targets;

   result = pvr_render_state_create_ppp_state(device, rstate);
   if (result != VK_SUCCESS)
      goto err_release_scratch_buffer;

   for (uint32_t i = 0; i < render_count; i++) {
      result = pvr_arch_spm_init_eot_state(device,
                                           &spm_eot_state_per_render[i],
                                           rstate,
                                           &renders[i]);
      if (result != VK_SUCCESS)
         goto err_finish_eot_state;

      result = pvr_arch_spm_init_bgobj_state(device,
                                             &spm_bgobj_state_per_render[i],
                                             rstate,
                                             &renders[i]);
      if (result != VK_SUCCESS)
         goto err_finish_bgobj_state;

      continue;

err_finish_bgobj_state:
      pvr_spm_finish_eot_state(device, &spm_eot_state_per_render[i]);

      for (uint32_t j = 0; j < i; j++)
         pvr_spm_finish_bgobj_state(device, &spm_bgobj_state_per_render[j]);

err_finish_eot_state:
      for (uint32_t j = 0; j < i; j++)
         pvr_spm_finish_eot_state(device, &spm_eot_state_per_render[j]);

      goto err_free_ppp_state_bo;
   }

   rstate->render_count = render_count;
   rstate->spm_eot_state_per_render = spm_eot_state_per_render;
   rstate->spm_bgobj_state_per_render = spm_bgobj_state_per_render;

   return VK_SUCCESS;

err_free_ppp_state_bo:
   pvr_bo_suballoc_free(rstate->ppp_state_bo);

err_release_scratch_buffer:
   pvr_spm_scratch_buffer_release(device, rstate->scratch_buffer);

err_finish_render_targets:
   pvr_render_targets_fini(rstate->render_targets, render_targets_count);

err_free_render_targets:
   vk_free2(&device->vk.alloc, pAllocator, rstate->render_targets);

   return result;
}

static inline uint64_t
pvr_render_pass_get_scratch_buffer_size(struct pvr_device *device,
                                        const struct pvr_render_pass *pass,
                                        const struct pvr_render_state *rstate)
{
   return pvr_arch_spm_scratch_buffer_calc_required_size(
      pass->hw_setup->renders,
      pass->hw_setup->render_count,
      pass->max_sample_count,
      rstate->width,
      rstate->height);
}

VkResult
PVR_PER_ARCH(CreateFramebuffer)(VkDevice _device,
                                const VkFramebufferCreateInfo *pCreateInfo,
                                const VkAllocationCallbacks *pAllocator,
                                VkFramebuffer *pFramebuffer)
{
   VK_FROM_HANDLE(pvr_render_pass, pass, pCreateInfo->renderPass);
   VK_FROM_HANDLE(pvr_device, device, _device);
   const VkFramebufferAttachmentsCreateInfoKHR *pImageless;
   struct pvr_framebuffer *framebuffer;
   struct pvr_image_view **attachments;
   struct pvr_render_state *rstate;
   VkResult result;

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO);

   pImageless = vk_find_struct_const(pCreateInfo->pNext,
                                     FRAMEBUFFER_ATTACHMENTS_CREATE_INFO);

   VK_MULTIALLOC(ma);
   vk_multialloc_add(&ma, &framebuffer, __typeof__(*framebuffer), 1);
   vk_multialloc_add(&ma, &rstate, __typeof__(*rstate), 1);
   vk_multialloc_add(&ma,
                     &attachments,
                     __typeof__(*attachments),
                     pCreateInfo->attachmentCount);

   if (!vk_multialloc_zalloc2(&ma,
                              &device->vk.alloc,
                              pAllocator,
                              VK_SYSTEM_ALLOCATION_SCOPE_OBJECT))
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   vk_object_base_init(&device->vk,
                       &framebuffer->base,
                       VK_OBJECT_TYPE_FRAMEBUFFER);

   framebuffer->attachments = attachments;
   if (!pImageless)
      framebuffer->attachment_count = pCreateInfo->attachmentCount;
   else
      framebuffer->attachment_count = pImageless->attachmentImageInfoCount;
   for (uint32_t i = 0; i < framebuffer->attachment_count; i++) {
      if (!pImageless) {
         framebuffer->attachments[i] =
            pvr_image_view_from_handle(pCreateInfo->pAttachments[i]);
      } else {
         assert(i < pImageless->attachmentImageInfoCount);
      }
   }

   rstate->width = pCreateInfo->width;
   rstate->height = pCreateInfo->height;
   rstate->layers = pCreateInfo->layers;
   rstate->scratch_buffer_size =
      pvr_render_pass_get_scratch_buffer_size(device, pass, rstate);

   result = pvr_arch_render_state_setup(device,
                                        pAllocator,
                                        rstate,
                                        pass->hw_setup->render_count,
                                        pass->hw_setup->renders);
   if (result != VK_SUCCESS)
      goto err_free_framebuffer;

   framebuffer->rstate = rstate;

   *pFramebuffer = pvr_framebuffer_to_handle(framebuffer);

   return VK_SUCCESS;

err_free_framebuffer:
   vk_object_base_finish(&framebuffer->base);
   vk_free2(&device->vk.alloc, pAllocator, framebuffer);

   return result;
}

void PVR_PER_ARCH(DestroyFramebuffer)(VkDevice _device,
                                      VkFramebuffer _fb,
                                      const VkAllocationCallbacks *pAllocator)
{
   VK_FROM_HANDLE(pvr_framebuffer, framebuffer, _fb);
   VK_FROM_HANDLE(pvr_device, device, _device);

   if (!framebuffer)
      return;

   pvr_render_state_cleanup(device, pAllocator, framebuffer->rstate);
   /* the render state is freed with the framebuffer */

   vk_object_base_finish(&framebuffer->base);
   vk_free2(&device->vk.alloc, pAllocator, framebuffer);
}
