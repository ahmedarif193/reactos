/*
 * PROJECT:     ReactOS CRT library
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     ARM64 implementation of exp()
 * COPYRIGHT:   Imported from fdlibm / FreeBSD msun (e_exp.c)
 *
 * ====================================================
 * Copyright (C) 2004 by Sun Microsystems, Inc. All rights reserved.
 * Developed at SunPro, a Sun Microsystems, Inc. business.
 * Permission to use, copy, modify, and distribute this
 * software is freely granted, provided that this notice
 * is preserved.
 * ====================================================
 *
 * NOTE (ARM64): AArch64 has no hardware transcendental instruction, so the
 * former "return __builtin_exp(x)" lowered to a call to exp() itself, i.e.
 * infinite self-recursion that overflowed the kernel/user stack the moment a
 * caller (mesa/gears) invoked it. This provides a real, self-contained
 * polynomial+scaling implementation instead.
 */

#include <math.h>
#include <stdint.h>

/* Endian-neutral IEEE-754 double word access (top/low 32 bits). */
#define GET_HIGH_WORD(hi, d) \
    do { union { double f; uint64_t i; } __u = { (d) }; \
         (hi) = (uint32_t)(__u.i >> 32); } while (0)
#define GET_LOW_WORD(lo, d) \
    do { union { double f; uint64_t i; } __u = { (d) }; \
         (lo) = (uint32_t)(__u.i); } while (0)
#define SET_HIGH_WORD(d, hi) \
    do { union { double f; uint64_t i; } __u = { (d) }; \
         __u.i = (__u.i & 0x00000000ffffffffULL) | ((uint64_t)(uint32_t)(hi) << 32); \
         (d) = __u.f; } while (0)

static const double
one         = 1.0,
halF[2]     = { 0.5, -0.5, },
o_threshold =  7.09782712893383973096e+02,  /* 0x40862E42, 0xFEFA39EF */
u_threshold = -7.45133219101941108420e+02,  /* 0xc0874910, 0xD52D3051 */
ln2HI[2]    = {  6.93147180369123816490e-01, /* 0x3fe62e42, 0xfee00000 */
               -6.93147180369123816490e-01,},/* 0xbfe62e42, 0xfee00000 */
ln2LO[2]    = {  1.90821492927058770002e-10, /* 0x3dea39ef, 0x35793c76 */
               -1.90821492927058770002e-10,},/* 0xbdea39ef, 0x35793c76 */
invln2      =  1.44269504088896341868e+00,   /* 0x3ff71547, 0x652b82fe */
P1          =  1.66666666666666019037e-01,   /* 0x3FC55555, 0x5555553E */
P2          = -2.77777777770155933842e-03,   /* 0xBF66C16C, 0x16BEBD93 */
P3          =  6.61375632143793436117e-05,   /* 0x3F11566A, 0xAF25DE2C */
P4          = -1.65339022054652515390e-06,   /* 0xBEBBBD41, 0xC5D26BF1 */
P5          =  4.13813679705723846039e-08;   /* 0x3E663769, 0x72BEA4D0 */

static const double
huge        = 1.0e+300,
twom1000    = 9.33263618503218878990e-302;   /* 2**-1000=0x01700000,0 */

double exp(double x)    /* default IEEE double exp */
{
    double y, hi = 0.0, lo = 0.0, c, t;
    int32_t k = 0, xsb;
    uint32_t hx;

    GET_HIGH_WORD(hx, x);
    xsb = (hx >> 31) & 1;       /* sign bit of x */
    hx &= 0x7fffffff;           /* high word of |x| */

    /* filter out non-finite argument */
    if (hx >= 0x40862E42) {                 /* if |x| >= 709.78... */
        if (hx >= 0x7ff00000) {
            uint32_t lx;
            GET_LOW_WORD(lx, x);
            if (((hx & 0xfffff) | lx) != 0)
                return x + x;               /* NaN */
            else
                return (xsb == 0) ? x : 0.0;/* exp(+-inf)={inf,0} */
        }
        if (x > o_threshold) return huge * huge;         /* overflow */
        if (x < u_threshold) return twom1000 * twom1000; /* underflow */
    }

    /* argument reduction */
    if (hx > 0x3fd62e42) {                  /* if |x| > 0.5 ln2 */
        if (hx < 0x3FF0A2B2) {              /* and |x| < 1.5 ln2 */
            hi = x - ln2HI[xsb]; lo = ln2LO[xsb]; k = 1 - xsb - xsb;
        } else {
            k  = (int32_t)(invln2 * x + halF[xsb]);
            t  = k;
            hi = x - t * ln2HI[0];          /* t*ln2HI is exact here */
            lo = t * ln2LO[0];
        }
        x = hi - lo;
    } else if (hx < 0x3e300000) {           /* when |x| < 2**-28 */
        if (huge + x > one) return one + x; /* trigger inexact */
    } else {
        k = 0;
    }

    /* x is now in primary range */
    t = x * x;
    c = x - t * (P1 + t * (P2 + t * (P3 + t * (P4 + t * P5))));
    if (k == 0)
        return one - ((x * c) / (c - 2.0) - x);
    else
        y = one - ((lo - (x * c) / (2.0 - c)) - hi);

    if (k >= -1021) {
        uint32_t hy;
        GET_HIGH_WORD(hy, y);
        SET_HIGH_WORD(y, hy + ((uint32_t)k << 20)); /* add k to y's exponent */
        return y;
    } else {
        uint32_t hy;
        GET_HIGH_WORD(hy, y);
        SET_HIGH_WORD(y, hy + ((uint32_t)(k + 1000) << 20));
        return y * twom1000;
    }
}
