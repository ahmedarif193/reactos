/*
 * Copyright 2025 Google LLC
 * SPDX-License-Identifier: MIT
 */

#include <gtest/gtest.h>

#include "test_common.h"

#define ANV_C_TEST(S, N, C) extern "C" void C(void); TEST(S, N) { C(); }

ANV_C_TEST(AnvSlabBo, AllocWithAlignment, anv_slab_bo_alloc_with_alignment);

extern "C" void FAIL_IN_GTEST(const char *file_path, unsigned line_number, const char *msg) {
   GTEST_FAIL_AT(file_path, line_number) << msg;
}
