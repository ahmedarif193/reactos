/* Copyright © 2024 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#pragma once

/* We reserve :
 *    - GPR 12 for 3DSTATE_BINDING_TABLE_POOL_ALLOC  address
 *    - GPR 13 for STATE_BASE_ADDRESS bindless surface base address
 *    - GPR 14 for perf queries
 *    - GPR 15 for conditional rendering
 */
#define MI_BUILDER_NUM_ALLOC_GPRS 12
#ifndef MI_BUILDER_CAN_WRITE_BATCH
#define MI_BUILDER_CAN_WRITE_BATCH true
#endif
/* Don't do any write check by default, we manually set it where it matters.
 */
#define MI_BUILDER_DEFAULT_WRITE_CHECK false
#define __gen_get_batch_dwords anv_batch_emit_dwords
#define __gen_address_offset anv_address_add
#define __gen_get_batch_address(b, a) anv_batch_address(b, a)
#define __gen_get_write_fencing_status(b) (&(b)->write_fence_status)
#include "common/mi_builder.h"

/* We reserve this MI ALU register for the purpose of handling predication.
 * Other code which uses the MI ALU should leave it alone.
 */
#define ANV_PREDICATE_RESULT_REG 0x2678 /* MI_ALU_REG15 */

/* We reserve this MI ALU register to pass around an offset computed from
 * VkPerformanceQuerySubmitInfoKHR::counterPassIndex VK_KHR_performance_query.
 * Other code which uses the MI ALU should leave it alone.
 */
#define ANV_PERF_QUERY_OFFSET_REG 0x2670 /* MI_ALU_REG14 */

/* We reserve this MI ALU register to hold the last programmed bindless
 * surface state base address so that we can predicate STATE_BASE_ADDRESS
 * emissions if the address doesn't change.
 */
#define ANV_BINDLESS_SURFACE_BASE_ADDR_REG 0x2668 /* MI_ALU_REG13 */

/* We reserve this MI ALU register to hold the last programmed
 * 3DSTATE_BINDING_TABLE_POOL_ALLOC address so that we can predicate
 * 3DSTATE_BINDING_TABLE_POOL_ALLOC emissions if the address doesn't change.
 */
#define ANV_BTP_ADDR_REG 0x2660 /* MI_ALU_REG12 */
