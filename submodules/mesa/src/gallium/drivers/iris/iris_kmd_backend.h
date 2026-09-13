/*
 * Copyright © 2023 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include <stdint.h>

#include "dev/intel_device_info.h"
#include "dev/intel_kmd.h"

struct iris_batch;
struct iris_bo;
struct iris_bufmgr;
enum bo_alloc_flags;
enum iris_heap;
enum iris_madvice;

struct iris_kmd_backend {
   uint32_t (*gem_create)(struct iris_bufmgr *bufmgr,
                          const struct intel_memory_class_instance **regions,
                          uint16_t regions_count, uint64_t size,
                          enum iris_heap heap_flags, enum bo_alloc_flags alloc_flags);
   uint32_t (*gem_create_userptr)(struct iris_bufmgr *bufmgr, void *ptr,
                                  uint64_t size);
   int (*gem_close)(struct iris_bufmgr *bufmgr, struct iris_bo *bo);
   bool (*bo_madvise)(struct iris_bo *bo, enum iris_madvice state);
   int (*bo_set_caching)(struct iris_bo *bo, bool cached);
   void *(*gem_mmap)(struct iris_bufmgr *bufmgr, struct iris_bo *bo);
   enum pipe_reset_status (*batch_check_for_reset)(struct iris_batch *batch);
   int (*batch_submit)(struct iris_batch *batch);
   bool (*gem_vm_bind)(struct iris_bo *bo, enum bo_alloc_flags flags);
   bool (*gem_vm_unbind)(struct iris_bo *bo);
};

const struct iris_kmd_backend *
iris_kmd_backend_get(enum intel_kmd_type type);

/* Internal functions, should not be called */
const struct iris_kmd_backend *i915_get_backend(void);
const struct iris_kmd_backend *xe_get_backend(void);
