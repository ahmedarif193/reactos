/*++ NDK Version: 0098

Copyright (c) Alex Ionescu.  All rights reserved.

Header Name:

    pstypes.h

Abstract:

    Type definitions for the Process Manager

Author:

    Alex Ionescu (alexi@tinykrnl.org) - Updated - 27-Feb-2006

--*/

#ifndef _PSTYPES_H
#define _PSTYPES_H

//
// Dependencies
//
#include <umtypes.h>
#include <ldrtypes.h>
#include <mmtypes.h>
#include <obtypes.h>
#include <rtltypes.h>
#ifndef NTOS_MODE_USER
#include <extypes.h>
#include <setypes.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

#ifndef NTOS_MODE_USER

//
// Kernel Exported Object Types
//
extern POBJECT_TYPE NTSYSAPI PsJobType;

#endif // !NTOS_MODE_USER

//
// Global Flags
//
#define FLG_STOP_ON_EXCEPTION                   0x00000001
#define FLG_SHOW_LDR_SNAPS                      0x00000002
#define FLG_DEBUG_INITIAL_COMMAND               0x00000004
#define FLG_STOP_ON_HUNG_GUI                    0x00000008
#define FLG_HEAP_ENABLE_TAIL_CHECK              0x00000010
#define FLG_HEAP_ENABLE_FREE_CHECK              0x00000020
#define FLG_HEAP_VALIDATE_PARAMETERS            0x00000040
#define FLG_HEAP_VALIDATE_ALL                   0x00000080
#define FLG_APPLICATION_VERIFIER                0x00000100
#define FLG_POOL_ENABLE_TAGGING                 0x00000400
#define FLG_HEAP_ENABLE_TAGGING                 0x00000800
#define FLG_USER_STACK_TRACE_DB                 0x00001000
#define FLG_KERNEL_STACK_TRACE_DB               0x00002000
#define FLG_MAINTAIN_OBJECT_TYPELIST            0x00004000
#define FLG_HEAP_ENABLE_TAG_BY_DLL              0x00008000
#define FLG_DISABLE_STACK_EXTENSION             0x00010000
#define FLG_ENABLE_CSRDEBUG                     0x00020000
#define FLG_ENABLE_KDEBUG_SYMBOL_LOAD           0x00040000
#define FLG_DISABLE_PAGE_KERNEL_STACKS          0x00080000
#if (NTDDI_VERSION < NTDDI_WINXP)
#define FLG_HEAP_ENABLE_CALL_TRACING            0x00100000
#else
#define FLG_ENABLE_SYSTEM_CRIT_BREAKS           0x00100000
#endif
#define FLG_HEAP_DISABLE_COALESCING             0x00200000
#define FLG_ENABLE_CLOSE_EXCEPTIONS             0x00400000
#define FLG_ENABLE_EXCEPTION_LOGGING            0x00800000
#define FLG_ENABLE_HANDLE_TYPE_TAGGING          0x01000000
#define FLG_HEAP_PAGE_ALLOCS                    0x02000000
#define FLG_DEBUG_INITIAL_COMMAND_EX            0x04000000
#define FLG_DISABLE_DEBUG_PROMPTS               0x08000000 // ReactOS-specific
#define FLG_VALID_BITS                          0x0FFFFFFF

//
// Flags for NtCreateProcessEx
//
#define PROCESS_CREATE_FLAGS_BREAKAWAY              0x00000001
#define PROCESS_CREATE_FLAGS_NO_DEBUG_INHERIT       0x00000002
#define PROCESS_CREATE_FLAGS_INHERIT_HANDLES        0x00000004
#define PROCESS_CREATE_FLAGS_OVERRIDE_ADDRESS_SPACE 0x00000008
#define PROCESS_CREATE_FLAGS_LARGE_PAGES            0x00000010
#define PROCESS_CREATE_FLAGS_ALL_LARGE_PAGE_FLAGS   PROCESS_CREATE_FLAGS_LARGE_PAGES
#define PROCESS_CREATE_FLAGS_LEGAL_MASK             (PROCESS_CREATE_FLAGS_BREAKAWAY | \
                                                     PROCESS_CREATE_FLAGS_NO_DEBUG_INHERIT | \
                                                     PROCESS_CREATE_FLAGS_INHERIT_HANDLES | \
                                                     PROCESS_CREATE_FLAGS_OVERRIDE_ADDRESS_SPACE | \
                                                     PROCESS_CREATE_FLAGS_ALL_LARGE_PAGE_FLAGS)

#if (NTDDI_VERSION >= NTDDI_LONGHORN)

//
// Thread creation flags for NtCreateUserProcess / NtCreateThreadEx
//
#define THREAD_CREATE_FLAGS_CREATE_SUSPENDED        0x00000001
#define THREAD_CREATE_FLAGS_SKIP_THREAD_ATTACH      0x00000002
#define THREAD_CREATE_FLAGS_HIDE_FROM_DEBUGGER      0x00000004
#define THREAD_CREATE_FLAGS_HAS_SECURITY_DESCRIPTOR 0x00000010
#define THREAD_CREATE_FLAGS_ACCESS_CHECK_IN_TARGET  0x00000020
#define THREAD_CREATE_FLAGS_INITIAL_THREAD          0x00000080

//
// PS_ATTRIBUTE flags
//
#define PS_ATTRIBUTE_THREAD                         0x00010000
#define PS_ATTRIBUTE_INPUT                          0x00020000
#define PS_ATTRIBUTE_ADDITIVE                       0x00040000

//
// PS_ATTRIBUTE_NUM - attribute number enumeration for NtCreateUserProcess
//
typedef enum _PS_ATTRIBUTE_NUM
{
    PsAttributeParentProcess,
    PsAttributeDebugPort,
    PsAttributeToken,
    PsAttributeClientId,
    PsAttributeTebAddress,
    PsAttributeImageName,
    PsAttributeImageInfo,
    PsAttributeMemoryReserve,
    PsAttributePriorityClass,
    PsAttributeErrorMode,
    PsAttributeStdHandleInfo,
    PsAttributeHandleList,
    PsAttributeGroupAffinity,
    PsAttributePreferredNode,
    PsAttributeIdealProcessor,
    PsAttributeUmsThread,
    PsAttributeMitigationOptions,
    PsAttributeProtectionLevel,
    PsAttributeSecureProcess,
    PsAttributeJobList,
    PsAttributeChildProcessPolicy,
    PsAttributeAllApplicationPackagesPolicy,
    PsAttributeWin32kFilter,
    PsAttributeSafeOpenPromptOriginClaim,
    PsAttributeBnoIsolation,
    PsAttributeDesktopAppPolicy,
    PsAttributeChpe,
    PsAttributeMitigationAuditOptions,
    PsAttributeMachineType,
    PsAttributeComponentFilter,
    PsAttributeEnableOptionalXStateFeatures,
    PsAttributeMax
} PS_ATTRIBUTE_NUM;

//
// Combined PS_ATTRIBUTE_* constants used in attribute lists
//
#define PS_ATTRIBUTE_PARENT_PROCESS     (PsAttributeParentProcess | PS_ATTRIBUTE_INPUT | PS_ATTRIBUTE_ADDITIVE)
#define PS_ATTRIBUTE_DEBUG_PORT         (PsAttributeDebugPort | PS_ATTRIBUTE_INPUT | PS_ATTRIBUTE_ADDITIVE)
#define PS_ATTRIBUTE_TOKEN              (PsAttributeToken | PS_ATTRIBUTE_INPUT | PS_ATTRIBUTE_ADDITIVE)
#define PS_ATTRIBUTE_CLIENT_ID          (PsAttributeClientId | PS_ATTRIBUTE_THREAD)
#define PS_ATTRIBUTE_TEB_ADDRESS        (PsAttributeTebAddress | PS_ATTRIBUTE_THREAD)
#define PS_ATTRIBUTE_IMAGE_NAME         (PsAttributeImageName | PS_ATTRIBUTE_INPUT)
#define PS_ATTRIBUTE_IMAGE_INFO         (PsAttributeImageInfo)
#define PS_ATTRIBUTE_MEMORY_RESERVE     (PsAttributeMemoryReserve | PS_ATTRIBUTE_INPUT)
#define PS_ATTRIBUTE_PRIORITY_CLASS     (PsAttributePriorityClass | PS_ATTRIBUTE_INPUT)
#define PS_ATTRIBUTE_ERROR_MODE         (PsAttributeErrorMode | PS_ATTRIBUTE_INPUT)
#define PS_ATTRIBUTE_STD_HANDLE_INFO    (PsAttributeStdHandleInfo | PS_ATTRIBUTE_INPUT)
#define PS_ATTRIBUTE_HANDLE_LIST        (PsAttributeHandleList | PS_ATTRIBUTE_INPUT)
#define PS_ATTRIBUTE_GROUP_AFFINITY     (PsAttributeGroupAffinity | PS_ATTRIBUTE_THREAD | PS_ATTRIBUTE_INPUT)
#define PS_ATTRIBUTE_PREFERRED_NODE     (PsAttributePreferredNode | PS_ATTRIBUTE_INPUT)
#define PS_ATTRIBUTE_IDEAL_PROCESSOR    (PsAttributeIdealProcessor | PS_ATTRIBUTE_THREAD | PS_ATTRIBUTE_INPUT)
#define PS_ATTRIBUTE_MITIGATION_OPTIONS (PsAttributeMitigationOptions | PS_ATTRIBUTE_INPUT)
#define PS_ATTRIBUTE_PROTECTION_LEVEL   (PsAttributeProtectionLevel | PS_ATTRIBUTE_INPUT | PS_ATTRIBUTE_ADDITIVE)
#define PS_ATTRIBUTE_JOB_LIST           (PsAttributeJobList | PS_ATTRIBUTE_INPUT)
#define PS_ATTRIBUTE_CHILD_PROCESS_POLICY (PsAttributeChildProcessPolicy | PS_ATTRIBUTE_INPUT)
#define PS_ATTRIBUTE_ALL_APPLICATION_PACKAGES_POLICY (PsAttributeAllApplicationPackagesPolicy | PS_ATTRIBUTE_INPUT)
#define PS_ATTRIBUTE_BNO_ISOLATION      (PsAttributeBnoIsolation | PS_ATTRIBUTE_INPUT)
#define PS_ATTRIBUTE_COMPONENT_FILTER   (PsAttributeComponentFilter | PS_ATTRIBUTE_INPUT)

//
// PS_CREATE_STATE - process creation result state
//
typedef enum _PS_CREATE_STATE
{
    PsCreateInitialState,
    PsCreateFailOnFileOpen,
    PsCreateFailOnSectionCreate,
    PsCreateFailExeFormat,
    PsCreateFailMachineMismatch,
    PsCreateFailExeName,
    PsCreateSuccess,
    PsCreateMaximumStates
} PS_CREATE_STATE;

//
// PS_ATTRIBUTE - single process/thread creation attribute
//
typedef struct _PS_ATTRIBUTE
{
    ULONG_PTR Attribute;
    SIZE_T Size;
    union
    {
        ULONG_PTR Value;
        PVOID ValuePtr;
    };
    PSIZE_T ReturnLength;
} PS_ATTRIBUTE, *PPS_ATTRIBUTE;

//
// PS_ATTRIBUTE_LIST - variable-length array of creation attributes
//
typedef struct _PS_ATTRIBUTE_LIST
{
    SIZE_T TotalLength;
    PS_ATTRIBUTE Attributes[1];
} PS_ATTRIBUTE_LIST, *PPS_ATTRIBUTE_LIST;

//
// PS_BNO_ISOLATION_PARAMETERS - BaseNamedObjects isolation parameters
//
typedef struct _PS_BNO_ISOLATION_PARAMETERS
{
    UNICODE_STRING IsolationPrefix;
    ULONG HandleCount;
    PVOID *Handles;
    BOOLEAN IsolationEnabled;
} PS_BNO_ISOLATION_PARAMETERS, *PPS_BNO_ISOLATION_PARAMETERS;

//
// PS_CREATE_INFO - input/output structure for NtCreateUserProcess
//
typedef struct _PS_CREATE_INFO
{
    SIZE_T Size;
    PS_CREATE_STATE State;
    union
    {
        //
        // Input: PsCreateInitialState
        //
        struct
        {
            union
            {
                ULONG InitFlags;
                struct
                {
                    UCHAR WriteOutputOnExit : 1;
                    UCHAR DetectManifest : 1;
                    UCHAR IFEOSkipDebugger : 1;
                    UCHAR IFEODoNotPropagateKeyState : 1;
                    UCHAR SpareBits1 : 4;
                    UCHAR SpareBits2 : 8;
                    USHORT ProhibitedImageCharacteristics : 16;
                };
            };
            ACCESS_MASK AdditionalFileAccess;
        } InitState;

        //
        // Output: PsCreateFailOnSectionCreate
        //
        struct
        {
            HANDLE FileHandle;
        } FailSection;

        //
        // Output: PsCreateFailExeFormat
        //
        struct
        {
            USHORT DllCharacteristics;
        } ExeFormat;

        //
        // Output: PsCreateFailExeName
        //
        struct
        {
            HANDLE IFEOKey;
        } ExeName;

        //
        // Output: PsCreateSuccess
        //
        struct
        {
            union
            {
                ULONG OutputFlags;
                struct
                {
                    UCHAR ProtectedProcess : 1;
                    UCHAR AddressSpaceOverride : 1;
                    UCHAR DevOverrideEnabled : 1;
                    UCHAR ManifestDetected : 1;
                    UCHAR ProtectedProcessLight : 1;
                    UCHAR SpareBits1 : 3;
                    UCHAR SpareBits2 : 8;
                    USHORT SpareBits3 : 16;
                };
            };
            HANDLE FileHandle;
            HANDLE SectionHandle;
            ULONGLONG UserProcessParametersNative;
            ULONG UserProcessParametersWow64;
            ULONG CurrentParameterFlags;
            ULONGLONG PebAddressNative;
            ULONG PebAddressWow64;
            ULONGLONG ManifestAddress;
            ULONG ManifestSize;
        } SuccessState;
    };
} PS_CREATE_INFO, *PPS_CREATE_INFO;

#endif /* NTDDI_VERSION >= NTDDI_LONGHORN */

//
// Process priority classes
//
#define PROCESS_PRIORITY_CLASS_INVALID          0
#define PROCESS_PRIORITY_CLASS_IDLE             1
#define PROCESS_PRIORITY_CLASS_NORMAL           2
#define PROCESS_PRIORITY_CLASS_HIGH             3
#define PROCESS_PRIORITY_CLASS_REALTIME         4
#define PROCESS_PRIORITY_CLASS_BELOW_NORMAL     5
#define PROCESS_PRIORITY_CLASS_ABOVE_NORMAL     6

//
// Process base priorities
//
#define PROCESS_PRIORITY_IDLE                   3
#define PROCESS_PRIORITY_NORMAL                 8
#define PROCESS_PRIORITY_NORMAL_FOREGROUND      9

