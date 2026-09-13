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

#include "pvr_device.h"

#include "vk_log.h"

#include "hwdef/pvr_hw_utils.h"

#include "pco_uscgen_programs.h"

#include "pvr_border.h"
#include "pvr_clear.h"
#include "pvr_entrypoints.h"
#include "pvr_framebuffer.h"
#include "pvr_free_list.h"
#include "pvr_instance.h"
#include "pvr_job_render.h"
#include "pvr_macros.h"
#include "pvr_physical_device.h"
#include "pvr_query.h"
#include "pvr_queue.h"
#include "pvr_robustness.h"
#include "pvr_tex_state.h"

#define PVR_GLOBAL_FREE_LIST_INITIAL_SIZE (2U * 1024U * 1024U)
#define PVR_GLOBAL_FREE_LIST_MAX_SIZE (256U * 1024U * 1024U)
#define PVR_GLOBAL_FREE_LIST_GROW_SIZE (1U * 1024U * 1024U)

/* After PVR_SECONDARY_DEVICE_THRESHOLD devices per instance are created,
 * devices will have a smaller global free list size, as usually this use-case
 * implies smaller amounts of work spread out. The free list can still grow as
 * required.
 */
#define PVR_SECONDARY_DEVICE_THRESHOLD (4U)
#define PVR_SECONDARY_DEVICE_FREE_LIST_INITAL_SIZE (512U * 1024U)

/* The grow threshold is a percentage. This is intended to be 12.5%, but has
 * been rounded up since the percentage is treated as an integer.
 */
#define PVR_GLOBAL_FREE_LIST_GROW_THRESHOLD 13U

/* Default size in bytes used by pvr_CreateDevice() for setting up the
 * suballoc_general, suballoc_pds and suballoc_usc suballocators.
 *
 * TODO: Investigate if a different default size can improve the overall
 * performance of internal driver allocations.
 */
#define PVR_SUBALLOCATOR_GENERAL_SIZE (128 * 1024)
#define PVR_SUBALLOCATOR_PDS_SIZE (128 * 1024)
#define PVR_SUBALLOCATOR_TRANSFER_SIZE (128 * 1024)
#define PVR_SUBALLOCATOR_USC_SIZE (128 * 1024)
#define PVR_SUBALLOCATOR_VIS_TEST_SIZE (128 * 1024)

static uint32_t pvr_get_simultaneous_num_allocs(
   const struct pvr_device_info *dev_info,
   ASSERTED const struct pvr_device_runtime_info *dev_runtime_info)
{
   uint32_t min_cluster_per_phantom;

   if (PVR_HAS_FEATURE(dev_info, s8xe))
      return PVR_GET_FEATURE_VALUE(dev_info, num_raster_pipes, 0U);

   assert(dev_runtime_info->num_phantoms == 1);
   min_cluster_per_phantom = PVR_GET_FEATURE_VALUE(dev_info, num_clusters, 1U);

   if (min_cluster_per_phantom >= 4)
      return 1;
   else if (min_cluster_per_phantom == 2)
      return 2;
   else
      return 4;
}

uint32_t pvr_arch_calc_fscommon_size_and_tiles_in_flight(
   const struct pvr_device_info *dev_info,
   const struct pvr_device_runtime_info *dev_runtime_info,
   uint32_t fs_common_size,
   uint32_t min_tiles_in_flight)
{
   const uint32_t available_shareds =
      dev_runtime_info->reserved_shared_size - dev_runtime_info->max_coeffs;
   const uint32_t max_tiles_in_flight =
      PVR_GET_FEATURE_VALUE(dev_info, isp_max_tiles_in_flight, 1U);
   uint32_t num_tile_in_flight;
   uint32_t num_allocs;

   if (fs_common_size == 0)
      return max_tiles_in_flight;

   num_allocs = pvr_get_simultaneous_num_allocs(dev_info, dev_runtime_info);

   if (fs_common_size == UINT32_MAX) {
      uint32_t max_common_size = available_shareds;

      num_allocs *= MIN2(min_tiles_in_flight, max_tiles_in_flight);

      if (!PVR_HAS_ENHANCEMENT(dev_info, 38748)) {
         /* Hardware needs space for one extra shared allocation. */
         num_allocs += 1;
      }

      /* Double resource requirements to deal with fragmentation. */
      max_common_size /= num_allocs * 2;
      max_common_size = MIN2(max_common_size, ROGUE_MAX_PIXEL_SHARED_REGISTERS);
      max_common_size =
         ROUND_DOWN_TO(max_common_size,
                       ROGUE_TA_STATE_PDS_SIZEINFO2_USC_SHAREDSIZE_UNIT_SIZE);

      return max_common_size;
   }

   num_tile_in_flight = available_shareds / (fs_common_size * 2);

   if (!PVR_HAS_ENHANCEMENT(dev_info, 38748))
      num_tile_in_flight -= 1;

   num_tile_in_flight /= num_allocs;

#if MESA_DEBUG
   /* Validate the above result. */

   assert(num_tile_in_flight >= MIN2(num_tile_in_flight, max_tiles_in_flight));
   num_allocs *= num_tile_in_flight;

   if (!PVR_HAS_ENHANCEMENT(dev_info, 38748)) {
      /* Hardware needs space for one extra shared allocation. */
      num_allocs += 1;
   }

   assert(fs_common_size <= available_shareds / (num_allocs * 2));
#endif

   return MIN2(num_tile_in_flight, max_tiles_in_flight);
}

