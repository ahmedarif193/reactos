/*
 * PROJECT:         ReactOS Kernel (ARM64)
 * PURPOSE:         Stubbed-out RTL helpers required while the real ARM64
 *                  runtime support is brought online.
 * COPYRIGHT:       Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
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
    ULONGLONG Low, High;

    UNREFERENCED_PARAMETER(MemoryOrder);

    __asm__ __volatile__("ldaxp %0, %1, [%2]"
                         : "=&r"(Low), "=&r"(High)
                         : "r"(Source)
                         : "memory");
    __asm__ __volatile__("clrex" ::: "memory");

    return ((__uint128_t)High << 64) | (__uint128_t)Low;
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
    ULONGLONG ExpectedLow, ExpectedHigh;
    ULONGLONG DesiredLow, DesiredHigh;
    ULONGLONG OldLow, OldHigh;
    ULONG Status;
    ULONG Retries = 1 << 20; /* bounded retries to avoid livelock on MMIO/shared lines */

    UNREFERENCED_PARAMETER(Weak);
    UNREFERENCED_PARAMETER(SuccessOrder);
    UNREFERENCED_PARAMETER(FailureOrder);

    /* Match GCC's libatomic semantics (strong CAS); always use acq-rel here. */
    ExpectedLow = ((volatile ULONGLONG *)Expected)[0];
    ExpectedHigh = ((volatile ULONGLONG *)Expected)[1];

    DesiredLow = (ULONGLONG)Desired;
    DesiredHigh = (ULONGLONG)(Desired >> 64);

    for (;;)
    {
        __asm__ __volatile__("ldaxp %0, %1, [%2]"
                             : "=&r"(OldLow), "=&r"(OldHigh)
                             : "r"(Destination)
                             : "memory");

        if ((OldLow != ExpectedLow) || (OldHigh != ExpectedHigh))
        {
            ((volatile ULONGLONG *)Expected)[0] = OldLow;
            ((volatile ULONGLONG *)Expected)[1] = OldHigh;
            __asm__ __volatile__("clrex" ::: "memory");
            return 0;
        }

        __asm__ __volatile__("stlxp %w0, %1, %2, [%3]"
                             : "=&r"(Status)
                             : "r"(DesiredLow), "r"(DesiredHigh), "r"(Destination)
                             : "memory");

        if (Status == 0)
        {
            return 1;
        }

        if (--Retries == 0)
        {
            __asm__ __volatile__("clrex" ::: "memory");
            DPRINT1("__atomic_compare_exchange_16: retry budget exhausted for %p\n", Destination);
            ((volatile ULONGLONG *)Expected)[0] = OldLow;
            ((volatile ULONGLONG *)Expected)[1] = OldHigh;
            return 0;
        }
    }
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

extern BOOLEAN KdDebuggerEnabled;
extern BOOLEAN KdDebuggerNotPresent;

ULONG
NTAPI
DebugService(
    _In_ ULONG Service,
    _In_ PVOID Argument1,
    _In_ PVOID Argument2,
    _In_ PVOID Argument3,
    _In_ PVOID Argument4)
{
    if (!KdDebuggerEnabled)
        return (ULONG)STATUS_SUCCESS;

    KPROCESSOR_MODE PreviousMode = KernelMode;
    PKTRAP_FRAME TrapFrame = NULL;
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
    KPROCESSOR_MODE PreviousMode = KernelMode;
    PKTRAP_FRAME TrapFrame = NULL;
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
    /*
     * Mask IRQ only (I bit). Keep SError (A) unmasked per IRQL policy
     * and avoid forcing full DAIF state changes that bypass IRQL tracking.
     * ISB is sufficient for DAIF changes (no memory ordering required).
     */
    __asm__ __volatile__("msr daifset, #0x2\n\tisb" ::: "memory");
}

VOID
__cdecl
_enable(VOID)
{
    /*
     * IRQL is enforced via GIC priority masking; DAIF.I is a global gate.
     * ISB is sufficient for DAIF changes (no memory ordering required).
     */
    __asm__ __volatile__("msr daifclr, #0x2\n\tisb" ::: "memory");
}

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