//
// Process memory priorities
//
#define MEMORY_PRIORITY_BACKGROUND             0
#define MEMORY_PRIORITY_UNKNOWN                1
#define MEMORY_PRIORITY_FOREGROUND             2

//
// Process Priority Separation Values (OR)
//
#define PSP_DEFAULT_QUANTUMS                    0x00
#define PSP_VARIABLE_QUANTUMS                   0x04
#define PSP_FIXED_QUANTUMS                      0x08
#define PSP_LONG_QUANTUMS                       0x10
#define PSP_SHORT_QUANTUMS                      0x20

//
// Process Handle Tracing Values
//
#define PROCESS_HANDLE_TRACE_TYPE_OPEN          1
#define PROCESS_HANDLE_TRACE_TYPE_CLOSE         2
#define PROCESS_HANDLE_TRACE_TYPE_BADREF        3
#define PROCESS_HANDLE_TRACING_MAX_STACKS       16

#ifndef NTOS_MODE_USER
//
// Thread Access Types
//
#define THREAD_QUERY_INFORMATION                0x0040
#define THREAD_SET_THREAD_TOKEN                 0x0080
#define THREAD_IMPERSONATE                      0x0100
#define THREAD_DIRECT_IMPERSONATION             0x0200

//
// Process Access Types
//
#define PROCESS_TERMINATE                       0x0001
#define PROCESS_CREATE_THREAD                   0x0002
#define PROCESS_SET_SESSIONID                   0x0004
#define PROCESS_VM_OPERATION                    0x0008
#define PROCESS_VM_READ                         0x0010
#define PROCESS_VM_WRITE                        0x0020
#define PROCESS_CREATE_PROCESS                  0x0080
#define PROCESS_SET_QUOTA                       0x0100
#define PROCESS_SET_INFORMATION                 0x0200
#define PROCESS_QUERY_INFORMATION               0x0400
#define PROCESS_SUSPEND_RESUME                  0x0800
#define PROCESS_QUERY_LIMITED_INFORMATION       0x1000
#define PROCESS_SET_LIMITED_INFORMATION         0x2000
#if (NTDDI_VERSION >= NTDDI_LONGHORN)
#define PROCESS_ALL_ACCESS                      (STANDARD_RIGHTS_REQUIRED | \
                                                 SYNCHRONIZE | \
                                                 0xFFFF)
#else
#define PROCESS_ALL_ACCESS                      (STANDARD_RIGHTS_REQUIRED | \
                                                 SYNCHRONIZE | \
                                                 0xFFF)
#endif

//
// Thread Base Priorities
//
#define THREAD_BASE_PRIORITY_LOWRT              15
#define THREAD_BASE_PRIORITY_MAX                2
#define THREAD_BASE_PRIORITY_MIN                -2
#define THREAD_BASE_PRIORITY_IDLE               -15

//
// TLS Slots
//
#define TLS_MINIMUM_AVAILABLE                   64

//
// TEB Active Frame Flags
//
#define TEB_ACTIVE_FRAME_CONTEXT_FLAG_EXTENDED 	0x1

//
// Job Access Types
//
#define JOB_OBJECT_ASSIGN_PROCESS               0x1
#define JOB_OBJECT_SET_ATTRIBUTES               0x2
#define JOB_OBJECT_QUERY                        0x4
#define JOB_OBJECT_TERMINATE                    0x8
#define JOB_OBJECT_SET_SECURITY_ATTRIBUTES      0x10
#define JOB_OBJECT_IMPERSONATE                  0x20
#define JOB_OBJECT_ALL_ACCESS                   (STANDARD_RIGHTS_REQUIRED | \
                                                 SYNCHRONIZE | \
                                                 0x3F)

//
// Job Limit Flags
//
#define JOB_OBJECT_LIMIT_WORKINGSET             0x1
#define JOB_OBJECT_LIMIT_PROCESS_TIME           0x2
#define JOB_OBJECT_LIMIT_JOB_TIME               0x4
#define JOB_OBJECT_LIMIT_ACTIVE_PROCESS         0x8
#define JOB_OBJECT_LIMIT_AFFINITY               0x10
#define JOB_OBJECT_LIMIT_PRIORITY_CLASS         0x20
#define JOB_OBJECT_LIMIT_PRESERVE_JOB_TIME      0x40
#define JOB_OBJECT_LIMIT_SCHEDULING_CLASS       0x80
#define JOB_OBJECT_LIMIT_PROCESS_MEMORY         0x100
#define JOB_OBJECT_LIMIT_JOB_MEMORY             0x200
#define JOB_OBJECT_LIMIT_DIE_ON_UNHANDLED_EXCEPTION 0x400
#define JOB_OBJECT_LIMIT_BREAKAWAY_OK           0x800
#define JOB_OBJECT_LIMIT_SILENT_BREAKAWAY_OK    0x1000
#define JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE      0x2000
#define JOB_OBJECT_LIMIT_SUBSET_AFFINITY         0x4000

//
// Job UI Restriction Flags
//
#define JOB_OBJECT_UILIMIT_NONE                 0x0000
#define JOB_OBJECT_UILIMIT_HANDLES              0x0001
#define JOB_OBJECT_UILIMIT_READCLIPBOARD        0x0002
#define JOB_OBJECT_UILIMIT_WRITECLIPBOARD       0x0004
#define JOB_OBJECT_UILIMIT_SYSTEMPARAMETERS     0x0008
#define JOB_OBJECT_UILIMIT_DISPLAYSETTINGS      0x0010
#define JOB_OBJECT_UILIMIT_GLOBALATOMS          0x0020
#define JOB_OBJECT_UILIMIT_DESKTOP              0x0040
#define JOB_OBJECT_UILIMIT_EXITWINDOWS          0x0080
#define JOB_OBJECT_UILIMIT_ALL                  0x00FF
#define JOB_OBJECT_UILIMIT_VALID_FLAGS          0x00FF

//
// Job Security Limit Flags
//
#define JOB_OBJECT_SECURITY_NO_ADMIN            0x0001
#define JOB_OBJECT_SECURITY_RESTRICTED_TOKEN    0x0002
#define JOB_OBJECT_SECURITY_ONLY_TOKEN          0x0004
#define JOB_OBJECT_SECURITY_FILTER_TOKENS       0x0008

//
// Job Completion Port Messages
//
#define JOB_OBJECT_MSG_END_OF_JOB_TIME          1
#define JOB_OBJECT_MSG_END_OF_PROCESS_TIME      2
#define JOB_OBJECT_MSG_ACTIVE_PROCESS_LIMIT     3
#define JOB_OBJECT_MSG_ACTIVE_PROCESS_ZERO      4
#define JOB_OBJECT_MSG_NEW_PROCESS              6
#define JOB_OBJECT_MSG_EXIT_PROCESS             7
#define JOB_OBJECT_MSG_ABNORMAL_EXIT_PROCESS    8
#define JOB_OBJECT_MSG_PROCESS_MEMORY_LIMIT     9
#define JOB_OBJECT_MSG_JOB_MEMORY_LIMIT         10
#define JOB_OBJECT_MSG_NOTIFICATION_LIMIT       11
#define JOB_OBJECT_MSG_JOB_CYCLE_TIME_LIMIT     12

//
// Cross Thread Flags
//
#define CT_TERMINATED_BIT                       0x1
#define CT_DEAD_THREAD_BIT                      0x2
#define CT_HIDE_FROM_DEBUGGER_BIT               0x4
#define CT_ACTIVE_IMPERSONATION_INFO_BIT        0x8
#define CT_SYSTEM_THREAD_BIT                    0x10
#define CT_HARD_ERRORS_ARE_DISABLED_BIT         0x20
#define CT_BREAK_ON_TERMINATION_BIT             0x40
#define CT_SKIP_CREATION_MSG_BIT                0x80
#define CT_SKIP_TERMINATION_MSG_BIT             0x100

//
// Same Thread Passive Flags
//
#define STP_ACTIVE_EX_WORKER_BIT                0x1
#define STP_EX_WORKER_CAN_WAIT_USER_BIT         0x2
#define STP_MEMORY_MAKER_BIT                    0x4
#define STP_KEYED_EVENT_IN_USE_BIT              0x8

//
// Same Thread APC Flags
//
#define STA_LPC_RECEIVED_MSG_ID_VALID_BIT       0x1
#define STA_LPC_EXIT_THREAD_CALLED_BIT          0x2
#define STA_ADDRESS_SPACE_OWNER_BIT             0x4
#define STA_OWNS_WORKING_SET_BITS               0x1F8

//
// Kernel Process flags (maybe in ketypes.h?)
//
#define KPSF_AUTO_ALIGNMENT_BIT                 0
#define KPSF_DISABLE_BOOST_BIT                  1
#define KPSF_CHECK_STACK_EXTENTS_BIT            5

//
// Process Flags
//
#define PSF_CREATE_REPORTED_BIT                 0x1
#define PSF_NO_DEBUG_INHERIT_BIT                0x2
#define PSF_PROCESS_EXITING_BIT                 0x4
#define PSF_PROCESS_DELETE_BIT                  0x8
#define PSF_MANAGE_EXECUTABLE_MEMORY_WRITES_BIT 0x10
#define PSF_VM_DELETED_BIT                      0x20
#define PSF_OUTSWAP_ENABLED_BIT                 0x40
#define PSF_OUTSWAPPED_BIT                      0x80
#define PSF_WOW64_VA_SPACE_4GB_BIT              0x200
#define PSF_ADDRESS_SPACE_INITIALIZED_BIT       0x400
#define PSF_SET_TIMER_RESOLUTION_BIT            0x1000
#define PSF_BREAK_ON_TERMINATION_BIT            0x2000
#define PSF_WRITE_WATCH_BIT                     0x8000
#define PSF_PROCESS_IN_SESSION_BIT              0x10000
#define PSF_OVERRIDE_ADDRESS_SPACE_BIT          0x20000
#define PSF_HAS_ADDRESS_SPACE_BIT               0x40000
#define PSF_LAUNCH_PREFETCHED_BIT               0x80000
#define PSF_VM_TOP_DOWN_BIT                     0x200000
#define PSF_IMAGE_NOTIFY_DONE_BIT               0x400000
#define PSF_PDE_UPDATE_NEEDED_BIT               0x800000
#define PSF_VDM_ALLOWED_BIT                     0x1000000
#define PSF_DEFAULT_IO_PRIORITY_BIT             0x8000000
#endif

//
// TLS/FLS Defines
//
#define TLS_EXPANSION_SLOTS                     1024

#ifdef NTOS_MODE_USER
//
// Thread Native Base Priorities
//
#define LOW_PRIORITY                            0
#define LOW_REALTIME_PRIORITY                   16
#define HIGH_PRIORITY                           31
#define MAXIMUM_PRIORITY                        32

//
// Current Process/Thread built-in 'special' handles
//
#define NtCurrentProcess()                      ((HANDLE)(LONG_PTR)-1)
#define ZwCurrentProcess()                      NtCurrentProcess()
#define NtCurrentThread()                       ((HANDLE)(LONG_PTR)-2)
#define ZwCurrentThread()                       NtCurrentThread()

//
// Process/Thread/Job Information Classes for NtQueryInformationProcess/Thread/Job
//
typedef enum _PROCESSINFOCLASS
{
    ProcessBasicInformation,
    ProcessQuotaLimits,
    ProcessIoCounters,
    ProcessVmCounters,
    ProcessTimes,
    ProcessBasePriority,
    ProcessRaisePriority,
    ProcessDebugPort,
    ProcessExceptionPort,
    ProcessAccessToken,
    ProcessLdtInformation,
    ProcessLdtSize,
    ProcessDefaultHardErrorMode,
    ProcessIoPortHandlers,
    ProcessPooledUsageAndLimits,
    ProcessWorkingSetWatch,
    ProcessUserModeIOPL,
    ProcessEnableAlignmentFaultFixup,
    ProcessPriorityClass,
    ProcessWx86Information,
    ProcessHandleCount,
    ProcessAffinityMask,
    ProcessPriorityBoost,
    ProcessDeviceMap,
    ProcessSessionInformation,
    ProcessForegroundInformation,
    ProcessWow64Information,
    ProcessImageFileName,
    ProcessLUIDDeviceMapsEnabled,
    ProcessBreakOnTermination,
    ProcessDebugObjectHandle,
    ProcessDebugFlags,
    ProcessHandleTracing,
    ProcessIoPriority,
    ProcessExecuteFlags,
    ProcessTlsInformation,
    ProcessCookie,
    ProcessImageInformation,
    ProcessCycleTime,
    ProcessPagePriority,
    ProcessInstrumentationCallback,
    ProcessThreadStackAllocation,
    ProcessWorkingSetWatchEx,
    ProcessImageFileNameWin32,
    ProcessImageFileMapping,
    ProcessAffinityUpdateMode,
    ProcessMemoryAllocationMode,
    ProcessGroupInformation,
    ProcessTokenVirtualizationEnabled,
    ProcessConsoleHostProcess,
    ProcessWindowInformation,
    ProcessHandleInformation,
    ProcessMitigationPolicy,
    ProcessDynamicFunctionTableInformation,
    ProcessHandleCheckingMode,
    ProcessKeepAliveCount,
    ProcessRevokeFileHandles,
    ProcessWorkingSetControl,
    ProcessHandleTable,
    ProcessCheckStackExtentsMode,
    ProcessCommandLineInformation,
    ProcessProtectionInformation,
    ProcessMemoryExhaustion,
    ProcessFaultInformation,
    ProcessTelemetryIdInformation,
    ProcessCommitReleaseInformation,
    ProcessDefaultCpuSetsInformation,
    ProcessAllowedCpuSetsInformation,
    ProcessSubsystemProcess,
    ProcessJobMemoryInformation,
    ProcessInPrivate,
    ProcessRaiseUMExceptionOnInvalidHandleClose,
    ProcessIumChallengeResponse,
    ProcessChildProcessInformation,
    ProcessHighGraphicsPriorityInformation,
    ProcessSubsystemInformation,
    ProcessEnergyValues,
    ProcessPowerThrottlingState,
    ProcessReserved3Information,
    ProcessWin32kSyscallFilterInformation,
    ProcessDisableSystemAllowedCpuSets,
    ProcessWakeInformation,
    ProcessEnergyTrackingState,
    ProcessManageWritesToExecutableMemory,
    MaxProcessInfoClass
} PROCESSINFOCLASS;

