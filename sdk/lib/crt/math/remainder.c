/*
 * Copyright (C) 1993 by Sun Microsystems, Inc. All rights reserved.
 *
 * Developed at SunSoft, a Sun Microsystems, Inc. business.
 * Permission to use, copy, modify, and distribute this
 * software is freely granted, provided that this notice
 * is preserved.
 *
 * Adapted from openlibm src/e_remainder.c: use binary64 words directly
 * and the existing CRT fmod implementation on every architecture.
 */

#include <math.h>
#include <stdint.h>

double __cdecl remainder(double x, double y)
{
    union { double f; uint64_t i; } ux = {x}, uy = {y};
    uint64_t sign = ux.i & UINT64_C(0x8000000000000000);
    uint64_t ax = ux.i & UINT64_C(0x7fffffffffffffff);
    uint64_t ay = uy.i & UINT64_C(0x7fffffffffffffff);
    double half;

    if (!ay || ax >= UINT64_C(0x7ff0000000000000) || ay > UINT64_C(0x7ff0000000000000))
        return (x * y) / (x * y);

    /* Reduce modulo 2*y without overflowing. This retains quotient parity,
     * which distinguishes the two possible answers at an exact half-way tie. */
    if (ay < UINT64_C(0x7fe0000000000000))
        x = fmod(x, y + y);
    if (ax == ay)
        return 0.0 * x;
    x = fabs(x);
    y = fabs(y);

    if (ay < UINT64_C(0x0020000000000000))
    {
        /* Compare doubled remainders instead of underflowing y/2. */
        if (x + x > y)
        {
            x -= y;
            if (x + x >= y) x -= y;
        }
    }
    else
    {
        half = 0.5 * y;
        if (x > half)
        {
            x -= y;
            if (x >= half) x -= y;
        }
    }

    ux.f = x;
    if (!(ux.i & UINT64_C(0x7fffffffffffffff))) ux.i = 0;
    ux.i ^= sign;
    return ux.f;
}
