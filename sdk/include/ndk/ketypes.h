/*++ NDK Version: 0098

Copyright (c) Alex Ionescu.  All rights reserved.

Header Name:

    ketypes.h

Abstract:

    Type definitions for the Kernel services.

Author:

    Alex Ionescu (alexi@tinykrnl.org) - Updated - 27-Feb-2006

--*/

#ifndef _KETYPES_H
#define _KETYPES_H

//
// Dependencies
//
#include <umtypes.h>
#ifndef NTOS_MODE_USER
#include <haltypes.h>
#include <potypes.h>
#include <ifssupp.h>
#endif

//
// A system call ID is formatted as such:
// .________________________________________________________________.
// | 14 | 13 | 12 | 11 | 10 | 9 | 8 | 7 | 6 | 5 | 4 | 3 | 2 | 1 | 0 |
// |--------------|-------------------------------------------------|
// | TABLE NUMBER |                  TABLE OFFSET                   |
// \----------------------------------------------------------------/
//
// The table number is then used as an index into the service descriptor table.
#define TABLE_NUMBER_BITS 1
#define TABLE_OFFSET_BITS 12

//
// There are 2 tables (kernel and shadow, used by Win32K)
//
#define NUMBER_SERVICE_TABLES 2
#define NTOS_SERVICE_INDEX   0
#define WIN32K_SERVICE_INDEX 1

//
// NB. From assembly code, the table number must be computed as an offset into
//     the service descriptor table.
//
//     Each entry into the table is 16 bytes long on 32-bit architectures, and
//     32 bytes long on 64-bit architectures.
//
//     Thus, Table Number 1 is offset 16 (0x10) on x86, and offset 32 (0x20) on
//     x64.
//
#ifdef _WIN64
#define BITS_PER_ENTRY 5 // (1 << 5) = 32 bytes
#else
#define BITS_PER_ENTRY 4 // (1 << 4) = 16 bytes
#endif

//
// We want the table number, but leave some extra bits to we can have the offset
// into the descriptor table.
//
#define SERVICE_TABLE_SHIFT (12 - BITS_PER_ENTRY)

//
// Now the table number (as an offset) is corrupted with part of the table offset
// This mask will remove the extra unwanted bits, and give us the offset into the
// descriptor table proper.
//
#define SERVICE_TABLE_MASK  (((1 << TABLE_NUMBER_BITS) - 1) << BITS_PER_ENTRY)

//
// To get the table offset (ie: the service call number), just keep the 12 bits
//
#define SERVICE_NUMBER_MASK ((1 << TABLE_OFFSET_BITS) - 1)

//
// We'll often need to check if this is a graphics call. This is done by comparing
// the table number offset with the known Win32K table number offset.
// This is usually index 1, so table number offset 0x10 (x86) or 0x20 (x64)
//
#define SERVICE_TABLE_TEST  (WIN32K_SERVICE_INDEX << BITS_PER_ENTRY)

//
// Context Record Flags
//
#define CONTEXT_DEBUGGER                (CONTEXT_FULL | CONTEXT_FLOATING_POINT)

//
// Maximum System Descriptor Table Entries
//
#define SSDT_MAX_ENTRIES                2

//
// Processor Architectures
//
#define PROCESSOR_ARCHITECTURE_INTEL    0
#define PROCESSOR_ARCHITECTURE_MIPS     1
#define PROCESSOR_ARCHITECTURE_ALPHA    2
#define PROCESSOR_ARCHITECTURE_PPC      3
#define PROCESSOR_ARCHITECTURE_SHX      4
#define PROCESSOR_ARCHITECTURE_ARM      5
#define PROCESSOR_ARCHITECTURE_IA64     6
#define PROCESSOR_ARCHITECTURE_ALPHA64  7
#define PROCESSOR_ARCHITECTURE_MSIL     8
#define PROCESSOR_ARCHITECTURE_AMD64    9
#define PROCESSOR_ARCHITECTURE_UNKNOWN  0xFFFF

//
// Object Type Mask for Kernel Dispatcher Objects
//
#define KOBJECT_TYPE_MASK               0x7F
#define KOBJECT_LOCK_BIT                0x80

//
// KWAIT_BLOCK.BlockState values (per-object dispatcher locking)
//
#define WaitBlockBypassStart            0
#define WaitBlockBypassComplete         1
#define WaitBlockActive                 4
#define WaitBlockInactive               5

//
// Dispatcher Priority increments
//
#define THREAD_ALERT_INCREMENT          2

//
// Quantum values and decrements
//
#define MAX_QUANTUM                     0x7F
#define WAIT_QUANTUM_DECREMENT          1
#define CLOCK_QUANTUM_DECREMENT         3

//
// Internal Exception Codes
//
#define KI_EXCEPTION_INTERNAL           0x10000000
#define KI_EXCEPTION_ACCESS_VIOLATION   (KI_EXCEPTION_INTERNAL | 0x04)

typedef struct _FIBER                                    /* Field offsets:    */
{                                                        /* i386  arm   x64   */
    PVOID FiberData;                                     /* 0x000 0x000 0x000 */
    struct _EXCEPTION_REGISTRATION_RECORD *ExceptionList;/* 0x004 0x004 0x008 */
    PVOID StackBase;                                     /* 0x008 0x008 0x010 */
    PVOID StackLimit;                                    /* 0x00C 0x00C 0x018 */
    PVOID DeallocationStack;                             /* 0x010 0x010 0x020 */
    CONTEXT FiberContext;                                /* 0x014 0x018 0x030 */
#if (NTDDI_VERSION >= NTDDI_LONGHORN)
    PVOID Wx86Tib;                                       /* 0x2E0 0x1b8 0x500 */
    struct _ACTIVATION_CONTEXT_STACK *ActivationContextStackPointer; /* 0x2E4 0x1bc 0x508 */
    PVOID FlsData;                                       /* 0x2E8 0x1c0 0x510 */
    ULONG GuaranteedStackBytes;                          /* 0x2EC 0x1c4 0x518 */
    ULONG TebFlags;                                      /* 0x2F0 0x1c8 0x51C */
#else
    ULONG GuaranteedStackBytes;                          /* 0x2E0         */
    PVOID FlsData;                                       /* 0x2E4         */
    struct _ACTIVATION_CONTEXT_STACK *ActivationContextStackPointer;
#endif
} FIBER, *PFIBER;

//
// KUSER_SHARED_DATA location in User Mode
//
#define USER_SHARED_DATA                0x7FFE0000

#ifndef NTOS_MODE_USER

//
// Extended processor affinity. The fixed backing array matches the native
// kernel ABI while Bitmap preserves the variable-length view used by helpers.
//
#define KAFFINITY_EX_INITIALIZED_GROUPS 20
#define KAFFINITY_EX_STATIC_GROUPS 32

typedef struct _KAFFINITY_EX
{
    USHORT Count;
    USHORT Size;
    ULONG Reserved;
    union
    {
        KAFFINITY Bitmap[ANYSIZE_ARRAY];
        KAFFINITY StaticBitmap[KAFFINITY_EX_STATIC_GROUPS];
    };
} KAFFINITY_EX;

typedef struct _KAFFINITY_ENUMERATION_CONTEXT
{
    PKAFFINITY_EX Affinity;
    KAFFINITY CurrentAffinity;
    USHORT CurrentGroup;
} KAFFINITY_ENUMERATION_CONTEXT, *PKAFFINITY_ENUMERATION_CONTEXT;

//
// Number of dispatch codes supported by KINTERRUPT
//
#ifdef _M_AMD64
#define DISPATCH_LENGTH                 4
#elif (NTDDI_VERSION >= NTDDI_LONGHORN)
#define DISPATCH_LENGTH                 135
#else
#define DISPATCH_LENGTH                 106
#endif

#else // NTOS_MODE_USER

//
// KPROCESSOR_MODE Type
//
typedef CCHAR KPROCESSOR_MODE;

//
// Dereferencable pointer to KUSER_SHARED_DATA in User-Mode
//
#define SharedUserData                  ((KUSER_SHARED_DATA *)USER_SHARED_DATA)

#ifdef _X86_
/* Macros for user-mode run-time checks of X86 system architecture */

#ifndef IsNEC_98
#define IsNEC_98     (SharedUserData->AlternativeArchitecture == NEC98x86)
#endif

#ifndef IsNotNEC_98
#define IsNotNEC_98  (SharedUserData->AlternativeArchitecture != NEC98x86)
#endif

/* User-mode cannot override the architecture */
#ifndef SetNEC_98
#define SetNEC_98
#endif

/* User-mode cannot override the architecture */
#ifndef SetNotNEC_98
#define SetNotNEC_98
#endif

#else // !_X86_
/* Correctly define these run-time definitions for non X86 machines */

#ifndef IsNEC_98
#define IsNEC_98 (FALSE)
#endif

#ifndef IsNotNEC_98
#define IsNotNEC_98 (TRUE)
#endif

#ifndef SetNEC_98
#define SetNEC_98
#endif

#ifndef SetNotNEC_98
#define SetNotNEC_98
#endif

#endif // _X86_

//
// Maximum WOW64 Entries in KUSER_SHARED_DATA
//
#define MAX_WOW64_SHARED_ENTRIES        16

//
// Maximum Processor Features supported in KUSER_SHARED_DATA
//
#define PROCESSOR_FEATURE_MAX           64

//
// Event Types
//
typedef enum _EVENT_TYPE
{
    NotificationEvent,
    SynchronizationEvent
} EVENT_TYPE;

//
// Timer Types
//
typedef enum _TIMER_TYPE
{
    NotificationTimer,
    SynchronizationTimer
} TIMER_TYPE;

//
// Wait Types
//
typedef enum _WAIT_TYPE
{
    WaitAll,
    WaitAny,
    WaitNotification,
    WaitDequeue,
    WaitDpc
} WAIT_TYPE;

//
// Processor Execution Modes
//
typedef enum _MODE
{
    KernelMode,
    UserMode,
    MaximumMode
} MODE;

//
// Wait Reasons
//
typedef enum _KWAIT_REASON
{
    Executive,
    FreePage,
    PageIn,
    PoolAllocation,
    DelayExecution,
    Suspended,
    UserRequest,
    WrExecutive,
    WrFreePage,
    WrPageIn,
    WrPoolAllocation,
    WrDelayExecution,
    WrSuspended,
    WrUserRequest,
    WrEventPair,
    WrQueue,
    WrLpcReceive,
    WrLpcReply,
    WrVirtualMemory,
    WrPageOut,
    WrRendezvous,
    WrKeyedEvent,
    WrTerminated,
    WrProcessInSwap,
    WrCpuRateControl,
    WrCalloutStack,
    WrKernel,
    WrResource,
    WrPushLock,
    WrMutex,
    WrQuantumEnd,
    WrDispatchInt,
    WrPreempted,
    WrYieldExecution,
    WrFastMutex,
    WrGuardedMutex,
    WrRundown,
    WrAlertByThreadId,
    WrDeferredPreempt,
#if (NTDDI_VERSION >= NTDDI_WIN10_RS3)
    WrPhysicalFault,
#endif
#if (NTDDI_VERSION >= NTDDI_WIN11)
    WrIoRing,
#endif
#if (NTDDI_VERSION >= NTDDI_WIN11_NI)
    WrMdlCache,
#endif
#if (NTDDI_VERSION >= NTDDI_WIN11_GE)
    WrRcu,
#endif
    MaximumWaitReason
} KWAIT_REASON;

//
// Profiling Sources
//
typedef enum _KPROFILE_SOURCE
{
    ProfileTime,
    ProfileAlignmentFixup,
    ProfileTotalIssues,
    ProfilePipelineDry,
    ProfileLoadInstructions,
    ProfilePipelineFrozen,
    ProfileBranchInstructions,
    ProfileTotalNonissues,
    ProfileDcacheMisses,
    ProfileIcacheMisses,
    ProfileCacheMisses,
    ProfileBranchMispredictions,
    ProfileStoreInstructions,
    ProfileFpInstructions,
    ProfileIntegerInstructions,
    Profile2Issue,
    Profile3Issue,
    Profile4Issue,
    ProfileSpecialInstructions,
    ProfileTotalCycles,
    ProfileIcacheIssues,
    ProfileDcacheAccesses,
    ProfileMemoryBarrierCycles,
    ProfileLoadLinkedIssues,
    ProfileMaximum
} KPROFILE_SOURCE;

//
// NT Product and Architecture Types
//
typedef enum _NT_PRODUCT_TYPE
{
    NtProductWinNt = 1,
    NtProductLanManNt,
    NtProductServer
} NT_PRODUCT_TYPE, *PNT_PRODUCT_TYPE;

typedef enum _ALTERNATIVE_ARCHITECTURE_TYPE
{
    StandardDesign,
    NEC98x86,
    EndAlternatives
} ALTERNATIVE_ARCHITECTURE_TYPE;

//
// Flags for NXSupportPolicy
//
#if (NTDDI_VERSION >= NTDDI_WINXPSP2)
#define NX_SUPPORT_POLICY_ALWAYSOFF 0
#define NX_SUPPORT_POLICY_ALWAYSON  1
#define NX_SUPPORT_POLICY_OPTIN     2
#define NX_SUPPORT_POLICY_OPTOUT    3
#endif

#endif // NTOS_MODE_USER

//
// Thread States
//
typedef enum _KTHREAD_STATE
{
    Initialized,
    Ready,
    Running,
    Standby,
    Terminated,
    Waiting,
    Transition,
    DeferredReady,
#if (NTDDI_VERSION >= NTDDI_WS03)
    GateWait
#endif
} KTHREAD_STATE, *PKTHREAD_STATE;

//
// Kernel Object Types
//
typedef enum _KOBJECTS
{
    EventNotificationObject = 0,
    EventSynchronizationObject = 1,
    MutantObject = 2,
    ProcessObject = 3,
    QueueObject = 4,
    SemaphoreObject = 5,
    ThreadObject = 6,
    GateObject = 7,
    TimerNotificationObject = 8,
    TimerSynchronizationObject = 9,
    Spare2Object = 10,
    Spare3Object = 11,
    Spare4Object = 12,
    Spare5Object = 13,
    Spare6Object = 14,
    Spare7Object = 15,
    Spare8Object = 16,
    Spare9Object = 17,
    ApcObject = 18,
    DpcObject = 19,
    DeviceQueueObject = 20,
    EventPairObject = 21,
    InterruptObject = 22,
    ProfileObject = 23,
    ThreadedDpcObject = 26,
    MaximumKernelObject = 27
} KOBJECTS;

//
// Adjust reasons
//
typedef enum _ADJUST_REASON
{
    AdjustNone = 0,
    AdjustUnwait = 1,
    AdjustBoost = 2
} ADJUST_REASON;

//
// Continue Status
//
typedef enum _KCONTINUE_STATUS
{
    ContinueError = 0,
    ContinueSuccess,
    ContinueProcessorReselected,
    ContinueNextProcessor
} KCONTINUE_STATUS;

typedef enum _KCONTINUE_TYPE
{
    KCONTINUE_UNWIND,
    KCONTINUE_RESUME,
    KCONTINUE_LONGJUMP,
    KCONTINUE_SET,
    KCONTINUE_LAST
} KCONTINUE_TYPE;

typedef struct _KCONTINUE_ARGUMENT
{
    KCONTINUE_TYPE ContinueType;
    ULONG ContinueFlags;
    ULONGLONG Reserved[2];
} KCONTINUE_ARGUMENT, *PKCONTINUE_ARGUMENT;

#define KCONTINUE_FLAG_TEST_ALERT  0x01
#define KCONTINUE_FLAG_DELIVER_APC 0x02

//
// Process States
//
typedef enum _KPROCESS_STATE
{
    ProcessInMemory,
    ProcessOutOfMemory,
    ProcessInTransition,
    ProcessInSwap,
    ProcessOutSwap,
} KPROCESS_STATE, *PKPROCESS_STATE;

//
// NtVdmControl Classes
//
typedef enum _VDMSERVICECLASS
{
   VdmStartExecution = 0,
   VdmQueueInterrupt = 1,
   VdmDelayInterrupt = 2,
   VdmInitialize = 3,
   VdmFeatures = 4,
   VdmSetInt21Handler = 5,
   VdmQueryDir = 6,
   VdmPrinterDirectIoOpen = 7,
   VdmPrinterDirectIoClose = 8,
   VdmPrinterInitialize = 9,
   VdmSetLdtEntries = 10,
   VdmSetProcessLdtInfo = 11,
   VdmAdlibEmulation = 12,
   VdmPMCliControl = 13,
   VdmQueryVdmProcess = 14,
} VDMSERVICECLASS;

#ifdef NTOS_MODE_USER

//
// APC Normal Routine
//
typedef VOID
(NTAPI *PKNORMAL_ROUTINE)(
    _In_ PVOID NormalContext,
    _In_ PVOID SystemArgument1,
    _In_ PVOID SystemArgument2
);

//
// Timer Routine
//
typedef VOID
(NTAPI *PTIMER_APC_ROUTINE)(
    _In_ PVOID TimerContext,
    _In_ ULONG TimerLowValue,
    _In_ LONG TimerHighValue
);

//
// System Time Structure
//
typedef struct _KSYSTEM_TIME
{
    ULONG LowPart;
    LONG High1Time;
    LONG High2Time;
} KSYSTEM_TIME, *PKSYSTEM_TIME;