typedef enum _THREADINFOCLASS
{
    ThreadBasicInformation,
    ThreadTimes,
    ThreadPriority,
    ThreadBasePriority,
    ThreadAffinityMask,
    ThreadImpersonationToken,
    ThreadDescriptorTableEntry,
    ThreadEnableAlignmentFaultFixup,
    ThreadEventPair_Reusable,
    ThreadQuerySetWin32StartAddress,
    ThreadZeroTlsCell,
    ThreadPerformanceCount,
    ThreadAmILastThread,
    ThreadIdealProcessor,
    ThreadPriorityBoost,
    ThreadSetTlsArrayAddress,
    ThreadIsIoPending,
    ThreadHideFromDebugger,
    ThreadBreakOnTermination,
    ThreadSwitchLegacyState,
    ThreadIsTerminated,
    ThreadLastSystemCall,
    ThreadIoPriority,
    ThreadCycleTime,
    ThreadPagePriority,
    ThreadActualBasePriority,
    ThreadTebInformation,
    ThreadCSwitchMon,

    // Windows 7
    ThreadCSwitchPmu, // 0x1C
    ThreadWow64Context,
    ThreadGroupInformation,
    ThreadUmsInformation,
    ThreadCounterProfiling,
    ThreadIdealProcessorEx,

    // Windows 8
    ThreadCpuAccountingInformation, // 0x22

    // Windows 8.1
    ThreadSuspendCount, // 0x23

    // Windows 10
    ThreadHeterogeneousCpuPolicy, // 0x24
    ThreadContainerId,
    ThreadNameInformation,
    ThreadSelectedCpuSets,
    ThreadSystemThreadInformation,
    ThreadActualGroupAffinity,

    ThreadDynamicCodePolicyInfo,
    ThreadExplicitCaseSensitivity,
    ThreadWorkOnBehalfTicket,
    ThreadSubsystemInformation,
    ThreadDbgkWerReportActive,
    ThreadAttachContainer,
    ThreadManageWritesToExecutableMemory,
    ThreadPowerThrottlingState,
    ThreadWorkloadClass,
    ThreadCreateStateChange,
    ThreadApplyStateChange,
    ThreadStrongerBadHandleChecks,
    ThreadEffectiveIoPriority,
    ThreadEffectivePagePriority,

    MaxThreadInfoClass
} THREADINFOCLASS;

typedef struct _MANAGE_WRITES_TO_EXECUTABLE_MEMORY
{
    ULONG Version : 8;
    ULONG ProcessEnableWriteExceptions : 1;
    ULONG ThreadAllowWrites : 1;
    ULONG Spare : 22;
    PVOID KernelWriteToExecutableSignal;
} MANAGE_WRITES_TO_EXECUTABLE_MEMORY, *PMANAGE_WRITES_TO_EXECUTABLE_MEMORY;

#else

typedef enum _PSPROCESSPRIORITYMODE
{
    PsProcessPriorityBackground,
    PsProcessPriorityForeground,
    PsProcessPrioritySpinning
} PSPROCESSPRIORITYMODE;

typedef enum _JOBOBJECTINFOCLASS
{
    JobObjectBasicAccountingInformation = 1,
    JobObjectBasicLimitInformation,
    JobObjectBasicProcessIdList,
    JobObjectBasicUIRestrictions,
    JobObjectSecurityLimitInformation,
    JobObjectEndOfJobTimeInformation,
    JobObjectAssociateCompletionPortInformation,
    JobObjectBasicAndIoAccountingInformation,
    JobObjectExtendedLimitInformation,
    JobObjectJobSetInformation,
    MaxJobObjectInfoClass
} JOBOBJECTINFOCLASS;

//
// Power Event Events for Win32K Power Event Callback
//
typedef enum _PSPOWEREVENTTYPE
{
    PsW32FullWake = 0,
    PsW32EventCode = 1,
    PsW32PowerPolicyChanged = 2,
    PsW32SystemPowerState = 3,
    PsW32SystemTime = 4,
    PsW32DisplayState = 5,
    PsW32CapabilitiesChanged = 6,
    PsW32SetStateFailed = 7,
    PsW32GdiOff = 8,
    PsW32GdiOn = 9,
    PsW32GdiPrepareResumeUI = 10,
    PsW32GdiOffRequest = 11,
    PsW32MonitorOff = 12,
} PSPOWEREVENTTYPE;

//
// Power State Tasks for Win32K Power State Callback
//
typedef enum _POWERSTATETASK
{
    PowerState_BlockSessionSwitch = 0,
    PowerState_Init = 1,
    PowerState_QueryApps = 2,
    PowerState_QueryServices = 3,
    PowerState_QueryAppsFailed = 4,
    PowerState_QueryServicesFailed = 5,
    PowerState_SuspendApps = 6,
    PowerState_SuspendServices = 7,
    PowerState_ShowUI = 8,
    PowerState_NotifyWL = 9,
    PowerState_ResumeApps = 10,
    PowerState_ResumeServices = 11,
    PowerState_UnBlockSessionSwitch = 12,
    PowerState_End = 13,
    PowerState_BlockInput = 14,
    PowerState_UnblockInput = 15,
} POWERSTATETASK;

//
// Win32K Job Callback Types
//
typedef enum _PSW32JOBCALLOUTTYPE
{
   PsW32JobCalloutSetInformation = 0,
   PsW32JobCalloutAddProcess = 1,
   PsW32JobCalloutTerminate = 2,
} PSW32JOBCALLOUTTYPE;

//
// Win32K Thread Callback Types
//
typedef enum _PSW32THREADCALLOUTTYPE
{
    PsW32ThreadCalloutInitialize,
    PsW32ThreadCalloutExit,
} PSW32THREADCALLOUTTYPE;

//
// Declare empty structure definitions so that they may be
// referenced by routines before they are defined.
//
//struct _EPROCESS;
//struct _ETHREAD;
struct _WIN32_POWEREVENT_PARAMETERS;
struct _WIN32_POWERSTATE_PARAMETERS;
struct _WIN32_JOBCALLOUT_PARAMETERS;
struct _WIN32_OPENMETHOD_PARAMETERS;
struct _WIN32_OKAYTOCLOSEMETHOD_PARAMETERS;
struct _WIN32_CLOSEMETHOD_PARAMETERS;
struct _WIN32_DELETEMETHOD_PARAMETERS;
struct _WIN32_PARSEMETHOD_PARAMETERS;

//
// Win32K Process and Thread Callbacks
//
typedef
NTSTATUS
(NTAPI *PKWIN32_PROCESS_CALLOUT)(
    _In_ struct _EPROCESS *Process,
    _In_ BOOLEAN Create
);

typedef
NTSTATUS
(NTAPI *PKWIN32_THREAD_CALLOUT)(
    _In_ struct _ETHREAD *Thread,
    _In_ PSW32THREADCALLOUTTYPE Type
);

typedef
PVOID
(NTAPI *PKWIN32_GLOBALATOMTABLE_CALLOUT)(
    VOID
);

typedef
NTSTATUS
(NTAPI *PKWIN32_POWEREVENT_CALLOUT)(
    _In_ struct _WIN32_POWEREVENT_PARAMETERS *Parameters
);

typedef
NTSTATUS
(NTAPI *PKWIN32_POWERSTATE_CALLOUT)(
    _In_ struct _WIN32_POWERSTATE_PARAMETERS *Parameters
);

typedef
NTSTATUS
(NTAPI *PKWIN32_JOB_CALLOUT)(
    _In_ struct _WIN32_JOBCALLOUT_PARAMETERS *Parameters
);

typedef
NTSTATUS
(NTAPI *PGDI_BATCHFLUSH_ROUTINE)(
    VOID
);

typedef
NTSTATUS
(NTAPI *PKWIN32_OPENMETHOD_CALLOUT)(
    _In_ struct _WIN32_OPENMETHOD_PARAMETERS *Parameters
);

typedef
NTSTATUS
(NTAPI *PKWIN32_OKTOCLOSEMETHOD_CALLOUT)(
    _In_ struct _WIN32_OKAYTOCLOSEMETHOD_PARAMETERS *Parameters
);

typedef
NTSTATUS
(NTAPI *PKWIN32_CLOSEMETHOD_CALLOUT)(
    _In_ struct _WIN32_CLOSEMETHOD_PARAMETERS *Parameters
);

typedef
NTSTATUS
(NTAPI *PKWIN32_DELETEMETHOD_CALLOUT)(
    _In_ struct _WIN32_DELETEMETHOD_PARAMETERS *Parameters
);

typedef
NTSTATUS
(NTAPI *PKWIN32_PARSEMETHOD_CALLOUT)(
    _In_ struct _WIN32_PARSEMETHOD_PARAMETERS *Parameters
);

typedef
NTSTATUS
(NTAPI *PKWIN32_SESSION_CALLOUT)(
    _In_ PVOID Parameter
);

#if (NTDDI_VERSION >= NTDDI_LONGHORN)
typedef
NTSTATUS
(NTAPI *PKWIN32_WIN32DATACOLLECTION_CALLOUT)(
    _In_ struct _EPROCESS *Process,
    _In_ PVOID Callback,
    _In_ PVOID Context
);
#endif

//
// Lego Callback
//
typedef
VOID
(NTAPI *PLEGO_NOTIFY_ROUTINE)(
    _In_ PKTHREAD Thread
);

#endif

typedef NTSTATUS
(NTAPI *PPOST_PROCESS_INIT_ROUTINE)(
    VOID
);

//
// Descriptor Table Entry Definition
//
#if (_M_IX86)
#define _DESCRIPTOR_TABLE_ENTRY_DEFINED
typedef struct _DESCRIPTOR_TABLE_ENTRY
{
    ULONG Selector;
    LDT_ENTRY Descriptor;
} DESCRIPTOR_TABLE_ENTRY, *PDESCRIPTOR_TABLE_ENTRY;
#endif

//
// PEB Lock Routine
//
typedef VOID
(NTAPI *PPEBLOCKROUTINE)(
    PVOID PebLock
);

//
// PEB Free Block Descriptor
//
typedef struct _PEB_FREE_BLOCK
{
    struct _PEB_FREE_BLOCK* Next;
    ULONG Size;
} PEB_FREE_BLOCK, *PPEB_FREE_BLOCK;

//
// Initial PEB
//
typedef struct _INITIAL_PEB
{
    BOOLEAN InheritedAddressSpace;
    BOOLEAN ReadImageFileExecOptions;
    BOOLEAN BeingDebugged;
    union
    {
        BOOLEAN BitField;
#if (NTDDI_VERSION >= NTDDI_WS03)
        struct
        {
            BOOLEAN ImageUsesLargePages:1;
#if (NTDDI_VERSION >= NTDDI_LONGHORN)
            BOOLEAN IsProtectedProcess:1;
            BOOLEAN IsLegacyProcess:1;
            BOOLEAN SpareBits:5;
#else
            BOOLEAN SpareBits:7;
#endif
        };
#else
        BOOLEAN SpareBool;
#endif
    };
    HANDLE Mutant;
} INITIAL_PEB, *PINITIAL_PEB;

//
// Initial TEB
//
typedef struct _INITIAL_TEB
{
    PVOID PreviousStackBase;
    PVOID PreviousStackLimit;
    PVOID StackBase;
    PVOID StackLimit;
    PVOID AllocatedStackBase;
} INITIAL_TEB, *PINITIAL_TEB;

//
// TEB Active Frame Structures
//
typedef struct _TEB_ACTIVE_FRAME_CONTEXT
{
    ULONG Flags;
    LPSTR FrameName;
} TEB_ACTIVE_FRAME_CONTEXT, *PTEB_ACTIVE_FRAME_CONTEXT;
typedef const struct _TEB_ACTIVE_FRAME_CONTEXT *PCTEB_ACTIVE_FRAME_CONTEXT;

typedef struct _TEB_ACTIVE_FRAME_CONTEXT_EX
{
    TEB_ACTIVE_FRAME_CONTEXT BasicContext;
    PCSTR SourceLocation;
} TEB_ACTIVE_FRAME_CONTEXT_EX, *PTEB_ACTIVE_FRAME_CONTEXT_EX;
typedef const struct _TEB_ACTIVE_FRAME_CONTEXT_EX *PCTEB_ACTIVE_FRAME_CONTEXT_EX;

typedef struct _TEB_ACTIVE_FRAME
{
    ULONG Flags;
    struct _TEB_ACTIVE_FRAME *Previous;
    PCTEB_ACTIVE_FRAME_CONTEXT Context;
} TEB_ACTIVE_FRAME, *PTEB_ACTIVE_FRAME;
typedef const struct _TEB_ACTIVE_FRAME *PCTEB_ACTIVE_FRAME;

typedef struct _TEB_ACTIVE_FRAME_EX
{
    TEB_ACTIVE_FRAME BasicFrame;
    PVOID ExtensionIdentifier;
} TEB_ACTIVE_FRAME_EX, *PTEB_ACTIVE_FRAME_EX;
typedef const struct _TEB_ACTIVE_FRAME_EX *PCTEB_ACTIVE_FRAME_EX;

typedef struct _CLIENT_ID32
{
    ULONG UniqueProcess;
    ULONG UniqueThread;
} CLIENT_ID32, *PCLIENT_ID32;

typedef struct _CLIENT_ID64
{
    ULONG64 UniqueProcess;
    ULONG64 UniqueThread;
} CLIENT_ID64, *PCLIENT_ID64;

#if (NTDDI_VERSION < NTDDI_WS03)
typedef struct _Wx86ThreadState
{
    PULONG  CallBx86Eip;
    PVOID   DeallocationCpu;
    BOOLEAN UseKnownWx86Dll;
    CHAR    OleStubInvoked;
} Wx86ThreadState, *PWx86ThreadState;
#endif

//
// PEB.AppCompatFlags.LowPart
// Tag FLAG_MASK_KERNEL
//
typedef enum _APPCOMPAT_FLAGS
{
    GetShortPathNameNT4 = 0x1,
    GetDiskFreeSpace2GB = 0x8,
    FTMFromCurrentAPI = 0x20,
    DisallowCOMBindingNotifications = 0x40,
    Ole32ValidatePointers = 0x80,
    DisableCicero = 0x100,
    Ole32EnableAsyncDocFile = 0x200,
    EnableLegacyExceptionHandlinginOLE = 0x400,
    DisableAdvanceRPCClientHardening = 0x800,
    DisableMaybeNULLSizeisConsistencycheck = 0x1000,
    DisableAdvancedRPCrangeCheck = 0x4000,
    EnableLegacyExceptionHandlingInRPC = 0x8000,
    EnableLegacyNTFSFlagsForDocfileOpens = 0x10000,
    DisableNDRIIDConsistencyCheck = 0x20000,
    UserDisableForwarderPatch = 0x40000,
    DisableNewWMPAINTDispatchInOLE = 0x100000,
    AddRestrictedSidInCoInitializeSecurity = 0x200000,
    AllocDebugInfoForCritSections = 0x400000,
    EnableLegacyLoadTypeLibForRelativePaths = 0x800000,
    AllowMaximizedWindowGamma = 0x1000000,
    CloudFilesHydrationDisallowed = 0x2000000,
    CloudFilesFullHydrationOnOpen = 0x4000000,
    CloudFilesFullHydration = 0x8000000,
    DisableParallelLoader = 0x10000000,
    DisguisePlaceholders = 0x20000000,
    CloudFilesHydrationInForeground = 0x40000000,
    DoNotAddToCache = 0x80000000,
} APPCOMPAT_FLAGS;

//
// PEB.AppCompatFlags.HighPart
// Tag FLAG_MASK_KERNEL
//
typedef enum _APPCOMPAT_FLAGS_HIGHPART
{
    PosixDeleteDisabled = 0x1,

    // ReactOS-specific
    RendererFull3D = 0x80000000,    // CORE-20322
} APPCOMPAT_FLAGS_HIGHPART;


