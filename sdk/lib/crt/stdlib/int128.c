/*
 * PROJECT:     ReactOS CRT library
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     Support routines for 128-bit integer arithmetic used by GCC
 */

#include <precomp.h>

#ifdef _MSC_VER
#error This file requires GCC extensions for 128-bit integers
#endif

typedef unsigned __int128 crt_uint128;
typedef __int128 crt_int128;

static crt_uint128
crt_udivmod128(crt_uint128 numerator, crt_uint128 denominator, crt_uint128 *remainder)
{
    if (denominator == 0)
        __builtin_trap();

    crt_uint128 quotient = 0;
    crt_uint128 rem = 0;

    for (int i = 127; i >= 0; --i)
    {
        rem <<= 1;
        rem |= (numerator >> i) & 1;
        if (rem >= denominator)
        {
            rem -= denominator;
            quotient |= ((crt_uint128)1) << i;
        }
    }

    if (remainder)
        *remainder = rem;

    return quotient;
}

crt_uint128 __udivti3(crt_uint128 numerator, crt_uint128 denominator)
{
    return crt_udivmod128(numerator, denominator, NULL);
}

crt_uint128 __umodti3(crt_uint128 numerator, crt_uint128 denominator)
{
    crt_uint128 rem;
    crt_udivmod128(numerator, denominator, &rem);
    return rem;
}

static crt_uint128
crt_abs128(crt_int128 value, int *is_negative)
{
    crt_uint128 temp = (crt_uint128)value;
    if (value < 0)
    {
        *is_negative = 1;
        temp = (~temp) + 1;
    }
    else
    {
        *is_negative = 0;
    }
    return temp;
}

crt_int128 __divti3(crt_int128 numerator, crt_int128 denominator)
{
    int numer_neg, denom_neg;
    crt_uint128 unumer = crt_abs128(numerator, &numer_neg);
    crt_uint128 udenom = crt_abs128(denominator, &denom_neg);

    crt_uint128 uquot = crt_udivmod128(unumer, udenom, NULL);

    if (numer_neg ^ denom_neg)
        uquot = (~uquot) + 1;

    return (crt_int128)uquot;
}

crt_int128 __modti3(crt_int128 numerator, crt_int128 denominator)
{
    int numer_neg, denom_neg;
    crt_uint128 unumer = crt_abs128(numerator, &numer_neg);
    crt_uint128 udenom = crt_abs128(denominator, &denom_neg);
    crt_uint128 rem;

    crt_udivmod128(unumer, udenom, &rem);

    if (numer_neg)
        rem = (~rem) + 1;

    return (crt_int128)rem;
}