//
// Shared Kernel User Data
// Keep in sync with sdk/include/xdk/ketypes.h
//
typedef struct _KUSER_SHARED_DATA
{
    ULONG TickCountLowDeprecated;                           // 0x0
    ULONG TickCountMultiplier;                              // 0x4
    volatile KSYSTEM_TIME InterruptTime;                    // 0x8
    volatile KSYSTEM_TIME SystemTime;                       // 0x14
    volatile KSYSTEM_TIME TimeZoneBias;                     // 0x20
    USHORT ImageNumberLow;                                  // 0x2c
    USHORT ImageNumberHigh;                                 // 0x2e
    WCHAR NtSystemRoot[260];                                // 0x30
    ULONG MaxStackTraceDepth;                               // 0x238
    ULONG CryptoExponent;                                   // 0x23c
    ULONG TimeZoneId;                                       // 0x240
    ULONG LargePageMinimum;                                 // 0x244

#if (NTDDI_VERSION >= NTDDI_WIN8)
    ULONG AitSamplingValue;                                 // 0x248
    ULONG AppCompatFlag;                                    // 0x24c
    ULONGLONG RNGSeedVersion;                               // 0x250
    ULONG GlobalValidationRunlevel;                         // 0x258
    volatile LONG TimeZoneBiasStamp;                        // 0x25c
#if (NTDDI_VERSION >= NTDDI_WIN10)
    ULONG NtBuildNumber;                                    // 0x260
#else
    ULONG Reserved2;                                        // 0x260
#endif
#else
    ULONG Reserved2[7];                                     // 0x248
#endif // NTDDI_VERSION >= NTDDI_WIN8

    NT_PRODUCT_TYPE NtProductType;                          // 0x264
    BOOLEAN ProductTypeIsValid;                             // 0x268
    BOOLEAN Reserved0[1];                                   // 0x269
#if (NTDDI_VERSION >= NTDDI_WIN8)
    USHORT NativeProcessorArchitecture;                     // 0x26a
#endif
    ULONG NtMajorVersion;                                   // 0x26c
    ULONG NtMinorVersion;                                   // 0x270
    BOOLEAN ProcessorFeatures[PROCESSOR_FEATURE_MAX];       // 0x274
    ULONG Reserved1;                                        // 0x2b4
    ULONG Reserved3;                                        // 0x2b8
    volatile ULONG TimeSlip;                                // 0x2bc
    ALTERNATIVE_ARCHITECTURE_TYPE AlternativeArchitecture;  // 0x2c0
#if (NTDDI_VERSION >= NTDDI_WIN10)
    ULONG BootId;                                           // 0x2c4
#else
    ULONG AltArchitecturePad[1];                            // 0x2c4
#endif
    LARGE_INTEGER SystemExpirationDate;                     // 0x2c8
    ULONG SuiteMask;                                        // 0x2d0
    BOOLEAN KdDebuggerEnabled;                              // 0x2d4
    union
    {
        UCHAR MitigationPolicies;                           // 0x2d5
        struct
        {
            UCHAR NXSupportPolicy : 2;
            UCHAR SEHValidationPolicy : 2;
            UCHAR CurDirDevicesSkippedForDlls : 2;
            UCHAR Reserved : 2;
        };
    };
#if (NTDDI_VERSION >= NTDDI_WIN10_19H1)
    USHORT CyclesPerYield;                                  // 0x2d6 // Win 10 19H1+
#else
    UCHAR Reserved6[2];                                     // 0x2d6
#endif
    volatile ULONG ActiveConsoleId;                         // 0x2d8
    volatile ULONG DismountCount;                           // 0x2dc
    ULONG ComPlusPackage;                                   // 0x2e0
    ULONG LastSystemRITEventTickCount;                      // 0x2e4
    ULONG NumberOfPhysicalPages;                            // 0x2e8
    BOOLEAN SafeBootMode;                                   // 0x2ec

#if (NTDDI_VERSION == NTDDI_WIN7)
    union
    {
        UCHAR TscQpcData;                                   // 0x2ed
        struct
        {
            UCHAR TscQpcEnabled:1;                          // 0x2ed
            UCHAR TscQpcSpareFlag:1;                        // 0x2ed
            UCHAR TscQpcShift:6;                            // 0x2ed
        } DUMMYSTRUCTNAME;
    } DUMMYUNIONNAME;
    UCHAR TscQpcPad[2];                                     // 0x2ee
#elif (NTDDI_VERSION >= NTDDI_WIN10_RS1)
    union
    {
        UCHAR VirtualizationFlags;                          // 0x2ed
#if defined(_ARM64_)
        struct
        {
            UCHAR ArchStartedInEl2 : 1;
            UCHAR QcSlIsSupported : 1;
            UCHAR : 6;
        };
#endif
    };
    UCHAR Reserved12[2];                                    // 0x2ee
#else
    UCHAR Reserved12[3];                                    // 0x2ed
#endif // NTDDI_VERSION == NTDDI_WIN7

#if (NTDDI_VERSION >= NTDDI_VISTA)
    union
    {
        ULONG SharedDataFlags;                              // 0x2f0
        struct
        {
            ULONG DbgErrorPortPresent : 1;                  // 0x2f0
            ULONG DbgElevationEnabled : 1;                  // 0x2f0
            ULONG DbgVirtEnabled : 1;                       // 0x2f0
            ULONG DbgInstallerDetectEnabled : 1;            // 0x2f0
#if (NTDDI_VERSION >= NTDDI_WIN8)
            ULONG DbgLkgEnabled : 1;                        // 0x2f0
#else
            ULONG DbgSystemDllRelocated : 1;                // 0x2f0
#endif
            ULONG DbgDynProcessorEnabled : 1;               // 0x2f0
#if (NTDDI_VERSION >= NTDDI_WIN8)
            ULONG DbgConsoleBrokerEnabled : 1;              // 0x2f0
#else
            ULONG DbgSEHValidationEnabled : 1;              // 0x2f0
#endif
            ULONG DbgSecureBootEnabled : 1;                 // 0x2f0 Win8+
            ULONG DbgMultiSessionSku : 1;                   // 0x2f0 Win 10+
            ULONG DbgMultiUsersInSessionSku : 1;            // 0x2f0 Win 10 RS1+
            ULONG DbgStateSeparationEnabled : 1;            // 0x2f0 Win 10 RS3+
            ULONG SpareBits                 : 21;           // 0x2f0
        } DUMMYSTRUCTNAME2;
    } DUMMYUNIONNAME2;
#else
    ULONG TraceLogging;
#endif // NTDDI_VERSION >= NTDDI_VISTA

    ULONG DataFlagsPad[1];                                  // 0x2f4
    ULONGLONG TestRetInstruction;                           // 0x2f8
#if (NTDDI_VERSION >= NTDDI_WIN8)
    ULONGLONG QpcFrequency;                                 // 0x300
#else
    ULONG SystemCall;                                       // 0x300
    ULONG SystemCallReturn;                                 // 0x304
#endif
#if (NTDDI_VERSION >= NTDDI_WIN10_TH2)
    ULONG SystemCall;                                       // 0x308
    ULONG SystemCallPad0;                                   // 0x30c Renamed to Reserved2 in Vibranium R3
    ULONGLONG SystemCallPad[2];                             // 0x310
#else
    ULONGLONG SystemCallPad[3];                             // 0x308
#endif
    union
    {
        volatile KSYSTEM_TIME TickCount;                    // 0x320
        volatile ULONG64 TickCountQuad;                     // 0x320
        struct
        {
            ULONG ReservedTickCountOverlay[3];              // 0x320
            ULONG TickCountPad[1];                          // 0x32c
        } DUMMYSTRUCTNAME;
    } DUMMYUNIONNAME3;
    ULONG Cookie;                                           // 0x330

#if (NTDDI_VERSION < NTDDI_VISTA)
    ULONG Wow64SharedInformation[MAX_WOW64_SHARED_ENTRIES]; // 0x334
#endif

//
// Windows Vista and later
//
#if (NTDDI_VERSION >= NTDDI_VISTA)

    ULONG CookiePad[1];                                     // 0x334
    LONGLONG ConsoleSessionForegroundProcessId;             // 0x338

#if (NTDDI_VERSION >= NTDDI_WIN8)
#if (NTDDI_VERSION >= NTDDI_WINBLUE)
    ULONGLONG TimeUpdateLock;                               // 0x340
#else
    ULONGLONG TimeUpdateSequence;                           // 0x340
#endif
    ULONGLONG BaselineSystemTimeQpc;                        // 0x348
    ULONGLONG BaselineInterruptTimeQpc;                     // 0x350
    ULONGLONG QpcSystemTimeIncrement;                       // 0x358
    ULONGLONG QpcInterruptTimeIncrement;                    // 0x360
#if (NTDDI_VERSION >= NTDDI_WIN10)
    UCHAR QpcSystemTimeIncrementShift;                      // 0x368
    UCHAR QpcInterruptTimeIncrementShift;                   // 0x369
    USHORT UnparkedProcessorCount;                          // 0x36a
    ULONG EnclaveFeatureMask[4];                            // 0x36c Win 10 TH2+
    ULONG TelemetryCoverageRound;                           // 0x37c Win 10 RS2+
#else // NTDDI_VERSION < NTDDI_WIN10
    ULONG QpcSystemTimeIncrement32;                         // 0x368
    ULONG QpcInterruptTimeIncrement32;                      // 0x36c
    UCHAR QpcSystemTimeIncrementShift;                      // 0x370
    UCHAR QpcInterruptTimeIncrementShift;                   // 0x371
#if (NTDDI_VERSION >= NTDDI_WINBLUE)
    USHORT UnparkedProcessorCount;                          // 0x372
    UCHAR Reserved8[12];                                    // 0x374
#else
    UCHAR Reserved8[14];                                    // 0x372
#endif
#endif // NTDDI_VERSION < NTDDI_WIN10
#elif (NTDDI_VERSION >= NTDDI_VISTASP2)
    ULONG DEPRECATED_Wow64SharedInformation[MAX_WOW64_SHARED_ENTRIES]; // 0x340
#else
    ULONG Wow64SharedInformation[MAX_WOW64_SHARED_ENTRIES]; // 0x340
#endif // NTDDI_VERSION >= NTDDI_VISTA

#if (NTDDI_VERSION >= NTDDI_WIN7)
    USHORT UserModeGlobalLogger[16];                        // 0x380
#else
    USHORT UserModeGlobalLogger[8];                         // 0x380
    ULONG HeapTracingPid[2];                                // 0x390
    ULONG CritSecTracingPid[2];                             // 0x398
#endif

    ULONG ImageFileExecutionOptions;                        // 0x3a0
    ULONG LangGenerationCount;                              // 0x3a4 Vista SP2+

#if (NTDDI_VERSION >= NTDDI_WIN8)
    ULONGLONG Reserved4;                                    // 0x3a8
#elif (NTDDI_VERSION >= NTDDI_WIN7)
    ULONGLONG Reserved5;                                    // 0x3a8
#else
    union
    {
        KAFFINITY ActiveProcessorAffinity;                  // 0x3a8
        ULONGLONG AffinityPad;                              // 0x3a8
    };
#endif

    volatile ULONGLONG InterruptTimeBias;                     // 0x3b0
#endif // NTDDI_VERSION >= NTDDI_VISTA

//
// Windows 7 and later
//
#if (NTDDI_VERSION >= NTDDI_WIN7)
    volatile ULONGLONG QpcBias;                            // 0x3b8 // Win7: TscQpcBias
    /* volatile */ ULONG ActiveProcessorCount;             // 0x3c0 // not volatile since Win 8.1 Update 1

#if (NTDDI_VERSION >= NTDDI_WIN8)
    volatile UCHAR ActiveGroupCount;                        // 0x3c4
    UCHAR Reserved9;                                        // 0x3c5
    union
    {
        USHORT QpcData;                                     // 0x3c6
        struct
        {
            volatile UCHAR QpcBypassEnabled;                // 0x3c6
            UCHAR QpcShift;                                 // 0x3c7
        };
    };
    LARGE_INTEGER TimeZoneBiasEffectiveStart;               // 0x3c8
    LARGE_INTEGER TimeZoneBiasEffectiveEnd;                 // 0x3d0
    XSTATE_CONFIGURATION XState;                            // 0x3d8
#else
    USHORT ActiveGroupCount;                                // 0x3c4
    USHORT Reserved4;                                       // 0x3c6
    volatile ULONG AitSamplingValue;                        // 0x3c8
    volatile ULONG AppCompatFlag;                           // 0x3cc
    ULONGLONG SystemDllNativeRelocation;                    // 0x3d0 deprecated in Win7 SP2
    ULONG SystemDllWowRelocation;                           // 0x3d8 deprecated in Win7 SP2
    ULONG XStatePad[1];                                     // 0x3dc
    XSTATE_CONFIGURATION XState;                            // 0x3e0
#endif // NTDDI_VERSION >= NTDDI_WIN8
#endif // NTDDI_VERSION >= NTDDI_WIN7

//
// Windows 10 Vibranium and later
//
#if (NTDDI_VERSION >= NTDDI_WIN10_VB)
    KSYSTEM_TIME FeatureConfigurationChangeStamp;           // 0x710 // Win 11: 0x720
    ULONG Spare;                                            // 0x71c // Win 11: 0x72c
#endif // NTDDI_VERSION >= NTDDI_WIN10_VB

//
// Windows 11 Nickel and later
//
#if (NTDDI_VERSION >= NTDDI_WIN11_NI)
    ULONG64 UserPointerAuthMask;                            // 0x730
#endif // NTDDI_VERSION >= NTDDI_WIN11_NI

#if (NTDDI_VERSION < NTDDI_WIN7) && defined(__REACTOS__)
    XSTATE_CONFIGURATION XState;
#endif
} KUSER_SHARED_DATA, *PKUSER_SHARED_DATA;

#endif

//
// Win10 19H1+ writable alias of KUSER_SHARED_DATA; the canonical kernel VA is mapped read-only
//
extern NTSYSAPI struct _KUSER_SHARED_DATA *MmWriteableSharedUserData;

#ifdef NTOS_MODE_USER

//
// VDM Structures
//
#include "pshpack1.h"
typedef struct _VdmVirtualIca
{
    LONG ica_count[8];
    LONG ica_int_line;
    LONG ica_cpu_int;
    USHORT ica_base;
    USHORT ica_hipiri;
    USHORT ica_mode;
    UCHAR ica_master;
    UCHAR ica_irr;
    UCHAR ica_isr;
    UCHAR ica_imr;
    UCHAR ica_ssr;
} VDMVIRTUALICA, *PVDMVIRTUALICA;
#include "poppack.h"

typedef struct _VdmIcaUserData
{
    PVOID pIcaLock;
    PVDMVIRTUALICA pIcaMaster;
    PVDMVIRTUALICA pIcaSlave;
    PULONG pDelayIrq;
    PULONG pUndelayIrq;
    PULONG pDelayIret;
    PULONG pIretHooked;
    PULONG pAddrIretBopTable;
    PHANDLE phWowIdleEvent;
    PLARGE_INTEGER pIcaTimeout;
    PHANDLE phMainThreadSuspended;
} VDMICAUSERDATA, *PVDMICAUSERDATA;

typedef struct _VDM_INITIALIZE_DATA
{
    PVOID TrapcHandler;
    PVDMICAUSERDATA IcaUserData;
} VDM_INITIALIZE_DATA, *PVDM_INITIALIZE_DATA;

#else

//
// System Thread Start Routine
//
typedef
VOID
(NTAPI *PKSYSTEM_ROUTINE)(
    PKSTART_ROUTINE StartRoutine,
    PVOID StartContext
);

#ifndef _NTSYSTEM_
typedef VOID
(NTAPI *PKNORMAL_ROUTINE)(
  IN PVOID NormalContext OPTIONAL,
  IN PVOID SystemArgument1 OPTIONAL,
  IN PVOID SystemArgument2 OPTIONAL);

typedef VOID
(NTAPI *PKRUNDOWN_ROUTINE)(
  IN struct _KAPC *Apc);

typedef VOID
(NTAPI *PKKERNEL_ROUTINE)(
  IN struct _KAPC *Apc,
  IN OUT PKNORMAL_ROUTINE *NormalRoutine OPTIONAL,
  IN OUT PVOID *NormalContext OPTIONAL,
  IN OUT PVOID *SystemArgument1 OPTIONAL,
  IN OUT PVOID *SystemArgument2 OPTIONAL);
#endif

//
// APC Environment Types
//
typedef enum _KAPC_ENVIRONMENT
{
    OriginalApcEnvironment,
    AttachedApcEnvironment,
    CurrentApcEnvironment,
    InsertApcEnvironment
} KAPC_ENVIRONMENT;

typedef struct _KTIMER_TABLE_ENTRY
{
#if (NTDDI_VERSION >= NTDDI_LONGHORN) || defined(_M_ARM) || defined(_M_AMD64)
    KSPIN_LOCK Lock;
#endif
    LIST_ENTRY Entry;
    ULARGE_INTEGER Time;
} KTIMER_TABLE_ENTRY, *PKTIMER_TABLE_ENTRY;

typedef struct _KTIMER_TABLE
{
    PKTIMER TimerExpiry[64];
    KTIMER_TABLE_ENTRY TimerEntries[256];
} KTIMER_TABLE, *PKTIMER_TABLE;

typedef struct _KDPC_LIST
{
    SINGLE_LIST_ENTRY ListHead;
    SINGLE_LIST_ENTRY* LastEntry;
} KDPC_LIST, *PKDPC_LIST;

typedef struct _SYNCH_COUNTERS
{
    ULONG SpinLockAcquireCount;
    ULONG SpinLockContentionCount;
    ULONG SpinLockSpinCount;
    ULONG IpiSendRequestBroadcastCount;
    ULONG IpiSendRequestRoutineCount;
    ULONG IpiSendSoftwareInterruptCount;
    ULONG ExInitializeResourceCount;
    ULONG ExReInitializeResourceCount;
    ULONG ExDeleteResourceCount;
    ULONG ExecutiveResourceAcquiresCount;
    ULONG ExecutiveResourceContentionsCount;
    ULONG ExecutiveResourceReleaseExclusiveCount;
    ULONG ExecutiveResourceReleaseSharedCount;
    ULONG ExecutiveResourceConvertsCount;
    ULONG ExAcqResExclusiveAttempts;
    ULONG ExAcqResExclusiveAcquiresExclusive;
    ULONG ExAcqResExclusiveAcquiresExclusiveRecursive;
    ULONG ExAcqResExclusiveWaits;
    ULONG ExAcqResExclusiveNotAcquires;
    ULONG ExAcqResSharedAttempts;
    ULONG ExAcqResSharedAcquiresExclusive;
    ULONG ExAcqResSharedAcquiresShared;
    ULONG ExAcqResSharedAcquiresSharedRecursive;
    ULONG ExAcqResSharedWaits;
    ULONG ExAcqResSharedNotAcquires;
    ULONG ExAcqResSharedStarveExclusiveAttempts;
    ULONG ExAcqResSharedStarveExclusiveAcquiresExclusive;
    ULONG ExAcqResSharedStarveExclusiveAcquiresShared;
    ULONG ExAcqResSharedStarveExclusiveAcquiresSharedRecursive;
    ULONG ExAcqResSharedStarveExclusiveWaits;
    ULONG ExAcqResSharedStarveExclusiveNotAcquires;
    ULONG ExAcqResSharedWaitForExclusiveAttempts;
    ULONG ExAcqResSharedWaitForExclusiveAcquiresExclusive;
    ULONG ExAcqResSharedWaitForExclusiveAcquiresShared;
    ULONG ExAcqResSharedWaitForExclusiveAcquiresSharedRecursive;
    ULONG ExAcqResSharedWaitForExclusiveWaits;
    ULONG ExAcqResSharedWaitForExclusiveNotAcquires;
    ULONG ExSetResOwnerPointerExclusive;
    ULONG ExSetResOwnerPointerSharedNew;
    ULONG ExSetResOwnerPointerSharedOld;
    ULONG ExTryToAcqExclusiveAttempts;
    ULONG ExTryToAcqExclusiveAcquires;
    ULONG ExBoostExclusiveOwner;
    ULONG ExBoostSharedOwners;
    ULONG ExEtwSynchTrackingNotificationsCount;
    ULONG ExEtwSynchTrackingNotificationsAccountedCount;
} SYNCH_COUNTERS, *PSYNCH_COUNTERS;

//
// PRCB DPC Data
//
// On arm64 the layout is fixed to the Win11 shape (sizeof == 0x30) for any NTDDI_VERSION
typedef struct _KDPC_DATA
{
#if (NTDDI_VERSION >= NTDDI_LONGHORN) || defined(_M_ARM64)
    KDPC_LIST DpcList;
#else
    LIST_ENTRY DpcListHead;
#endif
    ULONG_PTR DpcLock;
#if defined(_M_AMD64) || defined(_M_ARM)
    volatile LONG DpcQueueDepth;
#else
    volatile ULONG DpcQueueDepth;
#endif
    ULONG DpcCount;
#if (NTDDI_VERSION >= NTDDI_LONGHORN) || defined(_M_ARM) || defined(_M_ARM64)
    PKDPC ActiveDpc;
#if defined(_M_ARM64)
    ULONG LongDpcPresent;
    ULONG Padding;
#endif
#endif
} KDPC_DATA, *PKDPC_DATA;

//
// Per-Processor Lookaside List
//
typedef struct _PP_LOOKASIDE_LIST
{
    struct _GENERAL_LOOKASIDE *P;
    struct _GENERAL_LOOKASIDE *L;
} PP_LOOKASIDE_LIST, *PPP_LOOKASIDE_LIST;

//
// Architectural Types
//
#include <arch/ketypes.h>

