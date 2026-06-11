

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
#define SYNCH_LEVEL             DISPATCH_LEVEL

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

// Win11 26100 arm64 layout, sizeof 0x210
typedef struct _KARM64_VFP_STATE
{
    struct _KARM64_VFP_STATE *Link;      // 0x000
    ULONG Fpcr;                          // 0x008
    ULONG Fpsr;                          // 0x00C
    ARM64_NT_NEON128 V[32];              // 0x010
} KARM64_VFP_STATE, *PKARM64_VFP_STATE;

// Win11 26100 arm64 layout, sizeof 0x150
// Win11 unions PreviousMode/PreviousIrql at 0x003; ReactOS needs both live,
// so PreviousIrql sits in the low byte of Reserved (SVC cookie uses bits 8-31)
typedef struct _KTRAP_FRAME
{
    UCHAR ExceptionActive;               // 0x000
    UCHAR ContextFromKFramesUnwound;     // 0x001
    UCHAR DebugRegistersValid;           // 0x002
    CHAR PreviousMode;                   // 0x003
    union
    {
        ULONG Reserved;                  // 0x004
        struct
        {
            UCHAR PreviousIrql;          // 0x004 [ReactOS]
            UCHAR ReservedBytes[3];      // 0x005
        };
    };
    union
    {
        ULONG64 FaultAddress;            // 0x008
        ULONG64 TrapFrame;               // 0x008
    };
    PKARM64_VFP_STATE VfpState;          // 0x010
    ULONG Bcr[8];                        // 0x018
    ULONG64 Bvr[8];                      // 0x038
    ULONG Wcr[2];                        // 0x078
    ULONG64 Wvr[2];                      // 0x080
    ULONG Spsr;                          // 0x090
    ULONG Esr;                           // 0x094
    ULONG64 Sp;                          // 0x098
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
    ULONG64 Lr;                          // 0x138
    ULONG64 Fp;                          // 0x140
    ULONG64 Pc;                          // 0x148
} KTRAP_FRAME, *PKTRAP_FRAME;            // sizeof 0x150

#ifndef __ASSEMBLER__
C_ASSERT(sizeof(KTRAP_FRAME) == 0x150);
C_ASSERT(sizeof(KARM64_VFP_STATE) == 0x210);
C_ASSERT(FIELD_OFFSET(KTRAP_FRAME, VfpState) == 0x010);
C_ASSERT(FIELD_OFFSET(KTRAP_FRAME, Bcr) == 0x018);
C_ASSERT(FIELD_OFFSET(KTRAP_FRAME, Bvr) == 0x038);
C_ASSERT(FIELD_OFFSET(KTRAP_FRAME, Wcr) == 0x078);
C_ASSERT(FIELD_OFFSET(KTRAP_FRAME, Wvr) == 0x080);
C_ASSERT(FIELD_OFFSET(KTRAP_FRAME, Spsr) == 0x090);
C_ASSERT(FIELD_OFFSET(KTRAP_FRAME, Esr) == 0x094);
C_ASSERT(FIELD_OFFSET(KTRAP_FRAME, Sp) == 0x098);
C_ASSERT(FIELD_OFFSET(KTRAP_FRAME, X) == 0x0A0);
C_ASSERT(FIELD_OFFSET(KTRAP_FRAME, Lr) == 0x138);
C_ASSERT(FIELD_OFFSET(KTRAP_FRAME, Fp) == 0x140);
C_ASSERT(FIELD_OFFSET(KTRAP_FRAME, Pc) == 0x148);
#endif

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
    ULONG64 Midr_El1;
    ULONG64 Sctlr_El1;
    ULONG64 Actlr_El1;
    ULONG64 Cpacr_El1;
    ULONG64 Tcr_El1;
    ULONG64 Ttbr0_El1;
    ULONG64 Ttbr1_El1;
    ULONG64 Esr_El1;
    ULONG64 Far_El1;
    ULONG64 Pmcr_El0;
    ULONG64 Pmcntenset_El0;
    ULONG64 Pmccntr_El0;
    ULONG64 Pmxevcntr_El0[31];
    ULONG64 Pmxevtyper_El0[31];
    ULONG64 Pmovsclr_El0;
    ULONG64 Pmselr_El0;
    ULONG64 Pmuserenr_El0;
    ULONG64 Mair_El1;
    ULONG64 Vbar_El1;
    ULONG64 APIBKeyHi_El1;
    ULONG64 APIBKeyLo_El1;
    ULONG64 Mpam0_El1;
    ULONG64 Zcr_El1;
    ULONG64 Padding;
} KARM64_ARCH_STATE, *PKARM64_ARCH_STATE;

typedef struct _KPROCESSOR_STATE
{
    KSPECIAL_REGISTERS SpecialRegisters; // 0x000
    KARM64_ARCH_STATE ArchState;         // 0x0A0
    CONTEXT ContextFrame;                // 0x340
} KPROCESSOR_STATE, *PKPROCESSOR_STATE;   // sizeof 0x6D0 (Win11 26100 arm64)

typedef struct _KREQUEST_PACKET
{
    PVOID CurrentPacket[3];
    PVOID WorkerRoutine;
} KREQUEST_PACKET, *PKREQUEST_PACKET;

