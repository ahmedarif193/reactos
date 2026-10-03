/*
 * COPYRIGHT:       BSD - See COPYING.ARM in the top level directory
 * PROJECT:         ReactOS CRT library
 * PURPOSE:         Portable implementation of sqrt
 * PROGRAMMER:      Timo Kreuzer (timo.kreuzer@reactos.org)
 */

#include <math.h>
#include <errno.h>
#include <assert.h>

double __cdecl __acrt_report_math_error(int type, char const* name, double arg1, double arg2, double retval, int error);

double
__cdecl
sqrt(
    double x)
{
    const double threehalfs = 1.5;
    const double x2 = x * 0.5;
    long long bits;
    double inv, y;

    /* Handle special cases */
    if (x == 0.0)
    {
        return x;
    }
    else if (x < 0.0)
    {
        return __acrt_report_math_error(_DOMAIN, "sqrt", x, 0, -NAN, EDOM);
    }

    /* Convert into a 64  bit integer */
    bits = *(long long *)&x;

    /* Check for !finite(x) */
    if ((bits & 0x7ff0000000000000LL) == 0x7ff0000000000000LL)
    {
        return x + x;
    }

    /* Step 1: quick approximation of 1/sqrt(x) with bit magic
       See http://en.wikipedia.org/wiki/Fast_inverse_square_root */
    bits = 0x5fe6eb50c7b537a9ll - (bits >> 1);
    inv = *(double*)&bits;

    /* Step 2: 3 Newton iterations to approximate 1 / sqrt(x) */
    inv = inv * (threehalfs - (x2 * inv * inv));
    inv = inv * (threehalfs - (x2 * inv * inv));
    inv = inv * (threehalfs - (x2 * inv * inv));

    /* Step 3: 1 additional Heron iteration has shown to maximize the precision.
       Normally the formula would be: y = (y + (x / y)) * 0.5;
       Instead we use the inverse sqrt directly */
    y = ((1 / inv) + (x * inv)) * 0.5;

    //assert(y == (double)((y + (x / y)) * 0.5));
    /* GCC BUG: While the C-Standard requires that an explicit cast to
       double converts the result of a computation to the appropriate
       64 bit value, our GCC ignores this and uses an 80 bit FPU register
       in an intermediate value, so we need to make sure it is stored in
       a memory location before comparison */
//#if DBG
//    {
//        volatile double y1 = y, y2;
//        y2 = (y + (x / y)) * 0.5;
//        assert(y1 == y2);
//    }
//#endif

    return y;
}
