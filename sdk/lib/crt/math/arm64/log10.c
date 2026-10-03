/*
 * Base-10 logarithm for ARM64 CRT, using the natural log core.
 */

#include <math.h>
#include <errno.h>
#include <stdint.h>

#define asuint64(x) ((union { double f; uint64_t i; }){(x)}.i)
#define asdouble(x) ((union { uint64_t i; double f; }){(x)}.f)

static const double ivln10_hi = 0x1.bcb7b152p-2;
static const double ivln10_lo = 0x1.b9438ca9aadd5p-36;
static const double log10_2_hi = 0x1.34413509f6p-2;
static const double log10_2_lo = 0x1.9fef311f12b36p-42;
static const double Lg1 = 6.666666666666735130e-01;
static const double Lg2 = 3.999999999940941908e-01;
static const double Lg3 = 2.857142874366239149e-01;
static const double Lg4 = 2.222219843214978396e-01;
static const double Lg5 = 1.818357216161805012e-01;
static const double Lg6 = 1.531383769920937332e-01;
static const double Lg7 = 1.479819860511658591e-01;

double __cdecl __acrt_report_math_error(int type, char const* name, double arg1, double arg2, double retval, int error);

double log10(double x)
{
    uint64_t ix = asuint64(x);
    double f, s, z, w, R, hfsq, hi, lo, y, val_hi, val_lo, dk;
    int k = 0;

    if ((ix << 1) == 0)
        return __acrt_report_math_error(_SING, "log10", x, 0, -1.0 / (x * x), ERANGE);
    if (ix >> 63)
        return isnan(x) ? x + x : __acrt_report_math_error(_DOMAIN, "log10", x, 0, (x - x) / (x - x), EDOM);
    if (ix >= 0x7ff0000000000000ULL)
        return x + x;
    if (ix < 0x0010000000000000ULL)
    {
        k -= 54;
        x *= 0x1p54;
        ix = asuint64(x);
    }

    ix += 0x3ff0000000000000ULL - 0x3fe6a09e667f3bcdULL;
    k += (int)(ix >> 52) - 0x3ff;
    ix = (ix & 0x000fffffffffffffULL) + 0x3fe6a09e667f3bcdULL;
    x = asdouble(ix);

    f = x - 1.0;
    hfsq = 0.5 * f * f;
    s = f / (2.0 + f);
    z = s * s;
    w = z * z;
    R = z * (Lg1 + w * (Lg3 + w * (Lg5 + w * Lg7))) + w * (Lg2 + w * (Lg4 + w * Lg6));

    hi = asdouble(asuint64(f - hfsq) & 0xffffffff00000000ULL);
    lo = (f - hi) - hfsq + s * (hfsq + R);

    dk = k;
    y = dk * log10_2_hi;
    val_hi = hi * ivln10_hi;
    val_lo = dk * log10_2_lo + (lo + hi) * ivln10_lo + lo * ivln10_hi;

    w = y + val_hi;
    val_lo += (y - w) + val_hi;
    return val_lo + w;
}

#undef asuint64
#undef asdouble