VkResult pvr_arch_pds_compute_shader_create_and_upload(
   struct pvr_device *device,
   struct pvr_pds_compute_shader_program *program,
   struct pvr_pds_upload *const pds_upload_out)
{
   const struct pvr_device_info *dev_info = &device->pdevice->dev_info;
   const uint32_t cache_line_size = pvr_get_slc_cache_line_size(dev_info);
   size_t staging_buffer_size;
   uint32_t *staging_buffer;
   uint32_t *data_buffer;
   uint32_t *code_buffer;
   VkResult result;

   /* Calculate how much space we'll need for the compute shader PDS program.
    */
   pvr_pds_compute_shader(program, NULL, PDS_GENERATE_SIZES, dev_info);

   /* FIXME: Fix the below inconsistency of code size being in bytes whereas
    * data size being in dwords.
    */
   /* Code size is in bytes, data size in dwords. */
   staging_buffer_size =
      PVR_DW_TO_BYTES(program->data_size) + program->code_size;

   staging_buffer = vk_alloc(&device->vk.alloc,
                             staging_buffer_size,
                             8U,
                             VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
   if (!staging_buffer)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   data_buffer = staging_buffer;
   code_buffer = pvr_pds_compute_shader(program,
                                        data_buffer,
                                        PDS_GENERATE_DATA_SEGMENT,
                                        dev_info);

   pvr_pds_compute_shader(program,
                          code_buffer,
                          PDS_GENERATE_CODE_SEGMENT,
                          dev_info);

   for (unsigned u = 0; u < PVR_WORKGROUP_DIMENSIONS; ++u) {
      unsigned offset = program->num_workgroups_constant_offset_in_dwords[0];
      if (program->num_work_groups_regs[u] != PVR_PDS_REG_UNUSED)
         data_buffer[offset + u] = 0;

      offset = program->base_workgroup_constant_offset_in_dwords[0];
      if (program->work_group_input_regs[u] != PVR_PDS_REG_UNUSED)
         data_buffer[offset + u] = 0;
   }

   result = pvr_gpu_upload_pds(device,
                               data_buffer,
                               program->data_size,
                               ROGUE_CDMCTRL_KERNEL1_DATA_ADDR_ALIGNMENT,
                               code_buffer,
                               program->code_size / sizeof(uint32_t),
                               ROGUE_CDMCTRL_KERNEL2_CODE_ADDR_ALIGNMENT,
                               cache_line_size,
                               pds_upload_out);

   vk_free(&device->vk.alloc, staging_buffer);

   return result;
}

static VkResult pvr_device_init_compute_fence_program(struct pvr_device *device)
{
   struct pvr_pds_compute_shader_program program;

   pvr_pds_compute_shader_program_init(&program);
   /* Fence kernel. */
   program.fence = true;
   program.clear_pds_barrier = true;

   return pvr_arch_pds_compute_shader_create_and_upload(
      device,
      &program,
      &device->pds_compute_fence_program);
}

static VkResult pvr_device_init_compute_empty_program(struct pvr_device *device)
{
   struct pvr_pds_compute_shader_program program;

   pvr_pds_compute_shader_program_init(&program);
   program.clear_pds_barrier = true;

   return pvr_arch_pds_compute_shader_create_and_upload(
      device,
      &program,
      &device->pds_compute_empty_program);
}

static VkResult pvr_pds_idfwdf_programs_create_and_upload(
   struct pvr_device *device,
   pvr_dev_addr_t usc_addr,
   uint32_t shareds,
   uint32_t temps,
   pvr_dev_addr_t shareds_buffer_addr,
   struct pvr_pds_upload *const upload_out,
   struct pvr_pds_upload *const sw_compute_barrier_upload_out)
{
   const struct pvr_device_info *dev_info = &device->pdevice->dev_info;
   struct pvr_pds_vertex_shader_sa_program program = {
      .kick_usc = true,
      .clear_pds_barrier = PVR_NEED_SW_COMPUTE_PDS_BARRIER(dev_info),
   };
   size_t staging_buffer_size;
   uint32_t *staging_buffer;
   VkResult result;

