
#include <stdint.h>
#include <intrin.h>
#include <errno.h>
#include <limits.h>
#include <malloc.h>
#define _USE_MATH_DEFINES
#include <math.h>

// atexit is needed by libsupc++
extern int __cdecl _crt_atexit(void (__cdecl*)(void));
int __cdecl atexit(void (__cdecl* function)(void))
{
    return _crt_atexit(function);
}

void* __cdecl operator_new(size_t size)
{
    return malloc(size);
}

void _cdecl operator_delete(void *mem)
{
    free(mem);
}

#ifdef _M_IX86
void _chkesp_failed(void)
{
    __debugbreak();
}
#endif

int __cdecl __acrt_initialize_sse2(void)
{
    return 0;
}

// The following stubs cannot be implemented as stubs by spec2def, because they are intrinsics

#ifdef _MSC_VER
#pragma warning(disable:4163) // not available as an intrinsic function
#pragma warning(disable:4164) // intrinsic function not declared
#pragma function(log2)
#pragma function(log2f)
#pragma function(lrint)
#pragma function(lrintf)
#pragma function(llround)
#pragma function(llroundf)
#pragma function(lround)
#pragma function(lroundf)
#endif

double log2(double x)
{
    // Simplistic implementation: log2(x) = log(x) / log(2)
    return log(x) * M_LOG2E;
}

float log2f(float x)
{
    return (float)log2((double)x);
}

long int lrint(double x)
{
    double d;

    d = rint(x);
    if ((d < 0 && d != (double)(long int)d) ||
        (d >= 0 && d != (double)(unsigned long int)d))
    {
        *_errno() = EDOM;
        return 0;
    }
    return d;
}

long int lrintf(float x)
{
    float f;

    f = rintf(x);
    if ((f < 0 && f != (float)(long int)f) ||
        (f >= 0 && f != (float)(unsigned long int)f))
    {
        *_errno() = EDOM;
        return 0;
    }
    return f;
}

long long int llrint(double x)
{
    double d;

    d = rint(x);
    if ((d < 0 && d != (double)(long long int)d) ||
        (d >= 0 && d != (double)(unsigned long long int)d))
    {
        *_errno() = EDOM;
        return 0;
    }
    return d;
}

long long int llrintf(float x)
{
    float f;

    f = rintf(x);
    if ((f < 0 && f != (float)(long long int)f) ||
        (f >= 0 && f != (float)(unsigned long long int)f))
    {
        *_errno() = EDOM;
        return 0;
    }
    return f;
}

long long int llround(double x)
{
    double rounded = round(x);

    if (isnan(rounded) || rounded < (double)LLONG_MIN || rounded >= -(double)LLONG_MIN)
    {
        *_errno() = EDOM;
        return 0;
    }

    return (long long int)rounded;
}

long long int llroundf(float x)
{
    float rounded = roundf(x);

    if (isnan(rounded) || rounded < (float)LLONG_MIN || rounded >= -(float)LLONG_MIN)
    {
        *_errno() = EDOM;
        return 0;
    }

    return (long long int)rounded;
}

long int lround(double x)
{
    double rounded = round(x);

    if (isnan(rounded) || rounded < (double)LONG_MIN || rounded > (double)LONG_MAX)
    {
        *_errno() = EDOM;
        return 0;
    }

    return (long int)rounded;
}

long int lroundf(float x)
{
    float rounded = roundf(x);

    if (isnan(rounded) || rounded < (float)LONG_MIN || rounded >= -(float)LONG_MIN)
    {
        *_errno() = EDOM;
        return 0;
    }

    return (long int)rounded;
}
