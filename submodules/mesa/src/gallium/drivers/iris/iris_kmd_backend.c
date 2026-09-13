/*
 * Copyright © 2023 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "iris_kmd_backend.h"

#include <stdlib.h>

const struct iris_kmd_backend *
iris_kmd_backend_get(enum intel_kmd_type type)
{
   switch (type) {
   case INTEL_KMD_TYPE_I915:
      return i915_get_backend();
   case INTEL_KMD_TYPE_XE:
      return xe_get_backend();
   default:
      return NULL;
   }
}
