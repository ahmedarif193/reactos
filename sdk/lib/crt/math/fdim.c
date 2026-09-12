/*
 * LICENSE:         GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PROJECT:         LiberNT CRT library
 * PURPOSE:         Portable implementation of fdim
 * PROGRAMMER:      Ahmed ARIF
 */

#include <math.h>

double fdim(double x, double y)
{
    if (isnan(x))
        return x;
    if (isnan(y))
        return y;
    return (x > y) ? x - y : 0.0;
}

float fdimf(float x, float y)
{
    if (isnan(x))
        return x;
    if (isnan(y))
        return y;
    return (x > y) ? x - y : 0.0f;
}
