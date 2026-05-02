/* Minimal exp implementation for ARM64 CRT. */

#include <math.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Winfinite-recursion"

static inline double exp_builtin(double x)
{
    return __builtin_exp(x);
}

double exp(double x)
{
    return exp_builtin(x);
}

#pragma GCC diagnostic pop
