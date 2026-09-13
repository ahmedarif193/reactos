/*
 * Copyright 2026 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#pragma once

#include "slice.h"

#ifdef __cplusplus
extern "C" {
#endif

enum match_flags {
   MATCH_FLAG_NONE = 0,

   /* Allow substring matching in the entry final segment. */
   MATCH_FLAG_SUBSTRING_LAST = 1 << 0,
};

bool is_match(slice name_slice, slice pattern_slice, enum match_flags match_flags);

#ifdef __cplusplus
}
#endif
