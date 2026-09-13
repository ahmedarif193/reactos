/*
 * Copyright 2026 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include <gtest/gtest.h>

#include "mda_private.h"

TEST(MDA, IsMatch)
{
   constexpr match_flags _ = MATCH_FLAG_NONE;
   constexpr match_flags SUBSTRING_LAST = MATCH_FLAG_SUBSTRING_LAST;

   struct T {
      const char *entry;
      const char *pattern;
      match_flags flags;
      bool expected;
   } tests[] = {
      /* Empty pattern matches anything. */
      { "c9a50159a389eb1f/CS/NIR8/first", "", _,              true },
      { "c9a50159a389eb1f/CS/NIR8/first", "", SUBSTRING_LAST, true },

      /* Pattern segments are matched in order but can skip name segments. */
      { "c9a50159a389eb1f/CS/BRW8/00-03-brw_opt_dead_code_eliminate", "c9a/BRW8",   _, true },
      { "c9a50159a389eb1f/CS/BRW8/00-03-brw_opt_dead_code_eliminate", "c/CS/00-03", _, true },
      { "c9a50159a389eb1f/CS/BRW8/00-03-brw_opt_dead_code_eliminate", "CS/BRW8",    _, true },
      { "c9a50159a389eb1f/CS/BRW8/00-03-brw_opt_dead_code_eliminate", "c/BRW/CS",   _, false },

      /* Not a match if pattern has extra segments. */
      { "c9a50159a389eb1f/CS/NIR8/first", "c/CS/NIR8/first",     _, true },
      { "c9a50159a389eb1f/CS/NIR8/first", "c/CS/NIR8/first/out", _, false },

      /* Empty segments match any single name segment, but must match one. */
      { "c9a50159a389eb1f/CS/NIR8/first", "c//NIR8/first",    _, true },
      { "c9a50159a389eb1f/CS/NIR8/first", "c//CS/NIR8/first", _, false },
      { "c9a50159a389eb1f/CS/NIR8/first", "c/CS/NIR8/",       _, true },
      { "c9a50159a389eb1f/CS/NIR8/first", "c/CS/NIR8/first/", _, false },
      { "foo/bar", "/bar", _, true },
      { "bar",     "/bar", _, false },

      /* Last segment can be matched as substring with SUBSTRING_LAST. */
      { "c9a50159a389eb1f/CS/BRW8/00-03-brw_opt_dead_code_eliminate", "BRW8/dead_code", _,              false },
      { "c9a50159a389eb1f/CS/BRW8/00-03-brw_opt_dead_code_eliminate", "BRW8/dead_code", SUBSTRING_LAST, true },

      /* SUBSTRING_LAST only applies to last segment. */
      { "c9a50159a389eb1f/CS/BRW8/00-03-brw_opt_dead_code_eliminate", "dead_code", SUBSTRING_LAST, true },
      { "c9a50159a389eb1f/CS/BRW8/00-03-brw_opt_dead_code_eliminate", "8",         SUBSTRING_LAST, false },
      { "c9a50159a389eb1f/CS/BRW8/00-03-brw_opt_dead_code_eliminate", "9a501",     SUBSTRING_LAST, false },
   };

   for (const auto &t : tests) {
      slice entry   = slice_from_cstr(t.entry);
      slice pattern = slice_from_cstr(t.pattern);

      EXPECT_EQ(is_match(entry, pattern, t.flags), t.expected)
         << "entry='" << t.entry << "' "
         << "pattern='" << t.pattern << "' "
         << "flags=" << t.flags;
   }
}
