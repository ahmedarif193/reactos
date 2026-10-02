/*
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Windows NT PowerPC kernel-private types
 */

#ifndef _PPC_KETYPES_H
#define _PPC_KETYPES_H

#ifdef CONFIG_SMP
#define SYNCH_LEVEL (IPI_LEVEL - 2)
#else
#define SYNCH_LEVEL DISPATCH_LEVEL
#endif

/* System call selector for the debug service (DbgPrint and friends). */
#define PPC_DEBUG_SERVICE_CALL 0x10000

#define NUMBER_POOL_LOOKASIDE_LISTS 32

#define PRCB_BUILD_DEBUG        1
#define PRCB_BUILD_UNIPROCESSOR  2

/* Shared IPI request bit indices. */
#define IPI_APC           1
#define IPI_DPC           2
#define IPI_FREEZE        4
#define IPI_PACKET_READY  6
#define IPI_SYNCH_REQUEST 16

#define IPI_FROZEN_STATE_RUNNING       0
#define IPI_FROZEN_STATE_FROZEN        2
#define IPI_FROZEN_STATE_THAW          3
#define IPI_FROZEN_STATE_OWNER         4
#define IPI_FROZEN_STATE_TARGET_FREEZE 5
#define IPI_FROZEN_STATE_SAVING        6
#define IPI_FROZEN_FLAG_ACTIVE         0x20

/* No LDTs on PowerPC. */
#define LDT_ENTRY ULONG

#define INITIAL_STALL_COUNT     100
#define DOUBLE_FAULT_STACK_SIZE KERNEL_STACK_SIZE

/* MSR bits used by the kernel. */
#define MSR_EE 0x00008000UL /* external interrupt enable */
#define MSR_PR 0x00004000UL /* problem (user) state */
#define MSR_FP 0x00002000UL /* floating-point available */
#define MSR_ME 0x00001000UL /* machine check enable */
#define MSR_IR 0x00000020UL /* instruction relocation */
#define MSR_DR 0x00000010UL /* data relocation */
#define MSR_RI 0x00000002UL /* recoverable interrupt */
#define MSR_LE 0x00000001UL /* little-endian mode */

typedef struct _KTRAP_FRAME KTRAP_FRAME, *PKTRAP_FRAME;
typedef struct _KEXCEPTION_FRAME KEXCEPTION_FRAME, *PKEXCEPTION_FRAME;
typedef struct _KPROCESSOR_STATE KPROCESSOR_STATE, *PKPROCESSOR_STATE;
typedef struct _KPRCB KPRCB, *PKPRCB;
typedef struct _KPCR KPCR, *PKPCR;

/* Complete register bank saved by every interrupt entry, together with the
 * interrupt state. The interrupted Iar/Msr are Srr0/Srr1. */
struct DECLSPEC_ALIGN(16) _KTRAP_FRAME
{
    CONTEXT Context;
    ULONG Dar;
    ULONG Dsisr;
    ULONG Vector;
    PKTRAP_FRAME PreviousTrapFrame;
    KIRQL PreviousIrql;
    UCHAR PreviousMode;
    UCHAR Reserved[2];
};

C_ASSERT(FIELD_OFFSET(KTRAP_FRAME, Context) == 0);
C_ASSERT((sizeof(KTRAP_FRAME) & 15) == 0);

#include <ndk/ppc/exception.h>

/* Initial thread parameters, consumed by KiThreadStartup above the first
 * KSWITCH_FRAME. */
typedef struct _KSTART_FRAME
{
    ULONG SystemRoutine;
    ULONG StartRoutine;
    ULONG StartContext;
    ULONG Return;
} KSTART_FRAME, *PKSTART_FRAME;

C_ASSERT((sizeof(KSTART_FRAME) & 15) == 0);

/* Nonvolatile state saved by KiSwapContext; KTHREAD.KernelStack points at
 * this frame while the thread is switched out. */
typedef struct DECLSPEC_ALIGN(16) _KSWITCH_FRAME
{
    double Fpr14;
    double Fpr15;
    double Fpr16;
    double Fpr17;
    double Fpr18;
    double Fpr19;
    double Fpr20;
    double Fpr21;
    double Fpr22;
    double Fpr23;
    double Fpr24;
    double Fpr25;
    double Fpr26;
    double Fpr27;
    double Fpr28;
    double Fpr29;
    double Fpr30;
    double Fpr31;
    ULONG Gpr14;
    ULONG Gpr15;
    ULONG Gpr16;
    ULONG Gpr17;
    ULONG Gpr18;
    ULONG Gpr19;
    ULONG Gpr20;
    ULONG Gpr21;
    ULONG Gpr22;
    ULONG Gpr23;
    ULONG Gpr24;
    ULONG Gpr25;
    ULONG Gpr26;
    ULONG Gpr27;
    ULONG Gpr28;
    ULONG Gpr29;
    ULONG Gpr30;
    ULONG Gpr31;
    ULONG Cr;
    ULONG Lr;
    ULONG Gpr2;
    UCHAR ApcBypass;
    UCHAR Reserved[3];
} KSWITCH_FRAME, *PKSWITCH_FRAME;