   /* We'll need to DMA the shareds into the USC's Common Store. */
   program.num_dma_kicks = pvr_pds_encode_dma_burst(program.dma_control,
                                                    program.dma_address,
                                                    0,
                                                    shareds,
                                                    shareds_buffer_addr.addr,
                                                    false,
                                                    dev_info);

   /* DMA temp regs. */
   pvr_pds_setup_doutu(&program.usc_task_control,
                       usc_addr.addr,
                       temps,
                       ROGUE_PDSINST_DOUTU_SAMPLE_RATE_INSTANCE,
                       false);

   pvr_pds_vertex_shader_sa(&program, NULL, PDS_GENERATE_SIZES, dev_info);

   staging_buffer_size = PVR_DW_TO_BYTES(program.code_size + program.data_size);

   staging_buffer = vk_alloc(&device->vk.alloc,
                             staging_buffer_size,
                             8,
                             VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
   if (!staging_buffer)
      return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);

   /* FIXME: Add support for PDS_GENERATE_CODEDATA_SEGMENTS? */
   pvr_pds_vertex_shader_sa(&program,
                            staging_buffer,
                            PDS_GENERATE_DATA_SEGMENT,
                            dev_info);
   pvr_pds_vertex_shader_sa(&program,
                            &staging_buffer[program.data_size],
                            PDS_GENERATE_CODE_SEGMENT,
                            dev_info);

   /* At the time of writing, the SW_COMPUTE_PDS_BARRIER variant of the program
    * is bigger so we handle it first (if needed) and realloc() for a smaller
    * size.
    */
   if (PVR_NEED_SW_COMPUTE_PDS_BARRIER(dev_info)) {
      /* FIXME: Figure out the define for alignment of 16. */
      result = pvr_gpu_upload_pds(device,
                                  &staging_buffer[0],
                                  program.data_size,
                                  16,
                                  &staging_buffer[program.data_size],
                                  program.code_size,
                                  16,
                                  16,
                                  sw_compute_barrier_upload_out);
      if (result != VK_SUCCESS) {
         vk_free(&device->vk.alloc, staging_buffer);
         return result;
      }

      program.clear_pds_barrier = false;

      pvr_pds_vertex_shader_sa(&program, NULL, PDS_GENERATE_SIZES, dev_info);

      staging_buffer_size =
         PVR_DW_TO_BYTES(program.code_size + program.data_size);

      staging_buffer = vk_realloc(&device->vk.alloc,
                                  staging_buffer,
                                  staging_buffer_size,
                                  8,
                                  VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
      if (!staging_buffer) {
         pvr_bo_suballoc_free(sw_compute_barrier_upload_out->pvr_bo);

         return vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);
      }

      /* FIXME: Add support for PDS_GENERATE_CODEDATA_SEGMENTS? */
      pvr_pds_vertex_shader_sa(&program,
                               staging_buffer,
                               PDS_GENERATE_DATA_SEGMENT,
                               dev_info);
      pvr_pds_vertex_shader_sa(&program,
                               &staging_buffer[program.data_size],
                               PDS_GENERATE_CODE_SEGMENT,
                               dev_info);
   } else {
      *sw_compute_barrier_upload_out = (struct pvr_pds_upload){
         .pvr_bo = NULL,
      };
   }

   /* FIXME: Figure out the define for alignment of 16. */
   result = pvr_gpu_upload_pds(device,
                               &staging_buffer[0],
                               program.data_size,
                               16,
                               &staging_buffer[program.data_size],
                               program.code_size,
                               16,
                               16,
                               upload_out);
   if (result != VK_SUCCESS) {
      vk_free(&device->vk.alloc, staging_buffer);
      pvr_bo_suballoc_free(sw_compute_barrier_upload_out->pvr_bo);

      return result;
   }

   vk_free(&device->vk.alloc, staging_buffer);

