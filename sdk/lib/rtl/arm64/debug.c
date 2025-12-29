/*
 * PROJECT:         ReactOS Run-Time Library
 * LICENSE:         MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:         Minimal debugging helpers for ARM64 user-mode runtime
 */

#include <rtl.h>
#include <ndk/umtypes.h>

#define NDEBUG
#include <debug.h>

FORCEINLINE VOID
RtlpArm64DebugBreak(VOID)
{
#if defined(__GNUC__)
    __asm__ __volatile__("brk #0xf000" ::: "memory");
#else
    __debugbreak();
#endif
}

VOID
NTAPI
DbgBreakPoint(VOID)
{
    RtlpArm64DebugBreak();
}

VOID
NTAPI
DbgUserBreakPoint(VOID)
{
    RtlpArm64DebugBreak();
}

VOID
NTAPI
DbgBreakPointWithStatus(
    _In_ ULONG Status)
{
    UNREFERENCED_PARAMETER(Status);
    RtlpArm64DebugBreak();
}

VOID
NTAPI
RtlpBreakWithStatusInstruction(VOID)
{
    RtlpArm64DebugBreak();
}

VOID
NTAPI
RtlGetCallersAddress(
    _Out_opt_ PVOID *CallersAddress,
    _Out_opt_ PVOID *CallersCaller)
{
    if (CallersAddress != NULL)
    {
#if defined(__GNUC__)
        *CallersAddress = __builtin_return_address(0);
#else
        *CallersAddress = _ReturnAddress();
#endif
    }

    if (CallersCaller != NULL)
    {
        *CallersCaller = NULL;
    }
}

ULONG
NTAPI
DebugService(
    _In_ ULONG Service,
    _In_opt_ PVOID Argument1,
    _In_opt_ PVOID Argument2,
    _In_opt_ PVOID Argument3,
    _In_opt_ PVOID Argument4)
{
    UNREFERENCED_PARAMETER(Service);
    UNREFERENCED_PARAMETER(Argument1);
    UNREFERENCED_PARAMETER(Argument2);
    UNREFERENCED_PARAMETER(Argument3);
    UNREFERENCED_PARAMETER(Argument4);
    return (ULONG)STATUS_NOT_IMPLEMENTED;
}

VOID
NTAPI
DebugService2(
    _In_opt_ PVOID Argument1,
    _In_opt_ PVOID Argument2,
    _In_ ULONG ServiceType)
{
    UNREFERENCED_PARAMETER(Argument1);
    UNREFERENCED_PARAMETER(Argument2);
    UNREFERENCED_PARAMETER(ServiceType);
}
