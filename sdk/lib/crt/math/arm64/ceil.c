/* Minimal ceil implementation for ARM64 CRT. */

#include <math.h>

double ceil(double x)
{
    double f = floor(x);

    if (f == x)
        return x;

    return (x > f) ? f + 1.0 : f;
}
