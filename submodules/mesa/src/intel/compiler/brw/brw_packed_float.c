/*
 * Copyright © 2014 Intel Corporation
 * SPDX-License-Identifier: MIT
 */

#include "brw_reg.h"

union fu {
   float f;
   unsigned u;
   struct {
      unsigned mantissa:23;
      unsigned exponent:8;
      unsigned sign:1;
   } s;
};

int
brw_float_to_vf(float f)
{
   union fu fu = { .f = f };

   /* ±0.0f is special cased. */
   if (f == 0.0f)
      return fu.s.sign << 7;

   unsigned mantissa = fu.s.mantissa >> (23 - 4);
   unsigned exponent = fu.s.exponent - (127 - 3);
   unsigned vf = (fu.s.sign << 7) | (exponent << 4) | mantissa;

   /* 0.125 would have had the same representation as 0.0, so reject it. */
   if ((vf & 0x7f) == 0)
      return -1;

   /* Make sure the mantissa fits in 4-bits and the exponent in 3-bits. */
   if (fu.u & 0x7ffff || exponent > 7)
      return -1;

   return vf;
}

float
brw_vf_to_float(unsigned char vf)
{
   union fu fu;

   /* ±0.0f is special cased. */
   if (vf == 0x00 || vf == 0x80) {
      fu.u = (unsigned)vf << 24;
      return fu.f;
   }

   fu.s.sign = vf >> 7;
   fu.s.exponent = ((vf & 0x70) >> 4) + (127 - 3);
   fu.s.mantissa = (vf & 0xf) << (23 - 4);

   return fu.f;
}
