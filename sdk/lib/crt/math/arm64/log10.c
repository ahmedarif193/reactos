/*
 * Base-10 logarithm for ARM64 CRT, using the natural log core.
 */

#include <math.h>

static const double invln10_hi = 4.34294481878168880939e-01; /* 0x3fdbcb7b1526e50e */
static const double invln10_lo = 2.50829467116452752298e-11; /* 0x3dbb9438ca9aadd5 */

double log10(double x)
{
    double z = log(x);
    /* Split 1/ln(10) to keep rounding noise low. */
    return z * invln10_hi + z * invln10_lo;
}