//
// Kernel Memory Node
//
// The Win11 (ntkrnlmp.pdb _KNODE, type 0x1B32) _KNODE is a 0x178-byte topology
// node that bears no resemblance to the pre-Vista layout ReactOS historically
// used (ProcessorMask/Color/Seed/FreeCount...). The fields the tree actually
// reads (ProcessorMask, Seed, NodeNumber, FreeCount) are kept by name - either
// as a real Win11 field (NodeNumber) or as a ReactOS-compat alias unioned over
// the Win11 member that holds the same datum (ProcessorMask == ActiveGroups
// group-0 mask) or over a Win11 region ReactOS never touches (Seed, FreeCount).
//
typedef struct _KNODE
{
    /* Public Win11 architectural fields. */
    USHORT NodeNumber;                                   // 0x000
    USHORT PrimaryNodeNumber;                            // 0x002
    ULONG ProximityId;                                   // 0x004
    USHORT MaximumProcessors;                            // 0x008
    struct                                               // 0x00A (sizeof 1)
    {
        UCHAR ProcessorOnly : 1;
        UCHAR GroupsAssigned : 1;
        UCHAR MeasurableDistance : 1;
    } Flags;
    union                                                // 0x00B
    {
        UCHAR GroupSeed;                                 // Win11
        /* ReactOS-private: legacy per-node thread round-robin seed (procobj.c).
           Win11 keeps the scheduling seed in KSCHEDULER_SUBNODE.ProcessSeed. */
        UCHAR Seed;
    };
    UCHAR PrimaryGroup;                                  // 0x00C
    UCHAR Padding[3];                                    // 0x00D
    union                                                // 0x010
    {
        ULONG64 ActiveGroups[2];                         // Win11 KGROUP_MASK { ULONG64 Masks[2] }
        /* ReactOS-compat alias: this node's processor mask == ActiveGroups[0]
           (group 0), mirroring how Win11 stores the per-node affinity. Read and
           written across procobj.c/thrdobj.c/thrdschd.c/topology.c/sysinfo.c. */
        KAFFINITY ProcessorMask;
    };
    union                                                // 0x020
    {
        struct _KSCHEDULER_SUBNODE *SchedulerSubNodes[32];   // Win11 (256 bytes)
        /* ReactOS-private: legacy MM per-node free-page counts (read-only in
           sysinfo.c, never written in this tree -> always 0). Overlays the
           ReactOS-unused Win11 SchedulerSubNodes pointer array. */
        ULONG_PTR FreeCount[2];
    };
    ULONG ActiveTopologyElements[5];                     // 0x120 (20 bytes)
    UCHAR PerformanceSearchRanks[32];                    // 0x134 _KNODE_SUBNODE_SEARCH_RANKS[1]
    UCHAR EfficiencySearchRanks[32];                     // 0x154 _KNODE_SUBNODE_SEARCH_RANKS[1]
    UCHAR Pad174[0x178 - 0x174];                         // tail pad to sizeof 0x178
} KNODE, *PKNODE;

#ifdef _WIN64
C_ASSERT(sizeof(KNODE) == 0x178);
C_ASSERT(FIELD_OFFSET(KNODE, NodeNumber) == 0x0);
C_ASSERT(FIELD_OFFSET(KNODE, GroupSeed) == 0xB);
C_ASSERT(FIELD_OFFSET(KNODE, Seed) == 0xB);
C_ASSERT(FIELD_OFFSET(KNODE, ActiveGroups) == 0x10);
C_ASSERT(FIELD_OFFSET(KNODE, ProcessorMask) == 0x10);
C_ASSERT(FIELD_OFFSET(KNODE, SchedulerSubNodes) == 0x20);
C_ASSERT(FIELD_OFFSET(KNODE, FreeCount) == 0x20);
C_ASSERT(FIELD_OFFSET(KNODE, ActiveTopologyElements) == 0x120);
C_ASSERT(FIELD_OFFSET(KNODE, PerformanceSearchRanks) == 0x134);
C_ASSERT(FIELD_OFFSET(KNODE, EfficiencySearchRanks) == 0x154);
#endif

typedef struct _KSCHEDULER_SUBNODE
{
    ULONG_PTR SubNodeLock;
    KAFFINITY IdleNonParkedCpuSet;
    KAFFINITY IdleCpuSet;
    KAFFINITY IdleSmtSet;
    KAFFINITY IdleModuleSet;
    KAFFINITY NonPairedSmtSet;
    KAFFINITY ThreadQosGroupingSet;
    ULONG_PTR Spare1;
    UCHAR Pad40[0x80 - 0x40];
    KAFFINITY DeepIdleSet;
    KAFFINITY IdleConstrainedSet;
    KAFFINITY NonParkedSet;
    KAFFINITY ParkRequestSet;
    KAFFINITY SoftParkRequestSet;
    KAFFINITY ForceParkRequestSet;
    KAFFINITY NonIsrTargetedSet;
    LONG ParkLock;
    UCHAR ProcessSeed;
    UCHAR Spare5[3];
    UCHAR PadC0[0x100 - 0xC0];
    KAFFINITY Affinity;
    USHORT AffinityGroup;
    USHORT ParentNodeNumber;
    USHORT SubNodeNumber;
    USHORT Spare;
    KAFFINITY SiblingMask;
    KAFFINITY SharedReadyQueueMask;
    KAFFINITY StrideMask;
    KAFFINITY LLCLeaders;
    ULONG Lowest;
    ULONG Highest;
    UCHAR Flags;
    UCHAR WorkloadClasses;
    UCHAR Pad13A[0x180 - 0x13A];
    PVOID HeteroSets;
    PVOID PerformanceRanks;
    PVOID EfficiencyRanks;
    KAFFINITY Spare6[5];
    UCHAR Pad1C0[0x200 - 0x1C0];
    KAFFINITY PpmConfiguredQosSets[7];
    KAFFINITY Spare7;
    UCHAR Pad240[0x280 - 0x240];
    UCHAR PpmQosGroupingSets[16];
    KAFFINITY Spare8[6];
    UCHAR Pad2C0[0x300 - 0x2C0];
    volatile KAFFINITY StealableLocalReadyQueues;
    volatile KAFFINITY StealableSharedReadyQueues;
    volatile KAFFINITY StealableStandbyThreads;
    KAFFINITY Spare9[5];
    UCHAR Pad340[0x380 - 0x340];
    UCHAR SoftParkRanks[64];
    UCHAR CoreShareCounts[64];
    UCHAR ModuleShareCounts[64];
    UCHAR ThreadQosGroupingCoreShareCounts[64];
    UCHAR ThreadQosGroupingModuleShareCounts[64];
    UCHAR Pad4C0[0x500 - 0x4C0];
} KSCHEDULER_SUBNODE, *PKSCHEDULER_SUBNODE;

#ifdef _WIN64
C_ASSERT(sizeof(KSCHEDULER_SUBNODE) == 0x500);
C_ASSERT(FIELD_OFFSET(KSCHEDULER_SUBNODE, IdleCpuSet) == 0x10);
C_ASSERT(FIELD_OFFSET(KSCHEDULER_SUBNODE, IdleSmtSet) == 0x18);
C_ASSERT(FIELD_OFFSET(KSCHEDULER_SUBNODE, DeepIdleSet) == 0x80);
C_ASSERT(FIELD_OFFSET(KSCHEDULER_SUBNODE, NonParkedSet) == 0x90);
C_ASSERT(FIELD_OFFSET(KSCHEDULER_SUBNODE, Affinity) == 0x100);
C_ASSERT(FIELD_OFFSET(KSCHEDULER_SUBNODE, ParentNodeNumber) == 0x10A);
C_ASSERT(FIELD_OFFSET(KSCHEDULER_SUBNODE, SubNodeNumber) == 0x10C);
C_ASSERT(FIELD_OFFSET(KSCHEDULER_SUBNODE, SiblingMask) == 0x110);
C_ASSERT(FIELD_OFFSET(KSCHEDULER_SUBNODE, LLCLeaders) == 0x128);
C_ASSERT(FIELD_OFFSET(KSCHEDULER_SUBNODE, Lowest) == 0x130);
C_ASSERT(FIELD_OFFSET(KSCHEDULER_SUBNODE, Highest) == 0x134);
C_ASSERT(FIELD_OFFSET(KSCHEDULER_SUBNODE, HeteroSets) == 0x180);
#endif

//
// Structure for Get/SetContext APC
//
typedef struct _GETSETCONTEXT
{
    KAPC Apc;
    KEVENT Event;
    KPROCESSOR_MODE Mode;
    CONTEXT Context;
} GETSETCONTEXT, *PGETSETCONTEXT;

//
// Kernel Profile Object
//
typedef struct _KPROFILE
{
    CSHORT Type;
    CSHORT Size;
    LIST_ENTRY ProfileListEntry;
    struct _KPROCESS *Process;
    PVOID RangeBase;
    PVOID RangeLimit;
    ULONG BucketShift;
    PVOID Buffer;
    ULONG_PTR Segment;
    KAFFINITY Affinity;
    KPROFILE_SOURCE Source;
    BOOLEAN Started;
} KPROFILE, *PKPROFILE;

//
// Kernel Interrupt Object
//
typedef struct _KINTERRUPT
{
    CSHORT Type;
    CSHORT Size;
    LIST_ENTRY InterruptListEntry;
    PKSERVICE_ROUTINE ServiceRoutine;
#if (NTDDI_VERSION >= NTDDI_LONGHORN)
    PKSERVICE_ROUTINE MessageServiceRoutine;
    ULONG MessageIndex;
#endif
    PVOID ServiceContext;
    KSPIN_LOCK SpinLock;
    ULONG TickCount;
    PKSPIN_LOCK ActualLock;
    PKINTERRUPT_ROUTINE DispatchAddress;
    ULONG Vector;
    KIRQL Irql;
    KIRQL SynchronizeIrql;
    BOOLEAN FloatingSave;
    BOOLEAN Connected;
    CCHAR Number;
    BOOLEAN ShareVector;
    KINTERRUPT_MODE Mode;
#if (NTDDI_VERSION >= NTDDI_LONGHORN)
    KINTERRUPT_POLARITY Polarity;
#endif
    ULONG ServiceCount;
    ULONG DispatchCount;
#if (NTDDI_VERSION >= NTDDI_LONGHORN)
    ULONGLONG Rsvd1;
#endif
#ifdef _M_AMD64
    PKTRAP_FRAME TrapFrame;
    PVOID Reserved;
#endif
    ULONG DispatchCode[DISPATCH_LENGTH];
} KINTERRUPT;

//
// Kernel Event Pair Object
//
typedef struct _KEVENT_PAIR
{
    CSHORT Type;
    CSHORT Size;
    KEVENT LowEvent;
    KEVENT HighEvent;
} KEVENT_PAIR, *PKEVENT_PAIR;

//
// Kernel No Execute Options
//
typedef struct _KEXECUTE_OPTIONS
{
    UCHAR ExecuteDisable:1;
    UCHAR ExecuteEnable:1;
    UCHAR DisableThunkEmulation:1;
    UCHAR Permanent:1;
    UCHAR ExecuteDispatchEnable:1;
    UCHAR ImageDispatchEnable:1;
    UCHAR Spare:2;
} KEXECUTE_OPTIONS, *PKEXECUTE_OPTIONS;

#if (NTDDI_VERSION >= NTDDI_WIN7) || defined(_M_ARM64)
typedef union _KWAIT_STATUS_REGISTER
{
    UCHAR Flags;
    struct
    {
        UCHAR State:2;
        UCHAR Affinity:1;
        UCHAR Priority:1;
        UCHAR Apc:1;
        UCHAR UserApc:1;
        UCHAR Alert:1;
        UCHAR Unused:1;
    };
} KWAIT_STATUS_REGISTER, *PKWAIT_STATUS_REGISTER;

typedef struct _COUNTER_READING
{
    enum _HARDWARE_COUNTER_TYPE Type;
    ULONG Index;
    ULONG64 Start;
    ULONG64 Total;
}COUNTER_READING, *PCOUNTER_READING;

typedef struct _KTHREAD_COUNTERS
{
    ULONG64 WaitReasonBitMap;
    struct _THREAD_PERFORMANCE_DATA* UserData;
    ULONG Flags;
    ULONG ContextSwitches;
    ULONG64 CycleTimeBias;
    ULONG64 HardwareCounters;
    COUNTER_READING HwCounter[16];
}KTHREAD_COUNTERS, *PKTHREAD_COUNTERS;
#endif

/// FIXME: should move to rtltypes.h, but we can't include it here.
#if (NTDDI_VERSION >= NTDDI_WIN8)
typedef struct _RTL_RB_TREE
{
    PRTL_BALANCED_NODE Root;
    PRTL_BALANCED_NODE Min;
} RTL_RB_TREE, *PRTL_RB_TREE;
#endif

#if (NTDDI_VERSION >= NTDDI_WINBLUE)
#if defined(_WIN64) && (NTDDI_VERSION >= NTDDI_WIN11_GE)

//
// Win11 26100 KLOCK_ENTRY layout. This ABI is identical in the ARM64 and
// amd64 ntkrnlmp PDBs; older targets retain their version-specific definition.
//
typedef union _KLOCK_ENTRY_LOCK_STATE
{
    struct
    {
        ULONG_PTR CrossThreadReleasable : 1;
        ULONG_PTR Busy : 1;
        ULONG_PTR Reserved : 61;
        ULONG_PTR InTree : 1;
    };
    PVOID LockState;
} KLOCK_ENTRY_LOCK_STATE, *PKLOCK_ENTRY_LOCK_STATE;

typedef union _KLOCK_ENTRY_BOOST_BITMAP
{
    ULONG64 AllFields;
    struct
    {
        ULONG AllBoosts;
        ULONG WaiterCounts;
    };
    struct
    {
        ULONG CpuBoostsBitmap : 30;
        ULONG IoBoost : 1;
        ULONG IoQoSBoost : 1;
        ULONG IoNormalPriorityWaiterCount : 8;
        ULONG IoQoSWaiterCount : 7;
        ULONG : 17;
    };
} KLOCK_ENTRY_BOOST_BITMAP, *PKLOCK_ENTRY_BOOST_BITMAP;

typedef struct _KLOCK_ENTRY
{
    union
    {
        KLOCK_ENTRY_LOCK_STATE LockState;
        PVOID LockUnsafe;
        struct
        {
            volatile UCHAR CrossThreadReleasableAndBusyByte;
            UCHAR Reserved[6];
            volatile UCHAR InTreeByte;
        };
    };
    union
    {
        ULONG EntryFlags;
        struct
        {
            union
            {
                UCHAR StaticByte;
                struct
                {
                    UCHAR EntryIndex : 6;
                    UCHAR PreWaiting : 1;
                    UCHAR UserModeBit : 1;
                };
            };
            UCHAR WaitingByte;
            UCHAR AcquiredByte;
            union
            {
                UCHAR CrossThreadFlags;
                struct
                {
                    UCHAR HeadNodeBit : 1;
                    UCHAR IoPriorityBit : 1;
                    UCHAR IoQoSWaiter : 1;
                    UCHAR Spare1 : 5;
                };
            };
        };
        struct
        {
            ULONG StaticState : 8;
            ULONG AllFlags : 24;
        };
    };
    ULONG SpareFlags;
    RTL_BALANCED_NODE TreeNode;
    union
    {
        struct
        {
            RTL_RB_TREE OwnerTree;
            RTL_RB_TREE WaiterTree;
        };
        CHAR CpuPriorityKey;
    };
    ULONG64 EntryLock;
    KLOCK_ENTRY_BOOST_BITMAP BoostBitmap;
} KLOCK_ENTRY, *PKLOCK_ENTRY;

C_ASSERT(sizeof(KLOCK_ENTRY_LOCK_STATE) == 0x08);
C_ASSERT(sizeof(KLOCK_ENTRY_BOOST_BITMAP) == 0x08);
C_ASSERT(sizeof(KLOCK_ENTRY) == 0x58);
C_ASSERT(FIELD_OFFSET(KLOCK_ENTRY, LockState) == 0x00);
C_ASSERT(FIELD_OFFSET(KLOCK_ENTRY, CrossThreadReleasableAndBusyByte) == 0x00);
C_ASSERT(FIELD_OFFSET(KLOCK_ENTRY, InTreeByte) == 0x07);
C_ASSERT(FIELD_OFFSET(KLOCK_ENTRY, EntryFlags) == 0x08);
C_ASSERT(FIELD_OFFSET(KLOCK_ENTRY, StaticByte) == 0x08);
C_ASSERT(FIELD_OFFSET(KLOCK_ENTRY, WaitingByte) == 0x09);
C_ASSERT(FIELD_OFFSET(KLOCK_ENTRY, AcquiredByte) == 0x0A);
C_ASSERT(FIELD_OFFSET(KLOCK_ENTRY, CrossThreadFlags) == 0x0B);
C_ASSERT(FIELD_OFFSET(KLOCK_ENTRY, SpareFlags) == 0x0C);
C_ASSERT(FIELD_OFFSET(KLOCK_ENTRY, TreeNode) == 0x10);
C_ASSERT(FIELD_OFFSET(KLOCK_ENTRY, OwnerTree) == 0x28);
C_ASSERT(FIELD_OFFSET(KLOCK_ENTRY, WaiterTree) == 0x38);
C_ASSERT(FIELD_OFFSET(KLOCK_ENTRY, EntryLock) == 0x48);
C_ASSERT(FIELD_OFFSET(KLOCK_ENTRY, BoostBitmap) == 0x50);

#else

typedef struct _KLOCK_ENTRY_LOCK_STATE
{
    union
    {
        struct
        {
#if (NTDDI_VERSION >= NTDDI_WIN10) // since 6.4.9841.0
            ULONG_PTR CrossThreadReleasable : 1;
#else
            ULONG_PTR Waiting : 1;
#endif
            ULONG_PTR Busy : 1;
            ULONG_PTR Reserved : (8 * sizeof(PVOID)) - 3; // previously Spare
            ULONG_PTR InTree : 1;
        };
        PVOID LockState;
    };
    union
    {
        PVOID SessionState;
        struct
        {
            ULONG SessionId;
#ifdef _WIN64
            ULONG SessionPad;
#endif
        };
    };
} KLOCK_ENTRY_LOCK_STATE, *PKLOCK_ENTRY_LOCK_STATE;

typedef struct _KLOCK_ENTRY
{
    union
    {
        RTL_BALANCED_NODE TreeNode;
        SINGLE_LIST_ENTRY FreeListEntry;
    };
#if (NTDDI_VERSION >= NTDDI_WIN10)
    union
    {
        ULONG EntryFlags;
        struct
        {
            UCHAR EntryOffset;
            union
            {
                UCHAR ThreadLocalFlags;
                struct
                {
                    UCHAR WaitingBit : 1;
                    UCHAR Spare0 : 7;
                };
            };
            union
            {
                UCHAR AcquiredByte;
                UCHAR AcquiredBit : 1;
            };
            union
            {
                UCHAR CrossThreadFlags;
                struct
                {
                    UCHAR HeadNodeBit : 1;
                    UCHAR IoPriorityBit : 1;
                    UCHAR IoQoSWaiter : 1; // since TH2
                    UCHAR Spare1 : 5;
                };
            };
        };
        struct
        {
            ULONG StaticState : 8;
            ULONG AllFlags : 24;
        };
    };
#ifdef _WIN64
    ULONG SpareFlags;
#endif
#else
    union
    {
        PVOID ThreadUnsafe;
        struct
        {
            volatile UCHAR HeadNodeByte;
            UCHAR Reserved1[2];
            volatile UCHAR AcquiredByte;
        };
    };
#endif

    union
    {
        KLOCK_ENTRY_LOCK_STATE LockState;
        PVOID LockUnsafe;
        struct
        {
#if (NTDDI_VERSION >= NTDDI_WIN10)
            volatile UCHAR CrossThreadReleasableAndBusyByte;
#else
            volatile UCHAR WaitingAndBusyByte;
#endif
            UCHAR Reserved[sizeof(PVOID) - 2];
            UCHAR InTreeByte;
            union
            {
                PVOID SessionState;
                struct
                {
                    ULONG SessionId;
#ifdef _WIN64
                    ULONG SessionPad;
#endif
                };
            };
        };
    };
    union
    {
        struct
        {
            RTL_RB_TREE OwnerTree;
            RTL_RB_TREE WaiterTree;
        };
        CHAR CpuPriorityKey;
    };
    ULONG_PTR EntryLock;
    union
    {
#if _WIN64
        ULONG AllBoosts : 17;
#else
        USHORT AllBoosts;
#endif
        struct
        {
            struct
            {
                USHORT CpuBoostsBitmap : 15;
                USHORT IoBoost : 1;
            };
            struct
            {
                USHORT IoQoSBoost : 1;
                USHORT IoNormalPriorityWaiterCount : 8;
                USHORT IoQoSWaiterCount : 7;
            };
        };
    };
#if _WIN64
    ULONG SparePad;
#endif
} KLOCK_ENTRY, *PKLOCK_ENTRY;

