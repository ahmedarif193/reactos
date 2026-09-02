/*
 * PROJECT:     ReactOS CRT library
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     ARM64 sine, cosine, and tangent implementations
 * COPYRIGHT:   Adapted from musl libc trigonometric kernels
 *              https://git.musl-libc.org/cgit/musl/tree/src/math
 *              See https://git.musl-libc.org/cgit/musl/tree/COPYRIGHT
 */

#include <math.h>
#include <stdint.h>

void __remainder_piby2(double x, double *r, double *rr, int *region);

#if defined(_MSC_VER)
#pragma function(sin)
#pragma function(cos)
#pragma function(tan)
#endif

typedef union
{
    double d;
    uint64_t u;
} TRIG_DBL;

static __inline uint32_t TrigHighWord(double x)
{
    TRIG_DBL t;
    t.d = x;
    return (uint32_t)(t.u >> 32);
}

static __inline double TrigClearLowWord(double x)
{
    TRIG_DBL t;
    t.d = x;
    t.u &= 0xffffffff00000000ULL;
    return t.d;
}

static const double
S1 = -1.66666666666666324348e-01,
S2 =  8.33333333332248946124e-03,
S3 = -1.98412698298579493134e-04,
S4 =  2.75573137070700676789e-06,
S5 = -2.50507602534068634195e-08,
S6 =  1.58969099521155010221e-10;

static const double
C1 =  4.16666666666666019037e-02,
C2 = -1.38888888888741095749e-03,
C3 =  2.48015872894767294178e-05,
C4 = -2.75573143513906633035e-07,
C5 =  2.08757232129817482790e-09,
C6 = -1.13596475577881948265e-11;

static const double T[] = {
     3.33333333333334091986e-01,
     1.33333333333201242699e-01,
     5.39682539762260521377e-02,
     2.18694882948595424599e-02,
     8.86323982359930005737e-03,
     3.59207910759131235356e-03,
     1.45620945432529025516e-03,
     5.88041240820264096874e-04,
     2.46463134818469906812e-04,
     7.81794442939557092300e-05,
     7.14072491382608190305e-05,
    -1.85586374855275456654e-05,
     2.59073051863633712884e-05,
};
static const double pio4   = 7.85398163397448278999e-01;
static const double pio4lo = 3.06161699786838301793e-17;

static const double
toint   = 1.5 / 2.22044604925031308085e-16,
invpio2 = 6.36619772367581382433e-01,
pio2_1  = 1.57079632673412561417e+00,
pio2_1t = 6.07710050650619224932e-11,
pio2_2  = 6.07710050630396597660e-11,
pio2_2t = 2.02226624879595063154e-21,
pio2_3  = 2.02226624871116645580e-21,
pio2_3t = 8.47842766036889956997e-32;

static double TrigKernelSin(double x, double y, int iy)
{
    double z, r, v, w;

    z = x * x;
    w = z * z;
    r = S2 + z * (S3 + z * S4) + z * w * (S5 + z * S6);
    v = z * x;
    if (iy == 0)
        return x + v * (S1 + z * r);
    return x - ((z * (0.5 * y - v * r) - y) - v * S1);
}

static double TrigKernelCos(double x, double y)
{
    double hz, z, r, w;

    z = x * x;
    w = z * z;
    r = z * (C1 + z * (C2 + z * C3)) + w * w * (C4 + z * (C5 + z * C6));
    hz = 0.5 * z;
    w = 1.0 - hz;
    return w + (((1.0 - w) - hz) + (z * r - x * y));
}

