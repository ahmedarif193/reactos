/* Minimal floor implementation for ARM64 CRT. */

#include <math.h>

static inline double floor_builtin(double x)
{
    return __builtin_floor(x);
}

double floor(double x)
{
    return floor_builtin(x);
}