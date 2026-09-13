/*
 * Copyright © 2017 Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the next
 * paragraph) shall be included in all copies or substantial portions of the
 * Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
 * IN THE SOFTWARE.
 */

#include "mesa-blake3.h"

#include <gtest/gtest.h>

struct Params {
   const char *string;
   const char *expected_blake3;
};

static const Params test_data[] = {
   {"Mesa Rocks! 273", "dee075fc6fc8b4e55570d8038c629db8f29d6ec68cf962d2938e972c80342f0d"},
   {"Mesa Rocks! 300", "82661e51d2a48fbb9b5758bc3d64e7fd5da14a48cc5fdb44fdc903ddfbcc1f3c"},
   {"Mesa Rocks! 583", "42f5982e2ef3c5e388c50ee1070e7fd07e93bd44fa0dc3ea62e0892337c89ad8"},
};

class MesaBLAKE3TestFixture : public testing::TestWithParam<Params> {};
INSTANTIATE_TEST_SUITE_P(
   MesaBLAKE3Test,
   MesaBLAKE3TestFixture,
   testing::ValuesIn(test_data)
);

TEST_P(MesaBLAKE3TestFixture, Match)
{
   Params p = GetParam();

   unsigned char blake3[BLAKE3_KEY_LEN];
   _mesa_blake3_compute(p.string, strlen(p.string), blake3);

   char buf[BLAKE3_HEX_LEN];
   _mesa_blake3_format(buf, blake3);

   ASSERT_TRUE(memcmp(buf, p.expected_blake3, sizeof(buf)) == 0)
      << "For string \"" << p.string << "\", length " << strlen(p.string) << ":\n"
      << "\t  Actual: " << buf << "\n"
      << "\tExpected: " << p.expected_blake3 << "\n";
}
