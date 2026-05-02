/*
 * Minimal pow implementation for ARM64 builds.
 * This is a straightforward exp/log variant sufficient for our
 * printf-style float formatting needs.
 */

#include <math.h>

double pow(double x, double y)
{
    return exp(y * log(x));
}