#endif // defined(_WIN64) && (NTDDI_VERSION >= NTDDI_WIN11_GE)
#endif

//
// UMS (User-Mode Scheduling) types -- Win7+, amd64 only
// TODO: These are stub definitions for asm offset generation.
// Full UMS support is not yet implemented in ReactOS.
//
#if (NTDDI_VERSION >= NTDDI_WIN7) && defined(_M_AMD64)

typedef struct _KUMS_CONTEXT_HEADER
{
    ULONG64 P1Home;
    ULONG64 P2Home;
    ULONG64 P3Home;
    ULONG64 P4Home;
    PVOID StackTop;
    ULONG64 StackSize;
    ULONG64 RspOffset;
    ULONG64 Rip;
    PXMM_SAVE_AREA32 FltSave;
    union
    {
        struct
        {
            ULONG64 Volatile : 1;
            ULONG64 Reserved : 63;
        };
        ULONG64 Flags;
    };
    PKTRAP_FRAME TrapFrame;
    PKEXCEPTION_FRAME ExceptionFrame;
    struct _KTHREAD *SourceThread;
    ULONG64 Return;
} KUMS_CONTEXT_HEADER, *PKUMS_CONTEXT_HEADER;

/* TODO: UMS_CONTROL_BLOCK is opaque; stub with the field used by asm offsets */
typedef struct _UMS_CONTROL_BLOCK
{
    PVOID UmsTeb;
} UMS_CONTROL_BLOCK, *PUMS_CONTROL_BLOCK;

#endif /* NTDDI_WIN7 && _M_AMD64 */

#if (NTDDI_VERSION >= NTDDI_WIN8)
//
// Kernel Stack Segment and Control (Win8+, layout stable through Win11 26100)
//
typedef struct _KERNEL_STACK_SEGMENT
{
    ULONG_PTR StackBase;
    ULONG_PTR StackLimit;
    ULONG_PTR KernelStack;
    ULONG_PTR InitialStack;
} KERNEL_STACK_SEGMENT, *PKERNEL_STACK_SEGMENT;

// The Win11 (ntkrnlmp.pdb _KSTACK_CONTROL, type 0x1B79, fieldList 0x1B78) struct
// is 0x40 bytes. ReactOS historically used a simplified 0x30-byte body that
// omitted CalloutState/Padding, which left Previous at 0x10 instead of its real
// 0x20 - so the embedded KERNEL_STACK_SEGMENT (and every Previous.* OFFSET the
// asm helpers derive) sat 0x10 too low versus Win11.
typedef struct _KSTACK_CONTROL
{
    /* Public Win11 architectural fields. */
    ULONG_PTR StackBase;                                 // 0x00
    union                                                // 0x08
    {
        ULONG_PTR ActualLimit;
        ULONG_PTR StackExpansion:1;
    };
    PVOID CalloutState;                                  // 0x10 Win11 (was absent in ReactOS)
    PVOID Padding;                                       // 0x18 Win11 (was absent in ReactOS)
    KERNEL_STACK_SEGMENT Previous;                       // 0x20 (was wrongly at 0x10)
#ifdef _M_IX86
    /* ReactOS-private i386-only save slots, consumed by the i386 stack-expansion
       asm via ksx.template.h (KcTrapFrame/KcExceptionList). */
    struct _KTRAP_FRAME *PreviousTrapFrame;
    PVOID PreviousExceptionList;
#endif
} KSTACK_CONTROL, *PKSTACK_CONTROL;

#if defined(_M_ARM64)
/* Lock KSTACK_CONTROL to the Win11 ARM64 ntkrnlmp.pdb layout (type 0x1B79,
   sizeof 0x40). The OFFSET()s emitted from ksx.template.h - StackBase,
   ActualLimit and Previous.{StackBase,StackLimit,KernelStack,InitialStack} -
   are symbolic and auto-track these C offsets. amd64 is intentionally NOT
   asserted here: there is no amd64 PDB ground truth in this tree and the vendored
   ksamd64.inc reports a different size (KSTACK_CONTROL_LENGTH == 0x50), so a
   shared _WIN64 assert would be an unverified parity claim. */
C_ASSERT(sizeof(KERNEL_STACK_SEGMENT) == 0x20);
C_ASSERT(sizeof(KSTACK_CONTROL) == 0x40);
C_ASSERT(FIELD_OFFSET(KSTACK_CONTROL, StackBase) == 0x00);
C_ASSERT(FIELD_OFFSET(KSTACK_CONTROL, ActualLimit) == 0x08);
C_ASSERT(FIELD_OFFSET(KSTACK_CONTROL, CalloutState) == 0x10);
C_ASSERT(FIELD_OFFSET(KSTACK_CONTROL, Padding) == 0x18);
C_ASSERT(FIELD_OFFSET(KSTACK_CONTROL, Previous) == 0x20);
C_ASSERT(FIELD_OFFSET(KSTACK_CONTROL, Previous.StackBase) == 0x20);
C_ASSERT(FIELD_OFFSET(KSTACK_CONTROL, Previous.StackLimit) == 0x28);
C_ASSERT(FIELD_OFFSET(KSTACK_CONTROL, Previous.KernelStack) == 0x30);
C_ASSERT(FIELD_OFFSET(KSTACK_CONTROL, Previous.InitialStack) == 0x38);
#endif

#define KSTACK_ACTUAL_LIMIT_EXPANDED 1
#endif /* NTDDI_WIN8 */

//
// Kernel Thread (KTHREAD), Win11 26100 arm64 layout (ntkrnlmp.pdb 10.0.26100.8036)
// sizeof == 0x4A0; members marked [ReactOS] live in Win11 spare slots
//
#if defined(_M_ARM64)

typedef struct _KTHREAD
{
    DISPATCHER_HEADER Header;                            // 0x000
    PVOID SListFaultAddress;                             // 0x018
    ULONG64 QuantumTarget;                               // 0x020
    PVOID InitialStack;                                  // 0x028
    ULONG_PTR StackLimit;                                // 0x030
    PVOID StackBase;                                     // 0x038
    KSPIN_LOCK ThreadLock;                               // 0x040
    volatile ULONG64 CycleTime;                          // 0x048
    ULONG CurrentRunTime;                                // 0x050
    ULONG ExpectedRunTime;                               // 0x054
    PVOID KernelStack;                                   // 0x058
    PVOID SchedulingGroup;                               // 0x060 PKSCHEDULING_GROUP
    KWAIT_STATUS_REGISTER WaitRegister;                  // 0x068
    volatile BOOLEAN Running;                            // 0x069
    BOOLEAN Alerted[MaximumMode];                        // 0x06A
    union
    {
        struct
        {
            ULONG AutoBoostActive:1;                     // 0x06C
            ULONG ReadyTransition:1;
            ULONG WaitNext:1;
            ULONG SystemAffinityActive:1;
            ULONG Alertable:1;
            ULONG ProcessReadyQueue:1;                   // Win11 name: Reserved1 [ReactOS]
            ULONG ApcInterruptRequest:1;
            ULONG QuantumEndMigrate:1;
            ULONG SecureThread:1;
            ULONG TimerActive:1;
            ULONG SystemThread:1;
            ULONG ProcessDetachActive:1;
            ULONG GdiFlushActive:1;                      // Win11 name: Reserved2 [ReactOS]
            ULONG ScbReadyQueue:1;
            ULONG ApcQueueable:1;
            ULONG CycleChargePending:1;                  // Win11 name: Reserved3 [ReactOS]
            ULONG WaitNextClearWobPriorityFloor:1;
            ULONG TimerSuspended:1;
            ULONG SuspendedWaitMode:1;
            ULONG SuspendSchedulerApcWait:1;
            ULONG CetUserShadowStack:1;
            ULONG BypassProcessFreeze:1;
            ULONG CetKernelShadowStack:1;
            ULONG StateSaveAreaDecoupled:1;
            ULONG MiscFlagsReserved:8;
        };
        volatile LONG MiscFlags;                         // 0x06C
    };
    union
    {
        struct
        {
            ULONG UserIdealProcessorFixed:1;             // 0x070
            ULONG IsolationWidth:1;
            ULONG AutoAlignment:1;
            ULONG DisableBoost:1;
            ULONG AlertedByThreadId:1;
            ULONG QuantumDonation:1;
            ULONG EnableStackSwap:1;
            ULONG GuiThread:1;
            ULONG DisableQuantum:1;
            ULONG ChargeOnlySchedulingGroup:1;
            ULONG DeferPreemption:1;
            ULONG QueueDeferPreemption:1;
            ULONG ForceDeferSchedule:1;
            ULONG SharedReadyQueueAffinity:1;
            ULONG FreezeCountFlag:1;                     // Win11 name: FreezeCount
            ULONG TerminationApcRequest:1;
            ULONG AutoBoostEntriesExhausted:1;
            ULONG KernelStackResident:1;
            ULONG TerminateRequestReason:2;
            ULONG ProcessStackCountDecremented:1;
            ULONG RestrictedGuiThread:1;
            ULONG VpBackingThread:1;
            ULONG EtwStackTraceApc1Inserted:1;           // Win11 name: EtwStackTraceCrimsonApcDisabled [ReactOS]
            ULONG EtwStackTraceApc2Inserted:1;           // Win11: EtwStackTraceApcInserted:8 [ReactOS]
            ULONG ThreadFlagsReserved:7;
        };
        volatile LONG ThreadFlags;                       // 0x070
    };
    volatile UCHAR Tag;                                  // 0x074
    union
    {
        struct
        {
            UCHAR CalloutActive:1;                       // 0x075
            UCHAR ReservedStackInUse:1;
            UCHAR UserStackWalkActive:1;
            UCHAR SameThreadTransientReserved:5;
        };
        CHAR SameThreadTransientFlags;
    };
    volatile UCHAR SwapBusy;                             // 0x076 Win11 name: Spare0 [ReactOS]
    UCHAR Spare0a;                                       // 0x077
    ULONG SystemCallNumber;                              // 0x078
    ULONG ReadyTime;                                     // 0x07C
    PVOID FirstArgument;                                 // 0x080
    PKTRAP_FRAME TrapFrame;                              // 0x088
    union
    {
        KAPC_STATE ApcState;                             // 0x090
        struct
        {
            UCHAR ApcStateFill[FIELD_OFFSET(KAPC_STATE, UserApcPending) + 1];
            SCHAR Priority;                              // 0x0BB
            ULONG UserIdealProcessor;                    // 0x0BC
        };
    };
    LONG_PTR WaitStatus;                                 // 0x0C0
    union
    {
        PKWAIT_BLOCK WaitBlockList;                      // 0x0C8
        PKGATE GateObject;                               // [ReactOS]
    };
    union
    {
        LIST_ENTRY WaitListEntry;                        // 0x0D0
        SINGLE_LIST_ENTRY SwapListEntry;
    };
    PKQUEUE Queue;                                       // 0x0E0 Win11 type: _DISPATCHER_HEADER*
    struct _TEB *Teb;                                    // 0x0E8
    ULONG64 RelativeTimerBias;                           // 0x0F0
    KTIMER Timer;                                        // 0x0F8
    union
    {
        DECLSPEC_ALIGN(8) KWAIT_BLOCK WaitBlock[THREAD_WAIT_OBJECTS + 1]; // 0x138
        struct
        {
            UCHAR WaitBlockFill4[FIELD_OFFSET(KWAIT_BLOCK, SpareLong)];
            ULONG ContextSwitches;                       // 0x14C
        };
        struct
        {
            UCHAR WaitBlockFill5[1 * sizeof(KWAIT_BLOCK) + FIELD_OFFSET(KWAIT_BLOCK, SpareLong)];
            volatile UCHAR State;                        // 0x17C
            SCHAR Quantum;                               // 0x17D Win11 name: Spare13 [ReactOS]
            UCHAR WaitIrql;                              // 0x17E
            CHAR WaitMode;                               // 0x17F
        };
        struct
        {
            UCHAR WaitBlockFill6[2 * sizeof(KWAIT_BLOCK) + FIELD_OFFSET(KWAIT_BLOCK, SpareLong)];
            ULONG WaitTime;                              // 0x1AC
        };
        struct
        {
            UCHAR WaitBlockFill7[3 * sizeof(KWAIT_BLOCK) + FIELD_OFFSET(KWAIT_BLOCK, SpareLong)];
            union
            {
                struct
                {
                    SHORT KernelApcDisable;              // 0x1DC
                    SHORT SpecialApcDisable;             // 0x1DE
                };
                ULONG CombinedApcDisable;
            };
        };
        struct
        {
            UCHAR WaitBlockFill9[FIELD_OFFSET(KWAIT_BLOCK, SparePtr)];
            PVOID ThreadCounters;                        // 0x160 PKTHREAD_COUNTERS
        };
        struct
        {
            UCHAR WaitBlockFill10[1 * sizeof(KWAIT_BLOCK) + FIELD_OFFSET(KWAIT_BLOCK, SparePtr)];
            PVOID XStateSave;                            // 0x190 PXSTATE_SAVE
        };
        struct
        {
            UCHAR WaitBlockFill11[2 * sizeof(KWAIT_BLOCK) + FIELD_OFFSET(KWAIT_BLOCK, SparePtr)];
            PVOID Win32Thread;                           // 0x1C0
        };
        struct
        {
            UCHAR WaitBlockFill12[3 * sizeof(KWAIT_BLOCK) + FIELD_OFFSET(KWAIT_BLOCK, Object)];
            PVOID EmulationControlBlock;                 // 0x1E8
        };
        struct
        {
            UCHAR WaitBlockFill13[3 * sizeof(KWAIT_BLOCK) + FIELD_OFFSET(KWAIT_BLOCK, SparePtr)];
            union
            {
                ULONG64 Spare19;                         // 0x1F0
                volatile ULONG DeferredProcessor;        // [ReactOS]
            };
        };
    };
    volatile LONG ThreadFlags2;                          // 0x1F8
    volatile UCHAR BamQosLevel;                          // 0x1FC
    UCHAR HardwareFeedbackClass;                         // 0x1FD
    SHORT PriorityDecrement;                             // 0x1FE
    LIST_ENTRY QueueListEntry;                           // 0x200
    ULONG64 SwitchFrame[4];                              // 0x210 Win11 KSWITCH_FRAME (0x20)
    PVOID VfpState;                                      // 0x230 PKARM64_VFP_STATE
    volatile ULONG NextProcessor;                        // 0x238
    LONG QueuePriority;                                  // 0x23C
    struct _KPROCESS *Process;                           // 0x240
    KAFFINITY UserAffinity;                              // 0x248 Win11 type: PKAFFINITY_EX
    USHORT UserAffinityPrimaryGroup;                     // 0x250
    CCHAR PreviousMode;                                  // 0x252
    SCHAR BasePriority;                                  // 0x253
    CCHAR FreezeCount;                                   // 0x254 Win11 name: Spare24 [ReactOS]
    BOOLEAN Preempted;                                   // 0x255
    UCHAR AdjustReason;                                  // 0x256
    SCHAR AdjustIncrement;                               // 0x257
    ULONG64 AffinityVersion;                             // 0x258
    KAFFINITY Affinity;                                  // 0x260 Win11 type: PKAFFINITY_EX
    USHORT AffinityPrimaryGroup;                         // 0x268
    UCHAR ApcStateIndex;                                 // 0x26A
    UCHAR WaitBlockCount;                                // 0x26B
    ULONG IdealProcessor;                                // 0x26C
    ULONG64 NpxState;                                    // 0x270
    union
    {
        KAPC_STATE SavedApcState;                        // 0x278
        struct
        {
            UCHAR SavedApcStateFill[FIELD_OFFSET(KAPC_STATE, UserApcPending) + 1];
            UCHAR WaitReason;                            // 0x2A3
            CCHAR SuspendCount;                          // 0x2A4
            CCHAR Saturation;                            // 0x2A5
            USHORT SListFaultCount;                      // 0x2A6
        };
    };
    union
    {
        KAPC SchedulerApc;                               // 0x2A8
        KAPC SuspendApc;                                 // [ReactOS] alias
        struct
        {
            UCHAR SchedulerApcFill1[3];
            UCHAR QuantumReset;                          // 0x2AB
        };
        struct
        {
            UCHAR SchedulerApcFill2[4];
            ULONG KernelTime;                            // 0x2AC
        };
        struct
        {
            UCHAR SchedulerApcFill3[FIELD_OFFSET(KAPC, SystemArgument1)];
            PKPRCB WaitPrcb;                             // 0x2E8
        };
        struct
        {
            UCHAR SchedulerApcFill4[FIELD_OFFSET(KAPC, SystemArgument2)];
            PVOID LegoData;                              // 0x2F0
        };
        struct
        {
            UCHAR SchedulerApcFill5[FIELD_OFFSET(KAPC, Inserted) + 1];
            UCHAR CallbackNestingLevel;                  // 0x2FB
            ULONG UserTime;                              // 0x2FC
        };
    };
    union
    {
        KEVENT SuspendEvent;                             // 0x300
        KEVENT SuspendSemaphore;                         // [ReactOS] alias, KEVENT-sized (was KSEMAPHORE)
    };
    LIST_ENTRY ThreadListEntry;                          // 0x318
    LIST_ENTRY MutantListHead;                           // 0x328
    UCHAR AbEntryCounts[2];                              // 0x338
    UCHAR FreezeFlags;                                   // 0x33A
    CHAR WobPriority;                                    // 0x33B
    ULONG SecureThreadCookie;                            // 0x33C
    PVOID SchedulerSharedSystemSlot;                     // 0x340
    SINGLE_LIST_ENTRY PropagateBoostsEntry;              // 0x348
    SINGLE_LIST_ENTRY IoSelfBoostsEntry;                 // 0x350
    UCHAR PriorityFloorCounts[32];                       // 0x358
    ULONG PriorityFloorSummary;                          // 0x378
    volatile LONG AbCompletedIoBoostCount;               // 0x37C
    volatile LONG AbCompletedIoQoSBoostCount;            // 0x380
    volatile SHORT KeReferenceCount;                     // 0x384
    CHAR DecayBoost;                                     // 0x386
    UCHAR LargeStack;                                    // 0x387 Win11 name: Spare6 [ReactOS]
    ULONG ForegroundLossTime;                            // 0x388
    ULONG Spare38C;                                      // 0x38C
    union
    {
        LIST_ENTRY GlobalForegroundListEntry;            // 0x390
        struct
        {
            SINGLE_LIST_ENTRY ForegroundDpcStackListEntry;
            ULONG64 InGlobalForegroundList;
        };
    };
    LONG64 ReadOperationCount;                           // 0x3A0
    LONG64 WriteOperationCount;                          // 0x3A8
    LONG64 OtherOperationCount;                          // 0x3B0
    LONG64 ReadTransferCount;                            // 0x3B8
    LONG64 WriteTransferCount;                           // 0x3C0
    LONG64 OtherTransferCount;                           // 0x3C8
    PVOID QueuedScb;                                     // 0x3D0 PKSCB
    volatile ULONG ThreadTimerDelay;                     // 0x3D8
    USHORT Spare26;                                      // 0x3DC
    volatile UCHAR PpmPolicy;                            // 0x3DE
    UCHAR Spare27;                                       // 0x3DF
    ULONG64 TracingPrivate[1];                           // 0x3E0
    PVOID SchedulerAssist;                               // 0x3E8
    PVOID AbWaitObject;                                  // 0x3F0
    ULONG ReservedPreviousReadyTimeValue;                // 0x3F8
    ULONG Spare3FC;                                      // 0x3FC
    ULONG64 KernelWaitTime;                              // 0x400
    ULONG64 UserWaitTime;                                // 0x408
    union
    {
        LIST_ENTRY GlobalUpdateVpThreadPriorityListEntry; // 0x410
        struct
        {
            SINGLE_LIST_ENTRY UpdateVpThreadPriorityDpcStackListEntry;
            ULONG64 InGlobalUpdateVpThreadPriorityList;
        };
    };
    LONG SchedulerAssistPriorityFloor;                   // 0x420
    LONG RealtimePriorityFloor;                          // 0x424
    ULONG StateSaveAreaSveVectorOffset;                  // 0x428
    ULONG StateSaveAreaSvePredicateOffset;               // 0x42C
    ULONG SchedulerAssistYieldCounter;                   // 0x430
    ULONG SchedulerAssistYieldBoostCount;                // 0x434
    LONG64 SchedulerAssistLastYieldBoostTime;            // 0x438
    // Win11 Spare28[5] region: ReactOS private members
    KSPIN_LOCK ApcQueueLock;                             // 0x440 [ReactOS]
    PKAPC_STATE ApcStatePointer[2];                      // 0x448 [ReactOS]
    PVOID CallbackStack;                                 // 0x458 [ReactOS]
    ULONG64 Spare28;                                     // 0x460
    ULONG Spare29;                                       // 0x468
    volatile ULONG ModeHistory;                          // 0x46C
    SINGLE_LIST_ENTRY SystemAffinityTokenListHead;       // 0x470
    PVOID StateSaveArea;                                 // 0x478 _XSAVE_FORMAT*
    UCHAR ResourceIndex;                                 // 0x480
    volatile UCHAR CoreIsolationReasons;                 // 0x481
    UCHAR BamQosLevelFromAssistPage;                     // 0x482
    UCHAR SecureCallCoreIsolationCount;                  // 0x483
    ULONG SchedulerSharedOffset;                         // 0x484
    PVOID SchedulerSharedSwappablePage;                  // 0x488 PKSWAPPABLE_PAGE
    PVOID KernelAbEntries;                               // 0x490 PKLOCK_ENTRIES
    PVOID UserAbEntries;                                 // 0x498
} KTHREAD;                                               // sizeof 0x4A0

