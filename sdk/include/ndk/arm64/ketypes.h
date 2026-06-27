

#ifndef _ARM64_KETYPES_H
#define _ARM64_KETYPES_H

#define KSEG0_BASE 0xFFFF800000000000ULL

#ifdef __cplusplus
extern "C" {
#endif

/* Interrupt request levels */
#define PASSIVE_LEVEL           0
#define LOW_LEVEL               0
#define APC_LEVEL               1
#define DISPATCH_LEVEL          2
#define CMCI_LEVEL              5
#define CLOCK_LEVEL             13
#define IPI_LEVEL               14
#define DRS_LEVEL               14
#define POWER_LEVEL             14
#define PROFILE_LEVEL           15
#define HIGH_LEVEL              15
#define SYNCH_LEVEL             12

#define NUMBER_POOL_LOOKASIDE_LISTS 32

//
// IPI Types
//
/* NOTE: These constants are bit indices for InterlockedBitTestAndSet/Reset. */
#define IPI_APC                 1
#define IPI_DPC                 2
#define IPI_FREEZE              4
#define IPI_PACKET_READY        6
#define IPI_SYNCH_REQUEST       16

#define IPI_FROZEN_STATE_RUNNING        0
#define IPI_FROZEN_STATE_FROZEN         2
#define IPI_FROZEN_STATE_THAW           3
#define IPI_FROZEN_STATE_OWNER          4
#define IPI_FROZEN_STATE_TARGET_FREEZE  5
#define IPI_FROZEN_FLAG_ACTIVE          0x20

//
// PRCB Flags
//
#define PRCB_MAJOR_VERSION      1
#define PRCB_BUILD_DEBUG        1
#define PRCB_BUILD_UNIPROCESSOR 2

//
// No LDTs on ARM64
//
#define LDT_ENTRY              ULONG


//
// CONTEXT frame flags (Win11 ARM64 parity)
//
#ifndef CONTEXT_ARM64
#define CONTEXT_ARM64           0x00400000L
#define CONTEXT_CONTROL         (CONTEXT_ARM64 | 0x00000001L)
#define CONTEXT_INTEGER         (CONTEXT_ARM64 | 0x00000002L)
#define CONTEXT_FLOATING_POINT  (CONTEXT_ARM64 | 0x00000004L)
#define CONTEXT_DEBUG_REGISTERS (CONTEXT_ARM64 | 0x00000008L)
#define CONTEXT_X18             (CONTEXT_ARM64 | 0x00000010L)
#define CONTEXT_FULL            (CONTEXT_CONTROL | CONTEXT_INTEGER | CONTEXT_FLOATING_POINT)
#endif

//
// HAL Variables
//
#define INITIAL_STALL_COUNT     100
#define MM_HAL_VA_START         0xFFFFFFFFFFC00000ULL
#define MM_HAL_VA_END           0xFFFFFFFFFFFFFFFFULL

//
// Double fault stack size
//
#define DOUBLE_FAULT_STACK_SIZE 0x8000

//
// Structure for CPUID info
//
typedef union _CPU_INFO
{
    ULONG dummy;
} CPU_INFO, *PCPU_INFO;

typedef struct _KTRAP_FRAME
{
    UCHAR ExceptionActive;
    UCHAR ContextFromKFramesUnwound;
    UCHAR DebugRegistersValid;
    union
    {
        struct
        {
            CHAR PreviousMode;
            UCHAR PreviousIrql;
        };
    };
    ULONG Reserved;
    union
    {
        struct
        {
            ULONG64 FaultAddress;
            ULONG64 TrapFrame;
        };
    };
    //struct PKARM64_VFP_STATE VfpState;
    ULONG VfpState;
    ULONG Bcr[8];
    ULONG64 Bvr[8];
    ULONG Wcr[2];
    ULONG64 Wvr[2];
    ULONG Spsr;
    ULONG Esr;
    ULONG64 Sp;
    union
    {
        ULONG64 X[19];
        struct
        {
            ULONG64 X0;
            ULONG64 X1;
            ULONG64 X2;
            ULONG64 X3;
            ULONG64 X4;
            ULONG64 X5;
            ULONG64 X6;
            ULONG64 X7;
            ULONG64 X8;
            ULONG64 X9;
            ULONG64 X10;
            ULONG64 X11;
            ULONG64 X12;
            ULONG64 X13;
            ULONG64 X14;
            ULONG64 X15;
            ULONG64 X16;
            ULONG64 X17;
            ULONG64 X18;
        };
    };
    ULONG64 Lr;
    ULONG64 Fp;
    ULONG64 Pc;
} KTRAP_FRAME, *PKTRAP_FRAME;

typedef struct _KEXCEPTION_FRAME
{
    ULONG64 P1Home;
    ULONG64 P2Home;
    ULONG64 P3Home;
    ULONG64 P4Home;
    ULONG64 P5;
#if (NTDDI_VERSION >= NTDDI_WIN8)
    ULONG64 Spare1;
#else
    ULONG64 InitialStack;
#endif
    ULONG64 TrapFrame;
#if (NTDDI_VERSION < NTDDI_WIN8)
    ULONG64 CallbackStack;
#endif
    ULONG64 OutputBuffer;
    ULONG64 OutputLength;
#if (NTDDI_VERSION >= NTDDI_WIN8)
    ULONG64 Spare2;
#endif
    ULONG64 Fpcr;
    ULONG64 Fpsr;
    ULONG64 X19;
    ULONG64 X20;
    ULONG64 X21;
    ULONG64 X22;
    ULONG64 X23;
    ULONG64 X24;
    ULONG64 X25;
    ULONG64 X26;
    ULONG64 X27;
    ULONG64 X28;
    ULONG64 Fp;
    ULONG64 Lr;
    ULONG64 Return;
} KEXCEPTION_FRAME, *PKEXCEPTION_FRAME;

//
// Machine Frame
//
typedef struct _MACHINE_FRAME
{
    ULONG64 Sp;
    ULONG64 Pc;
} MACHINE_FRAME, *PMACHINE_FRAME;

//
// User side APC dispatcher frame
//
typedef struct _UAPC_FRAME
{
    CONTEXT Context;
    MACHINE_FRAME MachineFrame;
} UAPC_FRAME, *PUAPC_FRAME;

//
// Stack frame layout for KiUserExceptionDispatcher
//
typedef struct _KUSER_EXCEPTION_STACK
{
    CONTEXT Context;
    EXCEPTION_RECORD ExceptionRecord;
    ULONG64 Alignment;
    MACHINE_FRAME MachineFrame;
} KUSER_EXCEPTION_STACK, *PKUSER_EXCEPTION_STACK;

typedef KEXCEPTION_FRAME KCALLOUT_FRAME, *PKCALLOUT_FRAME;

//
// User side callout frame
//
typedef struct _UCALLOUT_FRAME
{
    ULONG64 P1Home;
    ULONG64 P2Home;
    ULONG64 P3Home;
    ULONG64 P4Home;
    PVOID Buffer;
    ULONG Length;
    ULONG ApiNumber;
    MACHINE_FRAME MachineFrame;
} UCALLOUT_FRAME, *PUCALLOUT_FRAME;

typedef struct _KSTART_FRAME
{
    ULONG64 StartRoutine;
    ULONG64 StartContext;
    ULONG64 SystemRoutine;
    ULONG64 Parameter;
    ULONG64 Return;
    ULONG64 Padding;  /* ARM64: Pad to 48 bytes for 16-byte stack alignment */
} KSTART_FRAME, *PKSTART_FRAME;

typedef struct _KSWITCH_FRAME
{
    ULONG64 X19;
    ULONG64 X20;
    ULONG64 X21;
    ULONG64 X22;
    ULONG64 X23;
    ULONG64 X24;
    ULONG64 X25;
    ULONG64 X26;
    ULONG64 X27;
    ULONG64 X28;
    ULONG64 Fp;
    ULONG64 Lr;
    ULONG64 ReturnAddress;
    UCHAR ApcBypass;
    UCHAR Reserved[7];
} KSWITCH_FRAME, *PKSWITCH_FRAME;

typedef struct _TRAPFRAME_LOG_ENTRY
{
    ULONG64 Thread;
    UCHAR CpuNumber;
    UCHAR TrapType;
    USHORT Padding;
    ULONG Cpsrl;
    ULONG64 X0;
    ULONG64 X1;
    ULONG64 X2;
    ULONG64 X3;
    ULONG64 X4;
    ULONG64 X5;
    ULONG64 X6;
    ULONG64 X7;
    ULONG64 Fp;
    ULONG64 Lr;
    ULONG64 Sp;
    ULONG64 Pc;
    ULONG64 Far;
    ULONG Esr;
    ULONG Reserved1;
} TRAPFRAME_LOG_ENTRY, *PTRAPFRAME_LOG_ENTRY;

//
// Processor Region Control Block (stubbed for ARM64 bring-up)
//
struct _PROCESSOR_POWER_STATE;
typedef struct _PROCESSOR_POWER_STATE PROCESSOR_POWER_STATE, *PPROCESSOR_POWER_STATE;

typedef struct _KPROCESSOR_STATE KPROCESSOR_STATE, *PKPROCESSOR_STATE;

typedef struct _KSPECIAL_REGISTERS
{
    ULONG64 Elr_El1;
    UINT32  Spsr_El1;
    ULONG64 Tpidr_El0;
    ULONG64 Tpidrro_El0;
    ULONG64 Tpidr_El1;
    ULONG64 KernelBvr[8];
    ULONG   KernelBcr[8];
    ULONG64 KernelWvr[2];
    ULONG   KernelWcr[2];
} KSPECIAL_REGISTERS, *PKSPECIAL_REGISTERS;

//
// ARM64 Architecture State
// Based on WoA symbols
//
typedef struct _KARM64_ARCH_STATE
{
    ULONG64 Midr_El1;                           /* 0x000 */
    ULONG64 Sctlr_El1;                          /* 0x008 */
    ULONG64 Actlr_El1;                          /* 0x010 */
    ULONG64 Cpacr_El1;                          /* 0x018 */
    ULONG64 Tcr_El1;                            /* 0x020 */
    ULONG64 Ttbr0_El1;                          /* 0x028 */
    ULONG64 Ttbr1_El1;                          /* 0x030 */
    ULONG64 Esr_El1;                            /* 0x038 */
    ULONG64 Far_El1;                            /* 0x040 */
    ULONG64 Pmcr_El0;                           /* 0x048 */
    ULONG64 Pmcntenset_El0;                     /* 0x050 */
    ULONG64 Pmccntr_El0;                        /* 0x058 */
    ULONG64 Pmxevcntr_El0[31];                  /* 0x060..0x157 */
    ULONG64 Pmxevtyper_El0[31];                 /* 0x158..0x24f */
    ULONG64 Pmovsclr_El0;                       /* 0x250 */
    ULONG64 Pmselr_El0;                         /* 0x258 */
    ULONG64 Pmuserenr_El0;                      /* 0x260 */
    ULONG64 Mair_El1;                           /* 0x268 */
    ULONG64 Vbar_El1;                           /* 0x270 */
    /* Extended ARM64 architectural state. */
    ULONG64 APIBKeyHi_El1;                      /* 0x278 */
    ULONG64 APIBKeyLo_El1;                      /* 0x280 */
    ULONG64 Mpam0_El1;                          /* 0x288 */
    ULONG64 Zcr_El1;                            /* 0x290 */
    ULONG64 Padding;                            /* 0x298 */
} KARM64_ARCH_STATE, *PKARM64_ARCH_STATE;
C_ASSERT(sizeof(KARM64_ARCH_STATE) == 0x2a0);

typedef struct _KPROCESSOR_STATE
{
    KSPECIAL_REGISTERS SpecialRegisters; // 0x000 (160 bytes)
    KARM64_ARCH_STATE ArchState;         // 0x0a0 (672 bytes)
    CONTEXT ContextFrame;                // 0x340 (912 bytes)
} KPROCESSOR_STATE, *PKPROCESSOR_STATE;
C_ASSERT(sizeof(KPROCESSOR_STATE) == 0x6d0);

typedef struct _KREQUEST_PACKET
{
    PVOID CurrentPacket[3];
    PVOID WorkerRoutine;
} KREQUEST_PACKET, *PKREQUEST_PACKET;

typedef struct _REQUEST_MAILBOX
{
    struct _REQUEST_MAILBOX *Next;
    ULONG_PTR RequestSummary;
    KREQUEST_PACKET RequestPacket;
    LONG volatile *NodeTargetCountAddr;
    LONG volatile NodeTargetCount;
} REQUEST_MAILBOX, *PREQUEST_MAILBOX;
C_ASSERT(sizeof(REQUEST_MAILBOX) == 0x40);

#if (NTDDI_VERSION < NTDDI_LONGHORN)
#define GENERAL_LOOKASIDE_POOL PP_LOOKASIDE_LIST
#endif

/*
 * ARM64 KPRCB.
 *
 * Layout keeps the ARM64 PRCB size and the offsets ReactOS currently depends
 * on. ReactOS-used members stay typed; unused private middle ranges are
 * held by PrcbReservedXXXX byte buffers.
 */
typedef struct _KPRCB
{
    /* 0x000-0x027 -- per-CPU dispatcher head */
    UCHAR LegacyNumber;                     /* 0x000 */
    UCHAR ReservedMustBeZero;               /* 0x001 */
    UCHAR IdleHalt;                         /* 0x002 */
    UCHAR InterruptRequest;                 /* 0x003 (ReactOS-private alias) */
    ULONG PrcbPad000;                       /* 0x004 */
    struct _KTHREAD *CurrentThread;         /* 0x008 */
    struct _KTHREAD *NextThread;            /* 0x010 */
    struct _KTHREAD *IdleThread;            /* 0x018 */
    UCHAR NestingLevel;                     /* 0x020 */
    UCHAR ClockOwner;                       /* 0x021 */
    UCHAR PendingTickFlags;                 /* 0x022 */
    UCHAR IdleState;                        /* 0x023 */
    ULONG Number;                           /* 0x024 */
    UINT64 PrcbLock;                        /* 0x028 */

    /* 0x030-0x03f -- PriorityState alias */
    UCHAR PriorityState[16];                /* 0x030 */

    /* 0x040 -- ProcessorState (KPROCESSOR_STATE, sizeof = 1744 / 0x6d0) */
    KPROCESSOR_STATE ProcessorState;        /* 0x040..0x70f */

    /* 0x710-0x75f -- HalReserved (80 bytes / 10 ULONG64) */
    UINT64 HalReserved[10];                 /* 0x710 */

    /* 0x760-0x77f -- version + cpu identity */
    USHORT MinorVersion;                    /* 0x760 */
    USHORT MajorVersion;                    /* 0x762 */
    UCHAR BuildType;                        /* 0x764 */
    UCHAR CpuVendor;                        /* 0x765 (also exposed as VendorString[0]) */
    UCHAR LegacyCoresPerPhysicalProcessor;  /* 0x766 */
    UCHAR LegacyLogicalProcessorsPerCore;   /* 0x767 */
    UINT64 AcpiReserved;                    /* 0x768 */
    USHORT ProcessorModel;                  /* 0x770 (= CpuModel alias) */
    USHORT ProcessorRevision;               /* 0x772 (= CpuStepping alias) */
    ULONG MHz;                              /* 0x774 */
    UINT64 CycleCounterFrequency;           /* 0x778 */

    /* 0x780-0x7ff */
    UINT64 GroupSetMember;                  /* 0x780 */
    UCHAR Group;                            /* 0x788 */
    UCHAR GroupIndex;                       /* 0x789 */
    UCHAR QpcToTscIncrementShift;           /* 0x78a */
    UCHAR PrcbPad3[5];                      /* 0x78b..0x78f -- pad to ULONG align */
    ULONG CoresPerPhysicalProcessor;        /* 0x790 */
    ULONG LogicalProcessorsPerCore;         /* 0x794 */
    UINT64 QpcToTscIncrement;               /* 0x798 */
    UCHAR PrcbPad4[0x60];                   /* 0x7a0..0x7ff */

    KSPIN_LOCK_QUEUE LockQueue[17];         /* 0x800..0x90f */
    UCHAR ProcessorVendorString[2];         /* 0x910 */
    USHORT PrcbPad5;                        /* 0x912 */
    ULONG FeatureBits;                      /* 0x914 */
    ULONG MaxBreakpoints;                   /* 0x918 */
    ULONG MaxWatchpoints;                   /* 0x91c */
    PVOID Context;                          /* 0x920 */
    ULONG ContextFlagsInit;                 /* 0x928 */
    ULONG EmulatedAccess;                   /* 0x92c */
    UINT64 EmulatedFaultSyndrome;           /* 0x930 */
    UINT64 EmulatedFaultAddress;            /* 0x938 */
    UINT64 EmulatedLoadStoreAcquireRelease; /* 0x940 */
    UINT64 EmulatedMisalignedAtomics;       /* 0x948 */
    ULONG EmulatedCoalesceCount;            /* 0x950 */
    ULONG TrapFrameLogIndex;                /* 0x954 */
    UCHAR TrapFrameLog[40];                 /* 0x958..0x97f */
    PP_LOOKASIDE_LIST PPLookasideList[16];  /* 0x980..0xa7f (16 * 16 = 256) */

    /* 0xa80-0xb17 -- packet barrier / mm / ios */
    UINT64 PacketBarrier;                   /* 0xa80 */
    SINGLE_LIST_ENTRY DeferredReadyListHead;/* 0xa88 */
    LONG MmPageFaultCount;                  /* 0xa90 */
    LONG MmCopyOnWriteCount;                /* 0xa94 */
    LONG MmTransitionCount;                 /* 0xa98 */
    LONG MmDemandZeroCount;                 /* 0xa9c */
    LONG MmPageReadCount;                   /* 0xaa0 */
    LONG MmPageReadIoCount;                 /* 0xaa4 */
    LONG MmDirtyPagesWriteCount;            /* 0xaa8 */
    LONG MmDirtyWriteIoCount;               /* 0xaac */
    LONG MmMappedPagesWriteCount;           /* 0xab0 */
    LONG MmMappedWriteIoCount;              /* 0xab4 */
    ULONG KeSystemCalls;                    /* 0xab8 */
    ULONG KeContextSwitches;                /* 0xabc */
    ULONG CcFastReadNoWait;                 /* 0xac0 */
    ULONG CcFastReadWait;                   /* 0xac4 */
    ULONG CcFastReadNotPossible;            /* 0xac8 */
    ULONG CcCopyReadNoWait;                 /* 0xacc */
    ULONG CcCopyReadWait;                   /* 0xad0 */
    ULONG CcCopyReadNoWaitMiss;             /* 0xad4 */
    LONG LookasideIrpFloat;                 /* 0xad8 */
    LONG IoReadOperationCount;              /* 0xadc */
    LONG IoWriteOperationCount;             /* 0xae0 */
    LONG IoOtherOperationCount;             /* 0xae4 */
    LARGE_INTEGER IoReadTransferCount;      /* 0xae8 */
    LARGE_INTEGER IoWriteTransferCount;     /* 0xaf0 */
    LARGE_INTEGER IoOtherTransferCount;     /* 0xaf8 */
    UINT64 Mailbox;                         /* 0xb00 */
    ULONG TargetCount;                      /* 0xb08 */
    ULONG IpiFrozen;                        /* 0xb0c */
    UINT64 RequestSummary;                  /* 0xb10 */

    /* 0xb18 -- DpcData[2] (KDPC_DATA[2]; KDPC_DATA sizeof = 0x30 / 48) */
    KDPC_DATA DpcData[2];                   /* 0xb18..0xb77 */

    PVOID DpcStack;                         /* 0xb78 */
    PVOID SpBase;                           /* 0xb80 */
    ULONG MaximumDpcQueueDepth;             /* 0xb88 */
    ULONG DpcRequestRate;                   /* 0xb8c */
    ULONG MinimumDpcRate;                   /* 0xb90 */
    ULONG DpcLastCount;                     /* 0xb94 */
    UCHAR ThreadDpcEnable;                  /* 0xb98 */
    UCHAR QuantumEnd;                       /* 0xb99 */
    UCHAR DpcRoutineActive;                 /* 0xb9a */
    UCHAR IdleSchedule;                     /* 0xb9b */
    union                                   /* 0xb9c (DPC request aggregate) */
    {
        ULONG DpcRequestSummary;            /* 0xb9c: ULONG aggregate */
        UCHAR DpcRequestSlot;               /* 0xb9c byte 0 */
        struct
        {
            USHORT NormalDpcState;          /* 0xb9c */
            USHORT ThreadDpcState;          /* 0xb9e */
        };
        /*
         * Bitfield aliases share the ULONG aggregate at 0xb9c.
         *   DpcThreadActive    = bit 16 (= ThreadDpcState bit 0)
         *   DpcThreadRequested = bit 17 (= ThreadDpcState bit 1)
         */
        struct
        {
            ULONG DpcNormalProcessingActive    : 1; /* bit  0 */
            ULONG DpcNormalProcessingRequested : 1; /* bit  1 */
            ULONG DpcNormalThreadSignal        : 1; /* bit  2 */
            ULONG DpcNormalTimerExpiration     : 1; /* bit  3 */
            ULONG DpcNormalDpcPresent          : 1; /* bit  4 */
            ULONG DpcNormalLocalInterrupt      : 1; /* bit  5 */
            ULONG DpcNormalPriorityAntiStarv   : 1; /* bit  6 */
            ULONG DpcNormalSwapToDpcDelegate   : 1; /* bit  7 */
            ULONG DpcNormalSpare               : 8; /* bits 8..15 */
            ULONG DpcThreadActive              : 1; /* bit 16 */
            ULONG DpcThreadRequested           : 1; /* bit 17 */
            ULONG DpcThreadSpare               : 14;/* bits 18..31 */
        };
    };
    ULONG LastTick;                         /* 0xba0 */
    ULONG ClockInterrupts;                  /* 0xba4 */
    ULONG ReadyScanTick;                    /* 0xba8 */
    ULONG PrcbFlags;                        /* 0xbac */
    ULONG InterruptLastCount;               /* 0xbb0 */
    ULONG InterruptRate;                    /* 0xbb4 */
    ULONG SingleDpcSoftTimeLimitTicks;      /* 0xbb8 */
    ULONG CumulativeDpcSoftTimeLimitTicks;  /* 0xbbc */
    UCHAR SingleDpcSoftTimeoutEventInfo[64];/* 0xbc0..0xbff */

    /* 0xc00 -- DpcGate (KGATE; equivalent shape to KEVENT) */
    UCHAR DpcGate[24];                      /* 0xc00..0xc17 */

    UINT64 MPAffinity;                      /* 0xc18 */
    KDPC CallDpc;                           /* 0xc20..0xc5f */
    LONG ClockKeepAlive;                    /* 0xc60 */
    ULONG PrcbPad11;                        /* 0xc64 */
    ULONG DpcWatchdogPeriodTicks;           /* 0xc68 */
    ULONG DpcWatchdogCount;                 /* 0xc6c */
    ULONG DpcWatchdogSequenceNumber;        /* 0xc70 */
    ULONG KeSpinLockOrdering;               /* 0xc74 */
    UINT64 TrappedSecurityDomain;           /* 0xc78 */
    LIST_ENTRY WaitListHead;                /* 0xc80..0xc8f */
    UINT64 WaitLock;                        /* 0xc90 */
    ULONG ReadySummary;                     /* 0xc98 */
    ULONG AffinitizedSelectionMask;         /* 0xc9c */
    ULONG QueueIndex;                       /* 0xca0 */
    ULONG NormalPriorityQueueIndex;         /* 0xca4 */
    UINT64 NormalPriorityReadyScanTick;     /* 0xca8 */
    KDPC TimerExpirationDpc;                /* 0xcb0..0xcef */
    UCHAR ScbQueue[16];                     /* 0xcf0..0xcff */
    UCHAR ScbList[128];                     /* 0xd00..0xd7f */

    /* 0xd80 -- DispatcherReadyListHead[32] (32 * 16 = 512) */
    LIST_ENTRY DispatcherReadyListHead[32]; /* 0xd80..0xf7f */

    /* 0xf80-0xfff -- counters and DPC time */
    ULONG InterruptCount;                   /* 0xf80 */
    ULONG KernelTime;                       /* 0xf84 */
    ULONG UserTime;                         /* 0xf88 */
    ULONG DpcTime;                          /* 0xf8c */
    ULONG InterruptTime;                    /* 0xf90 */
    ULONG AdjustDpcThreshold;               /* 0xf94 */
    UCHAR SkipTick;                         /* 0xf98 */
    UCHAR DebuggerSavedIRQL;                /* 0xf99 */
    UCHAR TbFlushListActive;                /* 0xf9a */
    UCHAR GroupSchedulingOverQuota;         /* 0xf9b */
    ULONG DpcTimeCount;                     /* 0xf9c */
    ULONG DpcTimeLimitTicks;                /* 0xfa0 */
    ULONG PeriodicCount;                    /* 0xfa4 */
    ULONG PeriodicBias;                     /* 0xfa8 */
    ULONG AvailableTime;                    /* 0xfac */
    ULONG ScbOffset;                        /* 0xfb0 */
    ULONG KeExceptionDispatchCount;         /* 0xfb4 */
    PVOID SchedulerSubNode;                 /* 0xfb8 */
    UINT64 AffinitizedCycles;               /* 0xfc0 */
    UINT64 StartCycles;                     /* 0xfc8 */
    UINT64 TaggedCycles[4];                 /* 0xfd0..0xfef */
    UINT64 CpuCycleScalingFactor;           /* 0xff0 */
    UCHAR EntropyTimingState[0x1150 - 0xff8];/* 0xff8..0x114f */
    UCHAR CachedStacks[0x10];               /* 0x1150..0x115f */
    ULONG PageColor;                        /* 0x1160 */
    ULONG NodeColor;                        /* 0x1164 */
    UCHAR PrcbPad18[0x1170 - 0x1168];       /* 0x1168..0x116f */
    UINT64 CycleTime;                       /* 0x1170 */
    UINT64 Cycles[8];                       /* 0x1178..0x11b7 */
    UCHAR CyclesTailPad[0x1200 - 0x11b8];   /* 0x11b8..0x11ff */

    /* 0x1200..0x137f -- reserved */
    UCHAR PrcbReservedSymCryptEntropyAccumulatorState[0x1380 - 0x1200];

    /* 0x1380..0x13f3 -- Cc fast counters */
    ULONG CcFastMdlReadNoWait;              /* 0x1380 */
    ULONG CcFastMdlReadWait;                /* 0x1384 */
    ULONG CcFastMdlReadNotPossible;         /* 0x1388 */
    ULONG CcMapDataNoWait;                  /* 0x138c */
    ULONG CcMapDataWait;                    /* 0x1390 */
    ULONG CcPinMappedDataCount;             /* 0x1394 */
    ULONG CcPinReadNoWait;                  /* 0x1398 */
    ULONG CcPinReadWait;                    /* 0x139c */
    ULONG CcMdlReadNoWait;                  /* 0x13a0 */
    ULONG CcMdlReadWait;                    /* 0x13a4 */
    ULONG CcLazyWriteHotSpots;              /* 0x13a8 */
    ULONG CcLazyWriteIos;                   /* 0x13ac */
    ULONG CcLazyWritePages;                 /* 0x13b0 */
    ULONG CcDataFlushes;                    /* 0x13b4 */
    ULONG CcDataPages;                      /* 0x13b8 */
    ULONG CcLostDelayedWrites;              /* 0x13bc */
    ULONG CcFastReadResourceMiss;           /* 0x13c0 */
    ULONG CcCopyReadWaitMiss;               /* 0x13c4 */
    ULONG CcFastMdlReadResourceMiss;        /* 0x13c8 */
    ULONG CcMapDataNoWaitMiss;              /* 0x13cc */
    ULONG CcMapDataWaitMiss;                /* 0x13d0 */
    ULONG CcPinReadNoWaitMiss;              /* 0x13d4 */
    ULONG CcPinReadWaitMiss;                /* 0x13d8 */
    ULONG CcMdlReadNoWaitMiss;              /* 0x13dc */
    ULONG CcMdlReadWaitMiss;                /* 0x13e0 */
    ULONG CcReadAheadIos;                   /* 0x13e4 */
    LONG MmCacheTransitionCount;            /* 0x13e8 */
    LONG MmCacheReadCount;                  /* 0x13ec */
    LONG MmCacheIoCount;                    /* 0x13f0 */
    UCHAR Pad1400[0x1400 - 0x13f4];         /* 0x13f4..0x13ff */
    /*
     * 0x1400..0x15f7 -- per-CPU power state.
     * ReactOS-side PROCESSOR_POWER_STATE (ndk/potypes.h) is smaller than the
     * ARM64 reserves 504 bytes here; pad to keep the trailing fields at the
     * required offsets. po/power.c accesses .IdleFunction,
     * .Idle0KernelTimeLimit, .CurrentThrottle, .CurrentThrottleIndex,
     * .PerfDpc, .PerfTimer; all of those live in the ROS struct.
     */
    union
    {
        PROCESSOR_POWER_STATE PowerState;   /* 0x1400 (typed for ROS callers) */
        UCHAR PowerStateBytes[0x15f8 - 0x1400];
    };

    /* 0x15f8..0x1677 -- DPC tracking */
    KDPC ForceIdleDpc;                                  /* 0x15f8..0x1637 */
    PVOID DpcRuntimeHistoryHashTable;                   /* 0x1638 */
    struct _KDPC *DpcRuntimeHistoryHashTableCleanupDpc; /* 0x1640 */
    PVOID CurrentDpcRoutine;                            /* 0x1648 */
    UINT64 CurrentDpcRuntimeHistoryCached;              /* 0x1650 */
    UINT64 CurrentDpcStartTime;                         /* 0x1658 */
    struct _KTHREAD *DpcDelegateThread;                 /* 0x1660 */
    ULONG DeviceInterrupts;                             /* 0x1668 */
    UCHAR DpcAreaTailPad[0x1670 - 0x166c];              /* 0x166c..0x166f */
    UINT64 IsrDpcStats;                                 /* 0x1670 */
    ULONG KeAlignmentFixupCount;            /* 0x1678 */
    UCHAR CycleAccumulationInitialized;     /* 0x167c */
    UCHAR PrcbPad21[3];                     /* 0x167d..0x167f */
    UINT64 MmSpinLockOrdering;              /* 0x1680 */

    /* 0x1688..0x16e0 -- watchdog + cycle qpc + secure-fault counters */
    KDPC DpcWatchdogDpc;                                /* 0x1688..0x16c7 */
    UINT64 StartCyclesQpc;                              /* 0x16c8 */
    UINT64 CycleTimeQpc;                                /* 0x16d0 */
    UINT64 NumberOfSecureFaults;                        /* 0x16d8 */
    UCHAR PrcbPad22[0x1710 - 0x16e0];                   /* 0x16e0..0x170f */
    UCHAR InterruptObjectPool[0x10];                    /* 0x1710..0x171f (SLIST_HEADER) */
    UCHAR PackageProcessorSet[0x108];                   /* 0x1720..0x1827 (KAFFINITY_EX, fwd-decl only) */
    union
    {
        ULONG TopologyId;                               /* 0x1828 */
        ULONG ProcessorId;                              /* 0x1828 */
    };
    ULONG CoreId;                                       /* 0x182c */
    ULONG ModuleId;                                     /* 0x1830 */
    ULONG DieId;                                        /* 0x1834 */
    ULONG PackageId;                                    /* 0x1838 */
    UCHAR PrcbPad25[0x1900 - 0x183c];                   /* 0x183c..0x18ff */
    UINT64 SharedReadyQueueMask;                        /* 0x1900 */
    PVOID SharedReadyQueue;                             /* 0x1908 */
    ULONG SharedQueueScanOwner;                         /* 0x1910 */
    ULONG ScanSiblingIndex;                             /* 0x1914 */
    PVOID CoreControlBlock;                             /* 0x1918 */
    UINT64 CoreProcessorSet;                            /* 0x1920 */
    UINT64 ScanSiblingMask;                             /* 0x1928 */
    UINT64 LLCMask;                                     /* 0x1930 */
    UINT64 GroupModuleProcessorSet;                     /* 0x1938 */
    UCHAR PrcbPad19[0x1960 - 0x1940];                   /* 0x1940..0x195f */
    PVOID SmtIsolationThread;                           /* 0x1960 */

    /* 0x1968..0x19af -- Cache descriptors */
    CACHE_DESCRIPTOR Cache[6];              /* 0x1968..0x19af */
    UCHAR CacheCount;                       /* 0x19b0 */
    UCHAR PrcbPad20;                        /* 0x19b1 */
    UCHAR SystemWorkKickInProgress;         /* 0x19b2 */
    UCHAR ExceptionStackActive;             /* 0x19b3 */
    ULONG CachedCommit;                     /* 0x19b4 */
    ULONG CachedResidentAvailable;          /* 0x19b8 */
    UCHAR Pad19BC[4];                       /* 0x19bc..0x19bf */
    UINT64 MmFaultCompletionInfo;           /* 0x19c0 */
    UINT64 MmInternal;                      /* 0x19c8 */
    UINT64 GenerationTarget;                /* 0x19d0 */
    UINT64 PrcbPad24;                       /* 0x19d8 */
    UINT64 VmInternal;                      /* 0x19e0 */
    UINT64 DpcLog;                          /* 0x19e8 */
    ULONG DpcLogIndex;                      /* 0x19f0 */
    ULONG DpcLogBufferSize;                 /* 0x19f4 */
    UINT64 ExceptionStack;                  /* 0x19f8 */
    PVOID WheaInfo;                         /* 0x1a00 */
    PVOID EtwSupport;                       /* 0x1a08 */
    UCHAR HypercallPageList[0x10];          /* 0x1a10 (SLIST_HEADER) */
    PVOID HypercallCachedPages;             /* 0x1a20 */
    PVOID VirtualApicAssist;                /* 0x1a28 */
    PVOID VirtualApicAssistPage;            /* 0x1a30 */
    UCHAR StatisticsPage[0x48];             /* 0x1a38..0x1a7f */
    UCHAR SynchCounters[0xb8];              /* 0x1a80..0x1b37 */

    /*
     * Private ARM64 PRCB area. Keep ReactOS-consumed members typed and hold
     * unused private slots as reserved bytes so later offsets stay put.
     */
    UCHAR PrcbReservedPteBitCache[8];       /* 0x1b38 */
    UCHAR PrcbReservedPteBitOffset[4];      /* 0x1b40 */
    UCHAR PrcbReservedPteBitOffsetPad[4];   /* 0x1b44 */
    UCHAR PrcbReservedFsCounters[0x10];     /* 0x1b48 */
    UINT64 PanicStackBase;                  /* 0x1b58 */
    UCHAR IsrStack[0x20];                   /* 0x1b60..0x1b7f */
    UCHAR TimerTable[0x4280];               /* 0x1b80..0x5dff (KTIMER_TABLE) */
    /* Use struct tag to bypass the NTDDI_VERSION < NTDDI_LONGHORN
     * `#define GENERAL_LOOKASIDE_POOL PP_LOOKASIDE_LIST` shim, which would
     * shrink each slot to 16 bytes and corrupt the ARM64 layout. The ARM64
     * KPRCB carries the full 96-byte per-slot pool, regardless of the
     * NTDDI level the translation unit advertises. */
    struct _GENERAL_LOOKASIDE_POOL PPNxPagedLookasideList[NUMBER_POOL_LOOKASIDE_LISTS]; /* 0x5e00..0x69ff */
    struct _GENERAL_LOOKASIDE_POOL PPNPagedLookasideList[NUMBER_POOL_LOOKASIDE_LISTS];  /* 0x6a00..0x75ff */
    struct _GENERAL_LOOKASIDE_POOL PPPagedLookasideList[NUMBER_POOL_LOOKASIDE_LISTS];   /* 0x7600..0x81ff */
    UCHAR PrcbReservedAbSelfIoBoostsList[0x8];          /* 0x8200 */
    UCHAR PrcbReservedAbPropagateBoostsList[0x8];       /* 0x8208 */
    UCHAR PrcbReservedAbDpc[0x40];                      /* 0x8210 */
    UCHAR PrcbReservedIoIrpStackProfilerCurrent[0x54];  /* 0x8250 */
    UCHAR PrcbReservedIoIrpStackProfilerPrevious[0x5c]; /* 0x82a4 */
    UCHAR LocalSharedReadyQueue[0x8];       /* 0x8300 */
    UCHAR PrcbReservedTimerExpirationTrace[0x100];      /* 0x8308 */
    UCHAR PrcbReservedTimerExpirationTraceCount[4];     /* 0x8408 */
    UCHAR PrcbReservedTimerExpirationTraceCountPad[0x74]; /* 0x840c */
    UCHAR PrcbReservedClockTimerState[0x518];           /* 0x8480 */
    UCHAR PrcbReservedExSaPageArray[0x8];               /* 0x8998 */
    UCHAR PrcbReservedTracepointLog[0x8];               /* 0x89a0 */
    UCHAR PrcbReservedRcuData[0x20];                    /* 0x89a8 */
    LONG FreezePowerOff;                    /* 0x89c8 */
    UCHAR PrcbReservedDpcWatchdogProfileBufferSize[4]; /* 0x89cc */
    UCHAR StaticAffinity[0x820];            /* 0x89d0..0x91ef */
    UCHAR DeferredDispatchInterrupts[0x210]; /* 0x91f0..0x93ff */
    UCHAR StaticRescheduleContext[0x8];     /* 0x9400..0x9407 */
    UCHAR SecureFault[0x18];                /* 0x9408..0x941f */
    PVOID CyclesByThreadType;               /* 0x9420 */
    ULONG ReadyThreadCount;                 /* 0x9428 */
    UCHAR ReadyThreadCountPad[4];           /* 0x942c..0x942f */
    UINT64 ReadyQueueExpectedRunTime;       /* 0x9430 */
    UCHAR PrcbReservedForceParkDutyCycleData[0x8];      /* 0x9438 */
    UCHAR CpuPartition[0x8];                /* 0x9440..0x9447 */
    UCHAR PrcbReservedDpcWatchdogProfileCumulativeDpcThresholdTicks[4]; /* 0x9448 */
    UCHAR PrcbReservedDpcWatchdogProfileSingleDpcThresholdTicks[4];     /* 0x944c */
    UCHAR PrcbReservedDpcWatchdogProfile[0x8];          /* 0x9450 */
    UCHAR PrcbReservedDpcWatchdogProfileCurrentEmptyCapture[0x8]; /* 0x9458 */
    UCHAR SchedulerAssist[0x8];             /* 0x9460..0x9467 */
    UCHAR PrcbReservedSelfmapLockHandle[0x60];          /* 0x9468 */
    UCHAR CacheProcessorSet[0x630];         /* 0x94c8..0x9af7 */
    UCHAR ModuleProcessorSet[0x108];        /* 0x9af8..0x9bff */
    UCHAR DieProcessorSet[0x108];           /* 0x9c00..0x9d07 */
    UCHAR LocalCoreControlBlock[0x30];      /* 0x9d08..0x9d37 */
    ULONG CoreControlBlockIndex;            /* 0x9d38 */
    ULONG PrcbPad26;                        /* 0x9d3c */
    UCHAR CoreControlBlockShadow[0x8];      /* 0x9d40..0x9d47 */
    UCHAR LocalCoreControlBlockShadow[0x40]; /* 0x9d48..0x9d87 */
    UCHAR NodeRelativeTopologyIndex[0x18];  /* 0x9d88 */
    UCHAR KstackFreeDpc[0x40];              /* 0x9da0..0x9ddf (KDPC) */
    UCHAR KstackFreeList[0x10];             /* 0x9de0..0x9def (SLIST_HEADER) */
    UCHAR IpiFrame[0x8];                    /* 0x9df0..0x9df7 */
    UCHAR SlistRollbackDpc[0x40];           /* 0x9df8..0x9e37 (KDPC) */
    UCHAR PrcbReservedLocalSearchContexts[0x10];        /* 0x9e38 */
    UCHAR PrcbReservedSearchContexts[0x10];             /* 0x9e48 */
    UCHAR PrcbReservedSearchGenerations[0x28];          /* 0x9e58 */
    REQUEST_MAILBOX RequestMailbox;         /* 0x9e80..0x9ebf */
    UCHAR RequestMailboxPad[0x40];          /* 0x9ec0..0x9eff */
} KPRCB, *PKPRCB;

/* Compile-time check for the ARM64 KPRCB size. */
_Static_assert(sizeof(KPRCB) == 0x9f00, "KPRCB sizeof drift");

//
// Processor Control Region
// Based on WoA
//
/*
 * ARM64 KIPCR layout (sizeof 0xa880).
 *
 * The leading region is the 56-byte NT_TIB, aliased over the named scalar
 * fields below. The current PRCB is the inline Prcb member at offset 0x980.
 */
typedef struct _KIPCR
{
    union
    {
        NT_TIB NtTib;
        struct
        {
            PVOID TibPad0[2];                       /* 0x000: ExceptionList, StackBase */
            PVOID Spare1;                           /* 0x010: StackLimit */
            struct _KPCR *Self;                     /* 0x018: SubSystemTib */
            ULONG64 PcrReserved0;                   /* 0x020 */
            struct _KSPIN_LOCK_QUEUE* LockArray;    /* 0x028: ArbitraryUserPointer */
            PVOID Used_Self;                        /* 0x030: NT_TIB.Self */
        };
    };
    KIRQL CurrentIrql;                              /* 0x038 */
    UCHAR SecondLevelCacheAssociativity;            /* 0x039 */
    UCHAR Pad1[2];                                  /* 0x03a */
    USHORT MajorVersion;                            /* 0x03c */
    USHORT MinorVersion;                            /* 0x03e */
    ULONG StallScaleFactor;                         /* 0x040 */
    ULONG SecondLevelCacheSize;                     /* 0x044 */
    union
    {
        USHORT SoftwareInterruptPending;            /* 0x048 */
        struct
        {
            UCHAR ApcInterrupt;                     /* 0x048 */
            UCHAR DispatchInterrupt;                /* 0x049 */
        };
    };
    USHORT InterruptPad;                            /* 0x04a */
    /* 0x04c -- BtiMitigation byte and bitfield aliases. */
    union
    {
        UCHAR BtiMitigation;                        /* 0x04c primary byte */
        struct
        {
            UCHAR KvaVbar:1;                        /* 0x04c bit 0 */
            UCHAR BtiVbar:1;                        /* 0x04c bit 1 */
            UCHAR BtiCswapHvc:1;                    /* 0x04c bit 2 */
            UCHAR BtiCswapSmc:1;                    /* 0x04c bit 3 */
            UCHAR BtiReserved:4;                    /* 0x04c bits 4..7 */
        };
    };
    /* 0x04d -- SsbMitigationFlags + bitfield aliases. */
    union
    {
        UCHAR SsbMitigationFlags;                   /* 0x04d primary byte */
        struct
        {
            UCHAR SsbMitigationFirmware:1;          /* 0x04d bit 0 */
            UCHAR SsbMitigationDynamic:1;           /* 0x04d bit 1 */
            UCHAR SsbMitigationKernel:1;            /* 0x04d bit 2 */
            UCHAR SsbMitigationUser:1;              /* 0x04d bit 3 */
            UCHAR SsbMitigationReserved:4;          /* 0x04d bits 4..7 */
        };
    };
    UCHAR BhbMitigation;                            /* 0x04e */
    /* 0x04f -- CachePrefetcherMitigationFlags + bitfield aliases. */
    union
    {
        UCHAR CachePrefetcherMitigationFlags;       /* 0x04f primary byte */
        struct
        {
            UCHAR CachePrefetcherMitigation:1;      /* 0x04f bit 0 */
            UCHAR Pad2:7;                           /* 0x04f bits 1..7 */
        };
    };
    ULONG64 PanicStorage[6];                        /* 0x050..0x07f */
    PVOID KdVersionBlock;                           /* 0x080 */
    PVOID HalReserved[14];                          /* 0x088..0x0f7 (14 * 8 = 112) */
    PVOID KvaUserModeTtbr1;                         /* 0x0f8 */

    /* Private members, not in ntddk.h */
    PVOID Idt[256];                                 /* 0x100..0x8ff (256 * 8 = 2048) */
    PVOID *IdtExt;                                  /* 0x900 */
    PVOID PcrAlign[15];                             /* 0x908..0x97f (15 * 8 = 120) */
    KPRCB Prcb;                                     /* 0x980 */
} KIPCR, *PKIPCR;

/* ARM64 KIPCR size: 0xa880 = 43136. */
typedef char _kipcr_size_dbg[sizeof(KIPCR)];

extern NTKERNELAPI PKIPCR KeArm64CurrentPcr;
extern NTKERNELAPI PKTHREAD KeArm64CurrentThread;
extern NTKERNELAPI KIRQL KeArm64CurrentIrql;
extern NTKERNELAPI BOOLEAN KeArm64DpcRoutineActive;

/*
 * ARM64: Use TPIDR_EL1 for per-CPU PCR access (ARM64 ABI convention).
 *
 * TPIDR_EL1 is the ARM64 standard system register for kernel per-CPU data.
 * Each CPU has its own TPIDR_EL1 value, making it SMP-safe.
 *
 * Early boot fallback: Before TPIDR_EL1 is initialized, KeGetPcr() may read
 * NULL. Kernel builds fall back to KeArm64CurrentPcr in that case.
 * _NTOSKRNL_EARLY_INIT_ can still be used to force the fallback path.
 */
#if defined(_M_ARM64) && !defined(_NTOSKRNL_EARLY_INIT_)
/* Runtime path: Read PCR from TPIDR_EL1 (per-CPU, SMP-safe) */
static __inline PKIPCR KeGetPcr(VOID)
{
    PKIPCR Pcr;
    __asm__ __volatile__("mrs %0, tpidr_el1" : "=r"(Pcr));
    return Pcr;
}
#else
/* Early boot path: Use global variable (before TPIDR_EL1 init) */
#define KeGetPcr() (KeArm64CurrentPcr)
#endif

/*
 * ARM64: KeGetCurrentPrcb must cache KeGetPcr() result to avoid calling it 4 times.
 * Reading TPIDR_EL1 is cheap but we should still avoid redundant system register reads.
 */
static __inline PKPRCB KeGetCurrentPrcb(VOID)
{
    PKIPCR Pcr = KeGetPcr();
    if (Pcr == NULL)
    {
#if defined(_NTOSKRNL_)
        return KeArm64CurrentPcr ? &KeArm64CurrentPcr->Prcb : NULL;
#else
        return NULL;
#endif
    }

    /*
     * The ARM64 bring-up uses the embedded PRCB in KPCR.  Do not trust
     * KPCR.CurrentPrcb here: stale loader PCR state or early memory scribbles
     * can redirect queued-spinlock users into an unmapped direct-map address.
     */
    return &Pcr->Prcb;
}
#define PRIMARY_VECTOR_BASE            0x00

//
// Special Registers Structure (outside of CONTEXT)
// Based on WoA symbols
//
#ifdef _NTOSKRNL_
/*
 * ARM64 SMP: KeGetCurrentIrql must read from per-CPU PCR, not the global.
 * IRQL is a per-CPU value. Using a single global works for UP but is
 * fundamentally broken for SMP: CPU 0 can overwrite IRQL while CPU 1
 * is in a critical section, causing spurious assertion failures and
 * incorrect scheduler behavior.
 *
 * KeGetPcr() reads TPIDR_EL1 (per-CPU system register), giving each
 * CPU its own IRQL. Fallback to the global for very early boot before
 * TPIDR_EL1 is initialized.
 */
#define KeGetCurrentIrql() \
    __extension__ ({ \
        PKIPCR _irql_pcr = KeGetPcr(); \
        (_irql_pcr != NULL) ? _irql_pcr->CurrentIrql : KeArm64CurrentIrql; \
    })
#endif
/*
 * ARM64 SMP: _KeGetCurrentThread must read from per-CPU PRCB, not the global.
 * The global KeArm64CurrentThread is written by ALL CPUs during context switch,
 * so reading it on SMP returns the wrong thread for all but the last writer.
 * Prcb->CurrentThread is always correct for the current CPU.
 */
#define _KeGetCurrentThread() \
    __extension__ ({ \
        PKIPCR _ct_pcr = KeGetPcr(); \
        PKTHREAD _ct_thread = (_ct_pcr != NULL) ? _ct_pcr->Prcb.CurrentThread : NULL; \
        (_ct_thread != NULL) ? _ct_thread : KeArm64CurrentThread; \
    })
#define _KeGetPreviousMode() \
    __extension__ ({ \
        PKTHREAD _pm_t = _KeGetCurrentThread(); \
        (_pm_t != NULL) ? _pm_t->PreviousMode : KernelMode; \
    })
#define _KeIsExecutingDpc() \
    __extension__ ({ \
        PKIPCR _dpc_pcr = KeGetPcr(); \
        (_dpc_pcr != NULL) ? _dpc_pcr->Prcb.DpcRoutineActive : KeArm64DpcRoutineActive; \
    })


#ifdef __cplusplus
}; // extern "C"
#endif

#endif // !_ARM64_KETYPES_H
