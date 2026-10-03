/*
 * PROJECT:     ReactOS CRT library
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     ARM64 implementation of ldexp()
 * COPYRIGHT:   Imported from musl libc
 *              https://git.musl-libc.org/cgit/musl/tree/src/math/ldexp.c
 *              See https://git.musl-libc.org/cgit/musl/tree/COPYRIGHT
 *
 * NOTE (ARM64): AArch64 has no hardware ldexp, so the former
 * "return __builtin_ldexp(x, exp)" lowered to a call to ldexp() itself --
 * infinite self-recursion that overflowed the stack. musl defines ldexp() as
 * scalbn(); scalbn() already has a correct musl implementation in this CRT
 * (sdk/lib/crt/math/scalbn.c) and is linked into the same libraries.
 */

#include <math.h>
#include <float.h>
#include <errno.h>

/* Correct musl scalbn() lives in sdk/lib/crt/math/scalbn.c (same libraries). */
double scalbn(double x, int n);
double __cdecl __acrt_report_math_error(int type, char const* name, double arg1, double arg2, double retval, int error);

double ldexp(double x, int exp)
{
    double result = scalbn(x, exp);

    if (_isnan(x))
        errno = EDOM;
    else if (_finite(x) && x != 0.0 && !_finite(result))
        return __acrt_report_math_error(_OVERFLOW, "ldexp", x, exp, result, ERANGE);
    else if (_finite(x) && x != 0.0 && result == 0.0)
        return __acrt_report_math_error(_UNDERFLOW, "ldexp", x, exp, result, 0);
    return result;
}