//
// Kernel Thread (KTHREAD), Win11 26100 amd64 layout (ntkrnlmp.pdb 10.0.26100.8036)
// sizeof == 0x4C0; members marked [ReactOS] live in Win11 spare/padding slots.
// Mirrors the arm64 variant above: genuine Windows fields are offset-accurate and
// C_ASSERT-locked; ReactOS-internal scheduler/APC fields are tucked into spares.
//
#elif defined(_M_AMD64) && (NTDDI_VERSION >= NTDDI_WIN11_GE)

typedef struct _KTHREAD
{
    DISPATCHER_HEADER Header;                            // 0x000
    PVOID SListFaultAddress;                             // 0x018
    ULONG64 QuantumTarget;                               // 0x020
    PVOID InitialStack;                                  // 0x028
    ULONG_PTR StackLimit;                                // 0x030 Win11 type: PVOID
    PVOID StackBase;                                     // 0x038
    KSPIN_LOCK ThreadLock;                               // 0x040
    volatile ULONG64 CycleTime;                          // 0x048
    ULONG CurrentRunTime;                                // 0x050
    ULONG ExpectedRunTime;                               // 0x054
    PVOID KernelStack;                                   // 0x058
    XSAVE_FORMAT *StateSaveArea;                         // 0x060
    struct _KSCHEDULING_GROUP *SchedulingGroup;          // 0x068
    KWAIT_STATUS_REGISTER WaitRegister;                  // 0x070
    volatile BOOLEAN Running;                            // 0x071
    BOOLEAN Alerted[MaximumMode];                        // 0x072
    union
    {
        struct
        {
            ULONG AutoBoostActive:1;                     // 0x074
            ULONG ReadyTransition:1;
            ULONG WaitNext:1;
            ULONG SystemAffinityActive:1;
            ULONG Alertable:1;
            ULONG ProcessReadyQueue:1;                   // Win11 name: Reserved1 [ReactOS]
            ULONG ApcInterruptRequest:1;
            ULONG QuantumEndMigrate:1;
            ULONG SecureThread:1;
            ULONG TimerActive:1;
            ULONG SystemThread:1;
            ULONG ProcessDetachActive:1;
            ULONG GdiFlushActive:1;                      // Win11 name: Reserved2 [ReactOS]
            ULONG ScbReadyQueue:1;
            ULONG ApcQueueable:1;
            ULONG CycleChargePending:1;                  // Win11 name: Reserved3 [ReactOS]
            ULONG WaitNextClearWobPriorityFloor:1;
            ULONG TimerSuspended:1;
            ULONG SuspendedWaitMode:1;
            ULONG SuspendSchedulerApcWait:1;
            ULONG CetUserShadowStack:1;
            ULONG BypassProcessFreeze:1;
            ULONG CetKernelShadowStack:1;
            ULONG StateSaveAreaDecoupled:1;
            ULONG MiscReserved:8;
        };
        LONG MiscFlags;                                  // 0x074
    };
    union
    {
        struct
        {
            ULONG UserIdealProcessorFixed:1;             // 0x078
            ULONG IsolationWidth:1;
            ULONG AutoAlignment:1;
            ULONG DisableBoost:1;
            ULONG AlertedByThreadId:1;
            ULONG QuantumDonation:1;
            ULONG EnableStackSwap:1;
            ULONG GuiThread:1;
            ULONG DisableQuantum:1;
            ULONG ChargeOnlySchedulingGroup:1;
            ULONG DeferPreemption:1;
            ULONG QueueDeferPreemption:1;
            ULONG ForceDeferSchedule:1;
            ULONG SharedReadyQueueAffinity:1;
            ULONG FreezeCount:1;
            ULONG TerminationApcRequest:1;
            ULONG AutoBoostEntriesExhausted:1;
            ULONG KernelStackResident:1;
            ULONG TerminateRequestReason:2;
            ULONG ProcessStackCountDecremented:1;
            ULONG RestrictedGuiThread:1;
            ULONG VpBackingThread:1;
            ULONG EtwStackTraceCrimsonApcDisabled:1;
            ULONG EtwStackTraceApcInserted:8;
        };
        volatile LONG ThreadFlags;                       // 0x078
    };
    volatile UCHAR Tag;                                  // 0x07C
    union
    {
        struct
        {
            UCHAR CalloutActive:1;                       // 0x07D
            UCHAR ReservedStackInUse:1;
            UCHAR UserStackWalkActive:1;
            UCHAR SameThreadTransientReserved:5;
        };
        CHAR SameThreadTransientFlags;
    };
    union
    {
        struct
        {
            UCHAR RunningNonRetpolineCode:1;             // 0x07E
            UCHAR SpecCtrlSpare:7;
        };
        UCHAR SpecCtrl;
    };
    ULONG SystemCallNumber;                              // 0x080
    ULONG ReadyTime;                                     // 0x084
    PVOID FirstArgument;                                 // 0x088
    PKTRAP_FRAME TrapFrame;                              // 0x090
    union
    {
        KAPC_STATE ApcState;                             // 0x098
        struct
        {
            UCHAR ApcStateFill[FIELD_OFFSET(KAPC_STATE, UserApcPending) + 1]; // 0x098, 0x2B
            SCHAR Priority;                              // 0x0C3
            ULONG UserIdealProcessor;                    // 0x0C4
        };
    };
    volatile LONGLONG WaitStatus;                        // 0x0C8
    PKWAIT_BLOCK WaitBlockList;                          // 0x0D0
    union
    {
        LIST_ENTRY WaitListEntry;                        // 0x0D8
        SINGLE_LIST_ENTRY SwapListEntry;
    };
    PKQUEUE Queue;                                       // 0x0E8 Win11 type: _DISPATCHER_HEADER*
    struct _TEB *Teb;                                    // 0x0F0
    ULONG64 RelativeTimerBias;                           // 0x0F8
    KTIMER Timer;                                        // 0x100
    union
    {
        DECLSPEC_ALIGN(8) KWAIT_BLOCK WaitBlock[THREAD_WAIT_OBJECTS + 1]; // 0x140
        struct
        {
            UCHAR WaitBlockFill4[FIELD_OFFSET(KWAIT_BLOCK, SpareLong)]; // 0x14
            ULONG ContextSwitches;                       // 0x154
        };
        struct
        {
            UCHAR WaitBlockFill5[1 * sizeof(KWAIT_BLOCK) + FIELD_OFFSET(KWAIT_BLOCK, SpareLong)]; // 0x44
            volatile UCHAR State;                        // 0x184
            CHAR Spare13;                                // 0x185
            UCHAR WaitIrql;                              // 0x186
            CHAR WaitMode;                               // 0x187
        };
        struct
        {
            UCHAR WaitBlockFill6[2 * sizeof(KWAIT_BLOCK) + FIELD_OFFSET(KWAIT_BLOCK, SpareLong)]; // 0x74
            ULONG WaitTime;                              // 0x1B4
        };
        struct
        {
            UCHAR WaitBlockFill7[3 * sizeof(KWAIT_BLOCK) + FIELD_OFFSET(KWAIT_BLOCK, SpareLong)]; // 0xA4
            union
            {
                struct
                {
                    SHORT KernelApcDisable;              // 0x1E4
                    SHORT SpecialApcDisable;             // 0x1E6
                };
                ULONG CombinedApcDisable;
            };
        };
        struct
        {
            UCHAR WaitBlockFill8[FIELD_OFFSET(KWAIT_BLOCK, SparePtr)]; // 0x28
            struct _KTHREAD_COUNTERS *ThreadCounters;    // 0x168
        };
        struct
        {
            UCHAR WaitBlockFill9[1 * sizeof(KWAIT_BLOCK) + FIELD_OFFSET(KWAIT_BLOCK, SparePtr)]; // 0x58
            PXSTATE_SAVE XStateSave;                     // 0x198
        };
        struct
        {
            UCHAR WaitBlockFill10[2 * sizeof(KWAIT_BLOCK) + FIELD_OFFSET(KWAIT_BLOCK, SparePtr)]; // 0x88
            PVOID Win32Thread;                           // 0x1C8
        };
        struct
        {
            UCHAR WaitBlockFill11[3 * sizeof(KWAIT_BLOCK) + FIELD_OFFSET(KWAIT_BLOCK, Object)]; // 0xB0
            /* This slot overlays WaitBlock[3].Object during timed waits. */
            ULONG64 Spare18;                             // 0x1F0
            ULONG64 LastXStateSaveDebugInfo;             // 0x1F8
        };
    };
    union
    {
        struct
        {
            ULONG DisableKasan:1;                        // 0x200
            ULONG AbContextSwitchState:1;
            ULONG ThreadFlags2Reserved:30;
        };
        volatile LONG ThreadFlags2;                      // 0x200
    };
    volatile UCHAR BamQosLevel;                          // 0x204
    UCHAR HardwareFeedbackClass;                         // 0x205
    union
    {
        SHORT PriorityDecrement;                         // 0x206
        struct
        {
            USHORT ForegroundBoost:4;
            USHORT UnusualBoost:8;
        };
    };
    LIST_ENTRY QueueListEntry;                           // 0x208
    union
    {
        volatile ULONG NextProcessor;                    // 0x218
        struct
        {
            ULONG NextProcessorNumber:31;
            ULONG SharedReadyQueue:1;
        };
    };
    LONG QueuePriority;                                  // 0x21C
    struct _KPROCESS *Process;                           // 0x220
    KAFFINITY UserAffinity;                              // 0x228 Win11 type: PKAFFINITY_EX
    USHORT UserAffinityPrimaryGroup;                     // 0x230
    CHAR PreviousMode;                                   // 0x232
    CHAR BasePriority;                                   // 0x233
    UCHAR Spare24;                                       // 0x234
    UCHAR Preempted;                                     // 0x235
    UCHAR AdjustReason;                                  // 0x236
    CHAR AdjustIncrement;                                // 0x237
    ULONG64 AffinityVersion;                             // 0x238
    KAFFINITY Affinity;                                  // 0x240 Win11 type: PKAFFINITY_EX
    USHORT AffinityPrimaryGroup;                         // 0x248
    UCHAR ApcStateIndex;                                 // 0x24A
    UCHAR WaitBlockCount;                                // 0x24B
    ULONG IdealProcessor;                                // 0x24C
    ULONG64 NpxState;                                    // 0x250
    union
    {
        KAPC_STATE SavedApcState;                        // 0x258
        struct
        {
            UCHAR SavedApcStateFill[FIELD_OFFSET(KAPC_STATE, UserApcPending) + 1]; // 0x2B
            UCHAR WaitReason;                            // 0x283
            CHAR SuspendCount;                           // 0x284
            CHAR Saturation;                             // 0x285
            SHORT SListFaultCount;                       // 0x286
        };
    };
    union
    {
        KAPC SchedulerApc;                               // 0x288
        KAPC SuspendApc;                                 // [ReactOS] alias
        struct
        {
            UCHAR SchedulerApcFill1[FIELD_OFFSET(KAPC, SpareByte1)]; // 0x03
            UCHAR QuantumReset;                          // 0x28B
        };
        struct
        {
            UCHAR SchedulerApcFill2[FIELD_OFFSET(KAPC, SpareLong0)]; // 0x04
            ULONG KernelTime;                            // 0x28C
        };
        struct
        {
            UCHAR SchedulerApcFill3[FIELD_OFFSET(KAPC, SystemArgument1)]; // 0x40
            PKPRCB WaitPrcb;                             // 0x2C8
        };
        struct
        {
            UCHAR SchedulerApcFill4[FIELD_OFFSET(KAPC, SystemArgument2)]; // 0x48
            PVOID LegoData;                              // 0x2D0
        };
        struct
        {
            UCHAR SchedulerApcFill5[FIELD_OFFSET(KAPC, Inserted) + 1]; // 0x53
            UCHAR CallbackNestingLevel;                  // 0x2DB
            ULONG UserTime;                              // 0x2DC
        };
    };
    union
    {
        KEVENT SuspendEvent;                             // 0x2E0
        KEVENT SuspendSemaphore;                         // [ReactOS] alias, KEVENT-sized
    };
    LIST_ENTRY ThreadListEntry;                          // 0x2F8
    LIST_ENTRY MutantListHead;                           // 0x308
    union
    {
        struct
        {
            volatile UCHAR AbWaitEntryCount;             // 0x318
            volatile UCHAR AbOwnedEntryCount;            // 0x319
        };
        volatile USHORT AbEntryCountValue;               // 0x318
    };
    union
    {
        struct
        {
            UCHAR FreezeCount2:1;                        // 0x31A
            UCHAR FreezeNormal:1;
            UCHAR FreezeDeep:1;
        };
        UCHAR FreezeFlags;                               // 0x31A
    };
    CHAR WobPriority;                                    // 0x31B
    ULONG SecureThreadCookie;                            // 0x31C
    PVOID SchedulerSharedSystemSlot;                     // 0x320
    SINGLE_LIST_ENTRY PropagateBoostsEntry;              // 0x328
    SINGLE_LIST_ENTRY IoSelfBoostsEntry;                 // 0x330
    UCHAR PriorityFloorCounts[32];                       // 0x338
    ULONG PriorityFloorSummary;                          // 0x358
    volatile LONG AbCompletedIoBoostCount;               // 0x35C
    volatile LONG AbCompletedIoQoSBoostCount;            // 0x360
    volatile SHORT KeReferenceCount;                     // 0x364
    CHAR DecayBoost;                                     // 0x366
    UCHAR LargeStack;                                    // 0x367 Win11 name: Spare6 [ReactOS]
    ULONG ForegroundLossTime;                            // 0x368
    union
    {
        LIST_ENTRY GlobalForegroundListEntry;            // 0x370
        struct
        {
            SINGLE_LIST_ENTRY ForegroundDpcStackListEntry;
            ULONG_PTR InGlobalForegroundList;
        };
    };
    LONG64 ReadOperationCount;                           // 0x380
    LONG64 WriteOperationCount;                          // 0x388
    LONG64 OtherOperationCount;                          // 0x390
    LONG64 ReadTransferCount;                            // 0x398
    LONG64 WriteTransferCount;                           // 0x3A0
    LONG64 OtherTransferCount;                           // 0x3A8
    struct _KSCB *QueuedScb;                             // 0x3B0
    volatile ULONG ThreadTimerDelay;                     // 0x3B8
    USHORT Spare26;                                      // 0x3BC
    volatile UCHAR PpmPolicy;                            // 0x3BE
    UCHAR Spare27;                                       // 0x3BF
    ULONG64 TracingPrivate[1];                           // 0x3C0
    PVOID SchedulerAssist;                               // 0x3C8
    PVOID AbWaitObject;                                  // 0x3D0
    ULONG ReservedPreviousReadyTimeValue;                // 0x3D8
    ULONG Spare3DC;                                      // 0x3DC
    ULONG64 KernelWaitTime;                              // 0x3E0
    ULONG64 UserWaitTime;                                // 0x3E8
    union
    {
        LIST_ENTRY GlobalUpdateVpThreadPriorityListEntry; // 0x3F0
        struct
        {
            SINGLE_LIST_ENTRY UpdateVpThreadPriorityDpcStackListEntry;
            ULONG_PTR InGlobalUpdateVpThreadPriorityList;
        };
    };
    LONG SchedulerAssistPriorityFloor;                   // 0x400
    LONG RealtimePriorityFloor;                          // 0x404
    PVOID KernelShadowStack;                             // 0x408
    PVOID KernelShadowStackInitial;                      // 0x410
    PVOID KernelShadowStackBase;                         // 0x418
    ULONG_PTR KernelShadowStackLimit;                    // 0x420 _KERNEL_SHADOW_STACK_LIMIT
    ULONG64 ExtendedFeatureDisableMask;                  // 0x428
    ULONG64 HgsFeedbackStartTime;                        // 0x430
    ULONG64 HgsFeedbackCycles;                           // 0x438
    ULONG HgsInvalidFeedbackCount;                       // 0x440
    ULONG HgsLowerPerfClassFeedbackCount;                // 0x444
    ULONG HgsHigherPerfClassFeedbackCount;               // 0x448
    volatile ULONG ModeHistory;                          // 0x44C
    SINGLE_LIST_ENTRY SystemAffinityTokenListHead;       // 0x450
    PVOID IptSaveArea;                                   // 0x458
    UCHAR ResourceIndex;                                 // 0x460
    volatile UCHAR CoreIsolationReasons;                 // 0x461
    UCHAR BamQosLevelFromAssistPage;                     // 0x462
    UCHAR SecureCallCoreIsolationCount;                  // 0x463
    ULONG SchedulerSharedOffset;                         // 0x464
    PVOID SchedulerSharedSwappablePage;                  // 0x468 PKSWAPPABLE_PAGE
    PVOID KernelAbEntries;                               // 0x470 PKLOCK_ENTRIES
    PVOID UserAbEntries;                                 // 0x478 PKLOCK_ENTRIES
    ULONG64 KcsanThread;                                 // 0x480
    ULONG SchedulerAssistYieldCounter;                   // 0x488
    ULONG SchedulerAssistYieldBoostCount;                // 0x48C
    LONG64 SchedulerAssistLastYieldBoostTime;            // 0x490
    // Win11 Padding[5] region (0x498..0x4C0): ReactOS private members
    KSPIN_LOCK ApcQueueLock;                             // 0x498 [ReactOS]
    PKAPC_STATE ApcStatePointer[2];                      // 0x4A0 [ReactOS]
    PVOID CallbackStack;                                 // 0x4B0 [ReactOS]
    ULONG64 Padding;                                     // 0x4B8
} KTHREAD;                                               // sizeof 0x4C0

