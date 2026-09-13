/*
 * Copyright © 2026 Igalia S.L.
 * SPDX-License-Identifier: MIT
 */

#include "panvk_cmd_buffer.h"
#include "panvk_entrypoints.h"

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdBeginConditionalRendering2EXT)(
   VkCommandBuffer commandBuffer,
   const VkConditionalRenderingBeginInfo2EXT *pConditionalRenderingBegin)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);

   assert(!cmdbuf->state.cond_render.enabled);
   cmdbuf->state.cond_render.enabled = true;
   cmdbuf->state.cond_render.addr =
      pConditionalRenderingBegin->addressRange.address;

   bool inverted = pConditionalRenderingBegin->flags &
                   VK_CONDITIONAL_RENDERING_INVERTED_BIT_EXT;

   /* Store the condition under which draws should execute.
    * Non-inverted: execute when value != 0 (NEQUAL).
    * Inverted: execute when value == 0 (EQUAL).
    */
   cmdbuf->state.cond_render.exec_cond =
      inverted ? MALI_CS_CONDITION_EQUAL : MALI_CS_CONDITION_NEQUAL;
}

VKAPI_ATTR void VKAPI_CALL
panvk_per_arch(CmdEndConditionalRenderingEXT)(VkCommandBuffer commandBuffer)
{
   VK_FROM_HANDLE(panvk_cmd_buffer, cmdbuf, commandBuffer);

   assert(cmdbuf->state.cond_render.enabled);
   cmdbuf->state.cond_render.enabled = false;
}
