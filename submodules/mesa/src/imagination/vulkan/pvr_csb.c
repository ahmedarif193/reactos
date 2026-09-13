/*
 * Copyright © 2022 Imagination Technologies Ltd.
 *
 * based in part on anv driver which is:
 * Copyright © 2015 Intel Corporation
 *
 * based in part on v3dv_cl.c which is:
 * Copyright © 2019 Raspberry Pi
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "pvr_csb.h"

#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include <vulkan/vulkan.h>

#include "hwdef/pvr_hw_utils.h"
#include "pvr_bo.h"
#include "pvr_debug.h"
#include "pvr_device.h"
#include "pvr_device_info.h"
#include "pvr_macros.h"
#include "pvr_physical_device.h"
#include "pvr_types.h"
#include "util/list.h"
#include "util/u_dynarray.h"
#include "vk_log.h"

/**
 * \file pvr_csb.c
 *
 * \brief Contains functions to manage Control Stream Builder (csb) object.
 *
 * A csb object can be used to create a primary/main control stream, referred
 * as control stream hereafter, or a secondary control stream, also referred as
 * a sub control stream. The main difference between these is that, the control
 * stream is the one directly submitted to the GPU and is terminated using
 * STREAM_TERMINATE. Whereas, the secondary control stream can be thought of as
 * an independent set of commands that can be referenced by a primary control
 * stream to avoid duplication and is instead terminated using STREAM_RETURN,
 * which means the control stream parser should return to the main stream it
 * came from.
 *
 * Note: Sub control stream is only supported for PVR_CMD_STREAM_TYPE_GRAPHICS
 * type control streams.
 */

/**
 * \brief Initializes the csb object.
 *
 * \param[in] device Logical device pointer.
 * \param[in] csb    Control Stream Builder object to initialize.
 *
 * \sa #pvr_csb_finish()
 */
void pvr_csb_init(struct pvr_device *device,
                  enum pvr_cmd_stream_type stream_type,
                  struct pvr_csb *csb)
{
   csb->start = NULL;
   csb->next = NULL;
   csb->pvr_bo = NULL;
   csb->end = NULL;
   csb->relocation_mark = NULL;

#if MESA_DEBUG
   csb->relocation_mark_status = PVR_CSB_RELOCATION_MARK_UNINITIALIZED;
#endif

   csb->device = device;
   csb->stream_type = stream_type;
   csb->status = VK_SUCCESS;

   if (stream_type == PVR_CMD_STREAM_TYPE_GRAPHICS_DEFERRED)
      csb->deferred_cs_mem = UTIL_DYNARRAY_INIT;
   else
      list_inithead(&csb->pvr_bo_list);
}

/**
 * \brief Frees the resources associated with the csb object.
 *
 * \param[in] csb Control Stream Builder object to free.
 *
 * \sa #pvr_csb_init()
 */
void pvr_csb_finish(struct pvr_csb *csb)
{
#if MESA_DEBUG
   assert(csb->relocation_mark_status ==
             PVR_CSB_RELOCATION_MARK_UNINITIALIZED ||
          csb->relocation_mark_status == PVR_CSB_RELOCATION_MARK_CLEARED);
#endif

   if (csb->stream_type == PVR_CMD_STREAM_TYPE_GRAPHICS_DEFERRED) {
      util_dynarray_fini(&csb->deferred_cs_mem);
   } else {
      list_for_each_entry_safe (struct pvr_bo, pvr_bo, &csb->pvr_bo_list, link) {
         list_del(&pvr_bo->link);
         pvr_bo_free(csb->device, pvr_bo);
      }
   }

   /* Leave the csb in a reset state to catch use after destroy instances */
   pvr_csb_init(NULL, PVR_CMD_STREAM_TYPE_INVALID, csb);
}

/**
 * \brief Discard information only required while building and return the BOs.
 *
 * \param[in] csb Control Stream Builder object to bake.
 * \param[out] bo_list_out A list of \c pvr_bo containing the control stream.
 *
 * \return The last status value of \c csb.
 *
 * The value of \c bo_list_out is only defined iff this function returns
 * \c VK_SUCCESS. It is not allowed to call this function on a \c pvr_csb for
 * a deferred control stream type.
 *
 * The state of \c csb after calling this function (iff it returns
 * \c VK_SUCCESS) is identical to that after calling #pvr_csb_finish().
 * Unlike #pvr_csb_finish(), however, the caller must free every entry in
 * \c bo_list_out itself.
 */
VkResult pvr_csb_bake(struct pvr_csb *const csb,
                      struct list_head *const bo_list_out)
{
   assert(csb->stream_type != PVR_CMD_STREAM_TYPE_GRAPHICS_DEFERRED);

   if (csb->status != VK_SUCCESS)
      return csb->status;

   list_replace(&csb->pvr_bo_list, bo_list_out);

   /* Same as pvr_csb_finish(). */
   pvr_csb_init(NULL, PVR_CMD_STREAM_TYPE_INVALID, csb);

   return VK_SUCCESS;
}
