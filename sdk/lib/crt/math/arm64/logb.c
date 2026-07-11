/*
 * PROJECT:     ReactOS CRT library
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     ARM64 implementation of _logb/_logbf (and logb/logbf)
 * COPYRIGHT:   Derived from musl libc ilogb (bit-manipulation on IEEE-754)
 *              See https://git.musl-libc.org/cgit/musl/tree/COPYRIGHT
 *
 * NOTE (ARM64): AArch64 has no hardware logb, so the former
 * "return __builtin_logb(x)" / "__builtin_logbf(x)" lowered to a call to
 * logb()/logbf() themselves -- infinite self-recursion that overflowed the
 * stack. logb(x) returns the unbiased binary exponent of x as a floating
 * point value; this computes it directly from the IEEE-754 representation.
 */

#include <math.h>
#include <stdint.h>

double _logb(double x)
{
    union { double f; uint64_t i; } u = { x };
    uint64_t m = u.i;
    int e = m >> 52 & 0x7ff;

    if (!e) {                       /* zero or subnormal */
        m <<= 12;                   /* drop sign+exponent, keep mantissa */
        if (m == 0)
            return -1.0 / (x * x);  /* logb(+-0) = -inf, raise div-by-zero */
        /* subnormal: normalize to find the true exponent */
        for (e = -0x3ff; m >> 63 == 0; e--, m <<= 1)
            ;
        return (double)e;
    }
    if (e == 0x7ff)                 /* inf or NaN */
        return x * x;               /* +inf for +-inf, NaN for NaN */
    return (double)(e - 0x3ff);
}

float _logbf(float x)
{
    union { float f; uint32_t i; } u = { x };
    uint32_t m = u.i;
    int e = m >> 23 & 0xff;

    if (!e) {                       /* zero or subnormal */
        m <<= 9;                    /* drop sign+exponent, keep mantissa */
        if (m == 0)
            return -1.0f / (x * x); /* logbf(+-0) = -inf, raise div-by-zero */
        for (e = -0x7f; m >> 31 == 0; e--, m <<= 1)
            ;
        return (float)e;
    }
    if (e == 0xff)                  /* inf or NaN */
        return x * x;               /* +inf for +-inf, NaN for NaN */
    return (float)(e - 0x7f);
}

double logb(double x)
{
    return _logb(x);
}

float logbf(float x)
{
    return _logbf(x);
}
