/* -*- c++ -*- */
/*
 * Copyright © 2010-2014 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "util/compiler.h"
#include "util/glheader.h"
#include "util/macros.h"
#include "util/rounding.h"
#include "util/u_math.h"

namespace elk {
   /**
    * Simple allocator used to keep track of virtual GRFs.
    */
   class simple_allocator {
   public:
      simple_allocator() :
         sizes(NULL), offsets(NULL), count(0), total_size(0), capacity(0)
      {
      }

      simple_allocator(const simple_allocator &) = delete;

      ~simple_allocator()
      {
         free(offsets);
         free(sizes);
      }

      simple_allocator & operator=(const simple_allocator &) = delete;

      unsigned
      allocate(unsigned size)
      {
         assert(size > 0);
         if (capacity <= count) {
            capacity = MAX2(16, capacity * 2);
            sizes = (unsigned *)realloc(sizes, capacity * sizeof(unsigned));
            offsets = (unsigned *)realloc(offsets, capacity * sizeof(unsigned));
         }

         sizes[count] = size;
         offsets[count] = total_size;
         total_size += size;

         return count++;
      }

      /**
       * Array of sizes for each allocation.  The allocation unit is up to the
       * back-end, but it's expected to be one scalar value in the FS back-end
       * and one vec4 in the VEC4 back-end.
       */
      unsigned *sizes;

      /**
       * Array of offsets from the start of the VGRF space in allocation
       * units.
       */
      unsigned *offsets;

      /** Total number of VGRFs allocated. */
      unsigned count;

      /** Cumulative size in allocation units. */
      unsigned total_size;

   private:
      unsigned capacity;
   };
}