#elif (NTDDI_VERSION < NTDDI_WIN8)

typedef struct _KTHREAD
{
    DISPATCHER_HEADER Header;
#if (NTDDI_VERSION >= NTDDI_LONGHORN) // [
    ULONGLONG CycleTime;
#ifndef _WIN64 // [
    ULONG HighCycleTime;
#endif // ]
    ULONGLONG QuantumTarget;
#else // ][
    LIST_ENTRY MutantListHead;
#endif // ]
    PVOID InitialStack;
    ULONG_PTR StackLimit; // FIXME: PVOID
    PVOID KernelStack;
    KSPIN_LOCK ThreadLock;
#if (NTDDI_VERSION >= NTDDI_WIN7) // [
    KWAIT_STATUS_REGISTER WaitRegister;
    BOOLEAN Running;
    BOOLEAN Alerted[2];
    union
    {
        struct
        {
            ULONG KernelStackResident:1;
            ULONG ReadyTransition:1;
            ULONG ProcessReadyQueue:1;
            ULONG WaitNext:1;
            ULONG SystemAffinityActive:1;
            ULONG Alertable:1;
            ULONG GdiFlushActive:1;
            ULONG UserStackWalkActive:1;
            ULONG ApcInterruptRequest:1;
            ULONG ForceDeferSchedule:1;
            ULONG QuantumEndMigrate:1;
            ULONG UmsDirectedSwitchEnable:1;
            ULONG TimerActive:1;
            ULONG Reserved:19;
        };
        LONG MiscFlags;
    };
#endif // ]
    union
    {
        KAPC_STATE ApcState;
        struct
        {
            UCHAR ApcStateFill[FIELD_OFFSET(KAPC_STATE, UserApcPending) + 1];
#if (NTDDI_VERSION >= NTDDI_LONGHORN) // [
            SCHAR Priority;
#if (NTDDI_VERSION >= NTDDI_WIN7) // [
            /* On x86, the following members "fall out" of the union */
            volatile ULONG NextProcessor;
            volatile ULONG DeferredProcessor;
#else // ][
            /* On x86, the following members "fall out" of the union */
            volatile USHORT NextProcessor;
            volatile USHORT DeferredProcessor;
#endif // ]
#else // ][
            UCHAR ApcQueueable;
            /* On x86, the following members "fall out" of the union */
            volatile UCHAR NextProcessor;
            volatile UCHAR DeferredProcessor;
            UCHAR AdjustReason;
            SCHAR AdjustIncrement;
#endif // ]
        };
    };
    KSPIN_LOCK ApcQueueLock;
#if !defined(_M_AMD64) && !defined(_M_ARM64) // [
    ULONG ContextSwitches;
    volatile UCHAR State;
    UCHAR NpxState;
    KIRQL WaitIrql;
    KPROCESSOR_MODE WaitMode;
#endif // ]
    LONG_PTR WaitStatus;
#if (NTDDI_VERSION >= NTDDI_WIN7) // [
    PKWAIT_BLOCK WaitBlockList;
#else // ][
    union
    {
        PKWAIT_BLOCK WaitBlockList;
        PKGATE GateObject;
    };
#if (NTDDI_VERSION >= NTDDI_LONGHORN) // [
    union
    {
        struct
        {
            ULONG KernelStackResident:1;
            ULONG ReadyTransition:1;
            ULONG ProcessReadyQueue:1;
            ULONG WaitNext:1;
            ULONG SystemAffinityActive:1;
            ULONG Alertable:1;
            ULONG GdiFlushActive:1;
            ULONG Reserved:25;
        };
        LONG MiscFlags;
    };
#else // ][
    BOOLEAN Alertable;
    BOOLEAN WaitNext;
#endif // ]
    UCHAR WaitReason;
#if (NTDDI_VERSION < NTDDI_LONGHORN)
    SCHAR Priority;
    BOOLEAN EnableStackSwap;
#endif // ]
    volatile UCHAR SwapBusy;
    BOOLEAN Alerted[MaximumMode];
#endif // ]
    union
    {
        LIST_ENTRY WaitListEntry;
        SINGLE_LIST_ENTRY SwapListEntry;
    };
    PKQUEUE Queue;
#if !defined(_M_AMD64) && !defined(_M_ARM64) // [
    ULONG WaitTime;
    union
    {
        struct
        {
            SHORT KernelApcDisable;
            SHORT SpecialApcDisable;
        };
        ULONG CombinedApcDisable;
    };
#endif // ]
    struct _TEB *Teb;

#if (NTDDI_VERSION >= NTDDI_WIN7) // [
    KTIMER Timer;
#else // ][
    union
    {
        KTIMER Timer;
        struct
        {
            UCHAR TimerFill[FIELD_OFFSET(KTIMER, Period) + sizeof(LONG)];
#if !defined(_WIN64) // [
        };
    };
#endif // ]
#endif // ]
            union
            {
                struct
                {
                    ULONG AutoAlignment:1;
                    ULONG DisableBoost:1;
#if (NTDDI_VERSION >= NTDDI_LONGHORN) // [
                    ULONG EtwStackTraceApc1Inserted:1;
                    ULONG EtwStackTraceApc2Inserted:1;
                    ULONG CycleChargePending:1;
                    ULONG CalloutActive:1;
                    ULONG ApcQueueable:1;
                    ULONG EnableStackSwap:1;
                    ULONG GuiThread:1;
                    ULONG ReservedFlags:23;
#else // ][
                    LONG ReservedFlags:30;
#endif // ]
                };
                LONG ThreadFlags;
            };
#if defined(_WIN64) && (NTDDI_VERSION < NTDDI_WIN7) // [
        };
    };
#endif // ]
#if (NTDDI_VERSION >= NTDDI_WIN7) // [
#if defined(_WIN64) // [
    ULONG Spare0;
#else // ][
    PVOID ServiceTable;
#endif // ]
#endif // ]
    union
    {
        DECLSPEC_ALIGN(8) KWAIT_BLOCK WaitBlock[THREAD_WAIT_OBJECTS + 1];
#if (NTDDI_VERSION < NTDDI_WIN7) // [
        struct
        {
            UCHAR WaitBlockFill0[FIELD_OFFSET(KWAIT_BLOCK, SpareByte)]; // 32bit = 23, 64bit = 43
#if (NTDDI_VERSION >= NTDDI_LONGHORN) // [
            UCHAR IdealProcessor;
#else // ][
            BOOLEAN SystemAffinityActive;
#endif // ]
        };
        struct
        {
            UCHAR WaitBlockFill1[1 * sizeof(KWAIT_BLOCK) + FIELD_OFFSET(KWAIT_BLOCK, SpareByte)]; // 47 / 91
            CCHAR PreviousMode;
        };
        struct
        {
            UCHAR WaitBlockFill2[2 * sizeof(KWAIT_BLOCK) + FIELD_OFFSET(KWAIT_BLOCK, SpareByte)]; // 71 / 139
            UCHAR ResourceIndex;
        };
        struct
        {
            UCHAR WaitBlockFill3[3 * sizeof(KWAIT_BLOCK) + FIELD_OFFSET(KWAIT_BLOCK, SpareByte)]; // 95 / 187
            UCHAR LargeStack;
        };
#endif // ]
#ifdef _WIN64 // [
        struct
        {
            UCHAR WaitBlockFill4[FIELD_OFFSET(KWAIT_BLOCK, SpareLong)];
            ULONG ContextSwitches;
        };
        struct
        {
            UCHAR WaitBlockFill5[1 * sizeof(KWAIT_BLOCK) + FIELD_OFFSET(KWAIT_BLOCK, SpareLong)];
            UCHAR State;
            UCHAR NpxState;
            UCHAR WaitIrql;
            CHAR WaitMode;
        };
        struct
        {
            UCHAR WaitBlockFill6[2 * sizeof(KWAIT_BLOCK) + FIELD_OFFSET(KWAIT_BLOCK, SpareLong)];
            ULONG WaitTime;
        };
#if (NTDDI_VERSION >= NTDDI_VISTA) // [
        struct
        {
            UCHAR WaitBlockFill7[168];
            PVOID TebMappedLowVa;
            struct _UMS_CONTROL_BLOCK* Ucb;
        };
#endif // ]
        struct
        {
#if (NTDDI_VERSION >= NTDDI_VISTA) // [
            UCHAR WaitBlockFill8[188];
#else // ][
            UCHAR WaitBlockFill7[3 * sizeof(KWAIT_BLOCK) + FIELD_OFFSET(KWAIT_BLOCK, SpareLong)];
#endif // ]
            union
            {
                struct
                {
                    SHORT KernelApcDisable;
                    SHORT SpecialApcDisable;
                };
                ULONG CombinedApcDisable;
            };
        };
#endif // ]
    };
    LIST_ENTRY QueueListEntry;
    PKTRAP_FRAME TrapFrame;
#if (NTDDI_VERSION >= NTDDI_LONGHORN) // [
    PVOID FirstArgument;
    union
    {
        PVOID CallbackStack;
        ULONG_PTR CallbackDepth;
    };
#else // ][
    PVOID CallbackStack;
#endif // ]
#if (NTDDI_VERSION < NTDDI_LONGHORN) || ((NTDDI_VERSION < NTDDI_WIN7) && !defined(_WIN64)) // [
    PVOID ServiceTable;
#endif // ]
#if (NTDDI_VERSION < NTDDI_LONGHORN) && defined(_WIN64) // [
    ULONG KernelLimit;
#endif // ]
    UCHAR ApcStateIndex;
#if (NTDDI_VERSION < NTDDI_LONGHORN) // [
    UCHAR IdealProcessor;
    BOOLEAN Preempted;
    BOOLEAN ProcessReadyQueue;
#ifdef _WIN64 // [
    PVOID Win32kTable;
    ULONG Win32kLimit;
#endif // ]
    BOOLEAN KernelStackResident;
#endif // ]
    SCHAR BasePriority;
    SCHAR PriorityDecrement;
#if (NTDDI_VERSION >= NTDDI_LONGHORN) // [
    BOOLEAN Preempted;
    UCHAR AdjustReason;
    CHAR AdjustIncrement;
#if (NTDDI_VERSION >= NTDDI_WIN7)
    UCHAR PreviousMode;
#else
    UCHAR Spare01;
#endif
#endif // ]
    CHAR Saturation;
#if (NTDDI_VERSION >= NTDDI_LONGHORN) // [
    ULONG SystemCallNumber;
#if (NTDDI_VERSION >= NTDDI_WIN7) // [
    ULONG FreezeCount;
#else // ][
    ULONG Spare02;
#endif // ]
#endif // ]
#if (NTDDI_VERSION >= NTDDI_WIN7) // [
    GROUP_AFFINITY UserAffinity;
    struct _KPROCESS *Process;
    GROUP_AFFINITY Affinity;
    ULONG IdealProcessor;
    ULONG UserIdealProcessor;
#else // ][
    KAFFINITY UserAffinity;
    struct _KPROCESS *Process;
    KAFFINITY Affinity;
#endif // ]
    PKAPC_STATE ApcStatePointer[2];
    union
    {
        KAPC_STATE SavedApcState;
        struct
        {
            UCHAR SavedApcStateFill[FIELD_OFFSET(KAPC_STATE, UserApcPending) + 1];
#if (NTDDI_VERSION >= NTDDI_WIN7) // [
            UCHAR WaitReason;
#else // ][
            CCHAR FreezeCount;
#endif // ]
#ifndef _WIN64 // [
        };
    };
#endif // ]
            CCHAR SuspendCount;
#if (NTDDI_VERSION >= NTDDI_WIN7) // [
            CCHAR Spare1;
#else // ][
            UCHAR UserIdealProcessor;
#endif // ]
#if (NTDDI_VERSION >= NTDDI_WIN7) // [
#elif (NTDDI_VERSION >= NTDDI_LONGHORN) // ][
            UCHAR Spare03;
#else // ][
            UCHAR CalloutActive;
#endif // ]
#ifdef _WIN64 // [
            UCHAR CodePatchInProgress;
        };
    };
#endif // ]
#if defined(_M_IX86) // [
#if (NTDDI_VERSION >= NTDDI_LONGHORN) // [
    union
    {
        UCHAR OtherPlatformFill;
        UCHAR Iopl;
    };
#else // ][
    UCHAR Iopl;
#endif // ]
#endif // ]
    PVOID Win32Thread;
    PVOID StackBase;
    union
    {
        KAPC SuspendApc;
        struct
        {
            UCHAR SuspendApcFill0[1];
#if (NTDDI_VERSION >= NTDDI_WIN7) // [
            UCHAR ResourceIndex;
#elif (NTDDI_VERSION >= NTDDI_LONGHORN) // ][
            CHAR Spare04;
#else // ][
            SCHAR Quantum;
#endif // ]
        };
        struct
        {
            UCHAR SuspendApcFill1[3];
            UCHAR QuantumReset;
        };
        struct
        {
            UCHAR SuspendApcFill2[4];
            ULONG KernelTime;
        };
        struct
        {
            UCHAR SuspendApcFill3[FIELD_OFFSET(KAPC, SystemArgument1)];
#if (NTDDI_VERSION >= NTDDI_LONGHORN)
            PKPRCB WaitPrcb;
#else
            PVOID TlsArray;
#endif
        };
        struct
        {
            UCHAR SuspendApcFill4[FIELD_OFFSET(KAPC, SystemArgument2)]; // 40 / 72
            PVOID LegoData;
        };
        struct
        {
            UCHAR SuspendApcFill5[FIELD_OFFSET(KAPC, Inserted) + 1]; // 47 / 83
#if (NTDDI_VERSION >= NTDDI_WIN7) // [
            UCHAR LargeStack;
#else // ][
            UCHAR PowerState;
#endif // ]
#ifdef _WIN64 // [
            ULONG UserTime;
#endif // ]
        };
    };
#ifndef _WIN64 // [
    ULONG UserTime;
#endif // ]
    union
    {
        KSEMAPHORE SuspendSemaphore;
        struct
        {
            UCHAR SuspendSemaphorefill[FIELD_OFFSET(KSEMAPHORE, Limit) + 4]; // 20 / 28
#ifdef _WIN64 // [
            ULONG SListFaultCount;
#endif // ]
        };
    };
#ifndef _WIN64 // [
    ULONG SListFaultCount;
#endif // ]
    LIST_ENTRY ThreadListEntry;
#if (NTDDI_VERSION >= NTDDI_LONGHORN) // [
    LIST_ENTRY MutantListHead;
#endif // ]
    PVOID SListFaultAddress;
#ifdef _M_AMD64 // [
    LONG64 ReadOperationCount;
    LONG64 WriteOperationCount;
    LONG64 OtherOperationCount;
    LONG64 ReadTransferCount;
    LONG64 WriteTransferCount;
    LONG64 OtherTransferCount;
#endif // ]
#if (NTDDI_VERSION >= NTDDI_WIN7) // [
    PKTHREAD_COUNTERS ThreadCounters;
    PXSTATE_SAVE XStateSave;
#elif (NTDDI_VERSION >= NTDDI_LONGHORN) // ][
    PVOID MdlForLockedTeb;
#endif // ]
#if defined(__REACTOS__) && defined(_M_AMD64) // HACK!
    XSAVE_FORMAT* StateSaveArea;
#elif defined(__REACTOS__) && defined(_M_ARM64)
    PVOID StateSaveArea;
    PVOID Arm64FpState;
#endif
} KTHREAD;

#else // not (NTDDI_VERSION < NTDDI_WIN8)

#if defined(_WIN64) && (NTDDI_VERSION < 0x06032580) // since WIN 8.1 Update1 6.3.9600.16384
#define NUMBER_OF_LOCK_ENTRIES 5
#else
#define NUMBER_OF_LOCK_ENTRIES 6
#endif