   return VK_SUCCESS;
}

static VkResult pvr_device_init_compute_idfwdf_state(struct pvr_device *device)
{
   struct pvr_sampler_descriptor sampler_state;
   struct pvr_image_descriptor image_state;
   struct pvr_texture_state_info tex_info;
   const pco_precomp_data *precomp_data;
   uint32_t *dword_ptr;
   VkResult result;

   precomp_data = (pco_precomp_data *)pco_usclib_common[CS_IDFWDF_COMMON];
   device->idfwdf_state.usc_shareds = _PVR_IDFWDF_DATA_COUNT;

   /* FIXME: Figure out the define for alignment of 16. */
   result = pvr_gpu_upload_usc(device,
                               precomp_data->binary,
                               precomp_data->size_dwords * sizeof(uint32_t),
                               16,
                               &device->idfwdf_state.usc);

   if (result != VK_SUCCESS)
      return result;

   result = pvr_bo_alloc(device,
                         device->heaps.general_heap,
                         PVR_IDFWDF_TEX_WIDTH * PVR_IDFWDF_TEX_HEIGHT *
                            vk_format_get_blocksize(PVR_IDFWDF_TEX_FORMAT),
                         4,
                         0,
                         &device->idfwdf_state.store_bo);
   if (result != VK_SUCCESS)
      goto err_free_usc_program;

   result = pvr_bo_alloc(device,
                         device->heaps.general_heap,
                         _PVR_IDFWDF_DATA_COUNT * ROGUE_REG_SIZE_BYTES,
                         ROGUE_REG_SIZE_BYTES,
                         PVR_BO_ALLOC_FLAG_CPU_MAPPED,
                         &device->idfwdf_state.shareds_bo);
   if (result != VK_SUCCESS)
      goto err_free_store_buffer;

   /* Pack state words. */

   pvr_csb_pack (&sampler_state.words[0], TEXSTATE_SAMPLER_WORD0, sampler) {
      sampler.dadjust = ROGUE_TEXSTATE_DADJUST_ZERO_UINT;
      sampler.magfilter = ROGUE_TEXSTATE_FILTER_POINT;
      sampler.addrmode_u = ROGUE_TEXSTATE_ADDRMODE_CLAMP_TO_EDGE;
      sampler.addrmode_v = ROGUE_TEXSTATE_ADDRMODE_CLAMP_TO_EDGE;
   }

   /* clang-format off */
   pvr_csb_pack (&sampler_state.words[1], TEXSTATE_SAMPLER_WORD1, sampler_word1) {}
   /* clang-format on */

   tex_info = (struct pvr_texture_state_info){
      .format = PVR_IDFWDF_TEX_FORMAT,
      .mem_layout = PVR_MEMLAYOUT_LINEAR,
      .flags = PVR_TEXFLAGS_INDEX_LOOKUP,
      .type = VK_IMAGE_VIEW_TYPE_2D,
      .extent = { .width = PVR_IDFWDF_TEX_WIDTH,
                  .height = PVR_IDFWDF_TEX_HEIGHT },
      .mip_levels = 1,
      .sample_count = 1,
      .stride = PVR_IDFWDF_TEX_STRIDE,
      .swizzle = { PIPE_SWIZZLE_X,
                   PIPE_SWIZZLE_Y,
                   PIPE_SWIZZLE_Z,
                   PIPE_SWIZZLE_W },
      .addr = device->idfwdf_state.store_bo->vma->dev_addr,
   };

   result = pvr_arch_pack_tex_state(device, &tex_info, &image_state);
   if (result != VK_SUCCESS)
      goto err_free_shareds_buffer;

   /* Fill the shareds buffer. */
   dword_ptr = (uint32_t *)device->idfwdf_state.shareds_bo->bo->map;

   memcpy(&dword_ptr[PVR_IDFWDF_DATA_TEX],
          image_state.words,
          sizeof(image_state.words));
   memcpy(&dword_ptr[PVR_IDFWDF_DATA_SMP],
          sampler_state.words,
          sizeof(sampler_state.words));

   dword_ptr[PVR_IDFWDF_DATA_ADDR_LO] =
      device->idfwdf_state.store_bo->vma->dev_addr.addr & 0xffffffff;
   dword_ptr[PVR_IDFWDF_DATA_ADDR_HI] =
      device->idfwdf_state.store_bo->vma->dev_addr.addr >> 32;

   pvr_bo_cpu_unmap(device, device->idfwdf_state.shareds_bo);
   dword_ptr = NULL;

   /* Generate and upload PDS programs. */
   result = pvr_pds_idfwdf_programs_create_and_upload(
      device,
      device->idfwdf_state.usc->dev_addr,
      _PVR_IDFWDF_DATA_COUNT,
      precomp_data->temps,
      device->idfwdf_state.shareds_bo->vma->dev_addr,
      &device->idfwdf_state.pds,
      &device->idfwdf_state.sw_compute_barrier_pds);

   if (result != VK_SUCCESS)
      goto err_free_shareds_buffer;

   return VK_SUCCESS;

err_free_shareds_buffer:
   pvr_bo_free(device, device->idfwdf_state.shareds_bo);

err_free_store_buffer:
   pvr_bo_free(device, device->idfwdf_state.store_bo);

err_free_usc_program:
   pvr_bo_suballoc_free(device->idfwdf_state.usc);

   return result;
}

static void pvr_device_finish_compute_idfwdf_state(struct pvr_device *device)
{
   pvr_bo_suballoc_free(device->idfwdf_state.pds.pvr_bo);
   pvr_bo_suballoc_free(device->idfwdf_state.sw_compute_barrier_pds.pvr_bo);
   pvr_bo_free(device, device->idfwdf_state.shareds_bo);
   pvr_bo_free(device, device->idfwdf_state.store_bo);
   pvr_bo_suballoc_free(device->idfwdf_state.usc);
}

/* FIXME: We should be calculating the size when we upload the code in
 * pvr_srv_setup_static_pixel_event_program().
 */
static void pvr_device_get_pixel_event_pds_program_data_size(
   const struct pvr_device_info *dev_info,
   uint32_t *const data_size_in_dwords_out)
{
   struct pvr_pds_event_program program = {
      /* No data to DMA, just a DOUTU needed. */
      .num_emit_word_pairs = 0,
   };

   pvr_pds_set_sizes_pixel_event(&program, dev_info);

   *data_size_in_dwords_out = program.data_size;
}

static VkResult pvr_device_init_nop_program(struct pvr_device *device)
{
   const uint32_t cache_line_size =
      pvr_get_slc_cache_line_size(&device->pdevice->dev_info);
   struct pvr_pds_kickusc_program program = { 0 };
   const pco_precomp_data *precomp_data;
   uint32_t staging_buffer_size;
   uint32_t *staging_buffer;
   VkResult result;

   precomp_data = (pco_precomp_data *)pco_usclib_common[FS_NOP_COMMON];
   result = pvr_gpu_upload_usc(device,
                               precomp_data->binary,
                               precomp_data->size_dwords * sizeof(uint32_t),
                               cache_line_size,
                               &device->nop_program.usc);
   if (result != VK_SUCCESS)
      return result;

   /* Setup a PDS program that kicks the static USC program. */
   pvr_pds_setup_doutu(&program.usc_task_control,
                       device->nop_program.usc->dev_addr.addr,
                       precomp_data->temps,
                       ROGUE_PDSINST_DOUTU_SAMPLE_RATE_INSTANCE,
                       false);

   pvr_pds_set_sizes_pixel_shader(&program);

   staging_buffer_size = PVR_DW_TO_BYTES(program.code_size + program.data_size);

   staging_buffer = vk_alloc(&device->vk.alloc,
                             staging_buffer_size,
                             8U,
                             VK_SYSTEM_ALLOCATION_SCOPE_COMMAND);
   if (!staging_buffer) {
      result = vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);
      goto err_free_nop_usc_bo;
   }