static double TrigKernelTan(double x, double y, int odd)
{
    double z, r, v, w, s, a, w0, a0;
    uint32_t hx;
    int big, sign;

    hx = TrigHighWord(x);
    big = (hx & 0x7fffffff) >= 0x3FE59428;
    if (big)
    {
        sign = hx >> 31;
        if (sign)
        {
            x = -x;
            y = -y;
        }
        x = (pio4 - x) + (pio4lo - y);
        y = 0.0;
    }
    z = x * x;
    w = z * z;
    r = T[1] + w * (T[3] + w * (T[5] + w * (T[7] + w * (T[9] + w * T[11]))));
    v = z * (T[2] + w * (T[4] + w * (T[6] + w * (T[8] + w * (T[10] + w * T[12])))));
    s = z * x;
    r = y + z * (s * (r + v) + y) + s * T[0];
    w = x + r;
    if (big)
    {
        s = 1 - 2 * odd;
        v = s - 2.0 * (x + (r - w * w / (w + s)));
        return sign ? -v : v;
    }
    if (!odd)
        return w;
    w0 = TrigClearLowWord(w);
    v = r - (w0 - x);
    a0 = a = -1.0 / w;
    a0 = TrigClearLowWord(a0);
    return a0 + a * (1.0 + a0 * w0 + a0 * v);
}

static int TrigRemPio2(double x, double *y)
{
    uint32_t ix = TrigHighWord(x) & 0x7fffffff;
    uint32_t ex, ey;
    double fn, r, w, t, y0;
    int n, region;

    if (ix >= 0x7ff00000)
    {
        y[0] = y[1] = x - x;
        return 0;
    }

    if (ix < 0x413921fb)
    {
        fn = (double)(x * invpio2 + toint) - toint;
        n = (int)fn;
        r = x - fn * pio2_1;
        w = fn * pio2_1t;
        y0 = r - w;
        ex = ix >> 20;
        ey = (TrigHighWord(y0) >> 20) & 0x7ff;
        if (ex - ey > 16)
        {
            t = r;
            w = fn * pio2_2;
            r = t - w;
            w = fn * pio2_2t - ((t - r) - w);
            y0 = r - w;
            ey = (TrigHighWord(y0) >> 20) & 0x7ff;
            if (ex - ey > 49)
            {
                t = r;
                w = fn * pio2_3;
                r = t - w;
                w = fn * pio2_3t - ((t - r) - w);
                y0 = r - w;
            }
        }
        y[0] = y0;
        y[1] = (r - y0) - w;
        return n;
    }

    if (x < 0.0)
    {
        __remainder_piby2(-x, &y[0], &y[1], &region);
        y[0] = -y[0];
        y[1] = -y[1];
        return (-region) & 3;
    }

    __remainder_piby2(x, &y[0], &y[1], &region);
    return region;
}

double sin(double x)
{
    double y[2];
    uint32_t ix = TrigHighWord(x) & 0x7fffffff;
    int n;

    if (ix <= 0x3fe921fb)
    {
        if (ix < 0x3e500000)
            return x;
        return TrigKernelSin(x, 0.0, 0);
    }
    if (ix >= 0x7ff00000)
        return x - x;

    n = TrigRemPio2(x, y);
    switch (n & 3)
    {
        case 0:  return  TrigKernelSin(y[0], y[1], 1);
        case 1:  return  TrigKernelCos(y[0], y[1]);
        case 2:  return -TrigKernelSin(y[0], y[1], 1);
        default: return -TrigKernelCos(y[0], y[1]);
    }
}

double cos(double x)
{
    double y[2];
    uint32_t ix = TrigHighWord(x) & 0x7fffffff;
    int n;

    if (ix <= 0x3fe921fb)
    {
        if (ix < 0x3e46a09e)
            return 1.0;
        return TrigKernelCos(x, 0.0);
    }
    if (ix >= 0x7ff00000)
        return x - x;

    n = TrigRemPio2(x, y);
    switch (n & 3)
    {
        case 0:  return  TrigKernelCos(y[0], y[1]);
        case 1:  return -TrigKernelSin(y[0], y[1], 1);
        case 2:  return -TrigKernelCos(y[0], y[1]);
        default: return  TrigKernelSin(y[0], y[1], 1);
    }
}

double tan(double x)
{
    double y[2];
    uint32_t ix = TrigHighWord(x) & 0x7fffffff;
    int n;

    if (ix <= 0x3fe921fb)
    {
        if (ix < 0x3e400000)
            return x;
        return TrigKernelTan(x, 0.0, 0);
    }
    if (ix >= 0x7ff00000)
        return x - x;

    n = TrigRemPio2(x, y);
    return TrigKernelTan(y[0], y[1], n & 1);
}
