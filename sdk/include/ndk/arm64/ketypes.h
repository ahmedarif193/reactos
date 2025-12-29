

#ifndef _ARM64_KETYPES_H
#define _ARM64_KETYPES_H

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
} KARM64_ARCH_STATE, *PKARM64_ARCH_STATE;

typedef struct _KPROCESSOR_STATE
{
    KSPECIAL_REGISTERS SpecialRegisters; // 0
    KARM64_ARCH_STATE ArchState;         // 160
    CONTEXT ContextFrame;                // 800
} KPROCESSOR_STATE, *PKPROCESSOR_STATE;

typedef struct _KREQUEST_PACKET
{
    PVOID CurrentPacket[3];
    PVOID WorkerRoutine;
} KREQUEST_PACKET, *PKREQUEST_PACKET;

typedef struct _REQUEST_MAILBOX
{
    INT64 RequestSummary;
    KREQUEST_PACKET RequestPacket;
    PVOID Virtual[7];
} REQUEST_MAILBOX, *PREQUEST_MAILBOX;

typedef struct _KPRCB
{
    ULONG MxCsr;
#if (NTDDI_VERSION >= NTDDI_LONGHORN)
    USHORT Number;
#else
    UCHAR Number;
    UCHAR NestingLevel;
#endif
    UCHAR InterruptRequest;
    UCHAR IdleHalt;
    struct _KTHREAD *CurrentThread;
    struct _KTHREAD *NextThread;
    struct _KTHREAD *IdleThread;
#if (NTDDI_VERSION >= NTDDI_LONGHORN)
    UCHAR NestingLevel;
    UCHAR Group;
    UCHAR PrcbPad00[6];
#else
    UINT64 UserRsp;
#endif
    UINT64 RspBase;
    UINT64 PrcbLock;
    UINT64 SetMember;
    KPROCESSOR_STATE ProcessorState;
    CHAR CpuType;
    CHAR CpuID;
#if (NTDDI_VERSION >= NTDDI_LONGHORN)
    union
    {
        USHORT CpuStep;
        struct
        {
            UCHAR CpuStepping;
            UCHAR CpuModel;
        };
    };
#else
    USHORT CpuStep;
#endif
    ULONG MHz;
    UINT64 HalReserved[8];
    USHORT MinorVersion;
    USHORT MajorVersion;
    UCHAR BuildType;
    UCHAR CpuVendor;
#if (NTDDI_VERSION >= NTDDI_LONGHORN)
    UCHAR CoresPerPhysicalProcessor;
    UCHAR LogicalProcessorsPerCore;
#else
    UCHAR InitialApicId;
    UCHAR LogicalProcessorsPerPhysicalProcessor;
#endif
    ULONG ApicMask;
#if (NTDDI_VERSION >= NTDDI_LONGHORN)
    ULONG CFlushSize;
#else
    UCHAR CFlushSize;
    UCHAR PrcbPad0x[3];
#endif
    PVOID AcpiReserved;
#if (NTDDI_VERSION >= NTDDI_LONGHORN)
    ULONG InitialApicId;
    ULONG Stride;
    UINT64 PrcbPad01[3];
#else
    UINT64 PrcbPad00[4];
#endif
    KSPIN_LOCK_QUEUE LockQueue[LockQueueMaximumLock]; // 2003: 33, vista:49
    PP_LOOKASIDE_LIST PPLookasideList[16];
    PP_LOOKASIDE_LIST PPNPagedLookasideList[NUMBER_POOL_LOOKASIDE_LISTS];
    PP_LOOKASIDE_LIST PPPagedLookasideList[NUMBER_POOL_LOOKASIDE_LISTS];
    UINT64 PacketBarrier;
    SINGLE_LIST_ENTRY DeferredReadyListHead;
    LONG MmPageFaultCount;
    LONG MmCopyOnWriteCount;
    LONG MmTransitionCount;
#if (NTDDI_VERSION < NTDDI_LONGHORN)
    LONG MmCacheTransitionCount;
#endif
    LONG MmDemandZeroCount;
    LONG MmPageReadCount;
    LONG MmPageReadIoCount;
#if (NTDDI_VERSION < NTDDI_LONGHORN)
    LONG MmCacheReadCount;
    LONG MmCacheIoCount;
#endif
    LONG MmDirtyPagesWriteCount;
    LONG MmDirtyWriteIoCount;
    LONG MmMappedPagesWriteCount;
    LONG MmMappedWriteIoCount;
#if (NTDDI_VERSION >= NTDDI_LONGHORN)
    ULONG KeSystemCalls;
    ULONG KeContextSwitches;
    ULONG CcFastReadNoWait;
    ULONG CcFastReadWait;
    ULONG CcFastReadNotPossible;
    ULONG CcCopyReadNoWait;
    ULONG CcCopyReadWait;
    ULONG CcCopyReadNoWaitMiss;
    LONG LookasideIrpFloat;
#else
    LONG LookasideIrpFloat;
    ULONG KeSystemCalls;
#endif
    LONG IoReadOperationCount;
    LONG IoWriteOperationCount;
    LONG IoOtherOperationCount;
    LARGE_INTEGER IoReadTransferCount;
    LARGE_INTEGER IoWriteTransferCount;
    LARGE_INTEGER IoOtherTransferCount;
#if (NTDDI_VERSION < NTDDI_LONGHORN)
    ULONG KeContextSwitches;
    UCHAR PrcbPad2[12];
#endif
    UINT64 TargetSet;
    ULONG IpiFrozen;
    UCHAR PrcbPad3[116];
    REQUEST_MAILBOX RequestMailbox[64];
    UINT64 SenderSummary;
    UCHAR PrcbPad4[120];
    KDPC_DATA DpcData[2];
    PVOID DpcStack;
#if (NTDDI_VERSION >= NTDDI_LONGHORN)
    PVOID SparePtr0;
#else
    PVOID SavedRsp;
#endif
    LONG MaximumDpcQueueDepth;
    ULONG DpcRequestRate;
    ULONG MinimumDpcRate;
    UCHAR DpcInterruptRequested;
    UCHAR DpcThreadRequested;
    UCHAR DpcRoutineActive;
    UCHAR DpcThreadActive;
    UINT64 TimerHand;
    UINT64 TimerRequest;
    LONG TickOffset;
    LONG MasterOffset;
    ULONG DpcLastCount;
    UCHAR ThreadDpcEnable;
    UCHAR QuantumEnd;
    UCHAR PrcbPad50;
    UCHAR IdleSchedule;
    LONG DpcSetEventRequest;
#if (NTDDI_VERSION >= NTDDI_LONGHORN)
    ULONG KeExceptionDispatchCount;
#else
    LONG PrcbPad40;
    PVOID DpcThread;
#endif
    KEVENT DpcEvent;
#if (NTDDI_VERSION >= NTDDI_LONGHORN)
    PVOID PrcbPad51;
#endif
    KDPC CallDpc;
#if (NTDDI_VERSION >= NTDDI_LONGHORN)
    LONG ClockKeepAlive;
    UCHAR ClockCheckSlot;
    UCHAR ClockPollCycle;
    UCHAR PrcbPad6[2];
    LONG DpcWatchdogPeriod;
    LONG DpcWatchdogCount;
    UINT64 PrcbPad70[2];
#else
    UINT64 PrcbPad7[4];
#endif
    LIST_ENTRY WaitListHead;
#if (NTDDI_VERSION >= NTDDI_LONGHORN)
    UINT64 WaitLock;
#endif
    ULONG ReadySummary;
    ULONG QueueIndex;
#if (NTDDI_VERSION >= NTDDI_LONGHORN)
    UINT64 PrcbPad71[12];
#endif
    LIST_ENTRY DispatcherReadyListHead[32];
    ULONG InterruptCount;
    ULONG KernelTime;
    ULONG UserTime;
    ULONG DpcTime;
    ULONG InterruptTime;
    ULONG AdjustDpcThreshold;
    UCHAR SkipTick;
    UCHAR DebuggerSavedIRQL;
    UCHAR PollSlot;
#if (NTDDI_VERSION >= NTDDI_LONGHORN)
    UCHAR PrcbPad80[5];
    ULONG DpcTimeCount;
    ULONG DpcTimeLimit;
    ULONG PeriodicCount;
    ULONG PeriodicBias;
    UINT64 PrcbPad81[2];
#else
    UCHAR PrcbPad8[13];
#endif
    struct _KNODE *ParentNode;
    UINT64 MultiThreadProcessorSet;
    struct _KPRCB *MultiThreadSetMaster;
#if (NTDDI_VERSION >= NTDDI_LONGHORN)
    UINT64 StartCycles;
    LONG MmSpinLockOrdering;
    ULONG PageColor;
    ULONG NodeColor;
    ULONG NodeShiftedColor;
    ULONG SecondaryColorMask;
#endif
    LONG Sleeping;
#if (NTDDI_VERSION >= NTDDI_LONGHORN)
    UINT64 CycleTime;
    ULONG CcFastMdlReadNoWait;
    ULONG CcFastMdlReadWait;
    ULONG CcFastMdlReadNotPossible;
    ULONG CcMapDataNoWait;
    ULONG CcMapDataWait;
    ULONG CcPinMappedDataCount;
    ULONG CcPinReadNoWait;
    ULONG CcPinReadWait;
    ULONG CcMdlReadNoWait;
    ULONG CcMdlReadWait;
    ULONG CcLazyWriteHotSpots;
    ULONG CcLazyWriteIos;
    ULONG CcLazyWritePages;
    ULONG CcDataFlushes;
    ULONG CcDataPages;
    ULONG CcLostDelayedWrites;
    ULONG CcFastReadResourceMiss;
    ULONG CcCopyReadWaitMiss;
    ULONG CcFastMdlReadResourceMiss;
    ULONG CcMapDataNoWaitMiss;
    ULONG CcMapDataWaitMiss;
    ULONG CcPinReadNoWaitMiss;
    ULONG CcPinReadWaitMiss;
    ULONG CcMdlReadNoWaitMiss;
    ULONG CcMdlReadWaitMiss;
    ULONG CcReadAheadIos;
    LONG MmCacheTransitionCount;
    LONG MmCacheReadCount;
    LONG MmCacheIoCount;
    ULONG PrcbPad91[3];
    PROCESSOR_POWER_STATE PowerState;
    ULONG KeAlignmentFixupCount;
    UCHAR VendorString[13];
    UCHAR PrcbPad10[3];
    ULONG FeatureBits;
    LARGE_INTEGER UpdateSignature;
    KDPC DpcWatchdogDpc;
    KTIMER DpcWatchdogTimer;
    CACHE_DESCRIPTOR Cache[5];
    ULONG CacheCount;
    ULONG CachedCommit;
    ULONG CachedResidentAvailable;
    PVOID HyperPte;
    PVOID WheaInfo;
    PVOID EtwSupport;
    SLIST_HEADER InterruptObjectPool;
    SLIST_HEADER HypercallPageList;
    PVOID HypercallPageVirtual;
    PVOID VirtualApicAssist;
    UINT64* StatisticsPage;
    PVOID RateControl;
    UINT64 CacheProcessorMask[5];
    UINT64 PackageProcessorSet;
    UINT64 CoreProcessorSet;
#else
    ULONG PrcbPad90[1];
    ULONG DebugDpcTime;
    ULONG PageColor;
    ULONG NodeColor;
    ULONG NodeShiftedColor;
    ULONG SecondaryColorMask;
    UCHAR PrcbPad9[12];
    ULONG CcFastReadNoWait;
    ULONG CcFastReadWait;
    ULONG CcFastReadNotPossible;
    ULONG CcCopyReadNoWait;
    ULONG CcCopyReadWait;
    ULONG CcCopyReadNoWaitMiss;
    ULONG KeAlignmentFixupCount;
    ULONG KeDcacheFlushCount;
    ULONG KeExceptionDispatchCount;
    ULONG KeFirstLevelTbFills;
    ULONG KeFloatingEmulationCount;
    ULONG KeIcacheFlushCount;
    ULONG KeSecondLevelTbFills;
    UCHAR VendorString[13];
    UCHAR PrcbPad10[2];
    ULONG FeatureBits;
    LARGE_INTEGER UpdateSignature;
    PROCESSOR_POWER_STATE PowerState;
    CACHE_DESCRIPTOR Cache[5];
    ULONG CacheCount;
#endif
#ifdef __REACTOS__
#if  (NTDDI_VERSION < NTDDI_WINBLUE)
    // On Win 8.1+ the FeatureBits field is extended to 64 bits
    ULONG FeatureBitsHigh;
#endif
#endif
} KPRCB, *PKPRCB;