//
// PEB.AppCompatFlagsUser.LowPart
// Tag FLAG_MASK_USER
//
typedef enum _APPCOMPAT_USERFLAGS
{
    DisableAnimation = 0x1,
    DisableKeyboardCues = 0x2,
    No50StylebitsInSetWindowLong = 0x4,
    DisableDrawPatternRect = 0x8,
    MSShellDialog = 0x10,
    NoDDETerminateDuringDestroy = 0x20,
    GiveupForeground = 0x40,
    AlwaysActiveMenus = 0x80,
    NoMouseHideInEdit = 0x100,
    NoGdiBatching = 0x200,
    FontSubstitution = 0x400,
    No50StylebitsInCreateWindow = 0x800,
    NoCustomPaperSizes = 0x1000,
    AllTheDdeHacks = 0x2000,
    UseDefaultCharset = 0x4000,
    NoCharDeadKey = 0x8000,
    NoTryExceptForWindowProc = 0x10000,
    NoInitInsertReplaceFlags = 0x20000,
    NoDdeSync = 0x40000,
    NoGhost = 0x80000,
    NoDdeAsyncReg = 0x100000,
    StrictLLHook = 0x200000,
    NoShadow = 0x400000,
    NoTimerCallbackProtection = 0x1000000,
    HighDpiAware = 0x2000000,
    OpenGLEmfAware = 0x4000000,
    EnableTransparantBltMirror = 0x8000000,
    NoPaddedBorder = 0x10000000,
    ForceLegacyResizeCM = 0x20000000,
    HardwareAudioMixer = 0x40000000,
    DisableSWCursorOnMoveSize = 0x80000000,
} APPCOMPAT_USERFLAGS;

//
// PEB.AppCompatFlagsUser.HighPart
// Tag FLAG_MASK_USER
//
typedef enum _APPCOMPAT_USERFLAGS_HIGHPART
{
    DisableWindowArrangement = 0x1,
    ReorderWaveForCommunications = 0x2,
    NoGdiHwAcceleration = 0x4,
    NoTimerCoalescing = 0x8,
    PrinterIsolationAware = 0x10,
    UseWARPRendering = 0x20,
    MirrorDriverDrawCursor = 0x40,
    InstallShieldInstaller = 0x80,
    Disable8And16BitModes = 0x100,
    Disable8And16BitD3D = 0x200,
    PromotePointer = 0x400,
    PreventMouseInPointer = 0x800,
    _8And16BitAggregateBlts = 0x1000,
    _8And16BitGDIRedraw = 0x2000,
    _8And16BitCopyOnFlip = 0x4000,
    _8And16BitNoIncRefCount = 0x8000,
    _8And16BitDXMaxWinMode = 0x10000,
    EarlyMouseDelegation = 0x20000,
    _8And16BitTimedPriSync = 0x40000,
    UseIntegratedGraphics = 0x80000,
    UseLegacyMouseWheelRouting = 0x100000,
    PerProcessSystemDPIForceOn = 0x200000,
    PerProcessSystemDPIForceOff = 0x400000,
    DPIUnaware = 0x800000,
    NoVirtWndRects = 0x1000000,
    CFDNoRedirectInitialFolder = 0x2000000,
    NoDTToDITMouseBatch = 0x4000000,
    GdiDPIScaling = 0x8000000,
    QueueMouseMoveOnReleaseCapture = 0x10000000,
    DisableFocusTracking = 0x20000000,
    GdiDPIScalingForceDisable = 0x40000000,
} APPCOMPAT_USERFLAGS_HIGHPART;

//
// Process Environment Block (PEB)
// Thread Environment Block (TEB)
//
#include "peb_teb.h"

#ifdef _WIN64
//
// Explicit 32 bit PEB/TEB
//
#define EXPLICIT_32BIT
#include "peb_teb.h"
#undef EXPLICIT_32BIT

//
// Explicit 64 bit PEB/TEB
//
#define EXPLICIT_64BIT
#include "peb_teb.h"
#undef EXPLICIT_64BIT
#endif

#ifdef NTOS_MODE_USER

//
// Process Information Structures for NtQueryProcessInformation
//
typedef struct _PAGE_PRIORITY_INFORMATION
{
    ULONG PagePriority;
} PAGE_PRIORITY_INFORMATION, *PPAGE_PRIORITY_INFORMATION;

typedef struct _PROCESS_BASIC_INFORMATION
{
    NTSTATUS ExitStatus;
    PPEB PebBaseAddress;
    ULONG_PTR AffinityMask;
    KPRIORITY BasePriority;
    ULONG_PTR UniqueProcessId;
    ULONG_PTR InheritedFromUniqueProcessId;
} PROCESS_BASIC_INFORMATION, *PPROCESS_BASIC_INFORMATION;

typedef struct _PROCESS_ACCESS_TOKEN
{
    HANDLE Token;
    HANDLE Thread;
} PROCESS_ACCESS_TOKEN, *PPROCESS_ACCESS_TOKEN;

typedef struct _PROCESS_DEVICEMAP_INFORMATION
{
    union
    {
        struct
        {
            HANDLE DirectoryHandle;
        } Set;
        struct
        {
            ULONG DriveMap;
            UCHAR DriveType[32];
        } Query;
    };
} PROCESS_DEVICEMAP_INFORMATION, *PPROCESS_DEVICEMAP_INFORMATION;

typedef struct _KERNEL_USER_TIMES
{
    LARGE_INTEGER CreateTime;
    LARGE_INTEGER ExitTime;
    LARGE_INTEGER KernelTime;
    LARGE_INTEGER UserTime;
} KERNEL_USER_TIMES, *PKERNEL_USER_TIMES;

typedef struct _POOLED_USAGE_AND_LIMITS
{
    SIZE_T PeakPagedPoolUsage;
    SIZE_T PagedPoolUsage;
    SIZE_T PagedPoolLimit;
    SIZE_T PeakNonPagedPoolUsage;
    SIZE_T NonPagedPoolUsage;
    SIZE_T NonPagedPoolLimit;
    SIZE_T PeakPagefileUsage;
    SIZE_T PagefileUsage;
    SIZE_T PagefileLimit;
} POOLED_USAGE_AND_LIMITS, *PPOOLED_USAGE_AND_LIMITS;

typedef struct _PROCESS_WS_WATCH_INFORMATION
{
    PVOID FaultingPc;
    PVOID FaultingVa;
} PROCESS_WS_WATCH_INFORMATION, *PPROCESS_WS_WATCH_INFORMATION;

typedef struct _PROCESS_SESSION_INFORMATION
{
    ULONG SessionId;
} PROCESS_SESSION_INFORMATION, *PPROCESS_SESSION_INFORMATION;

typedef struct _PROCESS_HANDLE_TRACING_ENTRY
{
    HANDLE Handle;
    CLIENT_ID ClientId;
    ULONG Type;
    PVOID Stacks[PROCESS_HANDLE_TRACING_MAX_STACKS];
} PROCESS_HANDLE_TRACING_ENTRY, *PPROCESS_HANDLE_TRACING_ENTRY;

typedef struct _PROCESS_HANDLE_TRACING_QUERY
{
    HANDLE Handle;
    ULONG TotalTraces;
    PROCESS_HANDLE_TRACING_ENTRY HandleTrace[ANYSIZE_ARRAY];
} PROCESS_HANDLE_TRACING_QUERY, *PPROCESS_HANDLE_TRACING_QUERY;

#endif

typedef struct _PROCESS_LDT_INFORMATION
{
    ULONG Start;
    ULONG Length;
    LDT_ENTRY LdtEntries[ANYSIZE_ARRAY];
} PROCESS_LDT_INFORMATION, *PPROCESS_LDT_INFORMATION;

typedef struct _PROCESS_LDT_SIZE
{
    ULONG Length;
} PROCESS_LDT_SIZE, *PPROCESS_LDT_SIZE;

typedef struct _PROCESS_PRIORITY_CLASS
{
    BOOLEAN Foreground;
    UCHAR PriorityClass;
} PROCESS_PRIORITY_CLASS, *PPROCESS_PRIORITY_CLASS;

// Compatibility with windows, see CORE-16757, CORE-17106, CORE-17247
C_ASSERT(sizeof(PROCESS_PRIORITY_CLASS) == 2);

typedef struct _PROCESS_FOREGROUND_BACKGROUND
{
    BOOLEAN Foreground;
} PROCESS_FOREGROUND_BACKGROUND, *PPROCESS_FOREGROUND_BACKGROUND;

//
// Apphelp SHIM Cache
//
typedef enum _APPHELPCACHESERVICECLASS
{
    ApphelpCacheServiceLookup = 0,
    ApphelpCacheServiceRemove = 1,
    ApphelpCacheServiceUpdate = 2,
    ApphelpCacheServiceFlush = 3,
    ApphelpCacheServiceDump = 4,

    ApphelpDBGReadRegistry = 0x100,
    ApphelpDBGWriteRegistry = 0x101,
} APPHELPCACHESERVICECLASS;


typedef struct _APPHELP_CACHE_SERVICE_LOOKUP
{
    UNICODE_STRING ImageName;
    HANDLE ImageHandle;
} APPHELP_CACHE_SERVICE_LOOKUP, *PAPPHELP_CACHE_SERVICE_LOOKUP;


//
// Thread Information Structures for NtQueryProcessInformation
//
typedef struct _THREAD_BASIC_INFORMATION
{
    NTSTATUS ExitStatus;
    PVOID TebBaseAddress;
    CLIENT_ID ClientId;
    KAFFINITY AffinityMask;
    KPRIORITY Priority;
    KPRIORITY BasePriority;
} THREAD_BASIC_INFORMATION, *PTHREAD_BASIC_INFORMATION;

typedef struct _THREAD_NAME_INFORMATION
{
    UNICODE_STRING ThreadName;
} THREAD_NAME_INFORMATION, *PTHREAD_NAME_INFORMATION;

typedef struct _PROCESS_CYCLE_TIME_INFORMATION
{
    ULONGLONG AccumulatedCycles;
    ULONGLONG CurrentCycleCount;
} PROCESS_CYCLE_TIME_INFORMATION, *PPROCESS_CYCLE_TIME_INFORMATION;

typedef union _ENERGY_STATE_DURATION
{
    ULONGLONG Value;
    struct
    {
        ULONG LastChangeTime;
        ULONG Duration:31;
        ULONG IsInState:1;
    } State;
} ENERGY_STATE_DURATION, *PENERGY_STATE_DURATION;

typedef struct _PROCESS_ENERGY_VALUES
{
    ULONGLONG Cycles[4][2];
    volatile ULONGLONG DiskEnergy;
    volatile ULONGLONG NetworkTailEnergy;
    volatile ULONGLONG MbbTailEnergy;
    volatile ULONGLONG NetworkTxRxBytes;
    volatile ULONGLONG MbbTxRxBytes;
    ENERGY_STATE_DURATION Durations[3];
    ULONG CompositionRendered;
    ULONG CompositionDirtyGenerated;
    ULONG CompositionDirtyPropagated;
    ULONG Reserved;
    ULONGLONG AttributedCycles[4][2];
    ULONGLONG WorkOnBehalfCycles[4][2];
    ULONGLONG Timelines[14];
    ENERGY_STATE_DURATION ExtendedDurations[5];
    ULONG KeyboardInput;
    ULONG MouseInput;
} PROCESS_ENERGY_VALUES, *PPROCESS_ENERGY_VALUES;

typedef struct _THREAD_CYCLE_TIME_INFORMATION
{
    ULONGLONG AccumulatedCycles;
    ULONGLONG CurrentCycleCount;
} THREAD_CYCLE_TIME_INFORMATION, *PTHREAD_CYCLE_TIME_INFORMATION;

#define THREAD_POWER_THROTTLING_CURRENT_VERSION 1
#define PROCESS_POWER_THROTTLING_CURRENT_VERSION 1
#define PROCESS_POWER_THROTTLING_EXECUTION_SPEED 0x1
#define PROCESS_POWER_THROTTLING_IGNORE_TIMER_RESOLUTION 0x4
#define PROCESS_POWER_THROTTLING_VALID_FLAGS 0x5

#ifndef _PROCESS_POWER_THROTTLING_STATE_DEFINED
#define _PROCESS_POWER_THROTTLING_STATE_DEFINED
typedef struct _PROCESS_POWER_THROTTLING_STATE
{
    ULONG Version;
    ULONG ControlMask;
    ULONG StateMask;
} PROCESS_POWER_THROTTLING_STATE;
#endif

#define THREAD_POWER_THROTTLING_EXECUTION_SPEED 0x1
#define THREAD_POWER_THROTTLING_VALID_FLAGS THREAD_POWER_THROTTLING_EXECUTION_SPEED

#ifndef _THREAD_POWER_THROTTLING_STATE_DEFINED
#define _THREAD_POWER_THROTTLING_STATE_DEFINED
typedef struct _THREAD_POWER_THROTTLING_STATE
{
    ULONG Version;
    ULONG ControlMask;
    ULONG StateMask;
} THREAD_POWER_THROTTLING_STATE;
#endif
typedef THREAD_POWER_THROTTLING_STATE *PTHREAD_POWER_THROTTLING_STATE;

#ifndef NTOS_MODE_USER

//
// Job Set Array
//
typedef struct _JOB_SET_ARRAY
{
    HANDLE JobHandle;
    ULONG MemberLevel;
    ULONG Flags;
} JOB_SET_ARRAY, *PJOB_SET_ARRAY;

//
// Process Quota Type
//
typedef enum _PS_QUOTA_TYPE
{
    PsNonPagedPool = 0,
    PsPagedPool,
    PsPageFile,
#if (NTDDI_VERSION >= NTDDI_LONGHORN)
    PsWorkingSet,
#endif
#if (NTDDI_VERSION == NTDDI_LONGHORN)
    PsCpuRate,
#endif
    PsQuotaTypes
} PS_QUOTA_TYPE;

//
// EPROCESS Quota Structures
//
typedef struct _EPROCESS_QUOTA_ENTRY
{
    SIZE_T Usage;
    SIZE_T Limit;
    SIZE_T Peak;
    SIZE_T Return;
} EPROCESS_QUOTA_ENTRY, *PEPROCESS_QUOTA_ENTRY;

typedef struct _EPROCESS_QUOTA_BLOCK
{
    EPROCESS_QUOTA_ENTRY QuotaEntry[PsQuotaTypes];
    LIST_ENTRY QuotaList;
    ULONG ReferenceCount;
    ULONG ProcessCount;
} EPROCESS_QUOTA_BLOCK, *PEPROCESS_QUOTA_BLOCK;

//
// Process Pagefault History
//
typedef struct _PAGEFAULT_HISTORY
{
    ULONG CurrentIndex;
    ULONG MapIndex;
    KSPIN_LOCK SpinLock;
    PVOID Reserved;
    PROCESS_WS_WATCH_INFORMATION WatchInfo[1];
} PAGEFAULT_HISTORY, *PPAGEFAULT_HISTORY;

