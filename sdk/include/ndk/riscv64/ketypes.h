/*
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 */

/* ReactOS RV64 kernel-private types. Do not substitute another NT
 * architecture's register frames or per-CPU layouts here. */
#ifndef _RISCV64_KETYPES_H
#define _RISCV64_KETYPES_H

/* Software synchronization priority selected by ABI-064. */
#define SYNCH_LEVEL 12

/* ReactOS-private U-mode ECALL selector, outside the NT service-table space. */
#define RISCV_DEBUG_SERVICE_CALL 0x10000

#define NUMBER_POOL_LOOKASIDE_LISTS 32

/* Shared IPI request bit indices, not hardware interrupt identifiers. */
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

/* No LDTs on RISC-V */
#define LDT_ENTRY ULONG

#define INITIAL_STALL_COUNT     100
/* The PCR panic stack is kernel-stack sized. */
#define DOUBLE_FAULT_STACK_SIZE KERNEL_STACK_SIZE

typedef struct _KTRAP_FRAME KTRAP_FRAME, *PKTRAP_FRAME;
typedef struct _KEXCEPTION_FRAME KEXCEPTION_FRAME, *PKEXCEPTION_FRAME;
typedef struct _KPROCESSOR_STATE KPROCESSOR_STATE, *PKPROCESSOR_STATE;
typedef struct _KPRCB KPRCB, *PKPRCB;
typedef struct _KPCR KPCR, *PKPCR;

/* Complete base register bank; no separate nonvolatile exception-frame bank.
 * The producer is the RISC-V resumable supervisor-trap entry. */
struct DECLSPEC_ALIGN(16) _KTRAP_FRAME
{
    CONTEXT Context;
    ULONG64 Sstatus;
    ULONG64 Scause;
    ULONG64 Stval;
    PKTRAP_FRAME PreviousTrapFrame;
    KIRQL PreviousIrql;
};

C_ASSERT(FIELD_OFFSET(KTRAP_FRAME, Context) == 0);
C_ASSERT((sizeof(KTRAP_FRAME) & 15) == 0);

#include <ndk/riscv64/exception.h>

/* Initial thread parameters, consumed by KiThreadStartup directly above the
 * first KSWITCH_FRAME. Return is reserved for a user-return path. */
typedef struct _KSTART_FRAME
{
    ULONG64 SystemRoutine;
    ULONG64 StartRoutine;
    ULONG64 StartContext;
    ULONG64 Return;
} KSTART_FRAME, *PKSTART_FRAME;

C_ASSERT((sizeof(KSTART_FRAME) & 15) == 0);

/* psABI callee-saved bank saved by KiSwapContext; KTHREAD.KernelStack points
 * at this frame while the thread is switched out. */
typedef struct _KSWITCH_FRAME
{
    ULONG64 S0;
    ULONG64 S1;
    ULONG64 S2;
    ULONG64 S3;
    ULONG64 S4;
    ULONG64 S5;
    ULONG64 S6;
    ULONG64 S7;
    ULONG64 S8;
    ULONG64 S9;
    ULONG64 S10;
    ULONG64 S11;
    ULONG64 Ra;
    UCHAR ApcBypass;
    UCHAR Reserved[7];
} KSWITCH_FRAME, *PKSWITCH_FRAME;

C_ASSERT((sizeof(KSWITCH_FRAME) & 15) == 0);

/* A suspended kernel call while an NT user callback executes. All links and
 * output-storage pointers are kernel-owned; no user frame restores them. */
typedef struct DECLSPEC_ALIGN(16) _KCALLOUT_FRAME
{
    ULONG64 S1[11];
    ULONG64 Gp;
    ULONG64 Tp;
    ULONG64 Sstatus;
    ULONG64 Fs[12];
    ULONG Fcsr;
    ULONG Reserved;
    PVOID CallbackStack;
    PVOID InitialStack;
    PKTRAP_FRAME TrapFrame;
    PVOID *OutputBuffer;
    PULONG OutputLength;
    ULONG64 S0;
    ULONG64 Ra;
} KCALLOUT_FRAME, *PKCALLOUT_FRAME;