//
// Processor Control Region
// Based on WoA
//
typedef struct _KIPCR
{
    union
    {
        struct
        {
            ULONG TibPad0[2];
            PVOID Spare1;
            struct _KPCR *Self;
            struct _KPRCB *CurrentPrcb;
            struct _KSPIN_LOCK_QUEUE* LockArray;
            PVOID Used_Self;
        };
    };
    KIRQL CurrentIrql;
    UCHAR SecondLevelCacheAssociativity;
    UCHAR Pad1[2];
    USHORT MajorVersion;
    USHORT MinorVersion;
    ULONG StallScaleFactor;
    ULONG SecondLevelCacheSize;
    struct
    {
        UCHAR ApcInterrupt;
        UCHAR DispatchInterrupt;
    };
    USHORT InterruptPad;
    UCHAR BtiMitigation;
    struct
    {
        UCHAR SsbMitigationFirmware:1;
        UCHAR SsbMitigationDynamic:1;
        UCHAR SsbMitigationKernel:1;
        UCHAR SsbMitigationUser:1;
        UCHAR SsbMitigationReserved:4;
    };
    UCHAR Pad2[2];
    ULONG64 PanicStorage[6];
    PVOID KdVersionBlock;
    PVOID HalReserved[134];
    PVOID KvaUserModeTtbr1;

    /* Private members, not in ntddk.h */
    PVOID Idt[256];
    PVOID* IdtExt;
    PVOID PcrAlign[15];
    KPRCB Prcb;
} KIPCR, *PKIPCR;

