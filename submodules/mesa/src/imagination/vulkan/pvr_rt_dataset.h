/*
 * Copyright © 2023 Imagination Technologies Ltd.
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef PVR_RT_DATASET_H
#define PVR_RT_DATASET_H

#include <stdint.h>
#include <stdbool.h>

#include "util/bitscan.h"

#include "pvr_framebuffer.h"
#include "pvr_rogue_fw.h"
#include "pvr_types.h"

struct pvr_bo;
struct pvr_device;
struct pvr_free_list;
struct pvr_winsys_rt_dataset;

struct pvr_rt_dataset {
   struct pvr_device *device;

   /* RT dataset information */
   uint32_t width;
   uint32_t height;
   uint32_t samples;
   uint32_t layers;

   struct pvr_free_list *global_free_list;
   struct pvr_free_list *local_free_list;

   struct pvr_bo *vheap_rtc_bo;
   pvr_dev_addr_t vheap_dev_addr;
   pvr_dev_addr_t rtc_dev_addr;

   struct pvr_bo *tpc_bo;
   uint64_t tpc_stride;
   uint64_t tpc_size;

   struct pvr_winsys_rt_dataset *ws_rt_dataset;

   /* RT data information */
   struct pvr_bo *mta_bo;
   struct pvr_bo *mlist_bo;

   struct pvr_bo *rgn_headers_bo;
   uint64_t rgn_headers_stride;

   bool need_frag;

   uint8_t rt_data_idx;

   struct {
      pvr_dev_addr_t mta_dev_addr;
      pvr_dev_addr_t mlist_dev_addr;
      pvr_dev_addr_t rgn_headers_dev_addr;
   } rt_datas[ROGUE_NUM_RTDATAS];
};

void pvr_rt_datas_fini(struct pvr_rt_dataset *rt_dataset);
void pvr_rt_mlist_data_fini(struct pvr_rt_dataset *rt_dataset);
void pvr_rt_mta_data_fini(struct pvr_rt_dataset *rt_dataset);

void pvr_rt_tpc_data_fini(struct pvr_rt_dataset *rt_dataset);
void pvr_rt_vheap_rtc_data_fini(struct pvr_rt_dataset *rt_dataset);

void pvr_render_target_dataset_destroy(struct pvr_rt_dataset *rt_dataset);

static inline void
pvr_render_targets_datasets_destroy(struct pvr_render_target *render_target)
{
   u_foreach_bit (valid_idx, render_target->valid_mask) {
      struct pvr_rt_dataset *rt_dataset = render_target->rt_dataset[valid_idx];

      if (rt_dataset && render_target->valid_mask & BITFIELD_BIT(valid_idx))
         pvr_render_target_dataset_destroy(rt_dataset);

      render_target->rt_dataset[valid_idx] = NULL;
      render_target->valid_mask &= ~BITFIELD_BIT(valid_idx);
   }
}

#endif /* PVR_RT_DATASET_H */
