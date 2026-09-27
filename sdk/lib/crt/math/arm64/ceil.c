/* Minimal ceil implementation for ARM64 CRT. */

#include <math.h>

double ceil(double x)
{
    double f = floor(x);

    if (f == x)
        return x;

    if (x > f)
    {
        f += 1.0;
        if (f == 0.0 && x < 0.0)
            return -0.0;
    }
    return f;
}
