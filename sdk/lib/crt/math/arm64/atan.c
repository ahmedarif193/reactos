/*
 * PROJECT:     ReactOS CRT library
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     ARM64 implementation of atan()
 * COPYRIGHT:   Imported from fdlibm / FreeBSD msun (s_atan.c)
 *
 * ====================================================
 * Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
 * Developed at SunPro, a Sun Microsystems, Inc. business.
 * Permission to use, copy, modify, and distribute this
 * software is freely granted, provided that this notice
 * is preserved.
 * ====================================================
 *
 * NOTE (ARM64): AArch64 has no hardware arctangent, so the former
 * "return __builtin_atan(x)" lowered to a call to atan() itself -- infinite
 * self-recursion that overflowed the stack (also reached through atanf() and
 * the atan2 core). This is the real fdlibm polynomial implementation.
 */

#include <math.h>
#include <stdint.h>

#define GET_HIGH_WORD(hi, d) \
    do { union { double f; uint64_t i; } __u = { (d) }; \
         (hi) = (int32_t)(uint32_t)(__u.i >> 32); } while (0)
#define GET_LOW_WORD(lo, d) \
    do { union { double f; uint64_t i; } __u = { (d) }; \
         (lo) = (uint32_t)(__u.i); } while (0)

static const double atanhi[] = {
    4.63647609000806093515e-01, /* atan(0.5)hi 0x3FDDAC67, 0x0561BB4F */
    7.85398163397448278999e-01, /* atan(1.0)hi 0x3FE921FB, 0x54442D18 */
    9.82793723247329054082e-01, /* atan(1.5)hi 0x3FEF730B, 0xD281F69B */
    1.57079632679489655800e+00, /* atan(inf)hi 0x3FF921FB, 0x54442D18 */
};

static const double atanlo[] = {
    2.26987774529616870924e-17, /* atan(0.5)lo 0x3C7A2B7F, 0x222F65E2 */
    3.06161699786838301793e-17, /* atan(1.0)lo 0x3C81A626, 0x33145C07 */
    1.39033110312309984516e-17, /* atan(1.5)lo 0x3C700788, 0x7AF0CBBD */
    6.12323399573676603587e-17, /* atan(inf)lo 0x3C91A626, 0x33145C07 */
};

static const double aT[] = {
    3.33333333333329318027e-01, /* 0x3FD55555, 0x5555550D */
   -1.99999999998764832476e-01, /* 0xBFC99999, 0x9998EBC4 */
    1.42857142725034663711e-01, /* 0x3FC24924, 0x920083FF */
   -1.11111104054623557880e-01, /* 0xBFBC71C6, 0xFE231671 */
    9.09088713343650656196e-02, /* 0x3FB745CD, 0xC54C206E */
   -7.69187620504482999495e-02, /* 0xBFB3B0F2, 0xAF749A6D */
    6.66107313738753120669e-02, /* 0x3FB10D66, 0xA0D03D51 */
   -5.83357013379057348645e-02, /* 0xBFADDE2D, 0x52DEFD9A */
    4.97687799461593236017e-02, /* 0x3FA97B4B, 0x24760DEB */
   -3.65315727442169155270e-02, /* 0xBFA2B444, 0x2C6A6C2F */
    1.62858201153657823623e-02, /* 0x3F90AD3A, 0xE322DA11 */
};

static const double
one  = 1.0,
huge = 1.0e300;

double atan(double x)
{
    double w, s1, s2, z;
    int32_t ix, hx, id;

    GET_HIGH_WORD(hx, x);
    ix = hx & 0x7fffffff;
    if (ix >= 0x44100000) {         /* if |x| >= 2^66 */
        uint32_t low;
        GET_LOW_WORD(low, x);
        if (ix > 0x7ff00000 ||
            (ix == 0x7ff00000 && (low != 0)))
            return x + x;           /* NaN */
        if (hx > 0) return  atanhi[3] + *(volatile double *)&atanlo[3];
        else        return -atanhi[3] - *(volatile double *)&atanlo[3];
    }
    if (ix < 0x3fdc0000) {          /* |x| < 0.4375 */
        if (ix < 0x3e400000) {      /* |x| < 2^-27 */
            if (huge + x > one) return x;   /* raise inexact */
        }
        id = -1;
    } else {
        x = fabs(x);
        if (ix < 0x3ff30000) {          /* |x| < 1.1875 */
            if (ix < 0x3fe60000) {      /* 7/16 <= |x| < 11/16 */
                id = 0; x = (2.0 * x - one) / (2.0 + x);
            } else {                    /* 11/16 <= |x| < 19/16 */
                id = 1; x = (x - one) / (x + one);
            }
        } else {
            if (ix < 0x40038000) {      /* |x| < 2.4375 */
                id = 2; x = (x - 1.5) / (one + 1.5 * x);
            } else {                    /* 2.4375 <= |x| < 2^66 */
                id = 3; x = -1.0 / x;
            }
        }
    }
    /* end of argument reduction */
    z = x * x;
    w = z * z;
    /* break sum from i=0 to 10 aT[i]z**(i+1) into odd and even poly */
    s1 = z * (aT[0] + w * (aT[2] + w * (aT[4] + w * (aT[6] + w * (aT[8] + w * aT[10])))));
    s2 = w * (aT[1] + w * (aT[3] + w * (aT[5] + w * (aT[7] + w * aT[9]))));
    if (id < 0) return x - x * (s1 + s2);
    else {
        z = atanhi[id] - ((x * (s1 + s2) - atanlo[id]) - x);
        return (hx < 0) ? -z : z;
    }
}
