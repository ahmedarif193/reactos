/*
 * PROJECT:         ReactOS Kernel (ARM64)
 * PURPOSE:         Minimal soft-float helpers to keep kernel logging working
 *                  until the real floating-point runtime support is wired up.
 */

#include <ntoskrnl.h>
#include <limits.h>

/* TODO(ARM64): Replace these placeholders with proper libgcc/libm quality
 * implementations once the ARM64 ABI work is complete. */

long double
__multf3(
    _In_ long double A,
    _In_ long double B)
{
    return A * B;
}

long double
__addtf3(
    _In_ long double A,
    _In_ long double B)
{
    return A + B;
}

long double
__divtf3(
    _In_ long double A,
    _In_ long double B)
{
    return (B == 0.0L) ? 0.0L : (A / B);
}

long double
__extenddftf2(
    _In_ double Value)
{
    return (long double)Value;
}

double
__trunctfdf2(
    _In_ long double Value)
{
    return (double)Value;
}

long double
__floatsitf(
    _In_ int Value)
{
    return (long double)Value;
}

long double
__floatunditf(
    _In_ unsigned long long Value)
{
    return (long double)Value;
}

unsigned long long
__fixunstfdi(
    _In_ long double Value)
{
    if (Value <= 0.0L)
    {
        return 0ULL;
    }

    if (Value >= (long double)ULLONG_MAX)
    {
        return ULLONG_MAX;
    }

    return (unsigned long long)Value;
}

int
__letf2(
    _In_ long double A,
    _In_ long double B)
{
    if (A < B)
    {
        return -1;
    }
    if (A > B)
    {
        return 1;
    }
    return 0;
}

int
__gttf2(
    _In_ long double A,
    _In_ long double B)
{
    if (A > B)
    {
        return 1;
    }
    if (A < B)
    {
        return -1;
    }
    return 0;
}

int
__lttf2(
    _In_ long double A,
    _In_ long double B)
{
    return (A < B) ? -1 : 0;
}

double
floor(
    _In_ double Value)
{
    LONG64 IntegerPart = (LONG64)Value;

    if ((double)IntegerPart > Value)
    {
        IntegerPart--;
    }

    return (double)IntegerPart;
}

double
pow(
    _In_ double Base,
    _In_ double Exponent)
{
    if (Exponent == 0.0)
    {
        return 1.0;
    }

    if (Base == 10.0)
    {
        double Result = 1.0;
        BOOLEAN Negative = FALSE;
        double Adjusted = Exponent;

        if (Adjusted < 0.0)
        {
            Negative = TRUE;
            Adjusted = -Adjusted;
        }

        LONG64 Whole = (LONG64)(Adjusted + 0.5);
        for (LONG64 Index = 0; Index < Whole; ++Index)
        {
            Result *= 10.0;
        }

        if (Negative && Result != 0.0)
        {
            Result = 1.0 / Result;
        }

        return Result;
    }

    /* Fallback stub */
    return 1.0;
}

double
log10(
    _In_ double Value)
{
    double Temp;
    double Result = 0.0;

    if (Value <= 0.0)
    {
        return 0.0;
    }

    Temp = Value;
    while (Temp >= 10.0)
    {
        Temp /= 10.0;
        Result += 1.0;
    }

    while (Temp > 0.0 && Temp < 1.0)
    {
        Temp *= 10.0;
        Result -= 1.0;
    }

    return Result;
}
