/*
 * Copyright © 2022 Imagination Technologies Ltd.
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

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include <vulkan/vulkan.h>

#include "fw-api/pvr_rogue_fwif.h"
#include "fw-api/pvr_rogue_fwif_rf.h"
#include "pvr_device_info.h"
#include "pvr_srv.h"
#include "pvr_srv_bridge.h"
#include "pvr_srv_job_common.h"
#include "pvr_srv_job_compute.h"
#include "pvr_srv_sync.h"
#include "pvr_winsys.h"
#include "util/macros.h"
#include "util/os_file.h"
#include "vk_alloc.h"
#include "vk_log.h"

VkResult pvr_srv_winsys_compute_ctx_create(
   struct pvr_winsys *ws,
   const struct pvr_winsys_compute_ctx_create_info *create_info,
   const struct pvr_device_info *dev_info,
   struct pvr_winsys_compute_ctx **const ctx_out)
{
   /* TODO: handle non-rogue GPUs */
   assert(dev_info->ident.arch == PVR_DEVICE_ARCH_ROGUE);
   const struct pvr_rogue_winsys_compute_ctx_static_state *rogue_state =
      &create_info->static_state.rogue;

   struct rogue_fwif_static_computecontext_state static_state = {
		.ctx_switch_regs = {
			.cdm_context_pds0 = rogue_state->cdm_ctx_store_pds0,
			.cdm_context_pds0_b = rogue_state->cdm_ctx_store_pds0_b,
			.cdm_context_pds1 = rogue_state->cdm_ctx_store_pds1,

			.cdm_terminate_pds = rogue_state->cdm_ctx_terminate_pds,
			.cdm_terminate_pds1 = rogue_state->cdm_ctx_terminate_pds1,

			.cdm_resume_pds0 = rogue_state->cdm_ctx_resume_pds0,
			.cdm_resume_pds0_b = rogue_state->cdm_ctx_resume_pds0_b,
		},
	};

   struct rogue_fwif_rf_cmd reset_cmd = { 0 };

   struct pvr_srv_winsys *srv_ws = to_pvr_srv_winsys(ws);
   struct pvr_srv_winsys_compute_ctx *srv_ctx;
   VkResult result;

   srv_ctx = vk_alloc(ws->alloc,
                      sizeof(*srv_ctx),
                      8U,
                      VK_SYSTEM_ALLOCATION_SCOPE_DEVICE);
   if (!srv_ctx)
      return vk_error(NULL, VK_ERROR_OUT_OF_HOST_MEMORY);

   result = pvr_srv_create_timeline(ws->render_fd, &srv_ctx->timeline);
   if (result != VK_SUCCESS)
      goto err_free_srv_ctx;

   /* TODO: Add support for reset framework. Currently we subtract
    * reset_cmd.regs size from reset_cmd size to only pass empty flags field.
    */
   result = pvr_srv_rgx_create_compute_context(
      ws->render_fd,
      pvr_srv_from_winsys_priority(create_info->priority),
      sizeof(reset_cmd) - sizeof(reset_cmd.regs),
      (uint8_t *)&reset_cmd,
      srv_ws->server_memctx_data,
      sizeof(static_state),
      (uint8_t *)&static_state,
      0U,
      RGX_CONTEXT_FLAG_DISABLESLR,
      0U,
      UINT_MAX,
      &srv_ctx->handle);
   if (result != VK_SUCCESS)
      goto err_close_timeline;

   srv_ctx->base.ws = ws;

   *ctx_out = &srv_ctx->base;

   return VK_SUCCESS;

err_close_timeline:
   close(srv_ctx->timeline);

err_free_srv_ctx:
   vk_free(ws->alloc, srv_ctx);

   return result;
}

void pvr_srv_winsys_compute_ctx_destroy(struct pvr_winsys_compute_ctx *ctx)
{
   struct pvr_srv_winsys *srv_ws = to_pvr_srv_winsys(ctx->ws);
   struct pvr_srv_winsys_compute_ctx *srv_ctx =
      to_pvr_srv_winsys_compute_ctx(ctx);

   pvr_srv_rgx_destroy_compute_context(srv_ws->base.render_fd, srv_ctx->handle);
   close(srv_ctx->timeline);
   vk_free(srv_ws->base.alloc, srv_ctx);
}