// Win11 26100 arm64 layout, sizeof 0x40
typedef struct _REQUEST_MAILBOX
{
    struct _REQUEST_MAILBOX *Next;       // 0x00
    INT64 RequestSummary;                // 0x08
    KREQUEST_PACKET RequestPacket;       // 0x10
    volatile LONG *SubNodeTargetCountAddr; // 0x30
    volatile LONG SubNodeTargetCount;    // 0x38
} REQUEST_MAILBOX, *PREQUEST_MAILBOX;

#if (NTDDI_VERSION < NTDDI_LONGHORN)
#define GENERAL_LOOKASIDE_POOL PP_LOOKASIDE_LIST
#endif

//
// Processor Control Block, Win11 26100 arm64 layout (ntkrnlmp.pdb 10.0.26100.8036)
// sizeof == 0x9F00; members marked [ReactOS] live inside Win11 pad regions
//
typedef struct _KPRCB
{
    UCHAR LegacyNumber;                                  // 0x000
    UCHAR ReservedMustBeZero;                            // 0x001
    UCHAR IdleHalt;                                      // 0x002
    UCHAR PrcbPad00[5];                                  // 0x003
    struct _KTHREAD *CurrentThread;                      // 0x008
    struct _KTHREAD *NextThread;                         // 0x010
    struct _KTHREAD *IdleThread;                         // 0x018
    UCHAR NestingLevel;                                  // 0x020
    UCHAR ClockOwner;                                    // 0x021
    union
    {
        UCHAR PendingTickFlags;                          // 0x022
        struct
        {
            UCHAR PendingTick:1;
            UCHAR PendingBackupTick:1;
        };
    };
    UCHAR IdleState;                                     // 0x023
    ULONG Number;                                        // 0x024
    ULONG64 PrcbLock;                                    // 0x028
    PVOID PriorityState;                                 // 0x030 PKPRIORITY_STATE
    ULONG64 PrcbPad01;                                   // 0x038
    KPROCESSOR_STATE ProcessorState;                     // 0x040
    ULONG64 HalReserved[10];                             // 0x710
    USHORT MinorVersion;                                 // 0x760
    USHORT MajorVersion;                                 // 0x762
    UCHAR BuildType;                                     // 0x764
    UCHAR CpuVendor;                                     // 0x765
    UCHAR LegacyCoresPerPhysicalProcessor;               // 0x766
    UCHAR LegacyLogicalProcessorsPerCore;                // 0x767
    PVOID AcpiReserved;                                  // 0x768
    USHORT ProcessorModel;                               // 0x770
    USHORT ProcessorRevision;                            // 0x772
    ULONG MHz;                                           // 0x774
    ULONG64 CycleCounterFrequency;                       // 0x778
    union
    {
        ULONG64 GroupSetMember;                          // 0x780
        ULONG64 SetMember;                               // [ReactOS] legacy alias
    };
    UCHAR Group;                                         // 0x788
    UCHAR GroupIndex;                                    // 0x789
    UCHAR QpcToTscIncrementShift;                        // 0x78A
    UCHAR PrcbPad3[5];                                   // 0x78B
    ULONG CoresPerPhysicalProcessor;                     // 0x790
    ULONG LogicalProcessorsPerCore;                      // 0x794
    ULONG64 QpcToTscIncrement;                           // 0x798
    // Win11 PrcbPad4[12] region: ReactOS legacy scheduler/DPC state
    struct _KNODE *ParentNode;                           // 0x7A0 [ReactOS]
    ULONG64 MultiThreadProcessorSet;                     // 0x7A8 [ReactOS]
    struct _KPRCB *MultiThreadSetMaster;                 // 0x7B0 [ReactOS]
    ULONG64 TimerRequest;                                // 0x7B8 [ReactOS]
    ULONG64 TimerHand;                                   // 0x7C0 [ReactOS]
    volatile ULONG64 TargetSet;                          // 0x7C8 [ReactOS]
    volatile LONG DpcSetEventRequest;                    // 0x7D0 [ReactOS]
    LONG Sleeping;                                       // 0x7D4 [ReactOS]
    volatile UCHAR DpcInterruptRequested;                // 0x7D8 [ReactOS]
    volatile UCHAR DpcThreadRequested;                   // 0x7D9 [ReactOS]
    volatile UCHAR DpcThreadActive;                      // 0x7DA [ReactOS]
    UCHAR PrcbPad4a[5];                                  // 0x7DB
    ULONG64 PrcbPad4[4];                                 // 0x7E0
    KSPIN_LOCK_QUEUE LockQueue[17];                      // 0x800 Win11 LockQueueMaximumLock == 17
    UCHAR ProcessorVendorString[2];                      // 0x910
    UCHAR PrcbPad5[2];                                   // 0x912
    ULONG FeatureBits;                                   // 0x914
    ULONG MaxBreakpoints;                                // 0x918
    ULONG MaxWatchpoints;                                // 0x91C
    PCONTEXT Context;                                    // 0x920
    ULONG ContextFlagsInit;                              // 0x928
    UCHAR EmulatedAccess;                                // 0x92C
    UCHAR PrcbPad92D[3];                                 // 0x92D
    ULONG EmulatedFaultSyndrome;                         // 0x930
    ULONG PrcbPad934;                                    // 0x934
    ULONG64 EmulatedFaultAddress;                        // 0x938
    ULONG64 EmulatedLoadStoreAcquireRelease;             // 0x940
    ULONG64 EmulatedMisalignedAtomics;                   // 0x948
    volatile LONG EmulatedCoalesceCount;                 // 0x950
    volatile ULONG TrapFrameLogIndex;                    // 0x954
    PTRAPFRAME_LOG_ENTRY TrapFrameLog;                   // 0x958
    ULONG64 PrcbPad960[4];                               // 0x960
    PP_LOOKASIDE_LIST PPLookasideList[16];               // 0x980
    volatile LONG PacketBarrier;                         // 0xA80
    ULONG PrcbPadA84;                                    // 0xA84
    SINGLE_LIST_ENTRY DeferredReadyListHead;             // 0xA88
    volatile LONG MmPageFaultCount;                      // 0xA90
    volatile LONG MmCopyOnWriteCount;                    // 0xA94
    volatile LONG MmTransitionCount;                     // 0xA98
    volatile LONG MmDemandZeroCount;                     // 0xA9C
    volatile LONG MmPageReadCount;                       // 0xAA0
    volatile LONG MmPageReadIoCount;                     // 0xAA4
    volatile LONG MmDirtyPagesWriteCount;                // 0xAA8
    volatile LONG MmDirtyWriteIoCount;                   // 0xAAC
    volatile LONG MmMappedPagesWriteCount;               // 0xAB0
    volatile LONG MmMappedWriteIoCount;                  // 0xAB4
    ULONG KeSystemCalls;                                 // 0xAB8
    ULONG KeContextSwitches;                             // 0xABC
    ULONG CcFastReadNoWait;                              // 0xAC0
    ULONG CcFastReadWait;                                // 0xAC4
    ULONG CcFastReadNotPossible;                         // 0xAC8
    ULONG CcCopyReadNoWait;                              // 0xACC
    ULONG CcCopyReadWait;                                // 0xAD0
    ULONG CcCopyReadNoWaitMiss;                          // 0xAD4
    LONG LookasideIrpFloat;                              // 0xAD8
    volatile LONG IoReadOperationCount;                  // 0xADC
    volatile LONG IoWriteOperationCount;                 // 0xAE0
    volatile LONG IoOtherOperationCount;                 // 0xAE4
    LARGE_INTEGER IoReadTransferCount;                   // 0xAE8
    LARGE_INTEGER IoWriteTransferCount;                  // 0xAF0
    LARGE_INTEGER IoOtherTransferCount;                  // 0xAF8
    struct _REQUEST_MAILBOX *Mailbox;                    // 0xB00
    volatile LONG TargetCount;                           // 0xB08
    volatile ULONG IpiFrozen;                            // 0xB0C
    volatile ULONG RequestSummary;                       // 0xB10
    ULONG PrcbPadB14;                                    // 0xB14
    KDPC_DATA DpcData[2];                                // 0xB18 KDPC_DATA is 0x30 on arm64
    PVOID DpcStack;                                      // 0xB78
    PVOID SpBase;                                        // 0xB80
    LONG MaximumDpcQueueDepth;                           // 0xB88
    ULONG DpcRequestRate;                                // 0xB8C
    ULONG MinimumDpcRate;                                // 0xB90
    ULONG DpcLastCount;                                  // 0xB94
    UCHAR ThreadDpcEnable;                               // 0xB98
    volatile UCHAR QuantumEnd;                           // 0xB99
    volatile UCHAR DpcRoutineActive;                     // 0xB9A
    volatile UCHAR IdleSchedule;                         // 0xB9B
    union
    {
        volatile LONG DpcRequestSummary;                 // 0xB9C
        SHORT DpcRequestSlot[2];
        struct
        {
            SHORT NormalDpcState;
            SHORT ThreadDpcState;
        };
        struct
        {
            ULONG DpcNormalProcessingActive:1;
            ULONG DpcNormalProcessingRequested:1;
            ULONG DpcNormalThreadSignal:1;
            ULONG DpcNormalTimerExpiration:1;
            ULONG DpcNormalDpcPresent:1;
            ULONG DpcNormalLocalInterrupt:1;
            ULONG DpcNormalPriorityAntiStarvation:1;
            ULONG DpcNormalSwapToDpcDelegate:1;
            ULONG DpcNormalSpare:8;
            ULONG DpcThreadActiveBit:1;                  // Win11 name: DpcThreadActive
            ULONG DpcThreadRequestedBit:1;               // Win11 name: DpcThreadRequested
            ULONG DpcThreadSpare:14;
        };
    };
    ULONG LastTick;                                      // 0xBA0
    ULONG ClockInterrupts;                               // 0xBA4
    ULONG ReadyScanTick;                                 // 0xBA8
    ULONG PrcbFlags;                                     // 0xBAC KPRCBFLAG
    ULONG InterruptLastCount;                            // 0xBB0
    ULONG InterruptRate;                                 // 0xBB4
    ULONG SingleDpcSoftTimeLimitTicks;                   // 0xBB8
    ULONG CumulativeDpcSoftTimeLimitTicks;               // 0xBBC
    PVOID SingleDpcSoftTimeoutEventInfo;                 // 0xBC0
    ULONG64 PrcbPadBC8[7];                               // 0xBC8
    union
    {
        KEVENT DpcGate;                                  // 0xC00 Win11 type: KGATE
        KEVENT DpcEvent;                                 // [ReactOS] legacy alias
    };
    ULONG64 MPAffinity;                                  // 0xC18
    KDPC CallDpc;                                        // 0xC20
    LONG ClockKeepAlive;                                 // 0xC60
    UCHAR PrcbPad11[4];                                  // 0xC64
    LONG DpcWatchdogPeriodTicks;                         // 0xC68
    LONG DpcWatchdogCount;                               // 0xC6C
    ULONG DpcWatchdogSequenceNumber;                     // 0xC70
    volatile LONG KeSpinLockOrdering;                    // 0xC74
    ULONG64 TrappedSecurityDomain;                       // 0xC78
    LIST_ENTRY WaitListHead;                             // 0xC80
    ULONG64 WaitLock;                                    // 0xC90
    ULONG ReadySummary;                                  // 0xC98
    LONG AffinitizedSelectionMask;                       // 0xC9C
    ULONG QueueIndex;                                    // 0xCA0
    ULONG NormalPriorityQueueIndex;                      // 0xCA4
    ULONG NormalPriorityReadyScanTick;                   // 0xCA8
    ULONG PrcbPadCAC;                                    // 0xCAC
    KDPC TimerExpirationDpc;                             // 0xCB0
    PVOID ScbQueue[2];                                   // 0xCF0 RTL_RB_TREE
    LIST_ENTRY ScbList;                                  // 0xD00
    ULONG64 PrcbPadD10[14];                              // 0xD10
    LIST_ENTRY DispatcherReadyListHead[32];              // 0xD80
    ULONG InterruptCount;                                // 0xF80
    ULONG KernelTime;                                    // 0xF84
    ULONG UserTime;                                      // 0xF88
    ULONG DpcTime;                                       // 0xF8C
    ULONG InterruptTime;                                 // 0xF90
    ULONG AdjustDpcThreshold;                            // 0xF94
    UCHAR SkipTick;                                      // 0xF98
    UCHAR DebuggerSavedIRQL;                             // 0xF99
    UCHAR TbFlushListActive;                             // 0xF9A
    UCHAR GroupSchedulingOverQuota;                      // 0xF9B
    ULONG DpcTimeCount;                                  // 0xF9C
    ULONG DpcTimeLimitTicks;                             // 0xFA0
    ULONG PeriodicCount;                                 // 0xFA4
    ULONG PeriodicBias;                                  // 0xFA8
    ULONG AvailableTime;                                 // 0xFAC
    ULONG ScbOffset;                                     // 0xFB0
    ULONG KeExceptionDispatchCount;                      // 0xFB4
    PVOID SchedulerSubNode;                              // 0xFB8 PKSCHEDULER_SUBNODE
    ULONG64 AffinitizedCycles;                           // 0xFC0
    ULONG64 StartCycles;                                 // 0xFC8
    ULONG64 TaggedCycles[4];                             // 0xFD0
    ULONG CpuCycleScalingFactor;                         // 0xFF0
    ULONG PrcbPadFF4;                                    // 0xFF4
    UCHAR EntropyTimingState[344];                       // 0xFF8 KENTROPY_TIMING_STATE
    PVOID CachedStacks[2];                               // 0x1150
    ULONG PageColor;                                     // 0x1160
    ULONG NodeColor;                                     // 0x1164
    ULONG PrcbPad18[2];                                  // 0x1168
    volatile ULONG64 CycleTime;                          // 0x1170
    ULONG64 Cycles[2][4];                                // 0x1178
    ULONG64 PrcbPad11B8[9];                              // 0x11B8
    UCHAR SymCryptEntropyAccumulatorState[384];          // 0x1200 SYMCRYPT_ENTROPY_ACCUMULATOR_STATE
    ULONG CcFastMdlReadNoWait;                           // 0x1380
    ULONG CcFastMdlReadWait;                             // 0x1384
    ULONG CcFastMdlReadNotPossible;                      // 0x1388
    ULONG CcMapDataNoWait;                               // 0x138C
    ULONG CcMapDataWait;                                 // 0x1390
    ULONG CcPinMappedDataCount;                          // 0x1394
    ULONG CcPinReadNoWait;                               // 0x1398
    ULONG CcPinReadWait;                                 // 0x139C
    ULONG CcMdlReadNoWait;                               // 0x13A0
    ULONG CcMdlReadWait;                                 // 0x13A4
    ULONG CcLazyWriteHotSpots;                           // 0x13A8
    ULONG CcLazyWriteIos;                                // 0x13AC
    ULONG CcLazyWritePages;                              // 0x13B0
    ULONG CcDataFlushes;                                 // 0x13B4
    ULONG CcDataPages;                                   // 0x13B8
    ULONG CcLostDelayedWrites;                           // 0x13BC
    ULONG CcFastReadResourceMiss;                        // 0x13C0
    ULONG CcCopyReadWaitMiss;                            // 0x13C4
    ULONG CcFastMdlReadResourceMiss;                     // 0x13C8
    ULONG CcMapDataNoWaitMiss;                           // 0x13CC
    ULONG CcMapDataWaitMiss;                             // 0x13D0
    ULONG CcPinReadNoWaitMiss;                           // 0x13D4
    ULONG CcPinReadWaitMiss;                             // 0x13D8
    ULONG CcMdlReadNoWaitMiss;                           // 0x13DC
    ULONG CcMdlReadWaitMiss;                             // 0x13E0
    ULONG CcReadAheadIos;                                // 0x13E4
    volatile LONG MmCacheTransitionCount;                // 0x13E8
    volatile LONG MmCacheReadCount;                      // 0x13EC
    volatile LONG MmCacheIoCount;                        // 0x13F0
    ULONG PrcbPad13F4[3];                                // 0x13F4
    union
    {
        PROCESSOR_POWER_STATE PowerState;                // 0x1400
        UCHAR PowerStateBlob[504];                       // Win11 PROCESSOR_POWER_STATE size
    };
    KDPC ForceIdleDpc;                                   // 0x15F8
    PVOID DpcRuntimeHistoryHashTable;                    // 0x1638 PRTL_HASH_TABLE
    PVOID DpcRuntimeHistoryHashTableCleanupDpc;          // 0x1640 PKDPC
    PVOID CurrentDpcRoutine;                             // 0x1648 PKDEFERRED_ROUTINE
    ULONG64 CurrentDpcRuntimeHistoryCached;              // 0x1650
    ULONG64 CurrentDpcStartTime;                         // 0x1658
    struct _KTHREAD *DpcDelegateThread;                  // 0x1660
    ULONG DeviceInterrupts;                              // 0x1668
    ULONG PrcbPad166C;                                   // 0x166C
    PVOID IsrDpcStats;                                   // 0x1670
    ULONG KeAlignmentFixupCount;                         // 0x1678
    UCHAR CycleAccumulationInitialized;                  // 0x167C
    UCHAR PrcbPad21[3];                                  // 0x167D
    volatile LONG64 MmSpinLockOrdering;                  // 0x1680
    KDPC DpcWatchdogDpc;                                 // 0x1688
    ULONG64 StartCyclesQpc;                              // 0x16C8
    ULONG64 CycleTimeQpc;                                // 0x16D0
    ULONG64 NumberOfSecureFaults;                        // 0x16D8
    ULONG64 PrcbPad22[6];                                // 0x16E0 Win11 [5] + tail alignment
    SLIST_HEADER InterruptObjectPool;                    // 0x1710
    ULONG64 PackageProcessorSet[33];                     // 0x1720 KAFFINITY_EX
    union
    {
        ULONG TopologyId[6];                             // 0x1828
        struct
        {
            ULONG ProcessorId;
            ULONG CoreId;
            ULONG ModuleId;
            ULONG DieId;
            ULONG PackageId;
            ULONG ComplexId;
        };
    };
    UCHAR PrcbPad25[80];                                 // 0x1840
    UCHAR PrcbPad1890[112];                              // 0x1890
    ULONG64 SharedReadyQueueMask;                        // 0x1900
    PVOID SharedReadyQueue;                              // 0x1908 PKSHARED_READY_QUEUE
    ULONG SharedQueueScanOwner;                          // 0x1910
    ULONG ScanSiblingIndex;                              // 0x1914
    PVOID CoreControlBlock;                              // 0x1918 PKCORE_CONTROL_BLOCK
    ULONG64 CoreProcessorSet;                            // 0x1920
    ULONG64 ScanSiblingMask;                             // 0x1928
    ULONG64 LLCMask;                                     // 0x1930
    ULONG64 GroupModuleProcessorSet;                     // 0x1938
    ULONG64 PrcbPad19[4];                                // 0x1940
    struct _KTHREAD *SmtIsolationThread;                 // 0x1960
    CACHE_DESCRIPTOR Cache[6];                           // 0x1968
    UCHAR CacheCount;                                    // 0x19B0
    UCHAR PrcbPad20;                                     // 0x19B1
    UCHAR SystemWorkKickInProgress;                      // 0x19B2
    UCHAR ExceptionStackActive;                          // 0x19B3
    volatile ULONG CachedCommit;                         // 0x19B4
    volatile ULONG CachedResidentAvailable;              // 0x19B8
    ULONG PrcbPad19BC;                                   // 0x19BC
    PVOID MmFaultCompletionInfo;                         // 0x19C0
    PVOID MmInternal;                                    // 0x19C8
    ULONG64 GenerationTarget;                            // 0x19D0
    ULONG64 PrcbPad24;                                   // 0x19D8
    PVOID VmInternal;                                    // 0x19E0
    PVOID DpcLog;                                        // 0x19E8
    ULONG DpcLogIndex;                                   // 0x19F0
    ULONG DpcLogBufferSize;                              // 0x19F4
    PVOID ExceptionStack;                                // 0x19F8
    PVOID WheaInfo;                                      // 0x1A00
    PVOID EtwSupport;                                    // 0x1A08
    SLIST_HEADER HypercallPageList;                      // 0x1A10
    PVOID HypercallCachedPages;                          // 0x1A20
    PVOID VirtualApicAssist;                             // 0x1A28
    LARGE_INTEGER VirtualApicAssistPage;                 // 0x1A30
    ULONG64 *StatisticsPage;                             // 0x1A38
    ULONG64 PrcbPad1A40[8];                              // 0x1A40
    UCHAR SynchCounters[184];                            // 0x1A80 SYNCH_COUNTERS
    ULONG64 PteBitCache;                                 // 0x1B38
    ULONG PteBitOffset;                                  // 0x1B40
    ULONG PrcbPad1B44;                                   // 0x1B44
    UCHAR FsCounters[16];                                // 0x1B48 FILESYSTEM_DISK_COUNTERS
    ULONG64 PanicStackBase;                              // 0x1B58
    PVOID IsrStack;                                      // 0x1B60
    ULONG64 PrcbPad1B68[3];                              // 0x1B68
    union
    {
        UCHAR TimerTable[16920];                         // 0x1B80 KTIMER_TABLE (opaque)
        KSPIN_LOCK_QUEUE TimerTableLockQueue[LOCK_QUEUE_TIMER_TABLE_LOCKS]; // [ReactOS] legacy timer locks
    };
    UCHAR PrcbPad5D98[104];                              // 0x5D98
#if (NTDDI_VERSION >= NTDDI_LONGHORN)
    GENERAL_LOOKASIDE_POOL PPNxPagedLookasideList[NUMBER_POOL_LOOKASIDE_LISTS]; // 0x5E00
    GENERAL_LOOKASIDE_POOL PPNPagedLookasideList[NUMBER_POOL_LOOKASIDE_LISTS];  // 0x6A00
    GENERAL_LOOKASIDE_POOL PPPagedLookasideList[NUMBER_POOL_LOOKASIDE_LISTS];   // 0x7600
#else
    // Layout-only stand-ins for TUs built with NTDDI < Vista (96-byte stride)
    UCHAR PPNxPagedLookasideList[3072];                  // 0x5E00
    UCHAR PPNPagedLookasideList[3072];                   // 0x6A00
    UCHAR PPPagedLookasideList[3072];                    // 0x7600
#endif
    SINGLE_LIST_ENTRY AbSelfIoBoostsList;                // 0x8200
    SINGLE_LIST_ENTRY AbPropagateBoostsList;             // 0x8208
    KDPC AbDpc;                                          // 0x8210
    ULONG IoIrpStackProfilerCurrent[21];                 // 0x8250 IOP_IRP_STACK_PROFILER
    ULONG IoIrpStackProfilerPrevious[21];                // 0x82A4
    ULONG64 PrcbPad82F8;                                 // 0x82F8
    PVOID LocalSharedReadyQueue;                         // 0x8300 PKSHARED_READY_QUEUE
    UCHAR TimerExpirationTrace[256];                     // 0x8308 KTIMER_EXPIRATION_TRACE[16]
    ULONG TimerExpirationTraceCount;                     // 0x8408
    UCHAR PrcbPad840C[116];                              // 0x840C
    UCHAR ClockTimerState[1304];                         // 0x8480 KCLOCK_TIMER_STATE
    PVOID ExSaPageArray;                                 // 0x8998
    PVOID TracepointLog;                                 // 0x89A0 PKPRCB_TRACEPOINT_LOG
    UCHAR RcuData[32];                                   // 0x89A8 KE_PRCB_RCU_DATA
    volatile LONG FreezePowerOff;                        // 0x89C8
    ULONG DpcWatchdogProfileBufferSize;                  // 0x89CC
    UCHAR StaticAffinity[2080];                          // 0x89D0 KSTATIC_AFFINITY_BLOCK
    UCHAR DeferredDispatchInterrupts[528];               // 0x91F0 KSOFTWARE_INTERRUPT_BATCH
    PVOID StaticRescheduleContext;                       // 0x9400 PKI_RESCHEDULE_CONTEXT
    UCHAR SecureFault[24];                               // 0x9408 KSECURE_FAULT_INFORMATION
    ULONG64 *CyclesByThreadType;                         // 0x9420
    ULONG ReadyThreadCount;                              // 0x9428
    ULONG PrcbPad942C;                                   // 0x942C
    ULONG64 ReadyQueueExpectedRunTime;                   // 0x9430
    PVOID ForceParkDutyCycleData;                        // 0x9438
    PVOID CpuPartition;                                  // 0x9440 PKCPU_PARTITION
    ULONG DpcWatchdogProfileCumulativeDpcThresholdTicks; // 0x9448
    ULONG DpcWatchdogProfileSingleDpcThresholdTicks;     // 0x944C
    PVOID *DpcWatchdogProfile;                           // 0x9450
    PVOID *DpcWatchdogProfileCurrentEmptyCapture;        // 0x9458
    PVOID SchedulerAssist;                               // 0x9460
    UCHAR SelfmapLockHandle[96];                         // 0x9468 KLOCK_QUEUE_HANDLE[4]
    ULONG64 CacheProcessorSet[198];                      // 0x94C8 KAFFINITY_EX[6]
    ULONG64 ModuleProcessorSet[33];                      // 0x9AF8 KAFFINITY_EX
    ULONG64 DieProcessorSet[33];                         // 0x9C00 KAFFINITY_EX
    UCHAR LocalCoreControlBlock[48];                     // 0x9D08 KCORE_CONTROL_BLOCK
    ULONG CoreControlBlockIndex;                         // 0x9D38
    ULONG PrcbPad26;                                     // 0x9D3C
    PVOID CoreControlBlockShadow;                        // 0x9D40 PKCORE_CONTROL_BLOCK_SHADOW
    UCHAR LocalCoreControlBlockShadow[64];               // 0x9D48 KCORE_CONTROL_BLOCK_SHADOW
    ULONG NodeRelativeTopologyIndex[6];                  // 0x9D88
    KDPC KstackFreeDpc;                                  // 0x9DA0
    SLIST_HEADER KstackFreeList;                         // 0x9DE0
    struct _KTRAP_FRAME *IpiFrame;                       // 0x9DF0
    KDPC SlistRollbackDpc;                               // 0x9DF8
    PVOID LocalSearchContexts[2];                        // 0x9E38 PKI_COOPERATIVE_IDLE_SEARCH_CONTEXT[2]
    PVOID SearchContexts[2];                             // 0x9E48
    PVOID SearchGenerations[2];                          // 0x9E58
    ULONG64 PrcbPad9E68[3];                              // 0x9E68
    REQUEST_MAILBOX RequestMailbox[1];                   // 0x9E80
    ULONG64 PrcbPadEnd[8];                               // 0x9EC0
} KPRCB, *PKPRCB;                                        // sizeof 0x9F00
//
// Processor Control Region, Win11 26100 arm64 layout (ntkrnlmp.pdb 10.0.26100.8036)
// public part is 0x100 bytes; Prcb at 0x980; sizeof == 0xA880
//
typedef struct _KIPCR
{
    union
    {
        struct
        {
            ULONG TibPad0[4];                       // 0x000
            PVOID Spare1;                           // 0x010
            struct _KPCR *Self;                     // 0x018
            struct _KPRCB *CurrentPrcb;             // 0x020 Win11 name: PcrReserved0, ReactOS keeps &Prcb here
            struct _KSPIN_LOCK_QUEUE* LockArray;    // 0x028
            PVOID Used_Self;                        // 0x030
        };
    };
    KIRQL CurrentIrql;                              // 0x038
    UCHAR SecondLevelCacheAssociativity;            // 0x039
    UCHAR Pad1[2];                                  // 0x03A
    USHORT MajorVersion;                            // 0x03C
    USHORT MinorVersion;                            // 0x03E
    ULONG StallScaleFactor;                         // 0x040
    ULONG SecondLevelCacheSize;                     // 0x044
    union
    {
        USHORT SoftwareInterruptPending;            // 0x048
        struct
        {
            UCHAR ApcInterrupt;                     // 0x048
            UCHAR DispatchInterrupt;                // 0x049
        };
    };
    USHORT InterruptPad;                            // 0x04A
    UCHAR BtiMitigation;                            // 0x04C
    union
    {
        UCHAR SsbMitigationFlags;                   // 0x04D
        struct
        {
            UCHAR SsbMitigationFirmware:1;
            UCHAR SsbMitigationDynamic:1;
            UCHAR SsbMitigationKernel:1;
            UCHAR SsbMitigationUser:1;
            UCHAR SsbMitigationReserved:4;
        };
    };
    UCHAR BhbMitigation;                            // 0x04E
    UCHAR CachePrefetcherMitigationFlags;           // 0x04F Win11 alias: Pad2
    ULONG64 PanicStorage[6];                        // 0x050
    PVOID KdVersionBlock;                           // 0x080
    PVOID HalReserved[14];                          // 0x088
    PVOID KvaUserModeTtbr1;                         // 0x0F8

    /* Private members, not in ntddk.h */
    PVOID Idt[256];                                 // 0x100
    PVOID* IdtExt;                                  // 0x900
    PVOID PcrAlign[15];                             // 0x908
    KPRCB Prcb;                                     // 0x980
} KIPCR, *PKIPCR;                                   // sizeof 0xA880

