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
    /*
     * Issue BRK #0xF003 to request a debug service from the kernel.
     * The kernel's BRK handler recognizes this immediate and dispatches
     * to KdpPrint/KdpPrompt based on the service type in X0.
     *
     * Register convention for BRK #0xF003:
     *   X0 = Service type (BREAKPOINT_PRINT, BREAKPOINT_PROMPT, etc.)
     *   X1 = Argument1 (e.g. string buffer for print)
     *   X2 = Argument2 (e.g. string length for print)
     *   X3 = Argument3 (e.g. ComponentId for print)
     *   X4 = Argument4 (e.g. Level for print)
     *   X0 = return status (set by kernel before resuming)
     */
    register ULONG_PTR x0 __asm__("x0") = (ULONG_PTR)Service;
    register ULONG_PTR x1 __asm__("x1") = (ULONG_PTR)Argument1;
    register ULONG_PTR x2 __asm__("x2") = (ULONG_PTR)Argument2;
    register ULONG_PTR x3 __asm__("x3") = (ULONG_PTR)Argument3;
    register ULONG_PTR x4 __asm__("x4") = (ULONG_PTR)Argument4;

    __asm__ __volatile__(
        "brk #0xF003"
        : "+r"(x0), "+r"(x1), "+r"(x2), "+r"(x3), "+r"(x4)
        :
        : "memory", "cc"
    );

    return (ULONG)x0;
}

VOID
NTAPI
DebugService2(
    _In_opt_ PVOID Argument1,
    _In_opt_ PVOID Argument2,
    _In_ ULONG ServiceType)
{
    DebugService(ServiceType, Argument1, Argument2, NULL, NULL);
}
