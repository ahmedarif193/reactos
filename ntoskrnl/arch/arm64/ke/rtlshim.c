/*
 * PROJECT:         ReactOS Kernel (ARM64)
 * PURPOSE:         Stubbed-out RTL helpers required while the real ARM64
 *                  runtime support is brought online.
 */

#include <ntoskrnl.h>
#include <debug.h>

#ifndef CONTEXT_ARM64
#define CONTEXT_ARM64   0x00400000L
#endif
#ifndef CONTEXT_CONTROL
#define CONTEXT_CONTROL (CONTEXT_ARM64 | 0x1L)
#endif
#ifndef CONTEXT_INTEGER
#define CONTEXT_INTEGER (CONTEXT_ARM64 | 0x2L)
#endif

/* TODO(ARM64): Drop these shims once genuine implementations exist. */

int _SEH2_Volatile0 = 0;
int _SEH2_VolatileExceptionCode = 0;

unsigned short
__cdecl
_byteswap_ushort(
    unsigned short Value)
{
    return (unsigned short)((Value >> 8) | (Value << 8));
}

unsigned long
__cdecl
_byteswap_ulong(
    unsigned long Value)
{
    return (Value >> 24) |
           ((Value >> 8) & 0x0000FF00UL) |
           ((Value << 8) & 0x00FF0000UL) |
           (Value << 24);
}

__uint128_t
__atomic_load_16(
    _In_ const volatile VOID *Source,
    _In_ int MemoryOrder)
{
    UNREFERENCED_PARAMETER(MemoryOrder);
    return *(__uint128_t volatile const *)Source;
}

_Bool
__atomic_compare_exchange_16(
    _Inout_ volatile VOID *Destination,
    _Inout_ VOID *Expected,
    _In_ __uint128_t Desired,
    _In_ _Bool Weak,
    _In_ int SuccessOrder,
    _In_ int FailureOrder)
{
    __uint128_t Current;

    UNREFERENCED_PARAMETER(Weak);
    UNREFERENCED_PARAMETER(SuccessOrder);
    UNREFERENCED_PARAMETER(FailureOrder);

    Current = *(__uint128_t volatile *)Destination;
    if (Current == *(__uint128_t *)Expected)
    {
        *(__uint128_t volatile *)Destination = Desired;
        return 1;
    }

    *(__uint128_t *)Expected = Current;
    return 0;
}

int
__netf2(
    _In_ long double A,
    _In_ long double B)
{
    return (A != B);
}

int
__getf2(
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

ULONG
NTAPI
DebugService(
    _In_ ULONG Service,
    _In_ PVOID Argument1,
    _In_ PVOID Argument2,
    _In_ PVOID Argument3,
    _In_ PVOID Argument4)
{
    KPROCESSOR_MODE PreviousMode = KeGetPreviousMode();
    PKTHREAD Thread = KeGetCurrentThread();
    PKTRAP_FRAME TrapFrame = Thread ? Thread->TrapFrame : NULL;
    ULONG_PTR Arg2Value = (ULONG_PTR)Argument2;
    ULONG_PTR Arg3Value = (ULONG_PTR)Argument3;
    ULONG_PTR Arg4Value = (ULONG_PTR)Argument4;
    BOOLEAN Handled = FALSE;

    switch (Service)
    {
        case BREAKPOINT_PRINT:
        {
            NTSTATUS Status = KdpPrint((ULONG)Arg3Value,
                                       (ULONG)Arg4Value,
                                       (PCHAR)Argument1,
                                       (USHORT)Arg2Value,
                                       PreviousMode,
                                       TrapFrame,
                                       NULL,
                                       &Handled);
            return (ULONG)Status;
        }

        case BREAKPOINT_PROMPT:
            return (ULONG)KdpPrompt((PCHAR)Argument1,
                                    (USHORT)Arg2Value,
                                    (PCHAR)Argument3,
                                    (USHORT)Arg4Value,
                                    PreviousMode,
                                    TrapFrame,
                                    NULL);

        default:
            break;
    }

    return (ULONG)STATUS_NOT_IMPLEMENTED;
}

VOID
NTAPI
DebugService2(
    _In_ PVOID Argument1,
    _In_ PVOID Argument2,
    _In_ ULONG Service)
{
    KPROCESSOR_MODE PreviousMode = KeGetPreviousMode();
    PKTHREAD Thread = KeGetCurrentThread();
    PKTRAP_FRAME TrapFrame = Thread ? Thread->TrapFrame : NULL;
    CONTEXT LocalContext;

    RtlZeroMemory(&LocalContext, sizeof(LocalContext));
    LocalContext.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;

    switch (Service)
    {
        case BREAKPOINT_LOAD_SYMBOLS:
        case BREAKPOINT_UNLOAD_SYMBOLS:
            KdpSymbol((PSTRING)Argument1,
                      (PKD_SYMBOLS_INFO)Argument2,
                      (Service == BREAKPOINT_UNLOAD_SYMBOLS),
                      PreviousMode,
                      &LocalContext,
                      TrapFrame,
                      NULL);
            break;

        case BREAKPOINT_COMMAND_STRING:
            KdpCommandString((PSTRING)Argument1,
                             (PSTRING)Argument2,
                             PreviousMode,
                             &LocalContext,
                             TrapFrame,
                             NULL);
            break;

        default:
            break;
    }
}

#ifndef __has_builtin
#define __has_builtin(x) 0
#endif

static VOID
__reactos_fastfail_impl(
    _In_ unsigned int Code)
{
    KeBugCheckEx(KERNEL_SECURITY_CHECK_FAILURE,
                 Code,
                 0,
                 0,
                 0);
}

VOID
__cdecl
__fastfail(
    _In_ unsigned int Code) __attribute__((alias("__reactos_fastfail_impl")));

extern BOOLEAN KdDebuggerEnabled;
extern BOOLEAN KdDebuggerNotPresent;

static VOID
__reactos_debugbreak_impl(VOID)
{
    if (!KdDebuggerEnabled || KdDebuggerNotPresent)
    {
        return;
    }

    __asm__ __volatile__("brk #0xf001" ::: "memory");
}

VOID
__cdecl
__debugbreak(VOID) __attribute__((alias("__reactos_debugbreak_impl")));

VOID
__cdecl
_disable(VOID)
{
    __asm__ __volatile__("msr daifset, #0xf" ::: "memory");
}

VOID
__cdecl
_enable(VOID)
{
    __asm__ __volatile__("msr daifclr, #0xf" ::: "memory");
}

#if !__has_builtin(_rotl)
unsigned int
__cdecl
_rotl(
    _In_ unsigned int Value,
    _In_ int Shift)
{
    unsigned int Amount = (unsigned int)(Shift & 31);
    if (!Amount)
    {
        return Value;
    }

    return (Value << Amount) | (Value >> (32 - Amount));
}
#endif

KIRQL
(__cdecl * const __imp_KeGetCurrentIrql)(VOID) = KeGetCurrentIrql;

KIRQL
(__cdecl * const __imp_KfRaiseIrql)(KIRQL) = KfRaiseIrql;

VOID
(__cdecl * const __imp_KfLowerIrql)(KIRQL) = KfLowerIrql;

KIRQL
(__cdecl * const __imp_KeRaiseIrqlToDpcLevel)(VOID) = KeRaiseIrqlToDpcLevel;

KIRQL
(__cdecl * const __imp_KeRaiseIrqlToSynchLevel)(VOID) = KeRaiseIrqlToSynchLevel;
