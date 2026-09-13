/*
 * Copyright 2026 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include <assert.h>

#include "mda_private.h"

bool
is_match(slice name, slice pattern, enum match_flags match_flags)
{
   assert(!slice_is_empty(name));

   if (slice_is_empty(pattern))
      return true;

   const bool use_substring_last = match_flags & MATCH_FLAG_SUBSTRING_LAST;

   while (!slice_is_empty(name) && !slice_is_empty(pattern)) {
      slice_cut_result pattern_cut = slice_cut(pattern, '/');
      slice_cut_result name_cut    = slice_cut(name, '/');
      slice pattern_segment        = pattern_cut.before;
      slice name_segment           = name_cut.before;
      const bool last              = !name_cut.found;

      /* Trailing '/' in pattern requires more name segments. */
      if (last && pattern_cut.found && slice_is_empty(pattern_cut.after))
         return false;

      bool matches;
      if (slice_is_empty(pattern_segment))
         matches = true;
      else if (last && use_substring_last)
         matches = slice_contains_str(name_segment, pattern_segment);
      else
         matches = slice_starts_with(name_segment, pattern_segment);

      if (matches)
         pattern = pattern_cut.after;

      name = name_cut.after;
   }

   return slice_is_empty(pattern);
}
