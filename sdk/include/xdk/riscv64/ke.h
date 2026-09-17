$if (_WDMDDK_)
/*
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
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

#ifndef __ASSEMBLER__
/* Opaque until the RISC-V kernel floating-point ownership ABI is implemented. */
typedef struct _KFLOATING_SAVE
{
    ULONG Reserved;
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
#define DbgRaiseAssertionFailure() __debugbreak()
#define KeMemoryBarrier() MemoryBarrier()

NTKERNELAPI PKTHREAD NTAPI KeGetCurrentThread(VOID);
NTKERNELAPI ULONG NTAPI KeGetCurrentProcessorNumber(VOID);

_Must_inspect_result_
_IRQL_requires_max_(DISPATCH_LEVEL)
FORCEINLINE
NTSTATUS
KeSaveFloatingPointState(
    _Out_ PKFLOATING_SAVE FloatSave)
{
    UNREFERENCED_PARAMETER(FloatSave);
    return STATUS_NOT_SUPPORTED;
}

_IRQL_requires_max_(DISPATCH_LEVEL)
FORCEINLINE
NTSTATUS
KeRestoreFloatingPointState(
    _In_ PKFLOATING_SAVE FloatSave)
{
    UNREFERENCED_PARAMETER(FloatSave);
    return STATUS_NOT_SUPPORTED;
}

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
NTKERNELAPI KIRQL NTAPI KeRaiseIrqlToDpcLevel(VOID);
NTKERNELAPI KIRQL NTAPI KeRaiseIrqlToSynchLevel(VOID);

extern NTKERNELAPI volatile KSYSTEM_TIME KeTickCount;
NTKERNELAPI VOID NTAPI KeQueryTickCount(PLARGE_INTEGER CurrentCount);
NTKERNELAPI VOID NTAPI KeFlushIoBuffers(PMDL Mdl, BOOLEAN ReadOperation, BOOLEAN DmaOperation);
$endif (_WDMDDK_)
