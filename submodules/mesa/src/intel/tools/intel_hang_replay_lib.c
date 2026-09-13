/*
 * Copyright 2025 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "intel_hang_replay_lib.h"

int
compare_bos(const void *b1, const void *b2)
{
   const struct gem_bo *gem_b1 = b1, *gem_b2 = b2;

   return gem_b2->size > gem_b1->size;
}

void
skip_data(int file_fd, size_t size)
{
   lseek(file_fd, size, SEEK_CUR);
}

void
write_malloc_data(void *out_data,
                  int file_fd,
                  size_t size)
{
   size_t total_read_len = 0;
   ssize_t read_len;
   while (total_read_len < size &&
          (read_len = read(file_fd, out_data + total_read_len, size - total_read_len)) > 0) {
      total_read_len += read_len;
   }
   assert(total_read_len == size);
}
