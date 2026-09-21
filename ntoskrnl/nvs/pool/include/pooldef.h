/*
 * PROJECT:     ReactOS Pool Allocator
 * FILE:        ntoskrnl/nvs/pool/include/pooldef.h
 * PURPOSE:     Pool allocator types and constants
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once

#define POOL_UNIT_SHIFT              20
#define POOL_UNIT_SIZE               ((SIZE_T)1 << POOL_UNIT_SHIFT)
#define POOL_UNIT_MASK               (POOL_UNIT_SIZE - 1)

#define POOL_SEGMENT_PAGES           ((ULONG)(POOL_UNIT_SIZE >> PAGE_SHIFT))
#define POOL_SEGMENT_MAP_WORDS       ((POOL_SEGMENT_PAGES + 63) / 64)
#define POOL_SEGMENT_PROBE_LIMIT     16
#define POOL_SHARD_COMMIT_CACHE      256

#define POOL_MAX_SLOTS               64
#define POOL_MAX_SHARDS              16
#define POOL_MAX_REGIONS             4

#define POOL_ALIGNMENT               16
#define POOL_CACHE_ALIGNMENT         128

#define POOL_LFH_CLASS_COUNT         28
#define POOL_LFH_MAX_BYTES           4096
#define POOL_LFH_ACTIVATION          16
#define POOL_LFH_MAX_BLOCKS          1024

#define POOL_VS_UNIT                 16
#define POOL_VS_FREE_LISTS           16
#define POOL_VS_RUN_BYTES            (64 * 1024)

#define POOL_RANGE_MAX_BYTES         (POOL_UNIT_SIZE / 2)

#define POOL_FREE_POISON             0xC0

#define POOL_SEGMENT_SIGNATURE       0x67655350
#define POOL_LFH_SIGNATURE           0x6866LU
#define POOL_VS_SIGNATURE            0x7376LU
#define POOL_HEAP_SIGNATURE          0x70486C50

#define POOL_ALLOC_ZERO              0x01
#define POOL_ALLOC_CACHE_ALIGNED     0x02

#define POOL_HEAP_POISON_ON_FREE     0x01

C_ASSERT((POOL_LFH_MAX_BYTES % POOL_ALIGNMENT) == 0);
C_ASSERT(POOL_RANGE_MAX_BYTES < POOL_UNIT_SIZE);
