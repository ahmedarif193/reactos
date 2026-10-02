$if (_WDMDDK_)
/*
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Windows NT PowerPC kernel definitions
 */

/* Interrupt request levels (Windows NT PowerPC). */
#define PASSIVE_LEVEL  0
#define LOW_LEVEL      0
#define APC_LEVEL      1
#define DISPATCH_LEVEL 2
#define PROFILE_LEVEL  27
#define CLOCK1_LEVEL   28
#define CLOCK2_LEVEL   28
#define CLOCK_LEVEL    CLOCK2_LEVEL
#define IPI_LEVEL      29
#define POWER_LEVEL    30
#define HIGH_LEVEL     31
#ifdef CONFIG_SMP
#define SYNCH_LEVEL    (IPI_LEVEL - 2)
#else
#define SYNCH_LEVEL    DISPATCH_LEVEL
#endif

#define PAGE_SIZE  0x1000
#define PAGE_SHIFT 12L

/* Kernel view of the shared user data page, inside the loader-mapped top
 * slot 0x80000000-0xBFFFFFFF; the user view is 0x7FFE0000. */
#define KI_USER_SHARED_DATA     0xBFFF0000UL
#define SharedUserData          ((KUSER_SHARED_DATA * const)KI_USER_SHARED_DATA)

#ifndef __ASSEMBLER__
/* FPR and FPSCR state saved for kernel-mode floating-point use. */
typedef struct DECLSPEC_ALIGN(8) _KFLOATING_SAVE
{
    double Fpr[32];
    double Fpscr;
    UCHAR Irql;
    UCHAR Reserved[3];
    PVOID Owner;
} KFLOATING_SAVE, *PKFLOATING_SAVE;
#endif

/* A trap frame carries the full register bank (0x1F0 bytes) and trap entry
 * leaves a 256-byte cushion below an interrupted kernel stack pointer. */
#define KERNEL_STACK_SIZE         0x6000
#define KERNEL_LARGE_STACK_SIZE   0xF000
#define KERNEL_LARGE_STACK_COMMIT 0x6000

#define EXCEPTION_READ_FAULT    0
#define EXCEPTION_WRITE_FAULT   1
#define EXCEPTION_EXECUTE_FAULT 8

#ifndef _PPC_YIELD_PROCESSOR_DEFINED
#define _PPC_YIELD_PROCESSOR_DEFINED
FORCEINLINE VOID YieldProcessor(VOID)
{
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
NTKERNELAPI KIRQL NTAPI KeRaiseIrqlToDpcLevel(VOID);
NTKERNELAPI KIRQL NTAPI KeRaiseIrqlToSynchLevel(VOID);

extern NTKERNELAPI volatile KSYSTEM_TIME KeTickCount;
NTKERNELAPI VOID NTAPI KeQueryTickCount(PLARGE_INTEGER CurrentCount);
NTKERNELAPI VOID NTAPI KeFlushIoBuffers(PMDL Mdl, BOOLEAN ReadOperation, BOOLEAN DmaOperation);
$endif (_WDMDDK_)
