#include <math.h>

#ifdef _MSC_VER
#pragma function(atan2)
#endif

static inline
long double
__arm64_internal_atan2l(
    _In_ long double y,
    _In_ long double x)
{
    return __builtin_atan2l(y, x);
}

long double
__cdecl
__extenddftf2(
    _In_ double value)
{
    return (long double)value;
}

double
__cdecl
__trunctfdf2(
    _In_ long double value)
{
    return (double)value;
}

#undef atan2l

long double
__cdecl
atan2l(
    _In_ long double y,
    _In_ long double x)
{
    return __arm64_internal_atan2l(y, x);
}

_Check_return_
double
__cdecl
atan2(
    _In_ double y,
    _In_ double x)
{
    return (double)__arm64_internal_atan2l((long double)y, (long double)x);
}