typedef struct _KTHREAD
{
    DISPATCHER_HEADER Header;
    PVOID SListFaultAddress;
    ULONG64 QuantumTarget;
    PVOID InitialStack;
    volatile VOID *StackLimit;
    PVOID StackBase;
    KSPIN_LOCK ThreadLock;
    volatile ULONG64 CycleTime;
#ifndef _WIN64
    volatile ULONG HighCycleTime;
    PVOID ServiceTable;
#endif
    ULONG CurrentRunTime;
    ULONG ExpectedRunTime;
    PVOID KernelStack;
    XSAVE_FORMAT* StateSaveArea;
#if defined(__REACTOS__) && defined(_M_ARM64)
    PVOID Arm64FpState;
#endif
    struct _KSCHEDULING_GROUP* SchedulingGroup;
    KWAIT_STATUS_REGISTER WaitRegister;
    BOOLEAN Running;
    BOOLEAN Alerted[MaximumMode];

    union
    {
        struct
        {
#if (NTDDI_VERSION < NTDDI_WIN10)
            ULONG KernelStackResident : 1;
#else
            ULONG AutoBoostActive : 1;
#endif
            ULONG ReadyTransition : 1;
#if (NTDDI_VERSION < NTDDI_WIN10TH2)
            ULONG ProcessReadyQueue : 1;
#endif
            ULONG ProcessReadyQueue : 1;
            ULONG WaitNext : 1;
            ULONG SystemAffinityActive : 1;
            ULONG Alertable : 1;
#if (NTDDI_VERSION < NTDDI_WIN81)
            ULONG CodePatchInProgress : 1;
#endif
            ULONG UserStackWalkActive : 1;
            ULONG ApcInterruptRequest : 1;
            ULONG QuantumEndMigrate : 1;
            ULONG UmsDirectedSwitchEnable : 1;
            ULONG TimerActive : 1;
            ULONG SystemThread : 1;
            ULONG ProcessDetachActive : 1;
            ULONG CalloutActive : 1;
            ULONG ScbReadyQueue : 1;
            ULONG ApcQueueable : 1;
            ULONG ReservedStackInUse : 1;
            ULONG UmsPerformingSyscall : 1;
            ULONG DisableStackCheck : 1;
            ULONG Reserved : 12;
        };
        LONG MiscFlags;
    };

    union
    {
        struct
        {
            ULONG AutoAlignment : 1;
            ULONG DisableBoost : 1;
            ULONG UserAffinitySet : 1;
            ULONG AlertedByThreadId : 1;
            ULONG QuantumDonation : 1;
            ULONG EnableStackSwap : 1;
            ULONG GuiThread : 1;
            ULONG DisableQuantum : 1;
            ULONG ChargeOnlyGroup : 1;
            ULONG DeferPreemption : 1;
            ULONG QueueDeferPreemption : 1;
            ULONG ForceDeferSchedule : 1;
            ULONG ExplicitIdealProcessor : 1;
            ULONG FreezeCount : 1;
#if (NTDDI_VERSION >= 0x060324D7) // since 6.3.9431.0
            ULONG TerminationApcRequest : 1;
#endif
#if (NTDDI_VERSION >= 0x06032580) // since 6.3.9600.16384
            ULONG AutoBoostEntriesExhausted : 1;
#endif
#if (NTDDI_VERSION >= 0x06032580) // since 6.3.9600.17031
            ULONG KernelStackResident : 1;
#endif
#if (NTDDI_VERSION >= NTDDI_WIN10)
            ULONG CommitFailTerminateRequest : 1;
            ULONG ProcessStackCountDecremented : 1;
            ULONG ThreadFlagsSpare : 5;
#endif
            ULONG EtwStackTraceApcInserted : 8;
#if (NTDDI_VERSION < NTDDI_WIN10)
            ULONG ReservedFlags : 10;
#endif
        };
        LONG ThreadFlags;
    };

#if (NTDDI_VERSION >= NTDDI_WIN10)
    volatile UCHAR Tag;
    UCHAR SystemHeteroCpuPolicy;
    UCHAR UserHeteroCpuPolicy : 7;
    UCHAR ExplicitSystemHeteroCpuPolicy : 1;
    UCHAR Spare0;
#else
    ULONG Spare0;
#endif
    ULONG SystemCallNumber;
#ifdef _WIN64
    ULONG Spare1; // Win 10: Spare10
#endif
    PVOID FirstArgument;
    PKTRAP_FRAME TrapFrame;

    union
    {
        KAPC_STATE ApcState;
        struct
        {
            UCHAR ApcStateFill[RTL_SIZEOF_THROUGH_FIELD(KAPC_STATE, UserApcPending)]; // 32bit: 23/0x17, 64bit: 43/0x2B
            SCHAR Priority;
            ULONG UserIdealProcessor;
        };
    };

#ifndef _WIN64
    ULONG ContextSwitches;
    volatile UCHAR State;
#if (NTDDI_VERSION >= NTDDI_WIN10) // since 10.0.10074.0
    CHAR Spare12;
#else
    CHAR NpxState;
#endif
    KIRQL WaitIrql;
    KPROCESSOR_MODE WaitMode;
#endif

    volatile INT_PTR WaitStatus;
    PKWAIT_BLOCK WaitBlockList;
    union
    {
        LIST_ENTRY WaitListEntry;
        SINGLE_LIST_ENTRY SwapListEntry;
    };
    PKQUEUE Queue;
    PVOID Teb;
#if (NTDDI_VERSION >= NTDDI_WIN8 /* 0x060223F0 */) // since 6.2.9200.16384
    ULONG64 RelativeTimerBias;
#endif
    KTIMER Timer;

    union
    {
        DECLSPEC_ALIGN(8) KWAIT_BLOCK WaitBlock[THREAD_WAIT_OBJECTS + 1];
#ifdef _WIN64
        struct
        {
            UCHAR WaitBlockFill4[FIELD_OFFSET(KWAIT_BLOCK, SpareLong)]; // 32bit: -, 64bit: 20/0x14
            ULONG ContextSwitches;
        };
        struct
        {
            UCHAR WaitBlockFill5[1 * sizeof(KWAIT_BLOCK) + FIELD_OFFSET(KWAIT_BLOCK, SpareLong)]; // 32bit: -, 64bit: 68/0x44
            UCHAR State;
#if (NTDDI_VERSION >= NTDDI_WIN10)
            CHAR Spare13;
#else
            CHAR NpxState;
#endif
            UCHAR WaitIrql;
            CHAR WaitMode;
        };
        struct
        {
            UCHAR WaitBlockFill6[2 * sizeof(KWAIT_BLOCK) + FIELD_OFFSET(KWAIT_BLOCK, SpareLong)]; // 32bit: -, 64bit: 116/0x74
            ULONG WaitTime;
        };
        struct
        {
            UCHAR WaitBlockFill7[3 * sizeof(KWAIT_BLOCK) + FIELD_OFFSET(KWAIT_BLOCK, SpareLong)]; // 32bit: -, 64bit: 164/0xA4
            union
            {
                struct
                {
                    SHORT KernelApcDisable;
                    SHORT SpecialApcDisable;
                };
                ULONG CombinedApcDisable;
            };
        };
#endif
        struct
        {
            UCHAR WaitBlockFill8[FIELD_OFFSET(KWAIT_BLOCK, SparePtr)]; // 32bit: 20/0x14, 64bit: 40/0x28
            struct _KTHREAD_COUNTERS *ThreadCounters;
        };
        struct
        {
            UCHAR WaitBlockFill9[1 * sizeof(KWAIT_BLOCK) + FIELD_OFFSET(KWAIT_BLOCK, SparePtr)]; // 32bit: 44/0x2C, 64bit: 88/0x58
            PXSTATE_SAVE XStateSave;
        };
        struct
        {
            UCHAR WaitBlockFill10[2 * sizeof(KWAIT_BLOCK) + FIELD_OFFSET(KWAIT_BLOCK, SparePtr)]; // 32bit: 68/0x44, 64bit: 136/0x88
            PVOID Win32Thread;
        };
        struct
        {
            UCHAR WaitBlockFill11[3 * sizeof(KWAIT_BLOCK) + FIELD_OFFSET(KWAIT_BLOCK, Object)]; // 32bit: 88/0x58, 64bit: 176/0xB0
#ifdef _WIN64
            struct _UMS_CONTROL_BLOCK* Ucb;
            struct _KUMS_CONTEXT_HEADER* Uch;
#else
            ULONG WaitTime;
            union
            {
                struct
                {
                    SHORT KernelApcDisable;
                    SHORT SpecialApcDisable;
                };
                ULONG CombinedApcDisable;
            };
#endif
        };
    };

#ifdef _WIN64
    PVOID TebMappedLowVa;
#endif
    LIST_ENTRY QueueListEntry;
#if (NTDDI_VERSION >= 0x060223F0) // since 6.2.9200.16384
    union
    {
        ULONG NextProcessor;
        struct
        {
            ULONG NextProcessorNumber : 31;
            ULONG SharedReadyQueue : 1;
        };
    };
    LONG QueuePriority;
#else
    ULONG NextProcessor;
    ULONG DeferredProcessor;
#endif
    PKPROCESS Process;

    union
    {
        GROUP_AFFINITY UserAffinity;
        struct
        {
            UCHAR UserAffinityFill[FIELD_OFFSET(GROUP_AFFINITY, Reserved)]; // 32bit: 6/0x6, 64bit: 10/0x0A
            CHAR PreviousMode;
            CHAR BasePriority;
            union
            {
                CHAR PriorityDecrement;
                struct
                {
                    UCHAR ForegroundBoost : 4;
                    UCHAR UnusualBoost : 4;
                };
            };
            UCHAR Preempted;
            UCHAR AdjustReason;
            CHAR AdjustIncrement;
        };
    };

#if (NTDDI_VERSION >= NTDDI_WIN10) // since 10.0.10240.16384
    ULONG_PTR AffinityVersion;
#endif
    union
    {
        GROUP_AFFINITY Affinity;
        struct
        {
            UCHAR AffinityFill[FIELD_OFFSET(GROUP_AFFINITY, Reserved)]; // 32bit: 6/0x6, 64bit: 10/0x0A
            UCHAR ApcStateIndex;
            UCHAR WaitBlockCount;
            ULONG IdealProcessor;
        };
    };

#if (NTDDI_VERSION >= NTDDI_WIN10) // since 10.0.10240.16384
#ifdef _WIN64
    ULONG64 NpxState;
#else
    ULONG Spare15;
#endif
#else
    PKAPC_STATE ApcStatePointer[2];
#endif

    union
    {
        KAPC_STATE SavedApcState;
        struct
        {
            UCHAR SavedApcStateFill[FIELD_OFFSET(KAPC_STATE, UserApcPending) + 1]; // 32bit: 23/0x17, 64bit: 43/0x2B
            UCHAR WaitReason;
            CHAR SuspendCount;
            CHAR Saturation;
            SHORT SListFaultCount;
        };
    };

    union
    {
        KAPC SchedulerApc;
        struct
        {
            UCHAR SchedulerApcFill0[FIELD_OFFSET(KAPC, SpareByte0)]; // 32bit:  1/0x01, 64bit: 1/0x01
            UCHAR ResourceIndex;
        };
        struct
        {
            UCHAR SchedulerApcFill1[FIELD_OFFSET(KAPC, SpareByte1)]; // 32bit:  3/0x03, 64bit: 3/0x03
            UCHAR QuantumReset;
        };
        struct
        {
            UCHAR SchedulerApcFill2[FIELD_OFFSET(KAPC, SpareLong0)]; // 32bit:  4/0x04, 64bit: 4/0x04
            ULONG KernelTime;
        };
        struct
        {
            UCHAR SuspendApcFill3[FIELD_OFFSET(KAPC, SystemArgument1)]; // 32 bit:, 64 bit: 64/0x40
            PKPRCB WaitPrcb;
        };
        struct
        {
            UCHAR SchedulerApcFill4[FIELD_OFFSET(KAPC, SystemArgument2)]; // 32 bit:, 64 bit: 72/0x48
            PVOID LegoData;
        };
        struct
        {
            UCHAR SchedulerApcFill5[FIELD_OFFSET(KAPC, Inserted) + 1]; // 32 bit:, 64 bit: 83/0x53
            UCHAR CallbackNestingLevel;
            ULONG UserTime;
        };
    };

    KEVENT SuspendEvent;
    LIST_ENTRY ThreadListEntry;
    LIST_ENTRY MutantListHead;

#if (NTDDI_VERSION >= NTDDI_WIN10)
    UCHAR AbEntrySummary;
    UCHAR AbWaitEntryCount;
    USHORT Spare20;
#if _WIN64
    ULONG SecureThreadCookie;
#endif
#elif (NTDDI_VERSION >= NTDDI_WINBLUE) // 6.3.9431.0
    SINGLE_LIST_ENTRY LockEntriesFreeList;
#endif

#if (NTDDI_VERSION >= NTDDI_WINBLUE /* 0x06032580 */) // since 6.3.9600.16384
    KLOCK_ENTRY LockEntries[NUMBER_OF_LOCK_ENTRIES];
    SINGLE_LIST_ENTRY PropagateBoostsEntry;
    SINGLE_LIST_ENTRY IoSelfBoostsEntry;
    UCHAR PriorityFloorCounts[16];
    ULONG PriorityFloorSummary;
    volatile LONG AbCompletedIoBoostCount;
  #if (NTDDI_VERSION >= NTDDI_WIN10_RS1)
    LONG AbCompletedIoQoSBoostCount;
  #endif

  #if (NTDDI_VERSION >= NTDDI_WIN10) // since 10.0.10240.16384
    volatile SHORT KeReferenceCount;
  #else
    volatile SHORT AbReferenceCount;
  #endif
  #if (NTDDI_VERSION >= 0x06040000) // since 6.4.9841.0
    UCHAR AbOrphanedEntrySummary;
    UCHAR AbOwnedEntryCount;
  #else
    UCHAR AbFreeEntryCount;
    UCHAR AbWaitEntryCount;
  #endif
    ULONG ForegroundLossTime;
    union
    {
        LIST_ENTRY GlobalForegroundListEntry;
        struct
        {
            SINGLE_LIST_ENTRY ForegroundDpcStackListEntry;
            ULONG_PTR InGlobalForegroundList;
        };
    };
#endif

#if _WIN64
    LONG64 ReadOperationCount;
    LONG64 WriteOperationCount;
    LONG64 OtherOperationCount;
    LONG64 ReadTransferCount;
    LONG64 WriteTransferCount;
    LONG64 OtherTransferCount;
#endif
#if (NTDDI_VERSION >= NTDDI_WIN10) // since 10.0.10041.0
    struct _KSCB *QueuedScb;
#ifndef _WIN64
    ULONG64 NpxState;
#endif
#endif
} KTHREAD;

#endif

#if defined(_M_ARM64) && !defined(__ASSEMBLER__)
C_ASSERT(sizeof(KTHREAD) == 0x4A0);
C_ASSERT(sizeof(KWAIT_BLOCK) == 0x30);
C_ASSERT(sizeof(KAPC) == 0x58);
C_ASSERT(sizeof(KAPC_STATE) == 0x30);
C_ASSERT(sizeof(KTIMER) == 0x40);
C_ASSERT(FIELD_OFFSET(KTHREAD, InitialStack) == 0x028);
C_ASSERT(FIELD_OFFSET(KTHREAD, StackLimit) == 0x030);
C_ASSERT(FIELD_OFFSET(KTHREAD, StackBase) == 0x038);
C_ASSERT(FIELD_OFFSET(KTHREAD, ThreadLock) == 0x040);
C_ASSERT(FIELD_OFFSET(KTHREAD, CycleTime) == 0x048);
C_ASSERT(FIELD_OFFSET(KTHREAD, KernelStack) == 0x058);
C_ASSERT(FIELD_OFFSET(KTHREAD, WaitRegister) == 0x068);
C_ASSERT(FIELD_OFFSET(KTHREAD, MiscFlags) == 0x06C);
C_ASSERT(FIELD_OFFSET(KTHREAD, ThreadFlags) == 0x070);
C_ASSERT(FIELD_OFFSET(KTHREAD, SwapBusy) == 0x076);
C_ASSERT(FIELD_OFFSET(KTHREAD, SystemCallNumber) == 0x078);
C_ASSERT(FIELD_OFFSET(KTHREAD, FirstArgument) == 0x080);
C_ASSERT(FIELD_OFFSET(KTHREAD, TrapFrame) == 0x088);
C_ASSERT(FIELD_OFFSET(KTHREAD, ApcState) == 0x090);
C_ASSERT(FIELD_OFFSET(KTHREAD, Priority) == 0x0BB);
C_ASSERT(FIELD_OFFSET(KTHREAD, UserIdealProcessor) == 0x0BC);
C_ASSERT(FIELD_OFFSET(KTHREAD, WaitStatus) == 0x0C0);
C_ASSERT(FIELD_OFFSET(KTHREAD, WaitBlockList) == 0x0C8);
C_ASSERT(FIELD_OFFSET(KTHREAD, WaitListEntry) == 0x0D0);
C_ASSERT(FIELD_OFFSET(KTHREAD, Queue) == 0x0E0);
C_ASSERT(FIELD_OFFSET(KTHREAD, Teb) == 0x0E8);
C_ASSERT(FIELD_OFFSET(KTHREAD, Timer) == 0x0F8);
C_ASSERT(FIELD_OFFSET(KTHREAD, WaitBlock) == 0x138);
C_ASSERT(FIELD_OFFSET(KTHREAD, ContextSwitches) == 0x14C);
C_ASSERT(FIELD_OFFSET(KTHREAD, State) == 0x17C);
C_ASSERT(FIELD_OFFSET(KTHREAD, WaitIrql) == 0x17E);
C_ASSERT(FIELD_OFFSET(KTHREAD, WaitTime) == 0x1AC);
C_ASSERT(FIELD_OFFSET(KTHREAD, Win32Thread) == 0x1C0);
C_ASSERT(FIELD_OFFSET(KTHREAD, KernelApcDisable) == 0x1DC);
C_ASSERT(FIELD_OFFSET(KTHREAD, QueueListEntry) == 0x200);
C_ASSERT(FIELD_OFFSET(KTHREAD, VfpState) == 0x230);
C_ASSERT(FIELD_OFFSET(KTHREAD, NextProcessor) == 0x238);
C_ASSERT(FIELD_OFFSET(KTHREAD, Process) == 0x240);
C_ASSERT(FIELD_OFFSET(KTHREAD, UserAffinity) == 0x248);
C_ASSERT(FIELD_OFFSET(KTHREAD, PreviousMode) == 0x252);
C_ASSERT(FIELD_OFFSET(KTHREAD, BasePriority) == 0x253);
C_ASSERT(FIELD_OFFSET(KTHREAD, Affinity) == 0x260);
C_ASSERT(FIELD_OFFSET(KTHREAD, ApcStateIndex) == 0x26A);
C_ASSERT(FIELD_OFFSET(KTHREAD, IdealProcessor) == 0x26C);
C_ASSERT(FIELD_OFFSET(KTHREAD, NpxState) == 0x270);
C_ASSERT(FIELD_OFFSET(KTHREAD, SavedApcState) == 0x278);
C_ASSERT(FIELD_OFFSET(KTHREAD, WaitReason) == 0x2A3);
C_ASSERT(FIELD_OFFSET(KTHREAD, SuspendCount) == 0x2A4);
C_ASSERT(FIELD_OFFSET(KTHREAD, SchedulerApc) == 0x2A8);
C_ASSERT(FIELD_OFFSET(KTHREAD, QuantumReset) == 0x2AB);
C_ASSERT(FIELD_OFFSET(KTHREAD, KernelTime) == 0x2AC);
C_ASSERT(FIELD_OFFSET(KTHREAD, WaitPrcb) == 0x2E8);
C_ASSERT(FIELD_OFFSET(KTHREAD, LegoData) == 0x2F0);
C_ASSERT(FIELD_OFFSET(KTHREAD, UserTime) == 0x2FC);
C_ASSERT(FIELD_OFFSET(KTHREAD, SuspendEvent) == 0x300);
C_ASSERT(FIELD_OFFSET(KTHREAD, ThreadListEntry) == 0x318);
C_ASSERT(FIELD_OFFSET(KTHREAD, MutantListHead) == 0x328);
C_ASSERT(FIELD_OFFSET(KTHREAD, ReadOperationCount) == 0x3A0);
C_ASSERT(FIELD_OFFSET(KTHREAD, ApcQueueLock) == 0x440);
C_ASSERT(FIELD_OFFSET(KTHREAD, StateSaveArea) == 0x478);
C_ASSERT(FIELD_OFFSET(KTHREAD, ResourceIndex) == 0x480);
C_ASSERT(FIELD_OFFSET(KTHREAD, CoreIsolationReasons) == 0x481);
C_ASSERT(FIELD_OFFSET(KTHREAD, BamQosLevelFromAssistPage) == 0x482);
C_ASSERT(FIELD_OFFSET(KTHREAD, SecureCallCoreIsolationCount) == 0x483);
C_ASSERT(FIELD_OFFSET(KTHREAD, SchedulerSharedOffset) == 0x484);
C_ASSERT(FIELD_OFFSET(KTHREAD, SchedulerSharedSwappablePage) == 0x488);
C_ASSERT(FIELD_OFFSET(KTHREAD, KernelAbEntries) == 0x490);
C_ASSERT(FIELD_OFFSET(KTHREAD, UserAbEntries) == 0x498);
#endif

