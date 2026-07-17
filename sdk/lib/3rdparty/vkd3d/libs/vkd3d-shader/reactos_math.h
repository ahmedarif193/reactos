/*
 * ReactOS CRT compatibility for the Wine 10.0 vkd3d-shader sources.
 *
 * The legacy ReactOS CRT math.h does not expose the C99 classification
 * macros or exp2f()/log2f(). Keep the adaptation at the library boundary so
 * the imported shader sources remain otherwise identical to Wine.
 */

#pragma once

#include <float.h>
#include <math.h>

#ifndef isfinite
# if defined(__clang__) || defined(__GNUC__)
#  define isfinite(x) __builtin_isfinite(x)
# else
#  define isfinite(x) _finite((double)(x))
# endif
#endif

#ifndef isnan
# if defined(__clang__) || defined(__GNUC__)
#  define isnan(x) __builtin_isnan(x)
# else
#  define isnan(x) _isnan((double)(x))
# endif
#endif

#ifndef signbit
# if defined(__clang__) || defined(__GNUC__)
#  define signbit(x) __builtin_signbit(x)
# else
static __inline int vkd3d_reactos_signbit(double value)
{
    union
    {
        double d;
        unsigned __int64 u;
    } bits;

    bits.d = value;
    return (int)(bits.u >> 63);
}
#  define signbit(x) vkd3d_reactos_signbit((double)(x))
# endif
#endif

#ifndef fmax
# if defined(__clang__) || defined(__GNUC__)
#  define fmax(x, y) __builtin_fmax((x), (y))
# else
static __inline double vkd3d_reactos_fmax(double x, double y)
{
    if (isnan(x)) return y;
    if (isnan(y)) return x;
    if (x == y) return signbit(x) ? y : x;
    return x > y ? x : y;
}
#  define fmax(x, y) vkd3d_reactos_fmax((x), (y))
# endif
#endif

#ifndef fmaxf
# if defined(__clang__) || defined(__GNUC__)
#  define fmaxf(x, y) __builtin_fmaxf((x), (y))
# else
#  define fmaxf(x, y) ((float)fmax((double)(x), (double)(y)))
# endif
#endif

#ifndef fmin
# if defined(__clang__) || defined(__GNUC__)
#  define fmin(x, y) __builtin_fmin((x), (y))
# else
static __inline double vkd3d_reactos_fmin(double x, double y)
{
    if (isnan(x)) return y;
    if (isnan(y)) return x;
    if (x == y) return signbit(x) ? x : y;
    return x < y ? x : y;
}
#  define fmin(x, y) vkd3d_reactos_fmin((x), (y))
# endif
#endif

#ifndef fminf
# if defined(__clang__) || defined(__GNUC__)
#  define fminf(x, y) __builtin_fminf((x), (y))
# else
#  define fminf(x, y) ((float)fmin((double)(x), (double)(y)))
# endif
#endif

static __inline float vkd3d_reactos_exp2f(float value)
{
    return powf(2.0f, value);
}

static __inline float vkd3d_reactos_log2f(float value)
{
    return logf(value) / 0.69314718055994530942f;
}

#define exp2f vkd3d_reactos_exp2f
#define log2f vkd3d_reactos_log2f