//
// Process Impersonation Information
//
typedef struct _PS_IMPERSONATION_INFORMATION
{
    PACCESS_TOKEN Token;
    BOOLEAN CopyOnOpen;
    BOOLEAN EffectiveOnly;
    SECURITY_IMPERSONATION_LEVEL ImpersonationLevel;
} PS_IMPERSONATION_INFORMATION, *PPS_IMPERSONATION_INFORMATION;

//
// Process Termination Port
//
typedef struct _TERMINATION_PORT
{
    struct _TERMINATION_PORT *Next;
    PVOID Port;
} TERMINATION_PORT, *PTERMINATION_PORT;

//
// Per-Process APC Rate Limiting
//
typedef struct _PSP_RATE_APC
{
    union
    {
        SINGLE_LIST_ENTRY NextApc;
        ULONGLONG ExcessCycles;
    };
    ULONGLONG TargetGEneration;
    KAPC RateApc;
} PSP_RATE_APC, *PPSP_RATE_APC;

//
// Executive Thread (ETHREAD)
//
typedef struct _ETHREAD
{
    KTHREAD Tcb;
    LARGE_INTEGER CreateTime;
    union
    {
        LARGE_INTEGER ExitTime;
        LIST_ENTRY LpcReplyChain;
        LIST_ENTRY KeyedWaitChain;
    };
#if (NTDDI_VERSION >= NTDDI_WIN10)
    union
    {
        LIST_ENTRY PostBlockList;
        struct
        {
            PVOID ReservedPostBlockPointer;
            PVOID StartAddress;
        };
    };
#else
    union
    {
        NTSTATUS ExitStatus;
        PVOID OfsChain;
    };
    LIST_ENTRY PostBlockList;
#endif
    union
    {
        struct _TERMINATION_PORT *TerminationPort;
        struct _ETHREAD *ReaperLink;
        PVOID KeyedWaitValue;
#if (NTDDI_VERSION >= NTDDI_LONGHORN)
        PVOID Win32StartParameter;
#endif
    };
    KSPIN_LOCK ActiveTimerListLock;
    LIST_ENTRY ActiveTimerListHead;
    CLIENT_ID Cid;
#if (NTDDI_VERSION >= NTDDI_WIN10)
    union
    {
        KSEMAPHORE KeyedWaitSemaphore;
        KSEMAPHORE AlpcWaitSemaphore;
    };
#elif (NTDDI_VERSION >= NTDDI_LONGHORN)
    KSEMAPHORE KeyedWaitSemaphore;
#else
    union
    {
        KSEMAPHORE LpcReplySemaphore;
        KSEMAPHORE KeyedWaitSemaphore;
    };
    union
    {
        PVOID LpcReplyMessage;
        PVOID LpcWaitingOnPort;
    };
#endif
    PPS_IMPERSONATION_INFORMATION ImpersonationInfo;
    LIST_ENTRY IrpList;
    ULONG_PTR TopLevelIrp;
    PDEVICE_OBJECT DeviceToVerify;
#if (NTDDI_VERSION >= NTDDI_WIN10)
    PVOID Win32StartAddress;
    PVOID ReservedThreadPointer;
    PVOID LegacyPowerObject;
#elif (NTDDI_VERSION >= NTDDI_LONGHORN)
    PPSP_RATE_APC RateControlApc;
    PVOID Win32StartAddress;
#else
    struct _EPROCESS *ThreadsProcess;
    PVOID Win32StartAddress;
    union
    {
        PKSTART_ROUTINE StartAddress;
        ULONG LpcReceivedMessageId;
    };
#endif
    LIST_ENTRY ThreadListEntry;
    EX_RUNDOWN_REF RundownProtect;
    EX_PUSH_LOCK ThreadLock;
#if (NTDDI_VERSION < NTDDI_LONGHORN)
    ULONG LpcReplyMessageId;
#endif
    ULONG ReadClusterSize;
#if (NTDDI_VERSION >= NTDDI_LONGHORN)
    ULONG SpareUlong0;
#else
    ACCESS_MASK GrantedAccess;
#endif
    union
    {
        struct
        {
           ULONG Terminated:1;
#if (NTDDI_VERSION >= NTDDI_LONGHORN)
           ULONG ThreadInserted:1;
#else
           ULONG DeadThread:1;
#endif
           ULONG HideFromDebugger:1;
           ULONG ActiveImpersonationInfo:1;
           ULONG SystemThread:1;
           ULONG HardErrorsAreDisabled:1;
           ULONG BreakOnTermination:1;
           ULONG SkipCreationMsg:1;
           ULONG SkipTerminationMsg:1;
#if (NTDDI_VERSION >= NTDDI_LONGHORN)
           ULONG CreateMsgSent:1;
           ULONG ThreadIoPriority:3;
           ULONG ThreadPagePriority:3;
           ULONG PendingRatecontrol:1;
#endif
        };
        ULONG CrossThreadFlags;
    };
    union
    {
        struct
        {
           ULONG ActiveExWorker:1;
           ULONG ExWorkerCanWaitUser:1;
           ULONG MemoryMaker:1;
           ULONG KeyedEventInUse:1;
#if (NTDDI_VERSION >= NTDDI_LONGHORN)
           ULONG RateApcState:2;
#endif
        };
        ULONG SameThreadPassiveFlags;
    };
    union
    {
        struct
        {
           ULONG LpcReceivedMsgIdValid:1;
           ULONG LpcExitThreadCalled:1;
#if (NTDDI_VERSION >= NTDDI_LONGHORN)
           ULONG Spare:1;
#else
           ULONG AddressSpaceOwner:1;
#endif
           ULONG OwnsProcessWorkingSetExclusive:1;
           ULONG OwnsProcessWorkingSetShared:1;
           ULONG OwnsSystemWorkingSetExclusive:1;
           ULONG OwnsSystemWorkingSetShared:1;
           ULONG OwnsSessionWorkingSetExclusive:1;
           ULONG OwnsSessionWorkingSetShared:1;
#if (NTDDI_VERSION >= NTDDI_LONGHORN)
           ULONG SuppressSymbolLoad:1;
           ULONG Spare1:3;
           ULONG PriorityRegionActive:4;
#else
           ULONG ApcNeeded:1;
#endif
        };
        ULONG SameThreadApcFlags;
    };
#if (NTDDI_VERSION >= NTDDI_LONGHORN)
    UCHAR CacheManagerActive;
#else
    UCHAR ForwardClusterOnly;
#endif
    UCHAR DisablePageFaultClustering;
    UCHAR ActiveFaultCount;
#if (NTDDI_VERSION >= NTDDI_WIN10)
    UCHAR ReservedThreadByte;
    ULONG ReservedThreadState[2];
    ULONG_PTR AlpcMessageId;
    union
    {
        PVOID AlpcMessage;
        ULONG AlpcReceiveAttributeSet;
    };
    LIST_ENTRY AlpcWaitListEntry;
    NTSTATUS ExitStatus;
    ULONG CacheManagerCount;
#elif (NTDDI_VERSION >= NTDDI_LONGHORN)
    ULONG AlpcMessageId;
    union
    {
        PVOID AlpcMessage;
        ULONG AlpcReceiveAttributeSet;
    };
    LIST_ENTRY AlpcWaitListEntry;
    KSEMAPHORE AlpcWaitSemaphore;
    ULONG CacheManagerCount;
#endif
    // TODO: Missing Vista+ members
#if (NTDDI_VERSION >= NTDDI_WIN10_RS1) || defined(__REACTOS__)
    PUNICODE_STRING ThreadName;
    // TODO: Missing Win10+ members
#endif
#if (NTDDI_VERSION >= NTDDI_WIN10_RS3) || defined(__REACTOS__)
    ULONG PowerThrottlingControlMask;
    ULONG PowerThrottlingStateMask;
#endif
#if defined(_M_ARM64)
    volatile LONG ExecutableWriteAllowed;
#endif
#if defined(__REACTOS__)
    CHAR Win32kPriorityFloor;
    volatile LONG DynamicCodeOptOut;
#endif
} ETHREAD;

#if defined(_M_ARM64) && (NTDDI_VERSION >= NTDDI_WIN10)
C_ASSERT(FIELD_OFFSET(ETHREAD, LegacyPowerObject) == 0x550);
#endif

typedef enum _PS_PROTECTED_TYPE
{
    PsProtectedTypeNone = 0,
    PsProtectedTypeProtectedLight = 1,
    PsProtectedTypeProtected = 2,
    PsProtectedTypeMax = 3
} PS_PROTECTED_TYPE;

typedef enum _PS_PROTECTED_SIGNER
{
    PsProtectedSignerNone = 0,
    PsProtectedSignerAuthenticode = 1,
    PsProtectedSignerCodeGen = 2,
    PsProtectedSignerAntimalware = 3,
    PsProtectedSignerLsa = 4,
    PsProtectedSignerWindows = 5,
    PsProtectedSignerWinTcb = 6,
    PsProtectedSignerWinSystem = 7,
    PsProtectedSignerApp = 8,
    PsProtectedSignerMax = 9
} PS_PROTECTED_SIGNER;

typedef struct _PS_PROTECTION
{
    union
    {
        UCHAR Level;
        struct
        {
            UCHAR Type:3;
            UCHAR Audit:1;
            UCHAR Signer:4;
        };
    };
} PS_PROTECTION, *PPS_PROTECTION;

typedef enum _SYSTEM_DLL_TYPE
{
    PsNativeSystemDll = 0,
    PsWowX86SystemDll = 1,
    PsWowChpeX86SystemDll = 2,
    PsChpeV2SystemDll = 3,
    PsVsmEnclaveRuntimeDll = 4,
    PsTrustedAppsRuntimeDll = 5,
    PsSystemDllTotalTypes = 6
} SYSTEM_DLL_TYPE;

typedef struct _EWOW64PROCESS
{
    PVOID Peb;
    SYSTEM_DLL_TYPE NtdllType;
    PVOID KernelWriteToExecutableSignal;
} EWOW64PROCESS, *PEWOW64PROCESS;

typedef struct _ALPC_PROCESS_CONTEXT
{
    EX_PUSH_LOCK Lock;
    LIST_ENTRY ViewListHead;
    volatile ULONG_PTR PagedPoolQuotaCache;
} ALPC_PROCESS_CONTEXT, *PALPC_PROCESS_CONTEXT;

typedef union _PS_INTERLOCKED_TIMER_DELAY_VALUES
{
    struct
    {
        ULONGLONG DelayMs:30;
        ULONGLONG CoalescingWindowMs:30;
        ULONGLONG Reserved:1;
        ULONGLONG NewTimerWheel:1;
        ULONGLONG Retry:1;
        ULONGLONG Locked:1;
    };
    ULONGLONG All;
} PS_INTERLOCKED_TIMER_DELAY_VALUES, *PPS_INTERLOCKED_TIMER_DELAY_VALUES;

typedef struct _WNF_STATE_NAME
{
    ULONG Data[2];
} WNF_STATE_NAME, *PWNF_STATE_NAME;

typedef struct _JOBOBJECT_WAKE_FILTER
{
    ULONG HighEdgeFilter;
    ULONG LowEdgeFilter;
} JOBOBJECT_WAKE_FILTER, *PJOBOBJECT_WAKE_FILTER;

typedef struct _PS_PROCESS_WAKE_INFORMATION
{
    ULONGLONG NotificationChannel;
    ULONG WakeCounters[7];
    JOBOBJECT_WAKE_FILTER WakeFilter;
    ULONG NoWakeCounter;
} PS_PROCESS_WAKE_INFORMATION, *PPS_PROCESS_WAKE_INFORMATION;

typedef struct _PS_DYNAMIC_ENFORCED_ADDRESS_RANGES
{
    RTL_AVL_TREE Tree;
    EX_PUSH_LOCK Lock;
} PS_DYNAMIC_ENFORCED_ADDRESS_RANGES, *PPS_DYNAMIC_ENFORCED_ADDRESS_RANGES;

typedef union _PROCESS_EXECUTION_TRANSITION
{
    volatile SHORT TransitionState;
    struct
    {
        USHORT InProgress:1;
        USHORT Reserved:7;
    };
} PROCESS_EXECUTION_TRANSITION, *PPROCESS_EXECUTION_TRANSITION;

typedef union _PROCESS_EXECUTION_STATE
{
    CHAR State;
    struct
    {
        UCHAR ProcessFrozen:1;
        UCHAR ProcessSwapped:1;
        UCHAR ProcessGraphicsFreezeOptimized:1;
        UCHAR Reserved:5;
    };
} PROCESS_EXECUTION_STATE, *PPROCESS_EXECUTION_STATE;

typedef union _PROCESS_EXECUTION
{
    volatile LONG State;
    struct
    {
        volatile PROCESS_EXECUTION_TRANSITION Transition;
        PROCESS_EXECUTION_STATE Current;
        PROCESS_EXECUTION_STATE Requested;
    };
} PROCESS_EXECUTION, *PPROCESS_EXECUTION;

