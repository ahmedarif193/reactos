/*
 * Copyright © 2025 Valve Corporation
 *
 * SPDX-License-Identifier: MIT
 */

#ifndef RINGBUFFER_H
#define RINGBUFFER_H

#include "util/macros.h"

#define RINGBUFFER_DECLARE(name, type, N)                                                          \
   struct {                                                                                        \
      type data[N];                                                                                \
      uint32_t head;                                                                               \
      uint32_t tail;                                                                               \
      uint32_t size;                                                                               \
      simple_mtx_t mtx;                                                                            \
   } name

#define ringbuffer_init(buffer)                                                                    \
   (buffer.head = buffer.tail = buffer.size = 0, simple_mtx_init(&buffer.mtx, mtx_plain))

#define ringbuffer_lock(buffer)   simple_mtx_lock(&buffer.mtx)
#define ringbuffer_unlock(buffer) simple_mtx_unlock(&buffer.mtx)

static inline uint32_t
__ringbuffer_add_wrap(uint32_t *val, uint32_t *size, uint32_t N)
{
   uint32_t prev = *val;
   *val = (*val + 1) % N;
   *size = *size + 1;
   assert(*size <= N);
   return prev;
}

#define ringbuffer_alloc(buffer)                                                                   \
   (buffer.size == ARRAY_SIZE(buffer.data)                                                         \
       ? NULL                                                                                      \
       : &buffer.data[__ringbuffer_add_wrap(&buffer.head, &buffer.size, ARRAY_SIZE(buffer.data))])

#define ringbuffer_free(buffer, elem)                                                              \
   assert(elem == NULL || elem == &buffer.data[buffer.tail]);                                      \
   buffer.size--;                                                                                  \
   assert(buffer.size < ARRAY_SIZE(buffer.data));                                                  \
   buffer.tail = (buffer.tail + 1) % ARRAY_SIZE(buffer.data)

#define ringbuffer_first(buffer) (&buffer.data[buffer.tail])

#define ringbuffer_last(buffer)                                                                    \
   (&buffer.data[(buffer.head + ARRAY_SIZE(buffer.data) - 1) % ARRAY_SIZE(buffer.data)])

#define ringbuffer_index(buffer, elem) (elem - buffer.data)

#define ringbuffer_next(buffer, elem)                                                              \
   (&buffer.data[(ringbuffer_index(buffer, elem) + 1) % ARRAY_SIZE(buffer.data)])

#endif /* RINGBUFFER_H */
