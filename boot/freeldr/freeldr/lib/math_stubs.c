/*
 * COPYRIGHT:       See COPYING.ARM in the top level directory
 * PROJECT:         FreeLoader
 * FILE:            boot/freeldr/freeldr/lib/math_stubs.c
 * PURPOSE:         Math function stubs for ARM64 rosload
 * PROGRAMMER:
 */

/* INCLUDES ******************************************************************/

#include <freeldr.h>

/* FUNCTIONS *****************************************************************/

#if defined(_ARM64_) || defined(_M_ARM64) || defined(__aarch64__)

/* Simple implementations for bootloader - we don't need precision here */

#define LN_10 2.30258509299

double pow(double x, double y)
{
    /* Very basic pow implementation for integer exponents */
    if (y == 0.0) return 1.0;
    if (y == 1.0) return x;

    double result = 1.0;
    int exp = (int)y;
    int i;

    if (exp > 0)
    {
        for (i = 0; i < exp; i++)
            result *= x;
    }
    else if (exp < 0)
    {
        for (i = 0; i < -exp; i++)
            result /= x;
    }

    return result;
}

double floor(double x)
{
    /* Simple floor implementation */
    long long n = (long long)x;
    if (x >= 0 || x == n)
        return (double)n;
    return (double)(n - 1);
}

double log10(double x)
{
    /* Very basic log10 approximation for bootloader use */
    /* Just return a reasonable value for formatting purposes */
    if (x <= 0) return 0;

    int exp = 0;
    while (x >= 10.0)
    {
        x /= 10.0;
        exp++;
    }
    while (x < 1.0)
    {
        x *= 10.0;
        exp--;
    }

    /* Approximate the fractional part (x is between 1 and 10) */
    /* Using a simple linear approximation */
    double frac = (x - 1.0) * 0.3; /* Rough approximation */

    return (double)exp + frac;
}

double log(double x)
{
    /* Very rough natural log approximation using log10 */
    if (x <= 0) return 0;
    return log10(x) * LN_10;
}

#endif /* ARM64 */
