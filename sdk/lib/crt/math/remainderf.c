/*
 * PROJECT:     LiberNT CRT
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Single-precision IEEE remainder
 */
#include <math.h>

float __cdecl remainderf(float x, float y)
{
    /* Every binary32 operand and its exact remainder fit in binary64.
     * The shared implementation preserves quotient parity at half-way ties
     * and the sign of zero, without overflowing x/y. */
    return (float)remainder((double)x, (double)y);
}