C_ASSERT((sizeof(KSWITCH_FRAME) & 15) == 0);

/* A suspended kernel call while an NT user callback executes. */
typedef struct DECLSPEC_ALIGN(16) _KCALLOUT_FRAME
{
    double Fpr14;
    double Fpr15;
    double Fpr16;
    double Fpr17;
    double Fpr18;
    double Fpr19;
    double Fpr20;
    double Fpr21;
    double Fpr22;
    double Fpr23;
    double Fpr24;
    double Fpr25;
    double Fpr26;
    double Fpr27;
    double Fpr28;
    double Fpr29;
    double Fpr30;
    double Fpr31;
    ULONG Gpr14;
    ULONG Gpr15;
    ULONG Gpr16;
    ULONG Gpr17;
    ULONG Gpr18;
    ULONG Gpr19;
    ULONG Gpr20;
    ULONG Gpr21;
    ULONG Gpr22;
    ULONG Gpr23;
    ULONG Gpr24;
    ULONG Gpr25;
    ULONG Gpr26;
    ULONG Gpr27;
    ULONG Gpr28;
    ULONG Gpr29;
    ULONG Gpr30;
    ULONG Gpr31;
    ULONG Cr;
    ULONG Lr;
    ULONG Gpr2;
    ULONG Msr;
    PVOID CallbackStack;
    PVOID InitialStack;
    PKTRAP_FRAME TrapFrame;
    PVOID *OutputBuffer;
    PULONG OutputLength;
    ULONG Reserved[3];
} KCALLOUT_FRAME, *PKCALLOUT_FRAME;

C_ASSERT((sizeof(KCALLOUT_FRAME) & 15) == 0);

/* Private user-stack record below the copied callback arguments. */
typedef struct _UCALLOUT_FRAME
{
    PVOID Buffer;
    ULONG Length;
    ULONG ApiNumber;
    ULONG Lr;
    ULONG Sp;
    ULONG Reserved[3];
} UCALLOUT_FRAME, *PUCALLOUT_FRAME;

C_ASSERT(sizeof(UCALLOUT_FRAME) == 32);

/* Supervisor register state. */
typedef struct _KSPECIAL_REGISTERS
{
    ULONG Msr;
    ULONG Sdr1;
    ULONG Sr[16];
    ULONG Hid0;
    ULONG Hid1;
    ULONG Sprg[4];
    ULONG Dar;
    ULONG Dsisr;
    ULONG Srr0;
    ULONG Srr1;
    ULONG Dec;
    ULONG Pvr;
    ULONG Reserved[2];
} KSPECIAL_REGISTERS, *PKSPECIAL_REGISTERS;

struct _KPROCESSOR_STATE
{
    CONTEXT ContextFrame;
    KSPECIAL_REGISTERS SpecialRegisters;
};

C_ASSERT(FIELD_OFFSET(KPROCESSOR_STATE, SpecialRegisters) == sizeof(CONTEXT));

struct _KPRCB
{
    PKTHREAD CurrentThread;
    PKTHREAD NextThread;
    PKTHREAD IdleThread;
    ULONG Number;
    ULONG MHz;
    KPROCESSOR_STATE ProcessorState;
    KAFFINITY SetMember;
    KSPIN_LOCK PrcbLock;
    BOOLEAN DpcThreadActive;
    BOOLEAN DpcRoutineActive;
    ULONG ReadySummary;
    ULONG QueueIndex;
    LIST_ENTRY DispatcherReadyListHead[MAXIMUM_PRIORITY];
    SINGLE_LIST_ENTRY DeferredReadyListHead;
    LIST_ENTRY WaitListHead;
    KSPIN_LOCK WaitLock;
    KSPIN_LOCK_QUEUE LockQueue[LockQueueMaximumLock];
    PP_LOOKASIDE_LIST PPLookasideList[16];
    GENERAL_LOOKASIDE_POOL PPNPagedLookasideList[NUMBER_POOL_LOOKASIDE_LISTS];
    GENERAL_LOOKASIDE_POOL PPPagedLookasideList[NUMBER_POOL_LOOKASIDE_LISTS];
    LONG LookasideIrpFloat;

