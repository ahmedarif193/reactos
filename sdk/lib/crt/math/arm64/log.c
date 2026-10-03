/*
 * Natural logarithm for ARM64 CRT.
 * Based on the classic fdlibm formulation used across multiple libm ports.
 */

#include <math.h>
#include <errno.h>
#include <stdint.h>

#define asuint64(x) ((union { double f; uint64_t i; }){(x)}.i)
#define asdouble(x) ((union { uint64_t i; double f; }){(x)}.f)

static const double ln2_hi = 6.93147180369123816490e-01;  /* 0x3fe62e42fee00000 */
static const double ln2_lo = 1.90821492927058770002e-10;  /* 0x3dea39ef35793c76 */
static const double Lg1 = 6.666666666666735130e-01;       /* 0x3fe5555555555593 */
static const double Lg2 = 3.999999999940941908e-01;       /* 0x3fd999999997fa04 */
static const double Lg3 = 2.857142874366239149e-01;       /* 0x3fd2492494229359 */
static const double Lg4 = 2.222219843214978396e-01;       /* 0x3fcc71c51d8e78af */
static const double Lg5 = 1.818357216161805012e-01;       /* 0x3fc7466496cb03de */
static const double Lg6 = 1.531383769920937332e-01;       /* 0x3fc39a09d078c69f */
static const double Lg7 = 1.479819860511658591e-01;       /* 0x3fc2f112df3e5244 */

double __cdecl __acrt_report_math_error(int type, char const* name, double arg1, double arg2, double retval, int error);

double log(double x)
{
    union { double f; uint64_t i; } u = { x };
    uint64_t ix = u.i;
    double f, s, z, w, t1, t2, R, hfsq;
    int k = 0;

    /* Handle subnormals, zero and negative values up-front. */
    if ((ix << 1) == 0)
        return __acrt_report_math_error(0, "log", x, 0, -1.0 / (x * x), ERANGE);
    if (ix >> 63)
        return isnan(x) ? x + x : __acrt_report_math_error(0, "log", x, 0, (x - x) / (x - x), EDOM);
    if (ix < 0x0010000000000000ULL) {
        k -= 54;
        x *= 0x1p54;
        ix = asuint64(x);
    }

    /* NaN or inf. */
    if (ix >= 0x7ff0000000000000ULL)
        return x + x;

    /* Normalize mantissa to [sqrt(1/2), sqrt(2)]. */
    ix += 0x3ff0000000000000ULL - 0x3fe6a09e667f3bcdULL;
    k += (int)(ix >> 52) - 0x3ff;
    ix = (ix & 0x000fffffffffffffULL) + 0x3fe6a09e667f3bcdULL;
    x = asdouble(ix);

    f = x - 1.0;
    s = f / (2.0 + f);
    z = s * s;
    w = z * z;
    t1 = w * (Lg2 + w * (Lg4 + w * Lg6));
    t2 = z * (Lg1 + w * (Lg3 + w * (Lg5 + w * Lg7)));
    R = t1 + t2;
    hfsq = 0.5 * f * f;

    return k * ln2_hi - ((hfsq - (s * (hfsq + R) + k * ln2_lo)) - f);
}

#undef asuint64
#undef asdouble
