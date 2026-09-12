/*
 * LICENSE:         GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PROJECT:         LiberNT CRT library
 * PURPOSE:         Portable implementation of fmin
 * PROGRAMMER:      Ahmed ARIF
 */

#include <math.h>

double fmin(double x, double y)
{
    if (isnan(x))
        return y;
    if (isnan(y))
        return x;
    return (x < y) ? x : y;
}

float fminf(float x, float y)
{
    if (isnan(x))
        return y;
    if (isnan(y))
        return x;
    return (x < y) ? x : y;
}