//
// Executive Process (EPROCESS)
//
typedef struct _EPROCESS
{
    KPROCESS Pcb;
    EX_PUSH_LOCK ProcessLock;
    HANDLE UniqueProcessId;
    LIST_ENTRY ActiveProcessLinks;
    EX_RUNDOWN_REF RundownProtect;
    union
    {
        ULONG Flags2;
        struct
        {
            ULONG JobNotReallyActive : 1;
            ULONG AccountingFolded : 1;
            ULONG NewProcessReported : 1;
            ULONG ExitProcessReported : 1;
            ULONG ReportCommitChanges : 1;
            ULONG LastReportMemory : 1;
            ULONG ForceWakeCharge : 1;
            ULONG CrossSessionCreate : 1;
            ULONG NeedsHandleRundown : 1;
            ULONG RefTraceEnabled : 1;
            ULONG PicoCreated : 1;
            ULONG EmptyJobEvaluated : 1;
            ULONG DefaultPagePriority : 3;
            ULONG PrimaryTokenFrozen : 1;
            ULONG ProcessVerifierTarget : 1;
            ULONG RestrictSetThreadContext : 1;
            ULONG AffinityPermanent : 1;
            ULONG AffinityUpdateEnable : 1;
            ULONG PropagateNode : 1;
            ULONG ExplicitAffinity : 1;
            ULONG Flags2Available1 : 2;
            ULONG EnableReadVmLogging : 1;
            ULONG EnableWriteVmLogging : 1;
            ULONG FatalAccessTerminationRequested : 1;
            ULONG DisableSystemAllowedCpuSet : 1;
            ULONG Flags2Available2 : 3;
            ULONG InPrivate : 1;
        };
    };
    union
    {
        ULONG Flags;
        struct
        {
            ULONG CreateReported : 1;
            ULONG NoDebugInherit : 1;
            ULONG ProcessExiting : 1;
            ULONG ProcessDelete : 1;
            ULONG ManageExecutableMemoryWrites : 1;
            ULONG VmDeleted : 1;
            ULONG OutswapEnabled : 1;
            ULONG Outswapped : 1;
            ULONG FailFastOnCommitFail : 1;
            ULONG Wow64VaSpace4Gb : 1;
            ULONG AddressSpaceInitialized : 2;
            ULONG SetTimerResolution : 1;
            ULONG BreakOnTermination : 1;
            ULONG DeprioritizeViews : 1;
            ULONG WriteWatch : 1;
            ULONG ProcessInSession : 1;
            ULONG OverrideAddressSpace : 1;
            ULONG HasAddressSpace : 1;
            ULONG LaunchPrefetched : 1;
            ULONG Reserved : 1;
            ULONG VmTopDown : 1;
            ULONG ImageNotifyDone : 1;
            ULONG PdeUpdateNeeded : 1;
            ULONG VdmAllowed : 1;
            ULONG ProcessRundown : 1;
            ULONG ProcessInserted : 1;
            ULONG DefaultIoPriority : 3;
            ULONG ProcessSelfDelete : 1;
            ULONG SetTimerResolutionLink : 1;
        };
    };
    LARGE_INTEGER CreateTime;
    SIZE_T ProcessQuotaUsage[2];
    SIZE_T ProcessQuotaPeak[2];
    SIZE_T PeakVirtualSize;
    SIZE_T VirtualSize;
    LIST_ENTRY SessionProcessLinks;
    union
    {
        PVOID ExceptionPortData;
        ULONG_PTR ExceptionPortValue;
        ULONG_PTR ExceptionPortState : 3;
    };
    EX_FAST_REF Token;
    ULONG_PTR MmReserved;
    EX_PUSH_LOCK AddressCreationLock;
    EX_PUSH_LOCK PageTableCommitmentLock;
    struct _ETHREAD *RotateInProgress;
    struct _ETHREAD *ForkInProgress;
    struct _EJOB *CommitChargeJob;
    RTL_AVL_TREE CloneRoot;
    volatile ULONG_PTR NumberOfPrivatePages;
    volatile ULONG_PTR NumberOfLockedPages;
    PVOID Win32Process;
    struct _EJOB *Job;
    PVOID SectionObject;
    PVOID SectionBaseAddress;
    ULONG Cookie;
    struct _PAGEFAULT_HISTORY *WorkingSetWatch;
    PVOID Win32WindowStation;
    HANDLE InheritedFromUniqueProcessId;
    volatile ULONG_PTR OwnerProcessId;
    struct _PEB *Peb;
    struct _PSP_SESSION_SPACE *Session;
    PVOID Spare1;
    struct _EPROCESS_QUOTA_BLOCK *QuotaBlock;
    struct _HANDLE_TABLE *ObjectTable;
    PVOID DebugPort;
    struct _EWOW64PROCESS *WoW64Process;
    EX_FAST_REF DeviceMap;
    PVOID EtwDataSource;
    ULONGLONG PageDirectoryPte;
    struct _FILE_OBJECT *ImageFilePointer;
    UCHAR ImageFileName[15];
    UCHAR PriorityClass;
    PVOID SecurityPort;
    SE_AUDIT_PROCESS_CREATION_INFO SeAuditProcessCreationInfo;
    LIST_ENTRY JobLinks;
    PVOID HighestUserAddress;
    LIST_ENTRY ThreadListHead;
    volatile ULONG ActiveThreads;
    ULONG ImagePathHash;
    ULONG DefaultHardErrorProcessing;
    NTSTATUS LastThreadExitStatus;
    EX_FAST_REF PrefetchTrace;
    PVOID LockedPagesList;
    LARGE_INTEGER ReadOperationCount;
    LARGE_INTEGER WriteOperationCount;
    LARGE_INTEGER OtherOperationCount;
    LARGE_INTEGER ReadTransferCount;
    LARGE_INTEGER WriteTransferCount;
    LARGE_INTEGER OtherTransferCount;
    SIZE_T CommitChargeLimit;
    volatile SIZE_T CommitCharge;
    volatile SIZE_T CommitChargePeak;
    MMSUPPORT_FULL Vm;
    LIST_ENTRY MmProcessLinks;
    volatile ULONG ModifiedPageCount;
    NTSTATUS ExitStatus;
    RTL_AVL_TREE VadRoot;
    PVOID VadHint;
    ULONG_PTR VadCount;
    volatile ULONG_PTR VadPhysicalPages;
    ULONG_PTR VadPhysicalPagesLimit;
    ALPC_PROCESS_CONTEXT AlpcContext;
    LIST_ENTRY TimerResolutionLink;
    struct _PO_DIAG_STACK_RECORD *TimerResolutionStackRecord;
    ULONG RequestedTimerResolution;
    ULONG SmallestTimerResolution;
    LARGE_INTEGER ExitTime;
    struct _INVERTED_FUNCTION_TABLE_KERNEL_MODE *InvertedFunctionTable;
    EX_PUSH_LOCK InvertedFunctionTableLock;
    ULONG ActiveThreadsHighWatermark;
    ULONG LargePrivateVadCount;
    EX_PUSH_LOCK ThreadListLock;
    PVOID WnfContext;
    struct _EJOB *ServerSilo;
    UCHAR SignatureLevel;
    UCHAR SectionSignatureLevel;
    PS_PROTECTION Protection;
    UCHAR HangCount : 3;
    UCHAR GhostCount : 3;
    UCHAR PrefilterException : 1;
    union
    {
        ULONG Flags3;
        struct
        {
            ULONG Minimal : 1;
            ULONG ReplacingPageRoot : 1;
            ULONG Crashed : 1;
            ULONG JobVadsAreTracked : 1;
            ULONG VadTrackingDisabled : 1;
            ULONG AuxiliaryProcess : 1;
            ULONG SubsystemProcess : 1;
            ULONG IndirectCpuSets : 1;
            ULONG RelinquishedCommit : 1;
            ULONG HighGraphicsPriority : 1;
            ULONG CommitFailLogged : 1;
            ULONG ReserveFailLogged : 1;
            ULONG SystemProcess : 1;
            ULONG AllImagesAtBasePristineBase : 1;
            ULONG AddressPolicyFrozen : 1;
            ULONG ProcessFirstResume : 1;
            ULONG ForegroundExternal : 1;
            ULONG ForegroundSystem : 1;
            ULONG HighMemoryPriority : 1;
            ULONG EnableProcessSuspendResumeLogging : 1;
            ULONG EnableThreadSuspendResumeLogging : 1;
            ULONG SecurityDomainChanged : 1;
            ULONG SecurityFreezeComplete : 1;
            ULONG VmProcessorHost : 1;
            ULONG VmProcessorHostTransition : 1;
            ULONG AltSyscall : 1;
            ULONG TimerResolutionIgnore : 1;
            ULONG DisallowUserTerminate : 1;
            ULONG EnableProcessRemoteExecProtectVmLogging : 1;
            ULONG EnableProcessLocalExecProtectVmLogging : 1;
            ULONG MemoryCompressionProcess : 1;
            ULONG EnableProcessImpersonationLogging : 1;
        };
    };
    LONG DeviceAsid;
    PVOID SvmData;
    EX_PUSH_LOCK SvmProcessLock;
    KSPIN_LOCK SvmLock;
    LIST_ENTRY SvmProcessDeviceListHead;
    ULONGLONG LastFreezeInterruptTime;
    struct _PROCESS_DISK_COUNTERS *DiskCounters;
    PVOID PicoContext;
    PVOID EnclaveTable;
    ULONG_PTR EnclaveNumber;
    EX_PUSH_LOCK EnclaveLock;
    ULONG HighPriorityFaultsAllowed;
    struct _PO_PROCESS_ENERGY_CONTEXT *EnergyContext;
    PVOID VmContext;
    ULONGLONG SequenceNumber;
    ULONGLONG CreateInterruptTime;
    ULONGLONG CreateUnbiasedInterruptTime;
    ULONGLONG TotalUnbiasedFrozenTime;
    ULONGLONG LastAppStateUpdateTime;
    ULONGLONG LastAppStateUptime : 61;
    ULONGLONG LastAppState : 3;
    volatile ULONG_PTR SharedCommitCharge;
    EX_PUSH_LOCK SharedCommitLock;
    LIST_ENTRY SharedCommitLinks;
    union
    {
        struct
        {
            ULONGLONG AllowedCpuSets;
            ULONGLONG DefaultCpuSets;
        };
        struct
        {
            PULONGLONG AllowedCpuSetsIndirect;
            PULONGLONG DefaultCpuSetsIndirect;
        };
    };
    PVOID DiskIoAttribution;
    PVOID DxgProcess;
    ULONG Win32KFilterSet;
    USHORT Machine;
    UCHAR MmSlabIdentity;
    UCHAR Spare0;
    volatile PS_INTERLOCKED_TIMER_DELAY_VALUES ProcessTimerDelay;
    volatile ULONG KTimerSets;
    volatile ULONG KTimer2Sets;
    volatile ULONG ThreadTimerSets;
    KSPIN_LOCK VirtualTimerListLock;
    LIST_ENTRY VirtualTimerListHead;
    union
    {
        WNF_STATE_NAME WakeChannel;
        PS_PROCESS_WAKE_INFORMATION WakeInfo;
    };
    union
    {
        ULONG MitigationFlags;
        struct
        {
            ULONG ControlFlowGuardEnabled : 1;
            ULONG ControlFlowGuardExportSuppressionEnabled : 1;
            ULONG ControlFlowGuardStrict : 1;
            ULONG DisallowStrippedImages : 1;
            ULONG ForceRelocateImages : 1;
            ULONG HighEntropyASLREnabled : 1;
            ULONG StackRandomizationDisabled : 1;
            ULONG ExtensionPointDisable : 1;
            ULONG DisableDynamicCode : 1;
            ULONG DisableDynamicCodeAllowOptOut : 1;
            ULONG DisableDynamicCodeAllowRemoteDowngrade : 1;
            ULONG AuditDisableDynamicCode : 1;
            ULONG DisallowWin32kSystemCalls : 1;
            ULONG AuditDisallowWin32kSystemCalls : 1;
            ULONG EnableFilteredWin32kAPIs : 1;
            ULONG AuditFilteredWin32kAPIs : 1;
            ULONG DisableNonSystemFonts : 1;
            ULONG AuditNonSystemFontLoading : 1;
            ULONG PreferSystem32Images : 1;
            ULONG ProhibitRemoteImageMap : 1;
            ULONG AuditProhibitRemoteImageMap : 1;
            ULONG ProhibitLowILImageMap : 1;
            ULONG AuditProhibitLowILImageMap : 1;
            ULONG SignatureMitigationOptIn : 1;
            ULONG AuditBlockNonMicrosoftBinaries : 1;
            ULONG AuditBlockNonMicrosoftBinariesAllowStore : 1;
            ULONG LoaderIntegrityContinuityEnabled : 1;
            ULONG AuditLoaderIntegrityContinuity : 1;
            ULONG EnableModuleTamperingProtection : 1;
            ULONG EnableModuleTamperingProtectionNoInherit : 1;
            ULONG RestrictIndirectBranchPrediction : 1;
            ULONG IsolateSecurityDomain : 1;
        } MitigationFlagsValues;
    };
    union
    {
        ULONG MitigationFlags2;
        struct
        {
            ULONG EnableExportAddressFilter : 1;
            ULONG AuditExportAddressFilter : 1;
            ULONG EnableExportAddressFilterPlus : 1;
            ULONG AuditExportAddressFilterPlus : 1;
            ULONG EnableRopStackPivot : 1;
            ULONG AuditRopStackPivot : 1;
            ULONG EnableRopCallerCheck : 1;
            ULONG AuditRopCallerCheck : 1;
            ULONG EnableRopSimExec : 1;
            ULONG AuditRopSimExec : 1;
            ULONG EnableImportAddressFilter : 1;
            ULONG AuditImportAddressFilter : 1;
            ULONG DisablePageCombine : 1;
            ULONG SpeculativeStoreBypassDisable : 1;
            ULONG CetUserShadowStacks : 1;
            ULONG AuditCetUserShadowStacks : 1;
            ULONG AuditCetUserShadowStacksLogged : 1;
            ULONG UserCetSetContextIpValidation : 1;
            ULONG AuditUserCetSetContextIpValidation : 1;
            ULONG AuditUserCetSetContextIpValidationLogged : 1;
            ULONG CetUserShadowStacksStrictMode : 1;
            ULONG BlockNonCetBinaries : 1;
            ULONG BlockNonCetBinariesNonEhcont : 1;
            ULONG AuditBlockNonCetBinaries : 1;
            ULONG AuditBlockNonCetBinariesLogged : 1;
            ULONG XtendedControlFlowGuard_Deprecated : 1;
            ULONG AuditXtendedControlFlowGuard_Deprecated : 1;
            ULONG PointerAuthUserIp : 1;
            ULONG AuditPointerAuthUserIp : 1;
            ULONG AuditPointerAuthUserIpLogged : 1;
            ULONG CetDynamicApisOutOfProcOnly : 1;
            ULONG UserCetSetContextIpValidationRelaxedMode : 1;
        } MitigationFlags2Values;
    };
    PVOID PartitionObject;
    ULONGLONG SecurityDomain;
    ULONGLONG ParentSecurityDomain;
    PVOID CoverageSamplerContext;
    PVOID MmHotPatchContext;
    RTL_AVL_TREE DynamicEHContinuationTargetsTree;
    EX_PUSH_LOCK DynamicEHContinuationTargetsLock;
    ULONGLONG PointerAuthUserIpKey[2];
    PS_DYNAMIC_ENFORCED_ADDRESS_RANGES DynamicEnforcedCetCompatibleRanges;
    ULONG DisabledComponentFlags;
    volatile LONG PageCombineSequence;
    PULONG PathRedirectionHashes;
    PVOID SyscallProviderReserved[4];
    union
    {
        ULONG MitigationFlags3;
        struct
        {
            ULONG RestrictCoreSharing : 1;
            ULONG DisallowFsctlSystemCalls : 1;
            ULONG AuditDisallowFsctlSystemCalls : 1;
            ULONG MitigationFlags3Spare : 29;
        } MitigationFlags3Values;
    };
    union
    {
        ULONG Flags4;
        struct
        {
            ULONG ThreadWasActive : 1;
            ULONG MinimalTerminate : 1;
            ULONG ImageExpansionDisable : 1;
            ULONG SessionFirstProcess : 1;
        };
    };
    union
    {
        ULONG SyscallUsage;
        struct
        {
            ULONG SystemModuleInformation : 1;
            ULONG SystemModuleInformationEx : 1;
            ULONG SystemLocksInformation : 1;
            ULONG SystemStackTraceInformation : 1;
            ULONG SystemHandleInformation : 1;
            ULONG SystemExtendedHandleInformation : 1;
            ULONG SystemObjectInformation : 1;
            ULONG SystemBigPoolInformation : 1;
            ULONG SystemExtendedProcessInformation : 1;
            ULONG SystemSessionProcessInformation : 1;
            ULONG SystemMemoryTopologyInformation : 1;
            ULONG SystemMemoryChannelInformation : 1;
            ULONG SystemUnused : 1;
            ULONG SystemPlatformBinaryInformation : 1;
            ULONG SystemFirmwareTableInformation : 1;
            ULONG SystemBootMetadataInformation : 1;
            ULONG SystemWheaIpmiHardwareInformation : 1;
            ULONG SystemSuperfetchPrefetch : 1;
            ULONG SystemSuperfetchPfnQuery : 1;
            ULONG SystemSuperfetchPrivSourceQuery : 1;
            ULONG SystemSuperfetchMemoryListQuery : 1;
            ULONG SystemSuperfetchMemoryRangesQuery : 1;
            ULONG SystemSuperfetchPfnSetPriority : 1;
            ULONG SystemSuperfetchMovePages : 1;
            ULONG SystemSuperfetchPfnSetPageHeat : 1;
            ULONG SysDbgGetTriageDump : 1;
            ULONG SysDbgGetLiveKernelDump : 1;
            ULONG SyscallUsageValuesSpare : 5;
        } SyscallUsageValues;
    };
    LONG SupervisorDeviceAsid;
    PVOID SupervisorSvmData;
    struct _PROCESS_NETWORK_COUNTERS *NetworkCounters;
    PROCESS_EXECUTION Execution;
    PVOID ThreadIndexTable;
#if defined(_M_IX86)
    PVOID LdtInformation;
    PVOID VdmObjects;
#endif
} EPROCESS;

