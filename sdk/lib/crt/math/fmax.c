/*
 * COPYRIGHT:       BSD - See COPYING.ARM in the top level directory
 * PROJECT:         ReactOS CRT library
 * PURPOSE:         Portable implementation of fmax
 * PROGRAMMER:      Ahmed ARIF
 */

#include <math.h>

double fmax(double x, double y)
{
    if (isnan(x))
        return y;
    if (isnan(y))
        return x;
    return (x > y) ? x : y;
}

float fmaxf(float x, float y)
{
    if (isnan(x))
        return y;
    if (isnan(y))
        return x;
    return (x > y) ? x : y;
}
