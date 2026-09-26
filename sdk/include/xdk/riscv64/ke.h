$if (_WDMDDK_)
/*
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 */

/* ReactOS RV64 software interrupt priorities. HAL mapping is separate. */
#define PASSIVE_LEVEL  0
#define LOW_LEVEL      0
#define APC_LEVEL      1
#define DISPATCH_LEVEL 2
#define CLOCK_LEVEL    13
#define IPI_LEVEL      14
#define POWER_LEVEL    14
#define PROFILE_LEVEL  15
#define HIGH_LEVEL     15

#define PAGE_SIZE  0x1000
#define PAGE_SHIFT 12L

/* The loader maps the shared user data page here; the constant loads with a
 * single lui. The memory manager later makes this view read-only and writes
 * through MmWriteableSharedUserData. */
#define KI_USER_SHARED_DATA     0xFFFFFFFFFFFE0000ULL
#define SharedUserData          ((KUSER_SHARED_DATA * const)KI_USER_SHARED_DATA)

#ifndef __ASSEMBLER__
/* RV64D register state. The kernel eagerly saves FP state at every trap;
 * each save belongs to the calling thread and must be restored at its IRQL. */
typedef struct DECLSPEC_ALIGN(16) _KFLOATING_SAVE
{
    ULONG64 F[32];
    ULONG Fcsr;
    UCHAR Irql;
    UCHAR Reserved[3];
    PVOID Owner;
} KFLOATING_SAVE, *PKFLOATING_SAVE;
#endif

/* Initial port stack policy; not a Windows RISC-V stack-size contract. */
#define KERNEL_STACK_SIZE         0xC000
#define KERNEL_LARGE_STACK_SIZE   0x18000
#define KERNEL_LARGE_STACK_COMMIT KERNEL_STACK_SIZE

#define EXCEPTION_READ_FAULT    0
#define EXCEPTION_WRITE_FAULT   1
#define EXCEPTION_EXECUTE_FAULT 8

#ifndef _RISCV64_YIELD_PROCESSOR_DEFINED
#define _RISCV64_YIELD_PROCESSOR_DEFINED
FORCEINLINE VOID YieldProcessor(VOID)
{
    /* Zihintpause is not part of the selected RV64GC baseline. */
    __asm__ __volatile__("nop" ::: "memory");
}
#endif
#define PAUSE_PROCESSOR YieldProcessor()

/* The time CSR is the counter every privilege level can read. */
FORCEINLINE
ULONG64
ReadTimeStampCounter(
    VOID)
{
    ULONG64 Value;

    __asm__ __volatile__("rdtime %0" : "=r"(Value) :: "memory");
    return Value;
}
#define DbgRaiseAssertionFailure() __debugbreak()
#define KeMemoryBarrier() MemoryBarrier()

NTKERNELAPI PKTHREAD NTAPI KeGetCurrentThread(VOID);

#if !defined(_NTOSKRNL_) && !defined(_NTSYSTEM_)
FORCEINLINE
ULONG
KeGetCurrentProcessorNumber(VOID)
{
    extern NTKERNELAPI ULONG NTAPI KeGetCurrentProcessorNumberEx(
        _Out_opt_ PPROCESSOR_NUMBER ProcNumber);
    return KeGetCurrentProcessorNumberEx(NULL);
}
#endif

_Must_inspect_result_
_IRQL_requires_max_(DISPATCH_LEVEL)
NTKERNELAPI
NTSTATUS
NTAPI
KeSaveFloatingPointState(
    _Out_ PKFLOATING_SAVE FloatSave);

_IRQL_requires_max_(DISPATCH_LEVEL)
NTKERNELAPI
NTSTATUS
NTAPI
KeRestoreFloatingPointState(
    _In_ PKFLOATING_SAVE FloatSave);

FORCEINLINE ULONG KeGetCurrentProcessorIndex(VOID)
{
    extern NTKERNELAPI ULONG NTAPI KeGetCurrentProcessorNumberEx(
        _Out_opt_ PPROCESSOR_NUMBER ProcNumber);
    return KeGetCurrentProcessorNumberEx(NULL);
}
NTKERNELAPI KIRQL NTAPI KeGetCurrentIrql(VOID);
NTKERNELAPI KIRQL FASTCALL KfRaiseIrql(KIRQL NewIrql);
NTKERNELAPI VOID FASTCALL KfLowerIrql(KIRQL NewIrql);
#define KeRaiseIrql(NewIrql, OldIrql) (*(OldIrql) = KfRaiseIrql(NewIrql))
#define KeLowerIrql(NewIrql) KfLowerIrql(NewIrql)
#if defined(_NTOSKRNL_) || defined(_NTHAL_) || defined(_NTSYSTEM_)
NTKERNELAPI KIRQL NTAPI KeRaiseIrqlToDpcLevel(VOID);
NTKERNELAPI KIRQL NTAPI KeRaiseIrqlToSynchLevel(VOID);
#else
/* Drivers raise inline, as on the other 64-bit architectures. */
FORCEINLINE KIRQL KeRaiseIrqlToDpcLevel(VOID) { return KfRaiseIrql(DISPATCH_LEVEL); }
FORCEINLINE KIRQL KeRaiseIrqlToSynchLevel(VOID) { return KfRaiseIrql(12); }
#endif

/* KUSER_SHARED_DATA.SystemTime is only 4-byte aligned and RV64 does not
 * guarantee atomic misaligned loads, so drivers call the kernel for the
 * clock values instead of reading the shared page as AMD64 and ARM64 do. */
extern NTKERNELAPI volatile KSYSTEM_TIME KeTickCount;
NTKERNELAPI VOID NTAPI KeQueryTickCount(PLARGE_INTEGER CurrentCount);
NTKERNELAPI VOID NTAPI KeFlushIoBuffers(PMDL Mdl, BOOLEAN ReadOperation, BOOLEAN DmaOperation);
$endif (_WDMDDK_)