#if defined(_M_ARM64) && !defined(__ASSEMBLER__)
C_ASSERT(sizeof(EPROCESS) == 0x900);
C_ASSERT(FIELD_OFFSET(EPROCESS, Pcb) == 0x000);
C_ASSERT(FIELD_OFFSET(EPROCESS, ProcessLock) == 0x1B8);
C_ASSERT(FIELD_OFFSET(EPROCESS, UniqueProcessId) == 0x1C0);
C_ASSERT(FIELD_OFFSET(EPROCESS, ActiveProcessLinks) == 0x1C8);
C_ASSERT(FIELD_OFFSET(EPROCESS, RundownProtect) == 0x1D8);
C_ASSERT(FIELD_OFFSET(EPROCESS, Flags2) == 0x1E0);
C_ASSERT(FIELD_OFFSET(EPROCESS, Flags) == 0x1E4);
C_ASSERT(FIELD_OFFSET(EPROCESS, CreateTime) == 0x1E8);
C_ASSERT(FIELD_OFFSET(EPROCESS, ProcessQuotaUsage) == 0x1F0);
C_ASSERT(FIELD_OFFSET(EPROCESS, ProcessQuotaPeak) == 0x200);
C_ASSERT(FIELD_OFFSET(EPROCESS, PeakVirtualSize) == 0x210);
C_ASSERT(FIELD_OFFSET(EPROCESS, VirtualSize) == 0x218);
C_ASSERT(FIELD_OFFSET(EPROCESS, SessionProcessLinks) == 0x220);
C_ASSERT(FIELD_OFFSET(EPROCESS, ExceptionPortData) == 0x230);
C_ASSERT(FIELD_OFFSET(EPROCESS, ExceptionPortValue) == 0x230);
C_ASSERT(FIELD_OFFSET(EPROCESS, Token) == 0x238);
C_ASSERT(FIELD_OFFSET(EPROCESS, MmReserved) == 0x240);
C_ASSERT(FIELD_OFFSET(EPROCESS, AddressCreationLock) == 0x248);
C_ASSERT(FIELD_OFFSET(EPROCESS, PageTableCommitmentLock) == 0x250);
C_ASSERT(FIELD_OFFSET(EPROCESS, RotateInProgress) == 0x258);
C_ASSERT(FIELD_OFFSET(EPROCESS, ForkInProgress) == 0x260);
C_ASSERT(FIELD_OFFSET(EPROCESS, CommitChargeJob) == 0x268);
C_ASSERT(FIELD_OFFSET(EPROCESS, CloneRoot) == 0x270);
C_ASSERT(FIELD_OFFSET(EPROCESS, NumberOfPrivatePages) == 0x278);
C_ASSERT(FIELD_OFFSET(EPROCESS, NumberOfLockedPages) == 0x280);
C_ASSERT(FIELD_OFFSET(EPROCESS, Win32Process) == 0x288);
C_ASSERT(FIELD_OFFSET(EPROCESS, Job) == 0x290);
C_ASSERT(FIELD_OFFSET(EPROCESS, SectionObject) == 0x298);
C_ASSERT(FIELD_OFFSET(EPROCESS, SectionBaseAddress) == 0x2A0);
C_ASSERT(FIELD_OFFSET(EPROCESS, Cookie) == 0x2A8);
C_ASSERT(FIELD_OFFSET(EPROCESS, WorkingSetWatch) == 0x2B0);
C_ASSERT(FIELD_OFFSET(EPROCESS, Win32WindowStation) == 0x2B8);
C_ASSERT(FIELD_OFFSET(EPROCESS, InheritedFromUniqueProcessId) == 0x2C0);
C_ASSERT(FIELD_OFFSET(EPROCESS, OwnerProcessId) == 0x2C8);
C_ASSERT(FIELD_OFFSET(EPROCESS, Peb) == 0x2D0);
C_ASSERT(FIELD_OFFSET(EPROCESS, Session) == 0x2D8);
C_ASSERT(FIELD_OFFSET(EPROCESS, Spare1) == 0x2E0);
C_ASSERT(FIELD_OFFSET(EPROCESS, QuotaBlock) == 0x2E8);
C_ASSERT(FIELD_OFFSET(EPROCESS, ObjectTable) == 0x2F0);
C_ASSERT(FIELD_OFFSET(EPROCESS, DebugPort) == 0x2F8);
C_ASSERT(FIELD_OFFSET(EPROCESS, WoW64Process) == 0x300);
C_ASSERT(FIELD_OFFSET(EPROCESS, DeviceMap) == 0x308);
C_ASSERT(FIELD_OFFSET(EPROCESS, EtwDataSource) == 0x310);
C_ASSERT(FIELD_OFFSET(EPROCESS, PageDirectoryPte) == 0x318);
C_ASSERT(FIELD_OFFSET(EPROCESS, ImageFilePointer) == 0x320);
C_ASSERT(FIELD_OFFSET(EPROCESS, ImageFileName) == 0x328);
C_ASSERT(FIELD_OFFSET(EPROCESS, PriorityClass) == 0x337);
C_ASSERT(FIELD_OFFSET(EPROCESS, SecurityPort) == 0x338);
C_ASSERT(FIELD_OFFSET(EPROCESS, SeAuditProcessCreationInfo) == 0x340);
C_ASSERT(FIELD_OFFSET(EPROCESS, JobLinks) == 0x348);
C_ASSERT(FIELD_OFFSET(EPROCESS, HighestUserAddress) == 0x358);
C_ASSERT(FIELD_OFFSET(EPROCESS, ThreadListHead) == 0x360);
C_ASSERT(FIELD_OFFSET(EPROCESS, ActiveThreads) == 0x370);
C_ASSERT(FIELD_OFFSET(EPROCESS, ImagePathHash) == 0x374);
C_ASSERT(FIELD_OFFSET(EPROCESS, DefaultHardErrorProcessing) == 0x378);
C_ASSERT(FIELD_OFFSET(EPROCESS, LastThreadExitStatus) == 0x37C);
C_ASSERT(FIELD_OFFSET(EPROCESS, PrefetchTrace) == 0x380);
C_ASSERT(FIELD_OFFSET(EPROCESS, LockedPagesList) == 0x388);
C_ASSERT(FIELD_OFFSET(EPROCESS, ReadOperationCount) == 0x390);
C_ASSERT(FIELD_OFFSET(EPROCESS, WriteOperationCount) == 0x398);
C_ASSERT(FIELD_OFFSET(EPROCESS, OtherOperationCount) == 0x3A0);
C_ASSERT(FIELD_OFFSET(EPROCESS, ReadTransferCount) == 0x3A8);
C_ASSERT(FIELD_OFFSET(EPROCESS, WriteTransferCount) == 0x3B0);
C_ASSERT(FIELD_OFFSET(EPROCESS, OtherTransferCount) == 0x3B8);
C_ASSERT(FIELD_OFFSET(EPROCESS, CommitChargeLimit) == 0x3C0);
C_ASSERT(FIELD_OFFSET(EPROCESS, CommitCharge) == 0x3C8);
C_ASSERT(FIELD_OFFSET(EPROCESS, CommitChargePeak) == 0x3D0);
C_ASSERT(FIELD_OFFSET(EPROCESS, Vm) == 0x400);
C_ASSERT(FIELD_OFFSET(EPROCESS, MmProcessLinks) == 0x600);
C_ASSERT(FIELD_OFFSET(EPROCESS, ModifiedPageCount) == 0x610);
C_ASSERT(FIELD_OFFSET(EPROCESS, ExitStatus) == 0x614);
C_ASSERT(FIELD_OFFSET(EPROCESS, VadRoot) == 0x618);
C_ASSERT(FIELD_OFFSET(EPROCESS, VadHint) == 0x620);
C_ASSERT(FIELD_OFFSET(EPROCESS, VadCount) == 0x628);
C_ASSERT(FIELD_OFFSET(EPROCESS, VadPhysicalPages) == 0x630);
C_ASSERT(FIELD_OFFSET(EPROCESS, VadPhysicalPagesLimit) == 0x638);
C_ASSERT(FIELD_OFFSET(EPROCESS, AlpcContext) == 0x640);
C_ASSERT(FIELD_OFFSET(EPROCESS, TimerResolutionLink) == 0x660);
C_ASSERT(FIELD_OFFSET(EPROCESS, TimerResolutionStackRecord) == 0x670);
C_ASSERT(FIELD_OFFSET(EPROCESS, RequestedTimerResolution) == 0x678);
C_ASSERT(FIELD_OFFSET(EPROCESS, SmallestTimerResolution) == 0x67C);
C_ASSERT(FIELD_OFFSET(EPROCESS, ExitTime) == 0x680);
C_ASSERT(FIELD_OFFSET(EPROCESS, InvertedFunctionTable) == 0x688);
C_ASSERT(FIELD_OFFSET(EPROCESS, InvertedFunctionTableLock) == 0x690);
C_ASSERT(FIELD_OFFSET(EPROCESS, ActiveThreadsHighWatermark) == 0x698);
C_ASSERT(FIELD_OFFSET(EPROCESS, LargePrivateVadCount) == 0x69C);
C_ASSERT(FIELD_OFFSET(EPROCESS, ThreadListLock) == 0x6A0);
C_ASSERT(FIELD_OFFSET(EPROCESS, WnfContext) == 0x6A8);
C_ASSERT(FIELD_OFFSET(EPROCESS, ServerSilo) == 0x6B0);
C_ASSERT(FIELD_OFFSET(EPROCESS, SignatureLevel) == 0x6B8);
C_ASSERT(FIELD_OFFSET(EPROCESS, SectionSignatureLevel) == 0x6B9);
C_ASSERT(FIELD_OFFSET(EPROCESS, Protection) == 0x6BA);
C_ASSERT(FIELD_OFFSET(EPROCESS, Flags3) == 0x6BC);
C_ASSERT(FIELD_OFFSET(EPROCESS, DeviceAsid) == 0x6C0);
C_ASSERT(FIELD_OFFSET(EPROCESS, SvmData) == 0x6C8);
C_ASSERT(FIELD_OFFSET(EPROCESS, SvmProcessLock) == 0x6D0);
C_ASSERT(FIELD_OFFSET(EPROCESS, SvmLock) == 0x6D8);
C_ASSERT(FIELD_OFFSET(EPROCESS, SvmProcessDeviceListHead) == 0x6E0);
C_ASSERT(FIELD_OFFSET(EPROCESS, LastFreezeInterruptTime) == 0x6F0);
C_ASSERT(FIELD_OFFSET(EPROCESS, DiskCounters) == 0x6F8);
C_ASSERT(FIELD_OFFSET(EPROCESS, PicoContext) == 0x700);
C_ASSERT(FIELD_OFFSET(EPROCESS, EnclaveTable) == 0x708);
C_ASSERT(FIELD_OFFSET(EPROCESS, EnclaveNumber) == 0x710);
C_ASSERT(FIELD_OFFSET(EPROCESS, EnclaveLock) == 0x718);
C_ASSERT(FIELD_OFFSET(EPROCESS, HighPriorityFaultsAllowed) == 0x720);
C_ASSERT(FIELD_OFFSET(EPROCESS, EnergyContext) == 0x728);
C_ASSERT(FIELD_OFFSET(EPROCESS, VmContext) == 0x730);
C_ASSERT(FIELD_OFFSET(EPROCESS, SequenceNumber) == 0x738);
C_ASSERT(FIELD_OFFSET(EPROCESS, CreateInterruptTime) == 0x740);
C_ASSERT(FIELD_OFFSET(EPROCESS, CreateUnbiasedInterruptTime) == 0x748);
C_ASSERT(FIELD_OFFSET(EPROCESS, TotalUnbiasedFrozenTime) == 0x750);
C_ASSERT(FIELD_OFFSET(EPROCESS, LastAppStateUpdateTime) == 0x758);
C_ASSERT(FIELD_OFFSET(EPROCESS, SharedCommitCharge) == 0x768);
C_ASSERT(FIELD_OFFSET(EPROCESS, SharedCommitLock) == 0x770);
C_ASSERT(FIELD_OFFSET(EPROCESS, SharedCommitLinks) == 0x778);
C_ASSERT(FIELD_OFFSET(EPROCESS, AllowedCpuSets) == 0x788);
C_ASSERT(FIELD_OFFSET(EPROCESS, DefaultCpuSets) == 0x790);
C_ASSERT(FIELD_OFFSET(EPROCESS, AllowedCpuSetsIndirect) == 0x788);
C_ASSERT(FIELD_OFFSET(EPROCESS, DefaultCpuSetsIndirect) == 0x790);
C_ASSERT(FIELD_OFFSET(EPROCESS, DiskIoAttribution) == 0x798);
C_ASSERT(FIELD_OFFSET(EPROCESS, DxgProcess) == 0x7A0);
C_ASSERT(FIELD_OFFSET(EPROCESS, Win32KFilterSet) == 0x7A8);
C_ASSERT(FIELD_OFFSET(EPROCESS, Machine) == 0x7AC);
C_ASSERT(FIELD_OFFSET(EPROCESS, MmSlabIdentity) == 0x7AE);
C_ASSERT(FIELD_OFFSET(EPROCESS, Spare0) == 0x7AF);
C_ASSERT(FIELD_OFFSET(EPROCESS, ProcessTimerDelay) == 0x7B0);
C_ASSERT(FIELD_OFFSET(EPROCESS, KTimerSets) == 0x7B8);
C_ASSERT(FIELD_OFFSET(EPROCESS, KTimer2Sets) == 0x7BC);
C_ASSERT(FIELD_OFFSET(EPROCESS, ThreadTimerSets) == 0x7C0);
C_ASSERT(FIELD_OFFSET(EPROCESS, VirtualTimerListLock) == 0x7C8);
C_ASSERT(FIELD_OFFSET(EPROCESS, VirtualTimerListHead) == 0x7D0);
C_ASSERT(FIELD_OFFSET(EPROCESS, WakeChannel) == 0x7E0);
C_ASSERT(FIELD_OFFSET(EPROCESS, WakeInfo) == 0x7E0);
C_ASSERT(FIELD_OFFSET(EPROCESS, MitigationFlags) == 0x810);
C_ASSERT(FIELD_OFFSET(EPROCESS, MitigationFlagsValues) == 0x810);
C_ASSERT(FIELD_OFFSET(EPROCESS, MitigationFlags2) == 0x814);
C_ASSERT(FIELD_OFFSET(EPROCESS, MitigationFlags2Values) == 0x814);
C_ASSERT(FIELD_OFFSET(EPROCESS, PartitionObject) == 0x818);
C_ASSERT(FIELD_OFFSET(EPROCESS, SecurityDomain) == 0x820);
C_ASSERT(FIELD_OFFSET(EPROCESS, ParentSecurityDomain) == 0x828);
C_ASSERT(FIELD_OFFSET(EPROCESS, CoverageSamplerContext) == 0x830);
C_ASSERT(FIELD_OFFSET(EPROCESS, MmHotPatchContext) == 0x838);
C_ASSERT(FIELD_OFFSET(EPROCESS, DynamicEHContinuationTargetsTree) == 0x840);
C_ASSERT(FIELD_OFFSET(EPROCESS, DynamicEHContinuationTargetsLock) == 0x848);
C_ASSERT(FIELD_OFFSET(EPROCESS, PointerAuthUserIpKey) == 0x850);
C_ASSERT(FIELD_OFFSET(EPROCESS, DynamicEnforcedCetCompatibleRanges) == 0x860);
C_ASSERT(FIELD_OFFSET(EPROCESS, DisabledComponentFlags) == 0x870);
C_ASSERT(FIELD_OFFSET(EPROCESS, PageCombineSequence) == 0x874);
C_ASSERT(FIELD_OFFSET(EPROCESS, PathRedirectionHashes) == 0x878);
C_ASSERT(FIELD_OFFSET(EPROCESS, SyscallProviderReserved) == 0x880);
C_ASSERT(FIELD_OFFSET(EPROCESS, MitigationFlags3) == 0x8A0);
C_ASSERT(FIELD_OFFSET(EPROCESS, MitigationFlags3Values) == 0x8A0);
C_ASSERT(FIELD_OFFSET(EPROCESS, Flags4) == 0x8A4);
C_ASSERT(FIELD_OFFSET(EPROCESS, SyscallUsage) == 0x8A8);
C_ASSERT(FIELD_OFFSET(EPROCESS, SyscallUsageValues) == 0x8A8);
C_ASSERT(FIELD_OFFSET(EPROCESS, SupervisorDeviceAsid) == 0x8AC);
C_ASSERT(FIELD_OFFSET(EPROCESS, SupervisorSvmData) == 0x8B0);
C_ASSERT(FIELD_OFFSET(EPROCESS, NetworkCounters) == 0x8B8);
C_ASSERT(FIELD_OFFSET(EPROCESS, Execution) == 0x8C0);
C_ASSERT(FIELD_OFFSET(EPROCESS, ThreadIndexTable) == 0x8C8);
#endif

