/*
 * Copyright © 2019 Red Hat.
 * Copyright © 2022 Collabora, LTD
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

#include "vk_alloc.h"
#include "vk_cmd_enqueue_entrypoints.h"
#include "vk_command_buffer.h"
#include "vk_device.h"
#include "vk_util.h"


VKAPI_ATTR void VKAPI_CALL
vk_cmd_enqueue_CmdDrawMultiEXT(VkCommandBuffer commandBuffer,
                               uint32_t drawCount,
                               const VkMultiDrawInfoEXT *pVertexInfo,
                               uint32_t instanceCount,
                               uint32_t firstInstance,
                               uint32_t stride)
{
   VK_FROM_HANDLE(vk_command_buffer, cmd_buffer, commandBuffer);

   struct vk_cmd_queue_entry *cmd =
      linear_zalloc_child(cmd_buffer->cmd_queue.ctx, sizeof(*cmd));
   if (!cmd)
      return;

   cmd->type = VK_CMD_DRAW_MULTI_EXT;
   list_addtail(&cmd->cmd_link, &cmd_buffer->cmd_queue.cmds);

   cmd->u.draw_multi_ext.draw_count = drawCount;
   if (pVertexInfo) {
      unsigned i = 0;
      cmd->u.draw_multi_ext.vertex_info =
         linear_alloc_child(cmd_buffer->cmd_queue.ctx,
                            sizeof(*cmd->u.draw_multi_ext.vertex_info) * drawCount);

      vk_foreach_multi_draw(draw, i, pVertexInfo, drawCount, stride) {
         memcpy(&cmd->u.draw_multi_ext.vertex_info[i], draw,
                sizeof(*cmd->u.draw_multi_ext.vertex_info));
      }
   }
   cmd->u.draw_multi_ext.instance_count = instanceCount;
   cmd->u.draw_multi_ext.first_instance = firstInstance;
   cmd->u.draw_multi_ext.stride = sizeof(*cmd->u.draw_multi_ext.vertex_info);
}

VKAPI_ATTR void VKAPI_CALL
vk_cmd_enqueue_CmdDrawMultiIndexedEXT(VkCommandBuffer commandBuffer,
                                      uint32_t drawCount,
                                      const VkMultiDrawIndexedInfoEXT *pIndexInfo,
                                      uint32_t instanceCount,
                                      uint32_t firstInstance,
                                      uint32_t stride,
                                      const int32_t *pVertexOffset)
{
   VK_FROM_HANDLE(vk_command_buffer, cmd_buffer, commandBuffer);

   struct vk_cmd_queue_entry *cmd =
      linear_zalloc_child(cmd_buffer->cmd_queue.ctx, sizeof(*cmd));
   if (!cmd)
      return;

   cmd->type = VK_CMD_DRAW_MULTI_INDEXED_EXT;
   list_addtail(&cmd->cmd_link, &cmd_buffer->cmd_queue.cmds);

   cmd->u.draw_multi_indexed_ext.draw_count = drawCount;

   if (pIndexInfo) {
      unsigned i = 0;
      cmd->u.draw_multi_indexed_ext.index_info =
         linear_alloc_child(cmd_buffer->cmd_queue.ctx,
                            sizeof(*cmd->u.draw_multi_indexed_ext.index_info) * drawCount);

      vk_foreach_multi_draw_indexed(draw, i, pIndexInfo, drawCount, stride) {
         cmd->u.draw_multi_indexed_ext.index_info[i].firstIndex = draw->firstIndex;
         cmd->u.draw_multi_indexed_ext.index_info[i].indexCount = draw->indexCount;
         if (pVertexOffset == NULL)
            cmd->u.draw_multi_indexed_ext.index_info[i].vertexOffset = draw->vertexOffset;
      }
   }

   cmd->u.draw_multi_indexed_ext.instance_count = instanceCount;
   cmd->u.draw_multi_indexed_ext.first_instance = firstInstance;
   cmd->u.draw_multi_indexed_ext.stride = sizeof(*cmd->u.draw_multi_indexed_ext.index_info);

   if (pVertexOffset) {
      cmd->u.draw_multi_indexed_ext.vertex_offset =
         linear_alloc_child(cmd_buffer->cmd_queue.ctx,
                            sizeof(*cmd->u.draw_multi_indexed_ext.vertex_offset));

      memcpy(cmd->u.draw_multi_indexed_ext.vertex_offset, pVertexOffset,
             sizeof(*cmd->u.draw_multi_indexed_ext.vertex_offset));
   }
}

#ifdef VK_ENABLE_BETA_EXTENSIONS

VKAPI_ATTR void VKAPI_CALL
vk_cmd_enqueue_CmdDispatchGraphAMDX(VkCommandBuffer commandBuffer, VkDeviceAddress scratch,
                                    VkDeviceSize scratchSize,
                                    const VkDispatchGraphCountInfoAMDX *pCountInfo)
{
   VK_FROM_HANDLE(vk_command_buffer, cmd_buffer, commandBuffer);

   if (vk_command_buffer_has_error(cmd_buffer))
      return;

   VkResult result = VK_SUCCESS;
   linear_ctx *ctx = cmd_buffer->cmd_queue.ctx;

   struct vk_cmd_queue_entry *cmd = linear_zalloc_child(ctx, sizeof(struct vk_cmd_queue_entry));
   if (!cmd) {
      result = VK_ERROR_OUT_OF_HOST_MEMORY;
      goto finish;
   }

   cmd->type = VK_CMD_DISPATCH_GRAPH_AMDX;

   cmd->u.dispatch_graph_amdx.scratch = scratch;
   cmd->u.dispatch_graph_amdx.scratch_size = scratchSize;

   cmd->u.dispatch_graph_amdx.count_info = linear_alloc_child(ctx, sizeof(VkDispatchGraphCountInfoAMDX));
   if (cmd->u.dispatch_graph_amdx.count_info == NULL) {
      result = VK_ERROR_OUT_OF_HOST_MEMORY;
      goto finish;
   }

   memcpy((void *)cmd->u.dispatch_graph_amdx.count_info, pCountInfo,
          sizeof(VkDispatchGraphCountInfoAMDX));

   uint32_t infos_size = pCountInfo->count * pCountInfo->stride;
   void *infos = linear_alloc_child(ctx, infos_size);
   cmd->u.dispatch_graph_amdx.count_info->infos.hostAddress = infos;
   memcpy(infos, pCountInfo->infos.hostAddress, infos_size);

   for (uint32_t i = 0; i < pCountInfo->count; i++) {
      VkDispatchGraphInfoAMDX *info = (void *)((const uint8_t *)infos + i * pCountInfo->stride);

      uint32_t payloads_size = info->payloadCount * info->payloadStride;
      void *dst_payload = linear_alloc_child(ctx, payloads_size);
      memcpy(dst_payload, info->payloads.hostAddress, payloads_size);
      info->payloads.hostAddress = dst_payload;
   }

   list_addtail(&cmd->cmd_link, &cmd_buffer->cmd_queue.cmds);
finish:
   if (unlikely(result != VK_SUCCESS))
      vk_command_buffer_set_error(cmd_buffer, result);
}
#endif

VKAPI_ATTR void VKAPI_CALL
vk_cmd_enqueue_CmdBuildAccelerationStructuresKHR(
   VkCommandBuffer commandBuffer, uint32_t infoCount,
   const VkAccelerationStructureBuildGeometryInfoKHR *pInfos,
   const VkAccelerationStructureBuildRangeInfoKHR *const *ppBuildRangeInfos)
{
   VK_FROM_HANDLE(vk_command_buffer, cmd_buffer, commandBuffer);

   if (vk_command_buffer_has_error(cmd_buffer))
      return;

   struct vk_cmd_queue *queue = &cmd_buffer->cmd_queue;

   struct vk_cmd_queue_entry *cmd =
      linear_zalloc_child(queue->ctx, vk_cmd_queue_type_sizes[VK_CMD_BUILD_ACCELERATION_STRUCTURES_KHR]);
   if (!cmd)
      goto err;

   cmd->type = VK_CMD_BUILD_ACCELERATION_STRUCTURES_KHR;

   struct vk_cmd_build_acceleration_structures_khr *build =
      &cmd->u.build_acceleration_structures_khr;

   build->info_count = infoCount;
   if (pInfos) {
      build->infos = linear_alloc_child(queue->ctx, sizeof(*build->infos) * infoCount);
      if (!build->infos)
         goto err;

      memcpy((VkAccelerationStructureBuildGeometryInfoKHR *)build->infos, pInfos,
             sizeof(*build->infos) * (infoCount));
   
      for (uint32_t i = 0; i < infoCount; i++) {
         uint32_t geometries_size =
            build->infos[i].geometryCount * sizeof(VkAccelerationStructureGeometryKHR);
         VkAccelerationStructureGeometryKHR *geometries = linear_alloc_child(queue->ctx, geometries_size);
         if (!geometries)
            goto err;

         if (pInfos[i].pGeometries) {
            memcpy(geometries, pInfos[i].pGeometries, geometries_size);
         } else {
            for (uint32_t j = 0; j < build->infos[i].geometryCount; j++)
               memcpy(&geometries[j], pInfos[i].ppGeometries[j], sizeof(VkAccelerationStructureGeometryKHR));
         }

         build->infos[i].pGeometries = geometries;
      }
   }
   if (ppBuildRangeInfos) {
      build->pp_build_range_infos =
         linear_alloc_child(queue->ctx, sizeof(*build->pp_build_range_infos) * infoCount);
      if (!build->pp_build_range_infos)
         goto err;

      VkAccelerationStructureBuildRangeInfoKHR **pp_build_range_infos =
         (void *)build->pp_build_range_infos;

      for (uint32_t i = 0; i < infoCount; i++) {
         uint32_t build_range_size =
            build->infos[i].geometryCount * sizeof(VkAccelerationStructureBuildRangeInfoKHR);
         VkAccelerationStructureBuildRangeInfoKHR *p_build_range_infos =
            linear_alloc_child(queue->ctx, build_range_size);
         if (!p_build_range_infos)
            goto err;

         memcpy(p_build_range_infos, ppBuildRangeInfos[i], build_range_size);

         pp_build_range_infos[i] = p_build_range_infos;
      }
   }

   list_addtail(&cmd->cmd_link, &queue->cmds);
   return;

err:
   vk_command_buffer_set_error(cmd_buffer, VK_ERROR_OUT_OF_HOST_MEMORY);
}
