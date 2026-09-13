/*
 * © Copyright 2017-2018 Alyssa Rosenzweig
 * SPDX-License-Identifier: MIT
 */

#ifndef __PAN_MEMPOOL_H__
#define __PAN_MEMPOOL_H__

#include "pan_bo.h"
#include "pan_pool.h"

/* Represents grow-only memory. It may be owned by the batch (OpenGL), or may
   be unowned for persistent uploads. */

struct panfrost_pool {
   /* Inherit from pan_pool */
   struct pan_pool base;

   /* Parent device for allocation */
   struct panfrost_device *dev;

   /* Label for created BOs */
   const char *label;

   /* BO flags to use in the pool */
   unsigned create_flags;

   /* BOs allocated by this pool */
   struct util_dynarray bos;

   /* Current transient BO */
   struct panfrost_bo *transient_bo;

   /* Within the topmost transient BO, how much has been used? */
   unsigned transient_offset;

   /* Mode of the pool. BO management is in the pool for owned mode, but
    * the consumed for unowned mode. */
   bool owned;
};

static inline struct panfrost_pool *
to_panfrost_pool(struct pan_pool *pool)
{
   return container_of(pool, struct panfrost_pool, base);
}

/* Reference to pool allocated memory for an unowned pool */

struct panfrost_pool_ref {
   /* Owning BO */
   struct panfrost_bo *bo;

   /* Mapped GPU VA */
   uint64_t gpu;
};

/* Take a reference to an allocation pool. Call directly after allocating from
 * an unowned pool for correct operation. */

static inline struct panfrost_pool_ref
panfrost_pool_take_ref(struct panfrost_pool *pool, uint64_t ptr)
{
   if (!pool->owned)
      panfrost_bo_reference(pool->transient_bo);

   return (struct panfrost_pool_ref){
      .bo = pool->transient_bo,
      .gpu = ptr,
   };
}

int panfrost_pool_init(struct panfrost_pool *pool, void *memctx,
                       struct panfrost_device *dev, unsigned create_flags,
                       size_t slab_size, const char *label, bool prealloc,
                       bool owned);

void panfrost_pool_cleanup(struct panfrost_pool *pool);

static inline unsigned
panfrost_pool_num_bos(struct panfrost_pool *pool)
{
   assert(pool->owned && "pool does not track BOs in unowned mode");
   return util_dynarray_num_elements(&pool->bos, struct panfrost_bo *);
}

void panfrost_pool_get_bo_handles(struct panfrost_pool *pool,
                                  uint32_t *handles);

#endif