   pvr_pds_generate_pixel_shader_program(&program, staging_buffer);

   /* FIXME: Figure out the define for alignment of 16. */
   result = pvr_gpu_upload_pds(device,
                               staging_buffer,
                               program.data_size,
                               16U,
                               &staging_buffer[program.data_size],
                               program.code_size,
                               16U,
                               16U,
                               &device->nop_program.pds);
   if (result != VK_SUCCESS)
      goto err_free_staging_buffer;

   vk_free(&device->vk.alloc, staging_buffer);

   return VK_SUCCESS;

err_free_staging_buffer:
   vk_free(&device->vk.alloc, staging_buffer);

err_free_nop_usc_bo:
   pvr_bo_suballoc_free(device->nop_program.usc);

   return result;
}

static VkResult
pvr_device_init_view_index_init_programs(struct pvr_device *device)
{
   uint32_t staging_buffer_size = 0;
   uint32_t *staging_buffer = NULL;
   VkResult result;
   unsigned i;

   for (i = 0; i < PVR_MAX_MULTIVIEW; ++i) {
      struct pvr_pds_view_index_init_program *program =
         &device->view_index_init_info[i];

      program->view_index = i;

      pvr_pds_generate_view_index_init_program(program,
                                               NULL,
                                               PDS_GENERATE_SIZES);

      if (program->data_size + program->code_size > staging_buffer_size) {
         staging_buffer_size = program->data_size + program->code_size;

         staging_buffer = vk_realloc(&device->vk.alloc,
                                     staging_buffer,
                                     staging_buffer_size,
                                     8U,
                                     VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);

         if (!staging_buffer) {
            result = vk_error(device, VK_ERROR_OUT_OF_HOST_MEMORY);
            break;
         }
      }

      pvr_pds_generate_view_index_init_program(program,
                                               staging_buffer,
                                               PDS_GENERATE_DATA_SEGMENT);
      pvr_pds_generate_view_index_init_program(
         program,
         &staging_buffer[program->data_size],
         PDS_GENERATE_CODE_SEGMENT);

      result =
         pvr_gpu_upload_pds(device,
                            (program->data_size == 0 ? NULL : staging_buffer),
                            program->data_size / sizeof(uint32_t),
                            16U,
                            &staging_buffer[program->data_size],
                            program->code_size / sizeof(uint32_t),
                            16U,
                            16U,
                            &device->view_index_init_programs[i]);

      if (result != VK_SUCCESS)
         break;
   }

   vk_free(&device->vk.alloc, staging_buffer);

   if (result != VK_SUCCESS)
      for (uint32_t u = 0; u < i; ++u)
         pvr_bo_suballoc_free(device->view_index_init_programs[u].pvr_bo);

   return result;
}

