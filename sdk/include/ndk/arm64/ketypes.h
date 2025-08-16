

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

//
// Synchronization Level
//
#ifndef CONFIG_SMP
#define SYNCH_LEVEL             DISPATCH_LEVEL
#else
#define SYNCH_LEVEL             (IPI_LEVEL - 2)
#endif

//
// IPI Types
//
#define IPI_APC                 1
#define IPI_DPC                 2
#define IPI_FREEZE              4
#define IPI_PACKET_READY        6
#define IPI_SYNCH_REQUEST       16

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
// HAL Variables
//
#define INITIAL_STALL_COUNT     100
#define MM_HAL_VA_START         0xFFFFFFFFFFC00000ULL
#define MM_HAL_VA_END           0xFFFFFFFFFFFFFFFFULL

//
// Static Kernel-Mode Address start (use MM_KSEG0_BASE for actual)
//
#define KSEG0_BASE              0xffff000000000000ULL

//
// Highest user address (end of user address space)
//
#define MI_HIGHEST_USER_ADDRESS 0x0000ffffffffffff

//
// Double fault stack size
//
#define DOUBLE_FAULT_STACK_SIZE 0x8000

//
// Forward declarations - these will be defined in ifssupp.h or elsewhere
// We just need to ensure they're available for ARM64
//
#ifndef _NTIFS_
// If these aren't defined yet, just forward declare them
struct _KAPC_STATE;
struct _KQUEUE;
#endif

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

/* Compatibility aliases for ARM64 */
#define Elr Pc
#define Far FaultAddress

/* ARM64 doesn't have EFlags/V86 mode - define for compatibility */
#define EFLAGS_V86_MASK 0
/* Use SPSR instead of EFlags on ARM64 */
#define EFlags Spsr

typedef struct _KEXCEPTION_FRAME
{
    ULONG dummy;
} KEXCEPTION_FRAME, *PKEXCEPTION_FRAME;

#ifndef NTOS_MODE_USER

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
// Special Registers Structure (outside of CONTEXT)
// Based on WoA symbols
//
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

//
// Processor Region Control Block
// Based on WoA
//
#define NUMBER_POOL_LOOKASIDE_LISTS 32
#define IMAGE_FILE_MACHINE_ARM64    0xAA64
#define KPCR_CONTAINED_PRCB_OFFSET  0x180

//
// Callout frame for ARM64
//
typedef struct _KCALLOUT_FRAME
{
    ULONG64 CallbackStack;
    ULONG64 Fp;  /* Frame pointer for ARM64 */
    ULONG64 Reserved[6];
} KCALLOUT_FRAME;

