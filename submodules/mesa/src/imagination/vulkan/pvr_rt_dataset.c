/*
 * Copyright © 2023 Imagination Technologies Ltd.
 *
 * SPDX-License-Identifier: MIT
 */

#include "pvr_rt_dataset.h"

#include "pvr_device.h"
#include "pvr_free_list.h"

static void pvr_rt_rgn_headers_data_fini(struct pvr_rt_dataset *rt_dataset)
{
   for (uint32_t i = 0; i < ARRAY_SIZE(rt_dataset->rt_datas); i++)
      rt_dataset->rt_datas[i].rgn_headers_dev_addr = PVR_DEV_ADDR_INVALID;

   pvr_bo_free(rt_dataset->device, rt_dataset->rgn_headers_bo);
   rt_dataset->rgn_headers_bo = NULL;
}

void pvr_rt_mlist_data_fini(struct pvr_rt_dataset *rt_dataset)
{
   for (uint32_t i = 0; i < ARRAY_SIZE(rt_dataset->rt_datas); i++)
      rt_dataset->rt_datas[i].mlist_dev_addr = PVR_DEV_ADDR_INVALID;

   pvr_bo_free(rt_dataset->device, rt_dataset->mlist_bo);
   rt_dataset->mlist_bo = NULL;
}

void pvr_rt_mta_data_fini(struct pvr_rt_dataset *rt_dataset)
{
   if (rt_dataset->mta_bo == NULL)
      return;

   for (uint32_t i = 0; i < ARRAY_SIZE(rt_dataset->rt_datas); i++)
      rt_dataset->rt_datas[i].mta_dev_addr = PVR_DEV_ADDR_INVALID;

   pvr_bo_free(rt_dataset->device, rt_dataset->mta_bo);
   rt_dataset->mta_bo = NULL;
}

void pvr_rt_datas_fini(struct pvr_rt_dataset *rt_dataset)
{
   pvr_rt_rgn_headers_data_fini(rt_dataset);
   pvr_rt_mlist_data_fini(rt_dataset);
   pvr_rt_mta_data_fini(rt_dataset);
}

void pvr_rt_tpc_data_fini(struct pvr_rt_dataset *rt_dataset)
{
   pvr_bo_free(rt_dataset->device, rt_dataset->tpc_bo);
   rt_dataset->tpc_bo = NULL;
}

void pvr_rt_vheap_rtc_data_fini(struct pvr_rt_dataset *rt_dataset)
{
   rt_dataset->rtc_dev_addr = PVR_DEV_ADDR_INVALID;

   pvr_bo_free(rt_dataset->device, rt_dataset->vheap_rtc_bo);
   rt_dataset->vheap_rtc_bo = NULL;
}

void pvr_render_target_dataset_destroy(struct pvr_rt_dataset *rt_dataset)
{
   struct pvr_device *device = rt_dataset->device;

   device->ws->ops->render_target_dataset_destroy(rt_dataset->ws_rt_dataset);

   pvr_rt_datas_fini(rt_dataset);
   pvr_rt_tpc_data_fini(rt_dataset);
   pvr_rt_vheap_rtc_data_fini(rt_dataset);

   pvr_free_list_destroy(rt_dataset->local_free_list);

   vk_free(&device->vk.alloc, rt_dataset);
}