static void pvr_device_init_tile_buffer_state(struct pvr_device *device)
{
   simple_mtx_init(&device->tile_buffer_state.mtx, mtx_plain);

   for (uint32_t i = 0; i < ARRAY_SIZE(device->tile_buffer_state.buffers); i++)
      device->tile_buffer_state.buffers[i] = NULL;

   device->tile_buffer_state.buffer_count = 0;
}

static void pvr_device_finish_tile_buffer_state(struct pvr_device *device)
{
   /* Destroy the mutex first to trigger asserts in case it's still locked so
    * that we don't put things in an inconsistent state by freeing buffers that
    * might be in use or attempt to free buffers while new buffers are being
    * allocated.
    */
   simple_mtx_destroy(&device->tile_buffer_state.mtx);
   pvr_device_free_tile_buffer_state(device);
}

static void pvr_device_init_default_sampler_state(struct pvr_device *device)
{
   pvr_csb_pack (&device->input_attachment_sampler,
                 TEXSTATE_SAMPLER_WORD0,
                 sampler) {
      sampler.addrmode_u = ROGUE_TEXSTATE_ADDRMODE_CLAMP_TO_EDGE;
      sampler.addrmode_v = ROGUE_TEXSTATE_ADDRMODE_CLAMP_TO_EDGE;
      sampler.addrmode_w = ROGUE_TEXSTATE_ADDRMODE_CLAMP_TO_EDGE;
      sampler.dadjust = ROGUE_TEXSTATE_DADJUST_ZERO_UINT;
      sampler.magfilter = ROGUE_TEXSTATE_FILTER_POINT;
      sampler.minfilter = ROGUE_TEXSTATE_FILTER_POINT;
      sampler.anisoctl = ROGUE_TEXSTATE_ANISOCTL_DISABLED;
      sampler.non_normalized_coords = true;
   }
}