typedef struct _KPRCB
{
    ULONG MxCsr;                           // Floating point control/status
    USHORT Number;                          // Processor number  
    UCHAR InterruptRequest;                 // Interrupt request flag
    UCHAR IdleHalt;                         // Idle halt flag
    struct _KTHREAD *CurrentThread;         // Current thread
    struct _KTHREAD *NextThread;            // Next thread to run
    struct _KTHREAD *IdleThread;            // Idle thread
    UCHAR NestingLevel;                     // Exception nesting level
    UCHAR Group;                            // Processor group
    UCHAR PrcbPad00[6];                     // Padding
    UINT64 RspBase;                         // Stack base
    UINT64 PrcbLock;                        // PRCB lock
    UINT64 SetMember;                       // CPU set member
    KPROCESSOR_STATE ProcessorState;        // Processor state
    CHAR CpuType;                           // CPU type
    CHAR CpuID;                             // CPU ID
    USHORT CpuStep;                         // CPU stepping
    ULONG MHz;                              // CPU frequency
    UINT64 HalReserved[8];                  // HAL reserved
    USHORT MinorVersion;                    // Minor version
    USHORT MajorVersion;                    // Major version
    UCHAR BuildType;                        // Build type
    UCHAR CpuVendor;                        // CPU vendor
    UCHAR CoresPerPhysicalProcessor;        // Cores per physical processor
    UCHAR LogicalProcessorsPerCore;         // Logical processors per core
    ULONG ApicMask;                         // APIC mask
    ULONG CFlushSize;                       // Cache flush size
    PVOID AcpiReserved;                     // ACPI reserved
    ULONG InitialApicId;                    // Initial APIC ID
    ULONG Stride;                           // Stride
    UINT64 PrcbPad01[3];                    // Padding
    
    // Lock queue
    KSPIN_LOCK_QUEUE LockQueue[LockQueueMaximumLock];  // Lock queue
    
    // Lookaside lists
    PP_LOOKASIDE_LIST PPLookasideList[16];  // Per-processor lookaside lists
    PP_LOOKASIDE_LIST PPNPagedLookasideList[32]; // Non-paged lookaside lists
    PP_LOOKASIDE_LIST PPPagedLookasideList[32];  // Paged lookaside lists
    ULONG LookasideIrpFloat;                // IRP lookaside float value
    
    // Scheduler fields
    LIST_ENTRY DispatcherReadyListHead[MAXIMUM_PRIORITY];  // Ready list heads
    ULONG ReadySummary;                     // Ready summary
    SINGLE_LIST_ENTRY DeferredReadyListHead; // Deferred ready list
    
    // DPC fields
    BOOLEAN DpcRoutineActive;               // DPC routine active
    BOOLEAN DpcThreadActive;                // DPC thread active
    BOOLEAN DpcThreadRequested;             // DPC thread requested
    BOOLEAN TimerRequest;                   // Timer request
    BOOLEAN DpcSetEventRequest;             // DPC set event request
    BOOLEAN ThreadDpcEnable;                // Thread DPC enable
    BOOLEAN Sleeping;                       // Processor sleeping
    BOOLEAN QuantumEnd;                     // Quantum end
    KEVENT DpcEvent;                        // DPC event
    
    // Statistics
    LONG MmPageFaultCount;                  // Page fault count
    LONG MmCopyOnWriteCount;                // Copy on write count
    LONG MmTransitionCount;                 // Transition count
    LONG MmDemandZeroCount;                 // Demand zero count
    LONG MmPageReadCount;                   // Page read count
    LONG MmPageReadIoCount;                 // Page read I/O count
    LONG MmDirtyPagesWriteCount;            // Dirty pages write count
    LONG MmDirtyWriteIoCount;               // Dirty write I/O count
    LONG MmMappedPagesWriteCount;           // Mapped pages write count
    LONG MmMappedWriteIoCount;              // Mapped write I/O count
    ULONG KeSystemCalls;                    // System calls
    ULONG KeContextSwitches;                // Context switches
    
    // Add more fields as needed for compatibility
    ULONG FeatureBits;                      // CPU feature bits
    UINT64 UpdateSignature;                 // Microcode update signature
    KDPC_DATA DpcData[2];                    // DPC data
    PVOID DpcStack;                          // DPC stack
    PVOID SavedRsp;                          // Saved RSP
    
    // Additional fields for compatibility
    ULONG QueueIndex;                        // Queue index for balmgr
    ULONG64 DebugDpcTime;                    // Debug DPC time
    LIST_ENTRY WaitListHead;                 // Wait list head
    ULONG MaximumDpcQueueDepth;              // Maximum DPC queue depth
    ULONG MinimumDpcRate;                    // Minimum DPC rate
    ULONG AdjustDpcThreshold;                // Adjust DPC threshold
    ULONG DpcRequestRate;                    // DPC request rate
    ULONG InterruptCount;                    // Interrupt count
    ULONG KernelTime;                        // Kernel time
    ULONG UserTime;                          // User time
    ULONG DpcTime;                           // DPC time
    ULONG InterruptTime;                     // Interrupt time
    ULONG SkipTick;                          // Skip tick
    ULONG TimerHand;                         // Timer hand
    PVOID CallDpc;                           // Call DPC
    ULONG DpcLastCount;                      // DPC last count
    ULONG DpcInterruptRequested;             // DPC interrupt requested
    KIRQL DebuggerSavedIRQL;                 // Debugger saved IRQL
    ULONG64 MultiThreadProcessorSet;         // Multi-thread processor set
    CHAR VendorString[13];                   // CPU vendor string
    
    // I/O statistics
    LARGE_INTEGER IoReadTransferCount;       // I/O read transfer count
    LARGE_INTEGER IoWriteTransferCount;      // I/O write transfer count
    LARGE_INTEGER IoOtherTransferCount;      // I/O other transfer count
    ULONG IoReadOperationCount;              // I/O read operation count
    ULONG IoWriteOperationCount;             // I/O write operation count
    ULONG IoOtherOperationCount;             // I/O other operation count
    
    // ARM64 specific counters (needed for compatibility)
    ULONG KeAlignmentFixupCount;             // Alignment fixup count
    ULONG KeExceptionDispatchCount;          // Exception dispatch count
    ULONG KeFloatingEmulationCount;          // Floating emulation count
    ULONG KeFirstLevelTbFills;               // First level TLB fills
    ULONG KeSecondLevelTbFills;              // Second level TLB fills
    PKTHREAD MultiThreadSetMaster;           // Multi-thread set master
    
    // Power management state
    PROCESSOR_POWER_STATE PowerState;        // Processor power state
    
    // Scheduler fields
    BOOLEAN IdleSchedule;                    // Idle schedule flag
    
    // Color mask for secondary colors
    ULONG SecondaryColorMask;                // Secondary color mask
    
    // Padding to ensure size compatibility
    UINT64 PrcbPad[8];                      // Additional padding
} KPRCB, *PKPRCB;

//
// Processor Control Region
// Based on WoA
//
typedef struct _KPCR
{
    union
    {
        struct
        {
            ULONG TibPad0[2];
            PVOID Spare1;
            struct _KPCR *Self;
            PVOID  PcrReserved0;
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
    PKPRCB Prcb;
} KPCR, *PKPCR, KIPCR, *PKIPCR;

//
// Macro to get current KPCR
// ARM64 stores the PCR pointer in TPIDR_EL1 system register
//
FORCEINLINE
struct _KPCR *
KeGetPcr(VOID)
{
    struct _KPCR *Pcr;
    __asm__ __volatile__("mrs %0, tpidr_el1" : "=r" (Pcr));
    return Pcr;
}

//
// Macro to get current KPRCB
//
FORCEINLINE
struct _KPRCB *
KeGetCurrentPrcb(VOID)
{  
    return KeGetPcr()->Prcb;
}

//
// Just read it from the PCR
//
#define KeGetCurrentIrql()             KeGetPcr()->CurrentIrql
#define _KeGetCurrentThread()          KeGetCurrentPrcb()->CurrentThread
#define _KeGetPreviousMode()           KeGetCurrentPrcb()->CurrentThread->PreviousMode
#define _KeIsExecutingDpc()            (KeGetCurrentPrcb()->DpcRoutineActive != 0)
#define KeGetCurrentThread()           _KeGetCurrentThread()
#define KeGetPreviousMode()            _KeGetPreviousMode()

#endif // !NTOS_MODE_USER

#ifdef __cplusplus
}; // extern "C"
#endif

#endif // !_ARM64_KETYPES_H
