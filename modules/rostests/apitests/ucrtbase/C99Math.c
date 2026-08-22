/*
 * PROJECT:     ReactOS API Tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Tests for mainstream UCRT C99 math exports
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#include <apitest.h>

#include <windows.h>
#include <errno.h>
#include <float.h>
#include <math.h>
#include <string.h>

typedef double (__cdecl *PUNARY_DOUBLE)(double);
typedef float (__cdecl *PUNARY_FLOAT)(float);
typedef double (__cdecl *PBINARY_DOUBLE)(double, double);
typedef float (__cdecl *PBINARY_FLOAT)(float, float);
typedef void (__cdecl *PSET_USER_MATHERR)(int (__cdecl *)(struct _exception *));

static struct _exception LastException;
static int MatherrCalls;
static int MatherrReturn;
static double MatherrResult;

static int __cdecl MatherrCallback(struct _exception *Exception)
{
    LastException = *Exception;
    MatherrCalls++;
    if (MatherrReturn) Exception->retval = MatherrResult;
    return MatherrReturn;
}

static FARPROC RequireExport(HMODULE Module, const char *Name)
{
    FARPROC Address = GetProcAddress(Module, Name);

    ok(Address != NULL, "%s is not exported\n", Name);
    return Address;
}

static double AbsoluteDifference(double Left, double Right)
{
    double Difference = Left - Right;

    return Difference < 0.0 ? -Difference : Difference;
}

static void CheckDouble(HMODULE Module, const char *Name, double Input, double Expected, double Tolerance)
{
    PUNARY_DOUBLE Function = (PUNARY_DOUBLE)(PVOID)RequireExport(Module, Name);
    double Result;

    if (!Function) return;
    Result = Function(Input);
    ok(AbsoluteDifference(Result, Expected) <= Tolerance, "%s(%.17g) returned %.17g, expected %.17g\n", Name, Input, Result, Expected);
}

static void CheckFloat(HMODULE Module, const char *Name, float Input, float Expected, float Tolerance)
{
    PUNARY_FLOAT Function = (PUNARY_FLOAT)(PVOID)RequireExport(Module, Name);
    float Result;

    if (!Function) return;
    Result = Function(Input);
    ok(AbsoluteDifference(Result, Expected) <= Tolerance, "%s(%.9g) returned %.9g, expected %.9g\n", Name, Input, Result, Expected);
}

static void CheckDoubleAliases(HMODULE Module, const char *const *Names, SIZE_T Count, double Input, double Expected, double Tolerance)
{
    SIZE_T Index;

    for (Index = 0; Index < Count; Index++) CheckDouble(Module, Names[Index], Input, Expected, Tolerance);
}

static void CheckDomainErrors(HMODULE Module)
{
    PUNARY_DOUBLE Acosh = (PUNARY_DOUBLE)(PVOID)RequireExport(Module, "acosh");
    PUNARY_DOUBLE Atanh = (PUNARY_DOUBLE)(PVOID)RequireExport(Module, "atanh");
    PUNARY_DOUBLE Log1p = (PUNARY_DOUBLE)(PVOID)RequireExport(Module, "log1p");
    double Result;

    if (Acosh)
    {
        errno = -1;
        Result = Acosh(0.0);
        ok(Result != Result, "acosh(0) returned %.17g instead of NaN\n", Result);
        ok_long(errno, EDOM);
    }

    if (Atanh)
    {
        errno = -1;
        Result = Atanh(1.0);
        ok(Result > DBL_MAX, "atanh(1) returned %.17g instead of infinity\n", Result);
        ok_long(errno, ERANGE);

        errno = -1;
        Result = Atanh(2.0);
        ok(Result != Result, "atanh(2) returned %.17g instead of NaN\n", Result);
        ok_long(errno, EDOM);
    }

    if (Log1p)
    {
        errno = -1;
        Result = Log1p(-1.0);
        ok(Result < -DBL_MAX, "log1p(-1) returned %.17g instead of negative infinity\n", Result);
        ok_long(errno, ERANGE);

        errno = -1;
        Result = Log1p(-2.0);
        ok(Result != Result, "log1p(-2) returned %.17g instead of NaN\n", Result);
        ok_long(errno, EDOM);
    }
}

static void CheckMatherrIsolation(HMODULE Module)
{
    PSET_USER_MATHERR SetUserMatherr = (PSET_USER_MATHERR)(PVOID)RequireExport(Module, "__setusermatherr");
    PUNARY_DOUBLE Expm1 = (PUNARY_DOUBLE)(PVOID)RequireExport(Module, "expm1");
    double Result;

    if (!SetUserMatherr || !Expm1) return;
    ZeroMemory(&LastException, sizeof(LastException));
    MatherrCalls = 0;
    MatherrReturn = 0;
    SetUserMatherr(MatherrCallback);
    errno = -1;
    Result = Expm1(1000.0);
    ok(Result > DBL_MAX, "expm1(1000) returned %.17g instead of infinity\n", Result);
    ok_long(errno, ERANGE);
    ok_long(MatherrCalls, 0);

    MatherrCalls = 0;
    MatherrReturn = 1;
    MatherrResult = 42.0;
    errno = -1;
    Result = Expm1(1000.0);
    ok(Result > DBL_MAX, "expm1 invoked the legacy matherr replacement and returned %.17g\n", Result);
    ok_long(errno, ERANGE);
    ok_long(MatherrCalls, 0);
    SetUserMatherr(NULL);
}

START_TEST(C99Math)
{
    static const char *const AcoshNames[] = {"acosh", "acoshl"};
    static const char *const AsinhNames[] = {"asinh", "asinhl"};
    static const char *const AtanhNames[] = {"atanh", "atanhl"};
    static const char *const CbrtNames[] = {"cbrt", "cbrtl"};
    static const char *const ErfNames[] = {"erf", "erfl"};
    static const char *const ErfcNames[] = {"erfc", "erfcl"};
    static const char *const Expm1Names[] = {"expm1", "expm1l"};
    static const char *const Log1pNames[] = {"log1p", "log1pl"};
    PBINARY_DOUBLE CopySign;
    PBINARY_DOUBLE CopySignLong;
    PBINARY_FLOAT CopySignFloat;
    HMODULE Module;

    Module = GetModuleHandleW(L"ucrtbase.dll");
    ok(Module != NULL, "ucrtbase.dll is not loaded\n");
    if (!Module) return;

    CheckDoubleAliases(Module, AcoshNames, _countof(AcoshNames), 2.0, 1.3169578969248166, 2e-15);
    CheckFloat(Module, "acoshf", 2.0f, 1.31695795f, 2e-7f);
    CheckDoubleAliases(Module, AsinhNames, _countof(AsinhNames), 0.5, 0.48121182505960347, 2e-15);
    CheckFloat(Module, "asinhf", 0.5f, 0.481211811f, 2e-7f);
    CheckDoubleAliases(Module, AtanhNames, _countof(AtanhNames), 0.5, 0.54930614433405489, 2e-15);
    CheckFloat(Module, "atanhf", 0.5f, 0.549306154f, 2e-7f);
    CheckDoubleAliases(Module, CbrtNames, _countof(CbrtNames), -8.0, -2.0, 0.0);
    CheckFloat(Module, "cbrtf", -8.0f, -2.0f, 0.0f);
    CheckDoubleAliases(Module, ErfNames, _countof(ErfNames), 1.0, 0.84270079294971489, 2e-15);
    CheckFloat(Module, "erff", 1.0f, 0.842700779f, 2e-7f);
    CheckDoubleAliases(Module, ErfcNames, _countof(ErfcNames), 1.0, 0.15729920705028513, 2e-15);
    CheckFloat(Module, "erfcf", 1.0f, 0.157299206f, 2e-7f);
    CheckDoubleAliases(Module, Expm1Names, _countof(Expm1Names), 1e-8, 1.0000000050000001e-8, 2e-24);
    CheckFloat(Module, "expm1f", 1e-4f, 1.00004996e-4f, 2e-11f);
    CheckDoubleAliases(Module, Log1pNames, _countof(Log1pNames), 1e-16, 1e-16, 1e-31);
    CheckFloat(Module, "log1pf", 1e-4f, 9.99949989e-5f, 2e-11f);

    CopySign = (PBINARY_DOUBLE)(PVOID)RequireExport(Module, "copysign");
    CopySignLong = (PBINARY_DOUBLE)(PVOID)RequireExport(Module, "copysignl");
    CopySignFloat = (PBINARY_FLOAT)(PVOID)RequireExport(Module, "copysignf");
    if (CopySign) ok(CopySign(3.0, -0.0) == -3.0, "copysign did not copy the sign\n");
    if (CopySignLong) ok(CopySignLong(3.0, -0.0) == -3.0, "copysignl did not copy the sign\n");
    if (CopySignFloat) ok(CopySignFloat(3.0f, -0.0f) == -3.0f, "copysignf did not copy the sign\n");

    CheckDomainErrors(Module);
    CheckMatherrIsolation(Module);
}