//
// Job Token Filter Data
//
typedef struct _PS_JOB_TOKEN_FILTER
{
    ULONG CapturedSidCount;
    PSID_AND_ATTRIBUTES CapturedSids;
    ULONG CapturedSidsLength;
    ULONG CapturedGroupCount;
    PSID_AND_ATTRIBUTES CapturedGroups;
    ULONG CapturedGroupsLength;
    ULONG CapturedPrivilegeCount;
    PLUID_AND_ATTRIBUTES CapturedPrivileges;
    ULONG CapturedPrivilegesLength;
} PS_JOB_TOKEN_FILTER, *PPS_JOB_TOKEN_FILTER;

//
// Executive Job (EJOB)
//
typedef struct _EJOB
{
    KEVENT Event;
    LIST_ENTRY JobLinks;
    LIST_ENTRY ProcessListHead;
    ERESOURCE JobLock;
    LARGE_INTEGER TotalUserTime;
    LARGE_INTEGER TotalKernelTime;
    LARGE_INTEGER ThisPeriodTotalUserTime;
    LARGE_INTEGER ThisPeriodTotalKernelTime;
    ULONG TotalPageFaultCount;
    ULONG TotalProcesses;
    ULONG ActiveProcesses;
    ULONG TotalTerminatedProcesses;
    LARGE_INTEGER PerProcessUserTimeLimit;
    LARGE_INTEGER PerJobUserTimeLimit;
    ULONG LimitFlags;
    ULONG MinimumWorkingSetSize;
    ULONG MaximumWorkingSetSize;
    ULONG ActiveProcessLimit;
    KAFFINITY Affinity;
    UCHAR PriorityClass;
    ULONG UIRestrictionsClass;
    ULONG SecurityLimitFlags;
    PVOID Token;
    PPS_JOB_TOKEN_FILTER Filter;
    ULONG EndOfJobTimeAction;
    PVOID CompletionPort;
    PVOID CompletionKey;
    ULONG SessionId;
    ULONG SchedulingClass;
    ULONGLONG ReadOperationCount;
    ULONGLONG WriteOperationCount;
    ULONGLONG OtherOperationCount;
    ULONGLONG ReadTransferCount;
    ULONGLONG WriteTransferCount;
    ULONGLONG OtherTransferCount;
    IO_COUNTERS IoInfo;
    ULONG ProcessMemoryLimit;
    ULONG JobMemoryLimit;
    ULONG PeakProcessMemoryUsed;
    ULONG PeakJobMemoryUsed;
    ULONG CurrentJobMemoryUsed;
#if (NTDDI_VERSION >= NTDDI_WINXP) && (NTDDI_VERSION < NTDDI_WS03)
    FAST_MUTEX MemoryLimitsLock;
#elif (NTDDI_VERSION >= NTDDI_WS03) && (NTDDI_VERSION < NTDDI_LONGHORN)
    KGUARDED_MUTEX MemoryLimitsLock;
#elif (NTDDI_VERSION >= NTDDI_LONGHORN)
    EX_PUSH_LOCK MemoryLimitsLock;
#endif
    LIST_ENTRY JobSetLinks;
    ULONG MemberLevel;
    ULONG JobFlags;
} EJOB, *PEJOB;

//
// Job Information Structures for NtQueryInformationJobObject
//

typedef struct _JOBOBJECT_BASIC_ACCOUNTING_INFORMATION
{
    LARGE_INTEGER TotalUserTime;
    LARGE_INTEGER TotalKernelTime;
    LARGE_INTEGER ThisPeriodTotalUserTime;
    LARGE_INTEGER ThisPeriodTotalKernelTime;
    ULONG TotalPageFaultCount;
    ULONG TotalProcesses;
    ULONG ActiveProcesses;
    ULONG TotalTerminatedProcesses;
} JOBOBJECT_BASIC_ACCOUNTING_INFORMATION, *PJOBOBJECT_BASIC_ACCOUNTING_INFORMATION;

typedef struct _JOBOBJECT_BASIC_LIMIT_INFORMATION
{
    LARGE_INTEGER PerProcessUserTimeLimit;
    LARGE_INTEGER PerJobUserTimeLimit;
    ULONG LimitFlags;
    SIZE_T MinimumWorkingSetSize;
    SIZE_T MaximumWorkingSetSize;
    ULONG ActiveProcessLimit;
    ULONG_PTR Affinity;
    ULONG PriorityClass;
    ULONG SchedulingClass;
} JOBOBJECT_BASIC_LIMIT_INFORMATION, *PJOBOBJECT_BASIC_LIMIT_INFORMATION;

typedef struct _JOBOBJECT_BASIC_PROCESS_ID_LIST
{
    ULONG NumberOfAssignedProcesses;
    ULONG NumberOfProcessIdsInList;
    ULONG_PTR ProcessIdList[1];
} JOBOBJECT_BASIC_PROCESS_ID_LIST, *PJOBOBJECT_BASIC_PROCESS_ID_LIST;

typedef struct _JOBOBJECT_BASIC_UI_RESTRICTIONS
{
    ULONG UIRestrictionsClass;
} JOBOBJECT_BASIC_UI_RESTRICTIONS, *PJOBOBJECT_BASIC_UI_RESTRICTIONS;

typedef struct _JOBOBJECT_SECURITY_LIMIT_INFORMATION
{
    ULONG SecurityLimitFlags;
    HANDLE JobToken;
    PTOKEN_GROUPS SidsToDisable;
    PTOKEN_PRIVILEGES PrivilegesToDelete;
    PTOKEN_GROUPS RestrictedSids;
} JOBOBJECT_SECURITY_LIMIT_INFORMATION, *PJOBOBJECT_SECURITY_LIMIT_INFORMATION;

typedef struct _JOBOBJECT_END_OF_JOB_TIME_INFORMATION
{
    ULONG EndOfJobTimeAction;
} JOBOBJECT_END_OF_JOB_TIME_INFORMATION, PJOBOBJECT_END_OF_JOB_TIME_INFORMATION;

typedef struct _JOBOBJECT_ASSOCIATE_COMPLETION_PORT
{
    PVOID CompletionKey;
    HANDLE CompletionPort;
} JOBOBJECT_ASSOCIATE_COMPLETION_PORT, *PJOBOBJECT_ASSOCIATE_COMPLETION_PORT;

typedef struct JOBOBJECT_BASIC_AND_IO_ACCOUNTING_INFORMATION
{
    JOBOBJECT_BASIC_ACCOUNTING_INFORMATION BasicInfo;
    IO_COUNTERS IoInfo;
} JOBOBJECT_BASIC_AND_IO_ACCOUNTING_INFORMATION, *PJOBOBJECT_BASIC_AND_IO_ACCOUNTING_INFORMATION;

typedef struct _JOBOBJECT_EXTENDED_LIMIT_INFORMATION
{
    JOBOBJECT_BASIC_LIMIT_INFORMATION BasicLimitInformation;
    IO_COUNTERS IoInfo;
    SIZE_T ProcessMemoryLimit;
    SIZE_T JobMemoryLimit;
    SIZE_T PeakProcessMemoryUsed;
    SIZE_T PeakJobMemoryUsed;
} JOBOBJECT_EXTENDED_LIMIT_INFORMATION, *PJOBOBJECT_EXTENDED_LIMIT_INFORMATION;


//
// Win32K Callback Registration Data
//
typedef struct _WIN32_POWEREVENT_PARAMETERS
{
    PSPOWEREVENTTYPE EventNumber;
    ULONG Code;
} WIN32_POWEREVENT_PARAMETERS, *PWIN32_POWEREVENT_PARAMETERS;

typedef struct _WIN32_POWERSTATE_PARAMETERS
{
    UCHAR Promotion;
    POWER_ACTION SystemAction;
    SYSTEM_POWER_STATE MinSystemState;
    ULONG Flags;
    POWERSTATETASK PowerStateTask;
} WIN32_POWERSTATE_PARAMETERS, *PWIN32_POWERSTATE_PARAMETERS;

typedef struct _WIN32_JOBCALLOUT_PARAMETERS
{
    PVOID Job;
    PSW32JOBCALLOUTTYPE CalloutType;
    PVOID Data;
} WIN32_JOBCALLOUT_PARAMETERS, *PWIN32_JOBCALLOUT_PARAMETERS;

typedef struct _WIN32_OPENMETHOD_PARAMETERS
{
    OB_OPEN_REASON OpenReason;
    KPROCESSOR_MODE AccessMode;
    PEPROCESS Process;
    PVOID Object;
    PACCESS_MASK GrantedAccess;
    ULONG HandleCount;
} WIN32_OPENMETHOD_PARAMETERS, *PWIN32_OPENMETHOD_PARAMETERS;

typedef struct _WIN32_OKAYTOCLOSEMETHOD_PARAMETERS
{
    PEPROCESS Process;
    PVOID Object;
    HANDLE Handle;
    KPROCESSOR_MODE PreviousMode;
} WIN32_OKAYTOCLOSEMETHOD_PARAMETERS, *PWIN32_OKAYTOCLOSEMETHOD_PARAMETERS;

typedef struct _WIN32_CLOSEMETHOD_PARAMETERS
{
    PEPROCESS Process;
    PVOID Object;
    ULONG_PTR ProcessHandleCount;
    ULONG_PTR SystemHandleCount;
} WIN32_CLOSEMETHOD_PARAMETERS, *PWIN32_CLOSEMETHOD_PARAMETERS;

typedef struct _WIN32_DELETEMETHOD_PARAMETERS
{
    PVOID Object;
} WIN32_DELETEMETHOD_PARAMETERS, *PWIN32_DELETEMETHOD_PARAMETERS;

typedef struct _WIN32_PARSEMETHOD_PARAMETERS
{
    PVOID ParseObject;
    PVOID ObjectType;
    PACCESS_STATE AccessState;
    KPROCESSOR_MODE AccessMode;
    ULONG Attributes;
    _Out_ PUNICODE_STRING CompleteName;
    PUNICODE_STRING RemainingName;
    PVOID Context;
    PSECURITY_QUALITY_OF_SERVICE SecurityQos;
    PVOID *Object;
} WIN32_PARSEMETHOD_PARAMETERS, *PWIN32_PARSEMETHOD_PARAMETERS;

typedef struct _WIN32_CALLOUTS_FPNS
{
    PKWIN32_PROCESS_CALLOUT ProcessCallout;
    PKWIN32_THREAD_CALLOUT ThreadCallout;
    PKWIN32_GLOBALATOMTABLE_CALLOUT GlobalAtomTableCallout;
    PKWIN32_POWEREVENT_CALLOUT PowerEventCallout;
    PKWIN32_POWERSTATE_CALLOUT PowerStateCallout;
    PKWIN32_JOB_CALLOUT JobCallout;
    PGDI_BATCHFLUSH_ROUTINE BatchFlushRoutine;
    PKWIN32_SESSION_CALLOUT DesktopOpenProcedure;
    PKWIN32_SESSION_CALLOUT DesktopOkToCloseProcedure;
    PKWIN32_SESSION_CALLOUT DesktopCloseProcedure;
    PKWIN32_SESSION_CALLOUT DesktopDeleteProcedure;
    PKWIN32_SESSION_CALLOUT WindowStationOkToCloseProcedure;
    PKWIN32_SESSION_CALLOUT WindowStationCloseProcedure;
    PKWIN32_SESSION_CALLOUT WindowStationDeleteProcedure;
    PKWIN32_SESSION_CALLOUT WindowStationParseProcedure;
    PKWIN32_SESSION_CALLOUT WindowStationOpenProcedure;
#if (NTDDI_VERSION >= NTDDI_LONGHORN)
    PKWIN32_WIN32DATACOLLECTION_CALLOUT Win32DataCollectionProcedure;
#endif
} WIN32_CALLOUTS_FPNS, *PWIN32_CALLOUTS_FPNS;

#endif // !NTOS_MODE_USER

#ifdef __cplusplus
}; // extern "C"
#endif

#endif // _PSTYPES_H