#ifndef __ASSEMBLER__
C_ASSERT(FIELD_OFFSET(KIPCR, Self) == 0x018);
C_ASSERT(FIELD_OFFSET(KIPCR, CurrentPrcb) == 0x020);
C_ASSERT(FIELD_OFFSET(KIPCR, CurrentIrql) == 0x038);
C_ASSERT(FIELD_OFFSET(KIPCR, StallScaleFactor) == 0x040);
C_ASSERT(FIELD_OFFSET(KIPCR, PanicStorage) == 0x050);
C_ASSERT(FIELD_OFFSET(KIPCR, KdVersionBlock) == 0x080);
C_ASSERT(FIELD_OFFSET(KIPCR, KvaUserModeTtbr1) == 0x0F8);
C_ASSERT(FIELD_OFFSET(KIPCR, Idt) == 0x100);
C_ASSERT(FIELD_OFFSET(KIPCR, IdtExt) == 0x900);
C_ASSERT(FIELD_OFFSET(KIPCR, Prcb) == 0x980);
C_ASSERT(sizeof(KIPCR) == 0xA880);
C_ASSERT(sizeof(KPRCB) == 0x9F00);
C_ASSERT(sizeof(KPROCESSOR_STATE) == 0x6D0);
C_ASSERT(sizeof(KSPECIAL_REGISTERS) == 0x0A0);
C_ASSERT(sizeof(KARM64_ARCH_STATE) == 0x2A0);
C_ASSERT(sizeof(REQUEST_MAILBOX) == 0x040);
C_ASSERT(sizeof(KDPC_DATA) == 0x030);
C_ASSERT(FIELD_OFFSET(KPRCB, CurrentThread) == 0x008);
C_ASSERT(FIELD_OFFSET(KPRCB, Number) == 0x024);
C_ASSERT(FIELD_OFFSET(KPRCB, ProcessorState) == 0x040);
C_ASSERT(FIELD_OFFSET(KPRCB, HalReserved) == 0x710);
C_ASSERT(FIELD_OFFSET(KPRCB, GroupSetMember) == 0x780);
C_ASSERT(FIELD_OFFSET(KPRCB, LockQueue) == 0x800);
C_ASSERT(FIELD_OFFSET(KPRCB, FeatureBits) == 0x914);
C_ASSERT(FIELD_OFFSET(KPRCB, PPLookasideList) == 0x980);
C_ASSERT(FIELD_OFFSET(KPRCB, PacketBarrier) == 0xA80);
C_ASSERT(FIELD_OFFSET(KPRCB, DeferredReadyListHead) == 0xA88);
C_ASSERT(FIELD_OFFSET(KPRCB, KeSystemCalls) == 0xAB8);
C_ASSERT(FIELD_OFFSET(KPRCB, IoReadTransferCount) == 0xAE8);
C_ASSERT(FIELD_OFFSET(KPRCB, IpiFrozen) == 0xB0C);
C_ASSERT(FIELD_OFFSET(KPRCB, DpcData) == 0xB18);
C_ASSERT(FIELD_OFFSET(KPRCB, DpcStack) == 0xB78);
C_ASSERT(FIELD_OFFSET(KPRCB, SpBase) == 0xB80);
C_ASSERT(FIELD_OFFSET(KPRCB, DpcRoutineActive) == 0xB9A);
C_ASSERT(FIELD_OFFSET(KPRCB, DpcRequestSummary) == 0xB9C);
C_ASSERT(FIELD_OFFSET(KPRCB, DpcGate) == 0xC00);
C_ASSERT(FIELD_OFFSET(KPRCB, CallDpc) == 0xC20);
C_ASSERT(FIELD_OFFSET(KPRCB, WaitListHead) == 0xC80);
C_ASSERT(FIELD_OFFSET(KPRCB, ReadySummary) == 0xC98);
C_ASSERT(FIELD_OFFSET(KPRCB, DispatcherReadyListHead) == 0xD80);
C_ASSERT(FIELD_OFFSET(KPRCB, InterruptCount) == 0xF80);
C_ASSERT(FIELD_OFFSET(KPRCB, KeExceptionDispatchCount) == 0xFB4);
C_ASSERT(FIELD_OFFSET(KPRCB, EntropyTimingState) == 0xFF8);
C_ASSERT(FIELD_OFFSET(KPRCB, PageColor) == 0x1160);
C_ASSERT(FIELD_OFFSET(KPRCB, SymCryptEntropyAccumulatorState) == 0x1200);
C_ASSERT(FIELD_OFFSET(KPRCB, CcFastMdlReadNoWait) == 0x1380);
C_ASSERT(FIELD_OFFSET(KPRCB, PowerState) == 0x1400);
C_ASSERT(FIELD_OFFSET(KPRCB, ForceIdleDpc) == 0x15F8);
C_ASSERT(FIELD_OFFSET(KPRCB, MmSpinLockOrdering) == 0x1680);
C_ASSERT(FIELD_OFFSET(KPRCB, InterruptObjectPool) == 0x1710);
C_ASSERT(FIELD_OFFSET(KPRCB, PackageProcessorSet) == 0x1720);
C_ASSERT(FIELD_OFFSET(KPRCB, TopologyId) == 0x1828);
C_ASSERT(FIELD_OFFSET(KPRCB, SharedReadyQueueMask) == 0x1900);
C_ASSERT(FIELD_OFFSET(KPRCB, Cache) == 0x1968);
C_ASSERT(FIELD_OFFSET(KPRCB, CacheCount) == 0x19B0);
C_ASSERT(FIELD_OFFSET(KPRCB, WheaInfo) == 0x1A00);
C_ASSERT(FIELD_OFFSET(KPRCB, SynchCounters) == 0x1A80);
C_ASSERT(FIELD_OFFSET(KPRCB, TimerTable) == 0x1B80);
C_ASSERT(FIELD_OFFSET(KPRCB, PPNxPagedLookasideList) == 0x5E00);
C_ASSERT(FIELD_OFFSET(KPRCB, PPNPagedLookasideList) == 0x6A00);
C_ASSERT(FIELD_OFFSET(KPRCB, PPPagedLookasideList) == 0x7600);
C_ASSERT(FIELD_OFFSET(KPRCB, AbSelfIoBoostsList) == 0x8200);
C_ASSERT(FIELD_OFFSET(KPRCB, ClockTimerState) == 0x8480);
C_ASSERT(FIELD_OFFSET(KPRCB, StaticAffinity) == 0x89D0);
C_ASSERT(FIELD_OFFSET(KPRCB, SelfmapLockHandle) == 0x9468);
C_ASSERT(FIELD_OFFSET(KPRCB, CacheProcessorSet) == 0x94C8);
C_ASSERT(FIELD_OFFSET(KPRCB, RequestMailbox) == 0x9E80);
C_ASSERT(sizeof(PROCESSOR_POWER_STATE) <= 504);
C_ASSERT(sizeof(KDPC) == 0x40);
C_ASSERT(FIELD_OFFSET(KDPC, DpcListEntry) == 0x08);
C_ASSERT(FIELD_OFFSET(KDPC, ProcessorHistory) == 0x10);
C_ASSERT(FIELD_OFFSET(KDPC, DeferredRoutine) == 0x18);
C_ASSERT(FIELD_OFFSET(KDPC, DpcData) == 0x38);
C_ASSERT(sizeof(KTIMER) == 0x40);
C_ASSERT(FIELD_OFFSET(KTIMER, DueTime) == 0x18);
C_ASSERT(FIELD_OFFSET(KTIMER, TimerListEntry) == 0x20);
C_ASSERT(FIELD_OFFSET(KTIMER, Dpc) == 0x30);
C_ASSERT(FIELD_OFFSET(KTIMER, Processor) == 0x38);
C_ASSERT(FIELD_OFFSET(KTIMER, Period) == 0x3C);
C_ASSERT(sizeof(DISPATCHER_HEADER) == 0x18);
C_ASSERT(FIELD_OFFSET(DISPATCHER_HEADER, SignalState) == 0x04);
C_ASSERT(FIELD_OFFSET(DISPATCHER_HEADER, WaitListHead) == 0x08);
C_ASSERT(sizeof(KQUEUE) == 0x40);
#endif // !__ASSEMBLER__

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