extern PKIPCR KeArm64CurrentPcr;
extern PKTHREAD KeArm64CurrentThread;
extern KIRQL KeArm64CurrentIrql;
extern BOOLEAN KeArm64DpcRoutineActive;

#define KeGetPcr()                     (KeArm64CurrentPcr)
#define KeGetCurrentPrcb()             ((KeArm64CurrentPcr && KeArm64CurrentPcr->CurrentPrcb) ? \
                                        KeArm64CurrentPcr->CurrentPrcb : \
                                        (KeArm64CurrentPcr ? &KeArm64CurrentPcr->Prcb : (PKPRCB)NULL))
#define PRIMARY_VECTOR_BASE            0x00

//
// Special Registers Structure (outside of CONTEXT)
// Based on WoA symbols
//
#ifdef _NTOSKRNL_
#define KeGetCurrentIrql()             (KeArm64CurrentIrql)
#endif
#define _KeGetCurrentThread()          (KeArm64CurrentThread)
#define _KeGetPreviousMode()           ((KeArm64CurrentThread) ? (KeArm64CurrentThread)->PreviousMode : KernelMode)
#define _KeIsExecutingDpc()            (KeArm64DpcRoutineActive)


#ifdef __cplusplus
}; // extern "C"
#endif

#endif // !_ARM64_KETYPES_H