    KDPC_DATA DpcData[2];
    PVOID DpcStack;
    LONG MaximumDpcQueueDepth;
    ULONG DpcRequestRate;
    ULONG MinimumDpcRate;
    UCHAR DpcInterruptRequested;
    UCHAR DpcThreadRequested;
    UCHAR ThreadDpcEnable;
    UCHAR QuantumEnd;
    UCHAR IdleSchedule;
    BOOLEAN Sleeping;
    ULONG TimerHand;
    ULONG TimerRequest;
    ULONG DpcLastCount;
    LONG DpcSetEventRequest;
    KEVENT DpcEvent;
    KDPC CallDpc;
    ULONG AdjustDpcThreshold;
    UCHAR SkipTick;
    KIRQL DebuggerSavedIRQL;
    PROCESSOR_POWER_STATE PowerState;

    struct _KNODE *ParentNode;
    KAFFINITY MultiThreadProcessorSet;
    PKPRCB MultiThreadSetMaster;

    ULONG KeSystemCalls;
    ULONG KeContextSwitches;
    ULONG KeAlignmentFixupCount;
    ULONG KeExceptionDispatchCount;
    ULONG KernelTime;
    ULONG UserTime;
    ULONG DpcTime;
    ULONG InterruptTime;
    ULONG InterruptCount;
    volatile LONG MmPageFaultCount;
    volatile LONG MmCopyOnWriteCount;
    volatile LONG MmDemandZeroCount;
    volatile LONG MmTransitionCount;
    volatile LONG IoReadOperationCount;
    volatile LONG IoWriteOperationCount;
    volatile LONG IoOtherOperationCount;
    LARGE_INTEGER IoReadTransferCount;
    LARGE_INTEGER IoWriteTransferCount;
    LARGE_INTEGER IoOtherTransferCount;

    volatile LONG RequestSummary;
    volatile ULONG IpiFrozen;
    USHORT BuildType;
};

/* Per-processor kernel storage. Supervisor code finds it through SPRG0 (its
 * KSEG0 address); the real-mode vector code uses SPRG1 (its physical
 * address). User r13 (the TEB) is never used to locate this structure. The
 * PCR lives in KSEG0, so accesses to it never miss the hashed page table. */
struct _KPCR
{
    ULONG ProcessorId;
    volatile KIRQL CurrentIrql;
    volatile UCHAR SoftwareInterrupts;  /* 1 << APC_LEVEL | 1 << DISPATCH_LEVEL */
    UCHAR InterruptsEnabled;            /* The HAL enabled interrupt delivery. */
    UCHAR TrapActive;
    volatile UCHAR PendingClock;        /* Decrementer taken at or above CLOCK_LEVEL. */
    volatile UCHAR PendingExternal;     /* External interrupt deferred at the device IRQL. */
    UCHAR Reserved0[2];
    ULONG PhysicalAddress;              /* Of this PCR. */
    ULONG PageTableRoot;                /* Physical root of the current address space. */
    ULONG HashTable;                    /* Physical base of the hashed page table. */
    ULONG HashTableMask;                /* PTEG index mask. */
    ULONG HashReplace;                  /* Round-robin eviction slot. */
    ULONG InitialStack;                 /* Kernel stack top for traps from user mode. */
    PVOID PanicStack;
    ULONG TrapVector;
    ULONG TrapDar;
    ULONG TrapDsisr;
    ULONG ReloadScratch[8];             /* Real-mode hashed page table reload. */
    ULONG TrapScratch[8];               /* Interrupted r1-r4, CR, SRR0, SRR1, XER. */
    ULONG ExitScratch[8];               /* Final r1-r4, CR, SRR0, SRR1 on trap exit. */
    ULONG HalReserved[16];
    KTRAP_FRAME PanicFrame;
    KPRCB Prcb;
    PVOID KdVersionBlock;
};

#ifdef __cplusplus
extern "C" {
#endif
FORCEINLINE
PKPCR
KeGetPcr(VOID)
{
    PKPCR Pcr;
    __asm__ __volatile__("mfsprg %0, 0" : "=r"(Pcr) :: "memory");
    return Pcr;
}

FORCEINLINE
PKPRCB
KeGetCurrentPrcb(VOID)
{
    return &KeGetPcr()->Prcb;
}
#ifdef __cplusplus
}
#endif

#endif /* _PPC_KETYPES_H */
