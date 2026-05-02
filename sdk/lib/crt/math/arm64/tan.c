/* Minimal tangent implementation for ARM64 CRT. */

#include <math.h>

double tan(double x)
{
    double c = cos(x);
    double s = sin(x);

    return s / c;
}
