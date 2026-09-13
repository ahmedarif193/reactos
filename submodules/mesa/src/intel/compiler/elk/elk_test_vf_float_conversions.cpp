/*
 * Copyright © 2014 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include <gtest/gtest.h>
#include <math.h>
#include "elk_reg.h"

class vf_float_conversion_test : public ::testing::Test {
   virtual void SetUp();

public:
   float vf_to_float[128];
};

void vf_float_conversion_test::SetUp() {
   /* 0 is special cased. */
   vf_to_float[0] = 0.0;

   for (int vf = 1; vf < 128; vf++) {
      int ebits = (vf >> 4) & 0x7;
      int mbits = vf & 0xf;

      float x = 1.0f + mbits / 16.0f;
      int exp = ebits - 3;

      vf_to_float[vf] = ldexpf(x, exp);
   }
}

union fu {
   float f;
   unsigned u;
};

static unsigned
f2u(float f)
{
   union fu fu;
   fu.f = f;
   return fu.u;
}

TEST_F(vf_float_conversion_test, test_vf_to_float)
{
   for (int vf = 0; vf < 256; vf++) {
      float expected = vf_to_float[vf % 128];
      if (vf > 127)
         expected = -expected;

      EXPECT_EQ(f2u(expected), f2u(elk_vf_to_float(vf)));
   }
}

TEST_F(vf_float_conversion_test, test_float_to_vf)
{
   for (int vf = 0; vf < 256; vf++) {
      float f = vf_to_float[vf % 128];
      if (vf > 127)
         f = -f;

      EXPECT_EQ(vf, elk_float_to_vf(f));
   }
}

TEST_F(vf_float_conversion_test, test_special_case_0)
{
   /* ±0.0f are special cased to the VFs that would otherwise correspond
    * to ±0.125f. Make sure we can't convert these values to VF.
    */
   EXPECT_EQ(elk_float_to_vf(+0.125f), -1);
   EXPECT_EQ(elk_float_to_vf(-0.125f), -1);

   EXPECT_EQ(f2u(elk_vf_to_float(elk_float_to_vf(+0.0f))), f2u(+0.0f));
   EXPECT_EQ(f2u(elk_vf_to_float(elk_float_to_vf(-0.0f))), f2u(-0.0f));
}

TEST_F(vf_float_conversion_test, test_nonrepresentable_float_input)
{
   EXPECT_EQ(elk_float_to_vf(+32.0f), -1);
   EXPECT_EQ(elk_float_to_vf(-32.0f), -1);

   EXPECT_EQ(elk_float_to_vf(+16.5f), -1);
   EXPECT_EQ(elk_float_to_vf(-16.5f), -1);

   EXPECT_EQ(elk_float_to_vf(+8.25f), -1);
   EXPECT_EQ(elk_float_to_vf(-8.25f), -1);

   EXPECT_EQ(elk_float_to_vf(+4.125f), -1);
   EXPECT_EQ(elk_float_to_vf(-4.125f), -1);
}
