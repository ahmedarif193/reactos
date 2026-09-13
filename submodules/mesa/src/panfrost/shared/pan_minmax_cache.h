/*
 * Copyright (c) 2020 Collabora, Ltd.
 * SPDX-License-Identifier: MIT
 */

#ifndef H_PAN_MINMAX_CACHE
#define H_PAN_MINMAX_CACHE

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

#define PANFROST_MINMAX_SIZE 64

struct pan_minmax_cache {
   uint64_t keys[PANFROST_MINMAX_SIZE];
   uint64_t values[PANFROST_MINMAX_SIZE];
   unsigned size;
   unsigned index;
};

bool pan_minmax_cache_get(struct pan_minmax_cache *cache, unsigned index_size,
                          unsigned start, unsigned count, unsigned *min_index,
                          unsigned *max_index);

void pan_minmax_cache_add(struct pan_minmax_cache *cache, unsigned index_size,
                          unsigned start, unsigned count, unsigned min_index,
                          unsigned max_index);

void pan_minmax_cache_invalidate(struct pan_minmax_cache *cache,
                                 unsigned index_size, size_t offset,
                                 size_t size);

#endif