//
// amd64 KTHREAD layout locks (Win11 26100 ntkrnlmp.pdb 10.0.26100.8036).
// Genuine Windows fields are pinned to their PDB offsets; the ReactOS-only
// fields live in the Win11 Padding[5] tail (ApcQueueLock/ApcStatePointer/CallbackStack)
// plus Spare6 (LargeStack). Spare18 overlays WaitBlock[3].Object and is not writable.
//
#if defined(_M_AMD64) && (NTDDI_VERSION >= NTDDI_WIN11_GE) && !defined(__ASSEMBLER__)
C_ASSERT(sizeof(KTHREAD) == 0x4C0);
C_ASSERT(sizeof(KWAIT_BLOCK) == 0x30);
C_ASSERT(sizeof(KAPC) == 0x58);
C_ASSERT(sizeof(KAPC_STATE) == 0x30);
C_ASSERT(sizeof(KTIMER) == 0x40);
C_ASSERT(FIELD_OFFSET(KTHREAD, InitialStack) == 0x028);
C_ASSERT(FIELD_OFFSET(KTHREAD, StackLimit) == 0x030);
C_ASSERT(FIELD_OFFSET(KTHREAD, StackBase) == 0x038);
C_ASSERT(FIELD_OFFSET(KTHREAD, ThreadLock) == 0x040);
C_ASSERT(FIELD_OFFSET(KTHREAD, CycleTime) == 0x048);
C_ASSERT(FIELD_OFFSET(KTHREAD, KernelStack) == 0x058);
C_ASSERT(FIELD_OFFSET(KTHREAD, StateSaveArea) == 0x060);
C_ASSERT(FIELD_OFFSET(KTHREAD, WaitRegister) == 0x070);
C_ASSERT(FIELD_OFFSET(KTHREAD, MiscFlags) == 0x074);
C_ASSERT(FIELD_OFFSET(KTHREAD, ThreadFlags) == 0x078);
C_ASSERT(FIELD_OFFSET(KTHREAD, SystemCallNumber) == 0x080);
C_ASSERT(FIELD_OFFSET(KTHREAD, FirstArgument) == 0x088);
C_ASSERT(FIELD_OFFSET(KTHREAD, TrapFrame) == 0x090);
C_ASSERT(FIELD_OFFSET(KTHREAD, ApcState) == 0x098);
C_ASSERT(FIELD_OFFSET(KTHREAD, Priority) == 0x0C3);
C_ASSERT(FIELD_OFFSET(KTHREAD, UserIdealProcessor) == 0x0C4);
C_ASSERT(FIELD_OFFSET(KTHREAD, WaitStatus) == 0x0C8);
C_ASSERT(FIELD_OFFSET(KTHREAD, WaitBlockList) == 0x0D0);
C_ASSERT(FIELD_OFFSET(KTHREAD, WaitListEntry) == 0x0D8);
C_ASSERT(FIELD_OFFSET(KTHREAD, SwapListEntry) == 0x0D8);
C_ASSERT(FIELD_OFFSET(KTHREAD, Queue) == 0x0E8);
C_ASSERT(FIELD_OFFSET(KTHREAD, Teb) == 0x0F0);
C_ASSERT(FIELD_OFFSET(KTHREAD, Timer) == 0x100);
C_ASSERT(FIELD_OFFSET(KTHREAD, WaitBlock) == 0x140);
C_ASSERT(FIELD_OFFSET(KTHREAD, ContextSwitches) == 0x154);
C_ASSERT(FIELD_OFFSET(KTHREAD, State) == 0x184);
C_ASSERT(FIELD_OFFSET(KTHREAD, WaitIrql) == 0x186);
C_ASSERT(FIELD_OFFSET(KTHREAD, WaitMode) == 0x187);
C_ASSERT(FIELD_OFFSET(KTHREAD, WaitTime) == 0x1B4);
C_ASSERT(FIELD_OFFSET(KTHREAD, ThreadCounters) == 0x168);
C_ASSERT(FIELD_OFFSET(KTHREAD, XStateSave) == 0x198);
C_ASSERT(FIELD_OFFSET(KTHREAD, Win32Thread) == 0x1C8);
C_ASSERT(FIELD_OFFSET(KTHREAD, KernelApcDisable) == 0x1E4);
C_ASSERT(FIELD_OFFSET(KTHREAD, CombinedApcDisable) == 0x1E4);
C_ASSERT(FIELD_OFFSET(KTHREAD, Spare18) == 0x1F0);
C_ASSERT(FIELD_OFFSET(KTHREAD, Spare18) == FIELD_OFFSET(KTHREAD, WaitBlock[3]) + FIELD_OFFSET(KWAIT_BLOCK, Object));
C_ASSERT(FIELD_OFFSET(KTHREAD, ThreadFlags2) == 0x200);
C_ASSERT(FIELD_OFFSET(KTHREAD, QueueListEntry) == 0x208);
C_ASSERT(FIELD_OFFSET(KTHREAD, NextProcessor) == 0x218);
C_ASSERT(FIELD_OFFSET(KTHREAD, QueuePriority) == 0x21C);
C_ASSERT(FIELD_OFFSET(KTHREAD, Process) == 0x220);
C_ASSERT(FIELD_OFFSET(KTHREAD, UserAffinity) == 0x228);
C_ASSERT(FIELD_OFFSET(KTHREAD, UserAffinityPrimaryGroup) == 0x230);
C_ASSERT(FIELD_OFFSET(KTHREAD, PreviousMode) == 0x232);
C_ASSERT(FIELD_OFFSET(KTHREAD, BasePriority) == 0x233);
C_ASSERT(FIELD_OFFSET(KTHREAD, AffinityVersion) == 0x238);
C_ASSERT(FIELD_OFFSET(KTHREAD, Affinity) == 0x240);
C_ASSERT(FIELD_OFFSET(KTHREAD, AffinityPrimaryGroup) == 0x248);
C_ASSERT(FIELD_OFFSET(KTHREAD, ApcStateIndex) == 0x24A);
C_ASSERT(FIELD_OFFSET(KTHREAD, WaitBlockCount) == 0x24B);
C_ASSERT(FIELD_OFFSET(KTHREAD, IdealProcessor) == 0x24C);
C_ASSERT(FIELD_OFFSET(KTHREAD, NpxState) == 0x250);
C_ASSERT(FIELD_OFFSET(KTHREAD, SavedApcState) == 0x258);
C_ASSERT(FIELD_OFFSET(KTHREAD, WaitReason) == 0x283);
C_ASSERT(FIELD_OFFSET(KTHREAD, SuspendCount) == 0x284);
C_ASSERT(FIELD_OFFSET(KTHREAD, SchedulerApc) == 0x288);
C_ASSERT(FIELD_OFFSET(KTHREAD, QuantumReset) == 0x28B);
C_ASSERT(FIELD_OFFSET(KTHREAD, KernelTime) == 0x28C);
C_ASSERT(FIELD_OFFSET(KTHREAD, WaitPrcb) == 0x2C8);
C_ASSERT(FIELD_OFFSET(KTHREAD, LegoData) == 0x2D0);
C_ASSERT(FIELD_OFFSET(KTHREAD, CallbackNestingLevel) == 0x2DB);
C_ASSERT(FIELD_OFFSET(KTHREAD, UserTime) == 0x2DC);
C_ASSERT(FIELD_OFFSET(KTHREAD, SuspendEvent) == 0x2E0);
C_ASSERT(FIELD_OFFSET(KTHREAD, ThreadListEntry) == 0x2F8);
C_ASSERT(FIELD_OFFSET(KTHREAD, MutantListHead) == 0x308);
C_ASSERT(FIELD_OFFSET(KTHREAD, SecureThreadCookie) == 0x31C);
C_ASSERT(FIELD_OFFSET(KTHREAD, LargeStack) == 0x367);
C_ASSERT(FIELD_OFFSET(KTHREAD, ForegroundLossTime) == 0x368);
C_ASSERT(FIELD_OFFSET(KTHREAD, ReadOperationCount) == 0x380);
C_ASSERT(FIELD_OFFSET(KTHREAD, QueuedScb) == 0x3B0);
C_ASSERT(FIELD_OFFSET(KTHREAD, KernelWaitTime) == 0x3E0);
C_ASSERT(FIELD_OFFSET(KTHREAD, KernelShadowStack) == 0x408);
C_ASSERT(FIELD_OFFSET(KTHREAD, ModeHistory) == 0x44C);
C_ASSERT(FIELD_OFFSET(KTHREAD, ResourceIndex) == 0x460);
C_ASSERT(FIELD_OFFSET(KTHREAD, KernelAbEntries) == 0x470);
C_ASSERT(FIELD_OFFSET(KTHREAD, UserAbEntries) == 0x478);
C_ASSERT(FIELD_OFFSET(KTHREAD, ApcQueueLock) == 0x498);
C_ASSERT(FIELD_OFFSET(KTHREAD, ApcStatePointer) == 0x4A0);
C_ASSERT(FIELD_OFFSET(KTHREAD, CallbackStack) == 0x4B0);
#endif


#define ASSERT_THREAD(object) \
    ASSERT((((object)->Header.Type & KOBJECT_TYPE_MASK) == ThreadObject))

//
// Kernel Process (KPROCESS), Win11 26100 arm64 layout for ARM64
// sizeof == 0x1B8; members marked [ReactOS] live in Win11 spare slots
//
#if defined(_M_ARM64)

typedef struct _KPROCESS
{
    DISPATCHER_HEADER Header;                            // 0x000
    LIST_ENTRY ProfileListHead;                          // 0x018
    ULONG_PTR DirectoryTableBase;                        // 0x028
    ULONG Asid;                                          // 0x030
    ULONG Spare0b;                                       // 0x034
    LIST_ENTRY ThreadListHead;                           // 0x038
    KSPIN_LOCK ProcessLock;                              // 0x048 Win11: ULONG ProcessLock + ULONG ProcessTimerDelay
    ULONG64 DeepFreezeStartTime;                         // 0x050
    KAFFINITY Affinity;                                  // 0x058 Win11 type: PKAFFINITY_EX
    ULONG64 AutoBoostState[2];                           // 0x060 KAB_UM_PROCESS_CONTEXT
    LIST_ENTRY ReadyListHead;                            // 0x070
    SINGLE_LIST_ENTRY SwapListEntry;                     // 0x080
    volatile KAFFINITY ActiveProcessors;                 // 0x088 Win11 type: PKAFFINITY_EX
    union
    {
        struct
        {
            LONG AutoAlignment:1;                        // 0x090
            LONG DisableBoost:1;
            LONG DisableQuantum:1;
            LONG DeepFreeze:1;
            LONG TimerVirtualization:1;
            LONG CheckStackExtents:1;
            LONG CacheIsolationEnabled:1;
            LONG PpmPolicy:4;
            LONG VaSpaceDeleted:1;
            LONG MultiGroup:1;
            LONG ForegroundProcess:1;
            LONG ReservedFlags:18;
        };
        volatile LONG ProcessFlags;
    };
    ULONG Spare0c;                                       // 0x094
    SCHAR BasePriority;                                  // 0x098
    SCHAR QuantumReset;                                  // 0x099
    UCHAR Visited;                                       // 0x09A
    union
    {
        KEXECUTE_OPTIONS Flags;                          // 0x09B
        UCHAR ExecuteOptions;
    };
    ULONG64 ActiveGroupsMask[2];                         // 0x0A0 KGROUP_MASK
    ULONG64 ActiveGroupPadding[2];                       // 0x0B0
    PVOID IdealProcessorAssignmentBlock;                 // 0x0C0
    // Win11 Padding[6] region: ReactOS private members
    ULONG_PTR Unused0;                                   // 0x0C8 [ReactOS]
    PVOID VdmTrapcHandler;                               // 0x0D0 [ReactOS]
    USHORT IopmOffset;                                   // 0x0D8 [ReactOS]
    UCHAR State;                                         // 0x0DA [ReactOS]
    UCHAR ThreadSeed;                                    // 0x0DB [ReactOS]
    UCHAR PowerState;                                    // 0x0DC [ReactOS]
    UCHAR IdealNode;                                     // 0x0DD [ReactOS]
    USHORT SpareDE;                                      // 0x0DE
    ULONG64 SpareE0[3];                                  // 0x0E0
    ULONG Padding2;                                      // 0x0F8
    ULONG SchedulerAssistYieldBoostCount;                // 0x0FC
    LONG64 SchedulerAssistYieldBoostAllowedTime;         // 0x100
    ULONG SveVectorLength;                               // 0x108
    USHORT IdealGlobalNode;                              // 0x10C
    USHORT Spare1;                                       // 0x10E
    volatile ULONG StackCount;                           // 0x110 KSTACK_COUNT
    ULONG Spare114;                                      // 0x114
    LIST_ENTRY ProcessListEntry;                         // 0x118
    ULONG64 CycleTime;                                   // 0x128
    ULONG64 ContextSwitches;                             // 0x130
    PVOID SchedulingGroup;                               // 0x138 PKSCHEDULING_GROUP
    ULONG64 KernelTime;                                  // 0x140
    ULONG64 UserTime;                                    // 0x148
    ULONG64 ReadyTime;                                   // 0x150
    ULONG FreezeCount;                                   // 0x158
    ULONG Spare4;                                        // 0x15C
    PVOID InstrumentationCallback;                       // 0x160
    ULONG64 SecureState;                                 // 0x168
    ULONG64 KernelWaitTime;                              // 0x170
    ULONG64 UserWaitTime;                                // 0x178
    ULONG64 LastRebalanceQpc;                            // 0x180
    PVOID PerProcessorCycleTimes;                        // 0x188
    USHORT PrimaryGroup;                                 // 0x190
    USHORT Spare3[3];                                    // 0x192
    LIST_ENTRY CpuPartitionList;                         // 0x198
    ULONG64 Mpam0El1;                                    // 0x1A8
    PVOID AvailableCpuState;                             // 0x1B0
} KPROCESS;                                              // sizeof 0x1B8

#else

typedef struct _KPROCESS
{
    DISPATCHER_HEADER Header;
    LIST_ENTRY ProfileListHead;
#if defined(_M_ARM64) || defined(__aarch64__)
    ULONG_PTR DirectoryTableBase[2];
    ULONG_PTR Unused0;
#elif (NTDDI_VERSION >= NTDDI_LONGHORN)
    ULONG_PTR DirectoryTableBase;
    ULONG_PTR Unused0;
#else
    ULONG_PTR DirectoryTableBase[2];
#endif
#if defined(_M_IX86)
    KGDTENTRY LdtDescriptor;
    KIDTENTRY Int21Descriptor;
#endif
#if defined(_M_AMD64) && (NTDDI_VERSION >= NTDDI_VISTA)
    KGDTENTRY64 LdtSystemDescriptor;
    PVOID LdtBaseAddress;
#endif
    USHORT IopmOffset;
#if defined(_M_IX86)
    UCHAR Iopl;
    UCHAR Unused;
#endif
    volatile KAFFINITY ActiveProcessors;
    ULONG KernelTime;
    ULONG UserTime;
    LIST_ENTRY ReadyListHead;
    SINGLE_LIST_ENTRY SwapListEntry;
    PVOID VdmTrapcHandler;
    LIST_ENTRY ThreadListHead;
    KSPIN_LOCK ProcessLock;
    KAFFINITY Affinity;
    union
    {
        struct
        {
            LONG AutoAlignment:1;
            LONG DisableBoost:1;
            LONG DisableQuantum:1;
            LONG ReservedFlags:29;
        };
        LONG ProcessFlags;
    };
    SCHAR BasePriority;
    SCHAR QuantumReset;
    UCHAR State;
    UCHAR ThreadSeed;
    UCHAR PowerState;
    UCHAR IdealNode;
    UCHAR Visited;
    union
    {
        KEXECUTE_OPTIONS Flags;
        UCHAR ExecuteOptions;
    };
    ULONG StackCount;
    LIST_ENTRY ProcessListEntry;
#if (NTDDI_VERSION >= NTDDI_LONGHORN) // [
    ULONGLONG CycleTime;
#endif // ]
} KPROCESS;

#endif

#if defined(_M_ARM64) && !defined(__ASSEMBLER__)
C_ASSERT(sizeof(KPROCESS) == 0x1B8);
C_ASSERT(FIELD_OFFSET(KPROCESS, DirectoryTableBase) == 0x028);
C_ASSERT(FIELD_OFFSET(KPROCESS, Asid) == 0x030);
C_ASSERT(FIELD_OFFSET(KPROCESS, Spare0b) == 0x034);
C_ASSERT(FIELD_OFFSET(KPROCESS, ThreadListHead) == 0x038);
C_ASSERT(FIELD_OFFSET(KPROCESS, ProcessLock) == 0x048);
C_ASSERT(FIELD_OFFSET(KPROCESS, Affinity) == 0x058);
C_ASSERT(FIELD_OFFSET(KPROCESS, ReadyListHead) == 0x070);
C_ASSERT(FIELD_OFFSET(KPROCESS, SwapListEntry) == 0x080);
C_ASSERT(FIELD_OFFSET(KPROCESS, ActiveProcessors) == 0x088);
C_ASSERT(FIELD_OFFSET(KPROCESS, ProcessFlags) == 0x090);
C_ASSERT(FIELD_OFFSET(KPROCESS, BasePriority) == 0x098);
C_ASSERT(FIELD_OFFSET(KPROCESS, Unused0) == 0x0C8);
C_ASSERT(FIELD_OFFSET(KPROCESS, StackCount) == 0x110);
C_ASSERT(FIELD_OFFSET(KPROCESS, ProcessListEntry) == 0x118);
C_ASSERT(FIELD_OFFSET(KPROCESS, CycleTime) == 0x128);
C_ASSERT(FIELD_OFFSET(KPROCESS, KernelTime) == 0x140);
C_ASSERT(FIELD_OFFSET(KPROCESS, UserTime) == 0x148);
#endif

#define ASSERT_PROCESS(object) \
    ASSERT((((object)->Header.Type & KOBJECT_TYPE_MASK) == ProcessObject))

//
// System Service Table Descriptor
//
typedef struct _KSERVICE_TABLE_DESCRIPTOR
{
    PULONG_PTR Base;
    PULONG Count;
    ULONG Limit;
#if defined(_IA64_)
    LONG TableBaseGpOffset;
#endif
    PUCHAR Number;
} KSERVICE_TABLE_DESCRIPTOR, *PKSERVICE_TABLE_DESCRIPTOR;

#if (NTDDI_VERSION >= NTDDI_WIN8)
//
// Entropy Timing State
//
typedef struct _KENTROPY_TIMING_STATE
{
    ULONG EntropyCount;
    ULONG Buffer[64];
    KDPC Dpc;
    ULONG LastDeliveredBuffer;
    PULONG RawDataBuffer;
} KENTROPY_TIMING_STATE, *PKENTROPY_TIMING_STATE;

//
// Constants from ks386.inc, ksamd64.inc and ksarm.h
//
#define KENTROPY_TIMING_INTERRUPTS_PER_BUFFER 0x400
#define KENTROPY_TIMING_BUFFER_MASK 0x7ff
#define KENTROPY_TIMING_ANALYSIS 0x0

#endif /* (NTDDI_VERSION >= NTDDI_WIN8) */

//
// Exported Loader Parameter Block
//
extern struct _LOADER_PARAMETER_BLOCK NTSYSAPI *KeLoaderBlock;

//
// Exported Hardware Data
//
extern ULONG NTSYSAPI KiDmaIoCoherency;
extern ULONG NTSYSAPI KeMaximumIncrement;
extern ULONG NTSYSAPI KeMinimumIncrement;
extern ULONG NTSYSAPI KeDcacheFlushCount;
extern ULONG NTSYSAPI KeIcacheFlushCount;
extern ULONG_PTR NTSYSAPI KiBugCheckData[];
extern BOOLEAN NTSYSAPI KiEnableTimerWatchdog;

//
// Exported System Service Descriptor Tables
//
extern KSERVICE_TABLE_DESCRIPTOR NTSYSAPI KeServiceDescriptorTable[SSDT_MAX_ENTRIES];
extern KSERVICE_TABLE_DESCRIPTOR NTSYSAPI KeServiceDescriptorTableShadow[SSDT_MAX_ENTRIES];

#endif // !NTOS_MODE_USER

#endif // _KETYPES_H