VkResult PVR_PER_ARCH(create_device)(struct pvr_physical_device *pdevice,
                                     const VkDeviceCreateInfo *pCreateInfo,
                                     const VkAllocationCallbacks *pAllocator,
                                     VkDevice *pDevice)
{
   uint32_t initial_free_list_size = PVR_GLOBAL_FREE_LIST_INITIAL_SIZE;
   struct pvr_instance *instance = pdevice->instance;
   struct vk_device_dispatch_table dispatch_table;
   struct pvr_device *device;
   struct pvr_winsys *ws;
   VkResult result;

   assert(pCreateInfo->sType == VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO);

   result = pvr_winsys_create(pdevice->render_path,
                              pdevice->display_path, false,
                              pAllocator ? pAllocator : &instance->vk.alloc,
                              &ws);
   if (result != VK_SUCCESS)
      goto err_out;

   device = vk_alloc2(&instance->vk.alloc,
                      pAllocator,
                      sizeof(*device),
                      8,
                      VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
   if (!device) {
      result = vk_error(instance, VK_ERROR_OUT_OF_HOST_MEMORY);
      goto err_pvr_winsys_destroy;
   }

   vk_device_dispatch_table_from_entrypoints(&dispatch_table,
                                             &PVR_PER_ARCH(device_entrypoints),
                                             true);
   vk_device_dispatch_table_from_entrypoints(&dispatch_table,
                                             &pvr_device_entrypoints,
                                             false);

   vk_device_dispatch_table_from_entrypoints(&dispatch_table,
                                             &wsi_device_entrypoints,
                                             false);

   result = vk_device_init(&device->vk,
                           &pdevice->vk,
                           &dispatch_table,
                           pCreateInfo,
                           pAllocator);
   if (result != VK_SUCCESS)
      goto err_free_device;

   device->instance = instance;
   device->pdevice = pdevice;
   device->ws = ws;

   vk_device_set_drm_fd(&device->vk, ws->render_fd);

   if (ws->features.supports_threaded_submit) {
      /* Queue submission can be blocked if the kernel CCBs become full,
       * so enable threaded submit to not block the submitter.
       */
      vk_device_enable_threaded_submit(&device->vk);
   }

   ws->ops->get_heaps_info(ws, &device->heaps);

   result = pvr_bo_store_create(device);
   if (result != VK_SUCCESS)
      goto err_vk_device_finish;

   pvr_bo_suballocator_init(&device->suballoc_general,
                            device->heaps.general_heap,
                            device,
                            PVR_SUBALLOCATOR_GENERAL_SIZE);
   pvr_bo_suballocator_init(&device->suballoc_pds,
                            device->heaps.pds_heap,
                            device,
                            PVR_SUBALLOCATOR_PDS_SIZE);
   pvr_bo_suballocator_init(&device->suballoc_transfer,
                            device->heaps.transfer_frag_heap,
                            device,
                            PVR_SUBALLOCATOR_TRANSFER_SIZE);
   pvr_bo_suballocator_init(&device->suballoc_usc,
                            device->heaps.usc_heap,
                            device,
                            PVR_SUBALLOCATOR_USC_SIZE);
   pvr_bo_suballocator_init(&device->suballoc_vis_test,
                            device->heaps.vis_test_heap,
                            device,
                            PVR_SUBALLOCATOR_VIS_TEST_SIZE);

   if (p_atomic_inc_return(&instance->active_device_count) >
       PVR_SECONDARY_DEVICE_THRESHOLD) {
      initial_free_list_size = PVR_SECONDARY_DEVICE_FREE_LIST_INITAL_SIZE;
   }

   result = pvr_free_list_create(device,
                                 initial_free_list_size,
                                 PVR_GLOBAL_FREE_LIST_MAX_SIZE,
                                 PVR_GLOBAL_FREE_LIST_GROW_SIZE,
                                 PVR_GLOBAL_FREE_LIST_GROW_THRESHOLD,
                                 NULL /* parent_free_list */,
                                 &device->global_free_list);
   if (result != VK_SUCCESS)
      goto err_dec_device_count;

   result = pvr_device_init_nop_program(device);
   if (result != VK_SUCCESS)
      goto err_pvr_free_list_destroy;

   result = pvr_device_init_compute_fence_program(device);
   if (result != VK_SUCCESS)
      goto err_pvr_free_nop_program;

   result = pvr_device_init_compute_empty_program(device);
   if (result != VK_SUCCESS)
      goto err_pvr_free_compute_fence;

   result = pvr_device_init_view_index_init_programs(device);
   if (result != VK_SUCCESS)
      goto err_pvr_free_compute_empty;

   result = pvr_arch_device_create_compute_query_programs(device);
   if (result != VK_SUCCESS)
      goto err_pvr_free_view_index;

   result = pvr_device_init_compute_idfwdf_state(device);
   if (result != VK_SUCCESS)
      goto err_pvr_destroy_compute_query_programs;

   result = pvr_device_init_graphics_static_clear_state(device);
   if (result != VK_SUCCESS)
      goto err_pvr_finish_compute_idfwdf;

   result = pvr_arch_device_init_spm_load_state(device);
   if (result != VK_SUCCESS)
      goto err_pvr_finish_graphics_static_clear_state;

   pvr_device_init_tile_buffer_state(device);

   result = pvr_arch_queues_create(device, pCreateInfo);
   if (result != VK_SUCCESS)
      goto err_pvr_finish_tile_buffer_state;

   pvr_device_init_default_sampler_state(device);

   pvr_spm_init_scratch_buffer_store(device);

   result = pvr_init_robustness_buffer(device);
   if (result != VK_SUCCESS)
      goto err_pvr_spm_finish_scratch_buffer_store;

   result = pvr_arch_border_color_table_init(device);
   if (result != VK_SUCCESS)
      goto err_pvr_robustness_buffer_finish;

   /* FIXME: Move this to a later stage and possibly somewhere other than
    * pvr_device. The purpose of this is so that we don't have to get the size
    * on each kick.
    */
   pvr_device_get_pixel_event_pds_program_data_size(
      &pdevice->dev_info,
      &device->pixel_event_data_size_in_dwords);

   device->global_cmd_buffer_submit_count = 0;
   device->global_queue_present_count = 0;

   simple_mtx_init(&device->rs_mtx, mtx_plain);
   list_inithead(&device->render_states);

   *pDevice = pvr_device_to_handle(device);

   return VK_SUCCESS;

err_pvr_robustness_buffer_finish:
   pvr_robustness_buffer_finish(device);

err_pvr_spm_finish_scratch_buffer_store:
   pvr_spm_finish_scratch_buffer_store(device);

   pvr_arch_queues_destroy(device);

err_pvr_finish_tile_buffer_state:
   pvr_device_finish_tile_buffer_state(device);
   pvr_arch_device_finish_spm_load_state(device);

err_pvr_finish_graphics_static_clear_state:
   pvr_device_finish_graphics_static_clear_state(device);

err_pvr_finish_compute_idfwdf:
   pvr_device_finish_compute_idfwdf_state(device);

err_pvr_destroy_compute_query_programs:
   pvr_arch_device_destroy_compute_query_programs(device);

err_pvr_free_view_index:
   for (uint32_t u = 0; u < PVR_MAX_MULTIVIEW; ++u)
      pvr_bo_suballoc_free(device->view_index_init_programs[u].pvr_bo);

err_pvr_free_compute_empty:
   pvr_bo_suballoc_free(device->pds_compute_empty_program.pvr_bo);

err_pvr_free_compute_fence:
   pvr_bo_suballoc_free(device->pds_compute_fence_program.pvr_bo);

err_pvr_free_nop_program:
   pvr_bo_suballoc_free(device->nop_program.pds.pvr_bo);
   pvr_bo_suballoc_free(device->nop_program.usc);

err_pvr_free_list_destroy:
   pvr_free_list_destroy(device->global_free_list);

err_dec_device_count:
   p_atomic_dec(&device->instance->active_device_count);

   pvr_bo_suballocator_fini(&device->suballoc_vis_test);
   pvr_bo_suballocator_fini(&device->suballoc_usc);
   pvr_bo_suballocator_fini(&device->suballoc_transfer);
   pvr_bo_suballocator_fini(&device->suballoc_pds);
   pvr_bo_suballocator_fini(&device->suballoc_general);

   pvr_bo_store_destroy(device);

err_vk_device_finish:
   vk_device_finish(&device->vk);

err_free_device:
   vk_free(&device->vk.alloc, device);

err_pvr_winsys_destroy:
   pvr_winsys_destroy(ws);

err_out:
   return result;
}

void PVR_PER_ARCH(destroy_device)(struct pvr_device *device,
                                  const VkAllocationCallbacks *pAllocator)
{
   if (!device)
      return;

   simple_mtx_lock(&device->rs_mtx);
   list_for_each_entry_safe (struct pvr_render_state,
                             rstate,
                             &device->render_states,
                             link) {
      pvr_render_state_cleanup(device, pAllocator, rstate);
      list_del(&rstate->link);

      vk_free(&device->vk.alloc, rstate);
   }
   simple_mtx_unlock(&device->rs_mtx);
   simple_mtx_destroy(&device->rs_mtx);

   pvr_arch_border_color_table_finish(device);
   pvr_robustness_buffer_finish(device);
   pvr_spm_finish_scratch_buffer_store(device);
   pvr_arch_queues_destroy(device);
   pvr_device_finish_tile_buffer_state(device);
   pvr_arch_device_finish_spm_load_state(device);
   pvr_device_finish_graphics_static_clear_state(device);
   pvr_device_finish_compute_idfwdf_state(device);
   pvr_arch_device_destroy_compute_query_programs(device);
   pvr_bo_suballoc_free(device->pds_compute_empty_program.pvr_bo);

   for (uint32_t u = 0; u < PVR_MAX_MULTIVIEW; ++u)
      pvr_bo_suballoc_free(device->view_index_init_programs[u].pvr_bo);

   pvr_bo_suballoc_free(device->pds_compute_fence_program.pvr_bo);
   pvr_bo_suballoc_free(device->nop_program.pds.pvr_bo);
   pvr_bo_suballoc_free(device->nop_program.usc);
   pvr_free_list_destroy(device->global_free_list);
   pvr_bo_suballocator_fini(&device->suballoc_vis_test);
   pvr_bo_suballocator_fini(&device->suballoc_usc);
   pvr_bo_suballocator_fini(&device->suballoc_transfer);
   pvr_bo_suballocator_fini(&device->suballoc_pds);
   pvr_bo_suballocator_fini(&device->suballoc_general);
   pvr_bo_store_destroy(device);
   pvr_winsys_destroy(device->ws);
   p_atomic_dec(&device->instance->active_device_count);
   vk_device_finish(&device->vk);
   vk_free(&device->vk.alloc, device);
}