C_ASSERT((sizeof(KCALLOUT_FRAME) & 15) == 0);
C_ASSERT(FIELD_OFFSET(KCALLOUT_FRAME, S0) == sizeof(KCALLOUT_FRAME) - 16);
C_ASSERT(FIELD_OFFSET(KCALLOUT_FRAME, Ra) == sizeof(KCALLOUT_FRAME) - 8);

/* Private user-stack record below the copied callback arguments. The shared
 * ntdll C dispatcher receives its arguments in a0-a2, not from this record. */
typedef struct _UCALLOUT_FRAME
{
    PVOID Buffer;
    ULONG Length;
    ULONG ApiNumber;
    ULONG64 Pc;
    ULONG64 Sp;
} UCALLOUT_FRAME, *PUCALLOUT_FRAME;

C_ASSERT(sizeof(UCALLOUT_FRAME) == 32);

/* Call-time diagnostic state, not a trap-return or debugger packet layout. */
typedef struct _KSPECIAL_REGISTERS
{
    ULONG64 Sstatus;
    ULONG64 Sie;
    ULONG64 Stvec;
    ULONG64 Sscratch;
    ULONG64 Sepc;
    ULONG64 Scause;
    ULONG64 Stval;
    ULONG64 Sip;
    ULONG64 Satp;
} KSPECIAL_REGISTERS, *PKSPECIAL_REGISTERS;

struct _KPROCESSOR_STATE
{
    CONTEXT ContextFrame;
    KSPECIAL_REGISTERS SpecialRegisters;
};

C_ASSERT(FIELD_OFFSET(KPROCESSOR_STATE, SpecialRegisters) == sizeof(CONTEXT));
C_ASSERT(sizeof(KSPECIAL_REGISTERS) == 9 * sizeof(ULONG64));

/* ReactOS-private scheduler state. Offsets are not a Windows RV64 ABI. */
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

    /* Common DPC queues and timer bookkeeping; HAL owns interrupt delivery. */
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
    ULONG64 TimerHand;
    ULONG64 TimerRequest;
    ULONG DpcLastCount;
    LONG DpcSetEventRequest;
    KEVENT DpcEvent;
    KDPC CallDpc;
    ULONG AdjustDpcThreshold;
    UCHAR SkipTick;
    KIRQL DebuggerSavedIRQL;
    PROCESSOR_POWER_STATE PowerState;

    /* Logical processor topology, independent of platform hart identifiers. */
    struct _KNODE *ParentNode;
    KAFFINITY MultiThreadProcessorSet;
    PKPRCB MultiThreadSetMaster;

    /* Counters consumed by the common scheduler, I/O manager and system queries. */
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

    /* Cross-processor requests and debugger freeze state. */
    volatile LONG RequestSummary;
    volatile ULONG IpiFrozen;
};

/* Kernel-owned resident storage, addressed by sscratch in supervisor code.
 * User tp is never used to locate this structure. */
struct _KPCR
{
    ULONG_PTR HartId;
    volatile KIRQL CurrentIrql;
    UCHAR SoftwareInterrupts;
    ULONG_PTR InterruptEnable;
    PVOID PanicStack;
    ULONG_PTR TrapScratch;
    ULONG_PTR TrapScratch2;
    ULONG_PTR TrapStack;
    BOOLEAN TrapActive;
    KTRAP_FRAME PanicFrame;
    KPRCB Prcb;
    /* Debugger version block published by KdInitSystem (shared kd64 code). */
    PVOID KdVersionBlock;
};

/* Startup must install the PCR before calling these accessors. */
#ifdef __cplusplus
extern "C" {
#endif
PKPCR NTAPI KeGetPcr(VOID);
PKPRCB NTAPI KeGetCurrentPrcb(VOID);
#ifdef __cplusplus
}
#endif

#endif /* _RISCV64_KETYPES_H */
