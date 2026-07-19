/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         GPL-2.0-or-later
 * PURPOSE:         Built-in ReactOS profiling control and streaming device
 * COPYRIGHT:       Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntoskrnl.h>
#include <reactos/rosprof.h>
#include <internal/proftrace.h>

#define NDEBUG
#include <debug.h>

#define ROSPROF_FILE_TAG 'fPrR'

typedef struct _ROSPROF_FILE_CONTEXT
{
    KGUARDED_MUTEX Lock;
    PEPROCESS OwnerProcess;
    UCHAR OwnerSid[SECURITY_MAX_SID_SIZE];
    BOOLEAN OwnerSidValid;
    PKPROF_TRACE_SESSION Session;
    KPROF_TRACE_CONFIG InternalConfig;
    ULONGLONG SessionId;
    ULONGLONG ConfigGeneration;
    ULONGLONG RequestedSources;
    ULONGLONG AcceptedSources;
    ULONGLONG RequestedRecordTypes;
    ULONGLONG AcceptedRecordTypes;
    ULONGLONG EffectiveSecurity;
    volatile LONG64 ConsumerSequence;
    volatile LONG64 ConsumedRecords;
    ULONG ConfigFlags;
    ULONG ReadWatermark;
    ULONG SecurityMode;
    ULONG PrincipalClass;
    ULONG RequestedStackFlags;
    ULONG GrantedStackFlags;
    ULONG NextProcessor;
    ULONG StopReason;
    NTSTATUS FinalStatus;
    BOOLEAN Configured;
    BOOLEAN Started;
    BOOLEAN NonBlocking;
    BOOLEAN RedactKernel;
    BOOLEAN RedactAll;
    BOOLEAN EndOfStreamDelivered;
    volatile LONG ReadBusy;
    volatile LONG Closing;
    KEVENT CancelEvent;
    KEVENT ReadDoneEvent;
} ROSPROF_FILE_CONTEXT, *PROSPROF_FILE_CONTEXT;

typedef union _ROSPROF_PUBLIC_RECORD
{
    ROSPROF_SAMPLE_RECORD_V1 Sample;
    ROSPROF_LOSS_RECORD_V1 Loss;
    ROSPROF_PROCESS_RECORD_V1 Process;
    ROSPROF_THREAD_RECORD_V1 Thread;
    ROSPROF_IMAGE_RECORD_V1 Image;
    ROSPROF_CONTEXT_SWITCH_RECORD_V1 ContextSwitch;
    ROSPROF_SCHED_WAKEUP_RECORD_V1 Wakeup;
    ROSPROF_SESSION_RECORD_V1 Session;
    ROSPROF_SECURITY_RECORD_V1 Security;
    ROSPROF_CLOCK_SYNC_RECORD_V1 ClockSync;
    UCHAR Bytes[sizeof(ROSPROF_SAMPLE_RECORD_V1) + sizeof(ULONGLONG)];
} ROSPROF_PUBLIC_RECORD, *PROSPROF_PUBLIC_RECORD;

C_ASSERT(sizeof(ROSPROF_PUBLIC_RECORD) >=
         sizeof(ROSPROF_SAMPLE_RECORD_V1) + sizeof(ULONGLONG));

static volatile LONG64 RosprofNextSessionId;
static PDEVICE_OBJECT RosprofDeviceObject;

static
ULONG
RosprofThreadState(
    IN UCHAR State)
{
    switch ((KTHREAD_STATE)State)
    {
        case Initialized: return ROSPROF_THREAD_STATE_INITIALIZED;
        case Ready: return ROSPROF_THREAD_STATE_READY;
        case Running: return ROSPROF_THREAD_STATE_RUNNING;
        case Standby: return ROSPROF_THREAD_STATE_STANDBY;
        case Terminated: return ROSPROF_THREAD_STATE_TERMINATED;
        case Waiting: return ROSPROF_THREAD_STATE_WAITING;
        case Transition: return ROSPROF_THREAD_STATE_TRANSITION;
        case DeferredReady: return ROSPROF_THREAD_STATE_DEFERRED_READY;
        default: return ROSPROF_THREAD_STATE_UNKNOWN;
    }
}

DRIVER_DISPATCH RosprofCreate;
DRIVER_DISPATCH RosprofCleanup;
DRIVER_DISPATCH RosprofClose;
DRIVER_DISPATCH RosprofDeviceControl;
DRIVER_DISPATCH RosprofRead;
DRIVER_CANCEL RosprofCancelRead;

static
NTSTATUS
RosprofPeekNextRecord(
    IN OUT PROSPROF_FILE_CONTEXT Context,
    OUT PKPROF_TRACE_RECORD Record,
    IN ULONG Capacity,
    OUT PULONG Processor);

static
VOID
RosprofCompleteIrp(
    IN OUT PIRP Irp,
    IN NTSTATUS Status,
    IN ULONG_PTR Information)
{
    Irp->IoStatus.Status = Status;
    Irp->IoStatus.Information = Information;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
}

static
VOID
RosprofInitializeStructHeader(
    OUT PROSPROF_STRUCT_HEADER Header,
    IN ULONG Size)
{
    RtlZeroMemory(Header, Size);
    Header->Size = Size;
    Header->MajorVersion = ROSPROF_ABI_VERSION_MAJOR;
    Header->MinorVersion = ROSPROF_ABI_VERSION_MINOR;
    Header->Flags = ROSPROF_STRUCT_FLAG_NONE;
}

static
BOOLEAN
RosprofValidateStructHeader(
    IN const ROSPROF_STRUCT_HEADER *Header,
    IN ULONG MinimumSize,
    IN ULONG BufferSize)
{
    return Header &&
           (BufferSize >= MinimumSize) &&
           (Header->Size >= MinimumSize) &&
           (Header->Size <= BufferSize) &&
           (Header->MajorVersion == ROSPROF_ABI_VERSION_MAJOR) &&
           (Header->MinorVersion <= ROSPROF_ABI_VERSION_MINOR) &&
           (Header->Flags == ROSPROF_STRUCT_FLAG_NONE);
}

static
ULONG
RosprofArchitecture(VOID)
{
#if defined(_M_IX86)
    return ROSPROF_ARCH_X86;
#elif defined(_M_AMD64)
    return ROSPROF_ARCH_AMD64;
#elif defined(_M_ARM64)
    return ROSPROF_ARCH_ARM64;
#elif defined(_M_ARM)
    return ROSPROF_ARCH_ARM;
#else
    return ROSPROF_ARCH_UNKNOWN;
#endif
}

static
ULONG
RosprofMapState(
    IN ULONG State)
{
    switch (State)
    {
        case 0: return ROSPROF_SESSION_CONFIGURED;
        case 1: return ROSPROF_SESSION_RUNNING;
        case 2: return ROSPROF_SESSION_STOPPING;
        case 3: return ROSPROF_SESSION_STOPPED;
        default: return ROSPROF_SESSION_FAULTED;
    }
}

static
VOID
RosprofQueryCapabilities(
    OUT PROSPROF_CAPABILITIES_V1 PublicCapabilities)
{
    KPROF_TRACE_CAPABILITIES Capabilities;
    ULONGLONG PublicFlags;
    ULONG MaximumRingBytes, ProcessorCount;

    KprofTraceQueryCapabilities(&Capabilities);
    PublicFlags = ROSPROF_CAP_PROCESS_LIFECYCLE |
                  ROSPROF_CAP_THREAD_LIFECYCLE |
                  ROSPROF_CAP_IMAGE_LIFECYCLE |
                  ROSPROF_CAP_CONTEXT_SWITCH |
                  ROSPROF_CAP_SCHED_WAKEUP |
                  ROSPROF_CAP_PER_CPU_BUFFER |
                  ROSPROF_CAP_LOSS_RECORDS |
                  ROSPROF_CAP_BATCH_READ |
                  ROSPROF_CAP_CANCELLABLE_READ |
                  ROSPROF_CAP_NONBLOCKING_READ |
                  ROSPROF_CAP_ADDRESS_REDACTION |
                  ROSPROF_CAP_CLOCK_SYNC |
                  ROSPROF_CAP_SYSTEM_WIDE |
                  ROSPROF_CAP_PROCESS_SCOPED;
    if (Capabilities.Capabilities & KPROF_TRACE_CAP_TIMER)
        PublicFlags |= ROSPROF_CAP_TIMER_SAMPLE;
    if (Capabilities.Capabilities & KPROF_TRACE_CAP_KERNEL_CHAIN)
        PublicFlags |= ROSPROF_CAP_KERNEL_STACK;

    RosprofInitializeStructHeader(&PublicCapabilities->Header, sizeof(*PublicCapabilities));
    PublicCapabilities->Capabilities = PublicFlags;
    PublicCapabilities->SupportedSources = ROSPROF_SOURCE_PROCESS |
                                           ROSPROF_SOURCE_THREAD |
                                           ROSPROF_SOURCE_IMAGE |
                                           ROSPROF_SOURCE_CONTEXT_SWITCH |
                                           ROSPROF_SOURCE_SCHED_WAKEUP |
                                           ROSPROF_SOURCE_CLOCK_SYNC;
    if (PublicFlags & ROSPROF_CAP_TIMER_SAMPLE)
        PublicCapabilities->SupportedSources |= ROSPROF_SOURCE_TIMER;
    PublicCapabilities->SupportedRecordTypes =
        ROSPROF_RECORD_MASK(ROSPROF_RECORD_SESSION) |
        ROSPROF_RECORD_MASK(ROSPROF_RECORD_LOSS) |
        ROSPROF_RECORD_MASK(ROSPROF_RECORD_PROCESS) |
        ROSPROF_RECORD_MASK(ROSPROF_RECORD_THREAD) |
        ROSPROF_RECORD_MASK(ROSPROF_RECORD_IMAGE) |
        ROSPROF_RECORD_MASK(ROSPROF_RECORD_CONTEXT_SWITCH) |
        ROSPROF_RECORD_MASK(ROSPROF_RECORD_SCHED_WAKEUP) |
        ROSPROF_RECORD_MASK(ROSPROF_RECORD_SECURITY) |
        ROSPROF_RECORD_MASK(ROSPROF_RECORD_CLOCK_SYNC);
    if (PublicFlags & ROSPROF_CAP_TIMER_SAMPLE)
    {
        PublicCapabilities->SupportedRecordTypes |=
            ROSPROF_RECORD_MASK(ROSPROF_RECORD_SAMPLE);
    }
    PublicCapabilities->SecurityCapabilities =
        ROSPROF_SECURITY_TARGET_OWNER |
        ROSPROF_SECURITY_SYSTEM_WIDE_PRIVILEGE |
        ROSPROF_SECURITY_KERNEL_ADDRESS_REDACTED |
        ROSPROF_SECURITY_USER_ADDRESS_REDACTED |
        ROSPROF_SECURITY_SECURE_ZERO;
    PublicCapabilities->TimestampFrequency = 10000000;
    PublicCapabilities->Architecture = RosprofArchitecture();
    PublicCapabilities->PointerWidth = sizeof(PVOID) * 8;
    PublicCapabilities->ProcessorCount = KeNumberProcessors;
    PublicCapabilities->ClockType = ROSPROF_CLOCK_INTERRUPT_TIME;
    if (PublicFlags & ROSPROF_CAP_TIMER_SAMPLE)
    {
        /* ProfileTime is global and each HAL normalizes requests differently.
         * Advertise the conservative explicit-request range; CONFIGURE returns
         * the achieved interval that START will reapply. */
        PublicCapabilities->MinimumPeriod100ns = 1;
        PublicCapabilities->MaximumPeriod100ns = MAXULONG;
    }
    PublicCapabilities->MinimumRingBytes = Capabilities.MinimumRingSize;
    ProcessorCount = max(1, (ULONG)KeNumberProcessors);
    MaximumRingBytes = min(Capabilities.MaximumRingSize, (ULONG)(KPROF_TRACE_MAX_SESSION_RING_BYTES / ProcessorCount));
    while (MaximumRingBytes & (MaximumRingBytes - 1))
        MaximumRingBytes &= MaximumRingBytes - 1;
    PublicCapabilities->MaximumRingBytes = MaximumRingBytes;
    PublicCapabilities->MaximumRecordBytes =
        sizeof(ROSPROF_IMAGE_RECORD_V1) +
        KPROF_TRACE_MAX_IMAGE_PATH_BYTES +
        KPROF_TRACE_IMAGE_BUILD_ID_BYTES +
        (2 * (ROSPROF_RECORD_ALIGNMENT - 1));
    /* Leaf IP only; full user/kernel walks are deliberately not advertised. */
    PublicCapabilities->MaximumStackDepth = 1;
    if (Capabilities.Capabilities & KPROF_TRACE_CAP_KERNEL_CHAIN) PublicCapabilities->MaximumStackDepth = KPROF_TRACE_MAX_FRAMES;
    PublicCapabilities->MaximumPmuEvents = 0;
    PublicCapabilities->RecordAlignment = ROSPROF_RECORD_ALIGNMENT;
    PublicCapabilities->ReadHeaderSize = sizeof(ROSPROF_READ_BATCH_V1);
}

static
NTSTATUS
RosprofReferenceTargetProcess(
    IN ULONG ProcessId,
    OUT PEPROCESS *TargetProcess)
{
    if (!ProcessId ||
        (ProcessId == HandleToULong(PsGetCurrentProcessId())))
    {
        *TargetProcess = PsGetCurrentProcess();
        ObReferenceObject(*TargetProcess);
        return STATUS_SUCCESS;
    }

    return PsLookupProcessByProcessId(ULongToHandle(ProcessId), TargetProcess);
}

static
NTSTATUS
RosprofBuildInternalConfig(
    IN OUT PROSPROF_FILE_CONTEXT Context,
    IN const ROSPROF_CONFIG_V1 *PublicConfig,
    IN ULONG InputLength,
    OUT PEPROCESS *ReferencedTarget)
{
    const ULONGLONG SupportedSources =
        ROSPROF_SOURCE_TIMER | ROSPROF_SOURCE_PROCESS |
        ROSPROF_SOURCE_THREAD | ROSPROF_SOURCE_IMAGE |
        ROSPROF_SOURCE_CONTEXT_SWITCH | ROSPROF_SOURCE_SCHED_WAKEUP |
        ROSPROF_SOURCE_CLOCK_SYNC;
    const ULONGLONG SupportedRecords =
        ROSPROF_RECORD_MASK(ROSPROF_RECORD_SAMPLE) |
        ROSPROF_RECORD_MASK(ROSPROF_RECORD_SESSION) |
        ROSPROF_RECORD_MASK(ROSPROF_RECORD_LOSS) |
        ROSPROF_RECORD_MASK(ROSPROF_RECORD_PROCESS) |
        ROSPROF_RECORD_MASK(ROSPROF_RECORD_THREAD) |
        ROSPROF_RECORD_MASK(ROSPROF_RECORD_IMAGE) |
        ROSPROF_RECORD_MASK(ROSPROF_RECORD_CONTEXT_SWITCH) |
        ROSPROF_RECORD_MASK(ROSPROF_RECORD_SCHED_WAKEUP) |
        ROSPROF_RECORD_MASK(ROSPROF_RECORD_SECURITY) |
        ROSPROF_RECORD_MASK(ROSPROF_RECORD_CLOCK_SYNC);
    const ULONGLONG SupportedConfigFlags =
        ROSPROF_CONFIG_FLAG_SYSTEM_WIDE |
        ROSPROF_CONFIG_FLAG_NONBLOCKING_READ |
        ROSPROF_CONFIG_FLAG_PER_CPU_BUFFER |
        ROSPROF_CONFIG_FLAG_DROP_NEW |
        ROSPROF_CONFIG_FLAG_EXCLUDE_PROFILER;
    KPROF_TRACE_CONFIG *Config = &Context->InternalConfig;
    PEPROCESS TargetProcess = NULL;
    KAFFINITY Affinity = KeActiveProcessors;
    ULONGLONG DerivedRecords =
        ROSPROF_RECORD_MASK(ROSPROF_RECORD_SESSION) |
        ROSPROF_RECORD_MASK(ROSPROF_RECORD_SECURITY) |
        ROSPROF_RECORD_MASK(ROSPROF_RECORD_LOSS);
    PUCHAR CpuMask;
    ULONG Index, ActiveProcessors = 0;
    NTSTATUS Status;

    *ReferencedTarget = NULL;
    if (!PublicConfig->Sources ||
        (PublicConfig->Sources & ~SupportedSources) ||
        (PublicConfig->RecordTypes & ~SupportedRecords) ||
        (PublicConfig->ConfigFlags & ~SupportedConfigFlags) ||
        PublicConfig->TargetThreadId || PublicConfig->PmuEventCount ||
        PublicConfig->PmuEventsOffset ||
        (PublicConfig->SamplePeriod100ns > MAXULONG) ||
        (PublicConfig->MaximumStackDepth > KPROF_TRACE_MAX_FRAMES) ||
        (PublicConfig->StackFlags & ~(ROSPROF_STACK_INCLUDE_IP | ROSPROF_STACK_KERNEL | ROSPROF_STACK_TRUNCATE_ALLOWED)))
    {
        return STATUS_NOT_SUPPORTED;
    }
#if !defined(_M_AMD64)
    if (PublicConfig->StackFlags & ROSPROF_STACK_KERNEL)
        return STATUS_NOT_SUPPORTED;
#endif
    if ((PublicConfig->SecurityMode > ROSPROF_SECURITY_REDACT_ALL_ADDRESSES) ||
        !PublicConfig->RingSizeBytes)
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (PublicConfig->CpuMaskBytes)
    {
        if (!RosProfRangeValidU32(PublicConfig->Header.Size, PublicConfig->CpuMaskOffset, PublicConfig->CpuMaskBytes) ||
            (PublicConfig->CpuMaskOffset < sizeof(*PublicConfig)) ||
            (PublicConfig->CpuMaskBytes > sizeof(KAFFINITY)) ||
            (PublicConfig->Header.Size > InputLength))
        {
            return STATUS_INVALID_PARAMETER;
        }
        CpuMask = (PUCHAR)PublicConfig + PublicConfig->CpuMaskOffset;
        Affinity = 0;
        for (Index = 0; Index < PublicConfig->CpuMaskBytes; Index++)
        {
            Affinity |= ((KAFFINITY)CpuMask[Index]) << (Index * 8);
        }
    }
    else if (PublicConfig->CpuMaskOffset)
    {
        return STATUS_INVALID_PARAMETER;
    }
    Affinity &= KeActiveProcessors;
    for (Index = 0; Index < MAXIMUM_PROCESSORS; Index++)
    {
        if (Affinity & AFFINITY_MASK(Index)) ActiveProcessors++;
    }
    if (!ActiveProcessors) return STATUS_INVALID_PARAMETER;
    if ((ULONGLONG)PublicConfig->RingSizeBytes * ActiveProcessors >
        KPROF_TRACE_MAX_SESSION_RING_BYTES)
        return STATUS_QUOTA_EXCEEDED;
    if (PublicConfig->ReadWatermarkBytes >
        (ULONGLONG)PublicConfig->RingSizeBytes * ActiveProcessors)
        return STATUS_INVALID_BUFFER_SIZE;

    RtlZeroMemory(Config, sizeof(*Config));
    Config->Version = KPROF_TRACE_CONFIG_VERSION;
    Config->Size = sizeof(*Config);
    Config->RingSize = PublicConfig->RingSizeBytes;
    Config->Affinity = Affinity;
    Config->Interval = (ULONG)PublicConfig->SamplePeriod100ns;
    Config->MaximumStackDepth = 1;
    if (PublicConfig->Sources & ROSPROF_SOURCE_TIMER)
    {
        Config->Flags |= KPROF_TRACE_FLAG_SAMPLE;
        Config->Source = ProfileTime;
        DerivedRecords |= ROSPROF_RECORD_MASK(ROSPROF_RECORD_SAMPLE);
        if (PublicConfig->StackFlags & ROSPROF_STACK_KERNEL)
        {
            if (PublicConfig->MaximumStackDepth <= 1)
                return STATUS_INVALID_PARAMETER;
            Config->Flags |= KPROF_TRACE_FLAG_CALLCHAIN;
            Config->MaximumStackDepth = PublicConfig->MaximumStackDepth;
        }
    }
    if (PublicConfig->Sources & ROSPROF_SOURCE_PROCESS)
    {
        Config->Flags |= KPROF_TRACE_FLAG_PROCESS;
        DerivedRecords |= ROSPROF_RECORD_MASK(ROSPROF_RECORD_PROCESS);
    }
    if (PublicConfig->Sources & ROSPROF_SOURCE_THREAD)
    {
        Config->Flags |= KPROF_TRACE_FLAG_THREAD;
        DerivedRecords |= ROSPROF_RECORD_MASK(ROSPROF_RECORD_THREAD);
    }
    if (PublicConfig->Sources & ROSPROF_SOURCE_IMAGE)
    {
        Config->Flags |= KPROF_TRACE_FLAG_IMAGE;
        DerivedRecords |= ROSPROF_RECORD_MASK(ROSPROF_RECORD_IMAGE);
    }
    if (PublicConfig->Sources & ROSPROF_SOURCE_CONTEXT_SWITCH)
    {
        Config->Flags |= KPROF_TRACE_FLAG_CONTEXT_SWITCH;
        DerivedRecords |= ROSPROF_RECORD_MASK(ROSPROF_RECORD_CONTEXT_SWITCH);
    }
    if (PublicConfig->Sources & ROSPROF_SOURCE_SCHED_WAKEUP)
    {
        Config->Flags |= KPROF_TRACE_FLAG_SCHED_WAKEUP;
        DerivedRecords |= ROSPROF_RECORD_MASK(ROSPROF_RECORD_SCHED_WAKEUP);
    }
    if (PublicConfig->Sources & ROSPROF_SOURCE_CLOCK_SYNC)
    {
        Config->Flags |= KPROF_TRACE_FLAG_CLOCK_SYNC;
        DerivedRecords |= ROSPROF_RECORD_MASK(ROSPROF_RECORD_CLOCK_SYNC);
    }
    if (PublicConfig->ConfigFlags & ROSPROF_CONFIG_FLAG_EXCLUDE_PROFILER)
        Config->Flags |= KPROF_TRACE_FLAG_EXCLUDE_OWNER;

    if (PublicConfig->RecordTypes &&
        (((PublicConfig->RecordTypes & ~DerivedRecords) != 0) ||
         ((DerivedRecords &
           ~(ROSPROF_RECORD_MASK(ROSPROF_RECORD_LOSS) |
             ROSPROF_RECORD_MASK(ROSPROF_RECORD_SESSION) |
             ROSPROF_RECORD_MASK(ROSPROF_RECORD_SECURITY)) &
           ~PublicConfig->RecordTypes) != 0)))
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (PublicConfig->ConfigFlags & ROSPROF_CONFIG_FLAG_SYSTEM_WIDE)
    {
        if (PublicConfig->TargetProcessId ||
            !SeSinglePrivilegeCheck(SeSystemProfilePrivilege, ExGetPreviousMode()))
        {
            return STATUS_PRIVILEGE_NOT_HELD;
        }
        Config->Scope = KprofTraceScopeSystem;
    }
    else
    {
        if (!SeSinglePrivilegeCheck(SeProfileSingleProcessPrivilege, ExGetPreviousMode()))
        {
            return STATUS_PRIVILEGE_NOT_HELD;
        }
        Status = RosprofReferenceTargetProcess(PublicConfig->TargetProcessId, &TargetProcess);
        if (!NT_SUCCESS(Status)) return Status;
        if ((TargetProcess != PsGetCurrentProcess()) &&
            !SeSinglePrivilegeCheck(SeDebugPrivilege, ExGetPreviousMode()))
        {
            ObDereferenceObject(TargetProcess);
            return STATUS_ACCESS_DENIED;
        }
        Config->Scope = KprofTraceScopeProcess;
        Config->TargetProcess = TargetProcess;
        *ReferencedTarget = TargetProcess;
    }

    Context->RequestedSources = PublicConfig->Sources;
    Context->AcceptedSources = PublicConfig->Sources;
    Context->RequestedRecordTypes = PublicConfig->RecordTypes ?
                                    PublicConfig->RecordTypes : DerivedRecords;
    Context->AcceptedRecordTypes = PublicConfig->RecordTypes ?
        ((PublicConfig->RecordTypes & DerivedRecords) |
         ROSPROF_RECORD_MASK(ROSPROF_RECORD_LOSS)) : DerivedRecords;
    Context->ConfigFlags = (ULONG)PublicConfig->ConfigFlags;
    Context->NonBlocking = !!(PublicConfig->ConfigFlags &
                              ROSPROF_CONFIG_FLAG_NONBLOCKING_READ);
    Context->ReadWatermark = PublicConfig->ReadWatermarkBytes;
    Context->SecurityMode = PublicConfig->SecurityMode;
    Context->RequestedStackFlags = PublicConfig->StackFlags;
    Context->GrantedStackFlags = ROSPROF_STACK_INCLUDE_IP;
    if (Config->Flags & KPROF_TRACE_FLAG_CALLCHAIN) Context->GrantedStackFlags |= ROSPROF_STACK_KERNEL | ROSPROF_STACK_TRUNCATE_ALLOWED;
    Context->RedactAll =
        (PublicConfig->SecurityMode == ROSPROF_SECURITY_REDACT_ALL_ADDRESSES);
    Context->RedactKernel = Context->RedactAll ||
        (PublicConfig->SecurityMode == ROSPROF_SECURITY_REDACT_KERNEL) ||
        !SeSinglePrivilegeCheck(SeDebugPrivilege, ExGetPreviousMode());
    Context->EffectiveSecurity = ROSPROF_SECURITY_SECURE_ZERO |
                                 ROSPROF_SECURITY_TARGET_OWNER;
    if (Config->Scope == KprofTraceScopeSystem)
        Context->EffectiveSecurity |= ROSPROF_SECURITY_SYSTEM_WIDE_PRIVILEGE;
    if (!Context->RedactKernel)
        Context->EffectiveSecurity |= ROSPROF_SECURITY_DEBUG_PRIVILEGE;
    if (Context->RedactKernel)
        Context->EffectiveSecurity |= ROSPROF_SECURITY_KERNEL_ADDRESS_REDACTED;
    if (Context->RedactAll)
        Context->EffectiveSecurity |= ROSPROF_SECURITY_USER_ADDRESS_REDACTED;
    if (PsGetCurrentProcess() == PsInitialSystemProcess)
        Context->PrincipalClass = ROSPROF_PRINCIPAL_SYSTEM;
    else if ((Config->Scope == KprofTraceScopeProcess) &&
             (TargetProcess == PsGetCurrentProcess()))
        Context->PrincipalClass = ROSPROF_PRINCIPAL_TARGET_OWNER;
    else
        Context->PrincipalClass = ROSPROF_PRINCIPAL_ADMINISTRATOR;
    return STATUS_SUCCESS;
}

static
NTSTATUS
RosprofResolveTimerInterval(
    IN OUT PROSPROF_FILE_CONTEXT Context)
{
    ULONG Interval;

    if (!(Context->AcceptedSources & ROSPROF_SOURCE_TIMER))
        return STATUS_SUCCESS;

    Interval = Context->InternalConfig.Interval;
    if (!Interval) Interval = KeQueryIntervalProfile(ProfileTime);
    if (!Interval) return STATUS_NOT_SUPPORTED;

    /* CONFIGURE must not reprogram the global profile source.  START applies
     * an explicit interval; a zero request inherits the current interval. */
    Context->InternalConfig.Interval = Interval;
    return STATUS_SUCCESS;
}

static
VOID
RosprofInitializeRecordHeader(
    OUT PROSPROF_RECORD_HEADER PublicHeader,
    IN const KPROF_TRACE_RECORD_HEADER *PrivateHeader,
    IN ULONG Size,
    IN USHORT Type)
{
    PublicHeader->Size = Size;
    PublicHeader->Type = Type;
    PublicHeader->Version = 1;
    PublicHeader->Flags = ROSPROF_RECORD_FLAG_COMMITTED;
    if (PrivateHeader->Flags & KPROF_TRACE_RECORD_FLAG_USER)
        PublicHeader->Flags |= ROSPROF_RECORD_FLAG_USER;
    if (PrivateHeader->Flags & KPROF_TRACE_RECORD_FLAG_KERNEL)
        PublicHeader->Flags |= ROSPROF_RECORD_FLAG_KERNEL;
    if (PrivateHeader->Flags & KPROF_TRACE_RECORD_FLAG_TRUNCATED)
        PublicHeader->Flags |= ROSPROF_RECORD_FLAG_TRUNCATED;
    PublicHeader->HeaderSize = sizeof(*PublicHeader);
    PublicHeader->ProcessorNumber = (PrivateHeader->Processor > MAXUSHORT) ?
                                    MAXUSHORT :
                                    (USHORT)PrivateHeader->Processor;
    PublicHeader->Sequence = PrivateHeader->Sequence;
    PublicHeader->Timestamp = PrivateHeader->Timestamp;
    PublicHeader->ProcessId = (ULONG)PrivateHeader->ProcessId;
    PublicHeader->ThreadId = (ULONG)PrivateHeader->ThreadId;
    PublicHeader->UniqueProcessKey = PrivateHeader->ProcessKey;
    PublicHeader->UniqueThreadKey = PrivateHeader->ThreadKey;
}

static
ULONGLONG
RosprofFilterAddress(
    IN PROSPROF_FILE_CONTEXT Context,
    IN ULONGLONG Address,
    IN OUT PULONG RecordFlags)
{
    BOOLEAN KernelAddress;

    KernelAddress = Address > (ULONGLONG)(ULONG_PTR)MmHighestUserAddress;
    if (Context->RedactAll || (Context->RedactKernel && KernelAddress))
    {
        *RecordFlags |= ROSPROF_RECORD_FLAG_REDACTED;
        return 0;
    }
    return Address;
}

static
NTSTATUS
RosprofImageMetadata(
    IN PROSPROF_FILE_CONTEXT Context,
    IN const KPROF_TRACE_RECORD *PrivateRecord,
    OUT PKPROF_TRACE_IMAGE_METADATA Metadata,
    OUT PULONG PathBytes)
{
    NTSTATUS Status;

    RtlZeroMemory(Metadata, sizeof(*Metadata));
    *PathBytes = 0;
    if (!PrivateRecord->Data.Image.MetadataId)
        return STATUS_SUCCESS;
    Status = KprofTraceQueryImageMetadata(Context->Session, PrivateRecord->Data.Image.MetadataId, Metadata, NULL, 0, PathBytes);
    if ((Status == STATUS_SUCCESS) || (Status == STATUS_BUFFER_TOO_SMALL))
        return STATUS_SUCCESS;
    if (Status == STATUS_NOT_FOUND)
    {
        RtlZeroMemory(Metadata, sizeof(*Metadata));
        *PathBytes = 0;
        return STATUS_SUCCESS;
    }
    return Status;
}

static
ULONG
RosprofSampleStoredFrames(
    IN const KPROF_TRACE_RECORD *PrivateRecord)
{
    ULONG StoredBytes;

    /* Frames ride after the fixed record; the header size is authoritative
     * for how many were actually stored for this session. */
    if (PrivateRecord->Header.Size <= sizeof(KPROF_TRACE_RECORD)) return 0;
    StoredBytes = PrivateRecord->Header.Size - sizeof(KPROF_TRACE_RECORD);
    return min(StoredBytes / sizeof(ULONGLONG), PrivateRecord->Data.Sample.FrameCount);
}

static
NTSTATUS
RosprofTranslatedRecordSize(
    IN PROSPROF_FILE_CONTEXT Context,
    IN const KPROF_TRACE_RECORD *PrivateRecord,
    OUT PULONG PublicSize)
{
    KPROF_TRACE_IMAGE_METADATA Metadata;
    ULONG PathBytes, Size;
    NTSTATUS Status;

    switch (PrivateRecord->Header.Type)
    {
        case KprofTraceRecordSample:
        {
            ULONG StoredFrames = RosprofSampleStoredFrames(PrivateRecord);
            *PublicSize = sizeof(ROSPROF_SAMPLE_RECORD_V1) + max(StoredFrames, 1UL) * sizeof(ULONGLONG);
            return STATUS_SUCCESS;
        }
        case KprofTraceRecordLost:
            *PublicSize = sizeof(ROSPROF_LOSS_RECORD_V1);
            return STATUS_SUCCESS;
        case KprofTraceRecordProcess:
            *PublicSize = sizeof(ROSPROF_PROCESS_RECORD_V1);
            return STATUS_SUCCESS;
        case KprofTraceRecordThread:
            *PublicSize = sizeof(ROSPROF_THREAD_RECORD_V1);
            return STATUS_SUCCESS;
        case KprofTraceRecordImage:
            Status = RosprofImageMetadata(Context, PrivateRecord, &Metadata, &PathBytes);
            if (!NT_SUCCESS(Status)) return Status;
            if (Metadata.BuildIdBytes > sizeof(Metadata.BuildId))
                return STATUS_DATA_ERROR;
            Size = sizeof(ROSPROF_IMAGE_RECORD_V1);
            if (PathBytes)
            {
                if (PathBytes > MAXULONG - Size)
                    return STATUS_INTEGER_OVERFLOW;
                Size = RosProfAlignUpU32(Size + PathBytes);
                if (!Size) return STATUS_INTEGER_OVERFLOW;
            }
            if (Metadata.BuildIdBytes)
            {
                if (Metadata.BuildIdBytes > MAXULONG - Size)
                    return STATUS_INTEGER_OVERFLOW;
                Size = RosProfAlignUpU32(Size + Metadata.BuildIdBytes);
                if (!Size) return STATUS_INTEGER_OVERFLOW;
            }
            *PublicSize = Size;
            return STATUS_SUCCESS;
        case KprofTraceRecordScheduler:
            *PublicSize =
                (PrivateRecord->Header.Flags & KPROF_TRACE_RECORD_FLAG_READY) ?
                sizeof(ROSPROF_SCHED_WAKEUP_RECORD_V1) :
                sizeof(ROSPROF_CONTEXT_SWITCH_RECORD_V1);
            return STATUS_SUCCESS;
        case KprofTraceRecordSession:
            *PublicSize = sizeof(ROSPROF_SESSION_RECORD_V1);
            return STATUS_SUCCESS;
        case KprofTraceRecordSecurity:
            *PublicSize = sizeof(ROSPROF_SECURITY_RECORD_V1);
            return STATUS_SUCCESS;
        case KprofTraceRecordClockSync:
            *PublicSize = sizeof(ROSPROF_CLOCK_SYNC_RECORD_V1);
            return STATUS_SUCCESS;
        default:
            return STATUS_NOT_SUPPORTED;
    }
}

static
NTSTATUS
RosprofTranslateRecord(
    IN PROSPROF_FILE_CONTEXT Context,
    IN const KPROF_TRACE_RECORD *PrivateRecord,
    OUT PVOID PublicBuffer,
    IN ULONG PublicCapacity,
    OUT PULONG PublicSize)
{
    PROSPROF_PUBLIC_RECORD PublicRecord = PublicBuffer;
    PROSPROF_RECORD_HEADER Header;
    KPROF_TRACE_IMAGE_METADATA Metadata;
    ULONG PathBytes, Offset;
    ULONGLONG Address;
    NTSTATUS Status;

    Status = RosprofTranslatedRecordSize(Context, PrivateRecord, PublicSize);
    if (!NT_SUCCESS(Status)) return Status;
    if (!PublicBuffer || (PublicCapacity < *PublicSize))
        return STATUS_BUFFER_TOO_SMALL;
    RtlZeroMemory(PublicBuffer, *PublicSize);
    switch (PrivateRecord->Header.Type)
    {
        case KprofTraceRecordSample:
        {
            const ULONGLONG UNALIGNED *PrivateFrames = (const ULONGLONG UNALIGNED *)((const UCHAR *)PrivateRecord + sizeof(KPROF_TRACE_RECORD));
            ULONGLONG UNALIGNED *PublicFrames;
            ULONG StoredFrames = RosprofSampleStoredFrames(PrivateRecord);
            ULONG ChainFlags = PrivateRecord->Data.Sample.ChainFlags;
            ULONG FrameIndex;

            *PublicSize = sizeof(ROSPROF_SAMPLE_RECORD_V1) + max(StoredFrames, 1UL) * sizeof(ULONGLONG);
            Header = &PublicRecord->Sample.Header;
            RosprofInitializeRecordHeader(Header, &PrivateRecord->Header, *PublicSize, ROSPROF_RECORD_SAMPLE);
            PublicRecord->Sample.Weight = 1;
            if (PrivateRecord->Data.Sample.Interval) PublicRecord->Sample.Weight = PrivateRecord->Data.Sample.Interval;
            PublicRecord->Sample.Source = ROSPROF_SOURCE_TIMER;
            PublicRecord->Sample.FramesOffset = sizeof(ROSPROF_SAMPLE_RECORD_V1);
            PublicFrames = (ULONGLONG UNALIGNED *)(PublicRecord->Bytes + PublicRecord->Sample.FramesOffset);
            Address = RosprofFilterAddress(Context, PrivateRecord->Data.Sample.ProgramCounter, &Header->Flags);
            PublicRecord->Sample.InstructionPointer = Address;
            if (StoredFrames > 1)
            {
                /* Kernel-mode chain captured at the interrupt: leaf first,
                 * every frame passes through the same redaction filter. */
                PublicRecord->Sample.FramesBytes = StoredFrames * sizeof(ULONGLONG);
                for (FrameIndex = 0; FrameIndex < StoredFrames; FrameIndex++)
                {
                    PublicFrames[FrameIndex] = RosprofFilterAddress(Context, PrivateFrames[FrameIndex], &Header->Flags);
                }
                PublicRecord->Sample.KernelDepth = (USHORT)StoredFrames;
            }
            else
            {
                PublicRecord->Sample.FramesBytes = sizeof(ULONGLONG);
                PublicFrames[0] = Address;
            }
            if (ChainFlags & KPROF_TRACE_CHAIN_TRUNCATED) PublicRecord->Sample.SampleFlags |= ROSPROF_SAMPLE_FLAG_CHAIN_TRUNCATED;
            if (ChainFlags & KPROF_TRACE_CHAIN_STOPPED) PublicRecord->Sample.SampleFlags |= ROSPROF_SAMPLE_FLAG_CHAIN_STOPPED;
            if (ChainFlags & KPROF_TRACE_CHAIN_USER_BOUNDARY) PublicRecord->Sample.SampleFlags |= ROSPROF_SAMPLE_FLAG_USER_BOUNDARY;
            if (PrivateRecord->Header.Flags & KPROF_TRACE_RECORD_FLAG_USER)
                PublicRecord->Sample.UserDepth = 1;
            else if (StoredFrames <= 1)
                PublicRecord->Sample.KernelDepth = 1;
            break;
        }

        case KprofTraceRecordLost:
            *PublicSize = sizeof(ROSPROF_LOSS_RECORD_V1);
            Header = &PublicRecord->Loss.Header;
            RosprofInitializeRecordHeader(Header, &PrivateRecord->Header, *PublicSize, ROSPROF_RECORD_LOSS);
            Header->Flags |= ROSPROF_RECORD_FLAG_SYNTHETIC;
            PublicRecord->Loss.Reason = ROSPROF_LOSS_RING_FULL;
            PublicRecord->Loss.Scope = ROSPROF_LOSS_SCOPE_PROCESSOR;
            PublicRecord->Loss.LostRecords = PrivateRecord->Data.Lost.Count;
            PublicRecord->Loss.LostBytes =
                PrivateRecord->Data.Lost.Count * sizeof(KPROF_TRACE_RECORD);
            PublicRecord->Loss.FirstLostSequence =
                PrivateRecord->Data.Lost.FirstSequence;
            PublicRecord->Loss.LastLostSequence =
                PrivateRecord->Data.Lost.LastSequence;
            PublicRecord->Loss.FirstLostTimestamp =
                PrivateRecord->Data.Lost.FirstTimestamp;
            PublicRecord->Loss.LastLostTimestamp =
                PrivateRecord->Data.Lost.LastTimestamp;
            break;

        case KprofTraceRecordProcess:
            *PublicSize = sizeof(ROSPROF_PROCESS_RECORD_V1);
            Header = &PublicRecord->Process.Header;
            RosprofInitializeRecordHeader(Header, &PrivateRecord->Header, *PublicSize, ROSPROF_RECORD_PROCESS);
            PublicRecord->Process.Event =
                (PrivateRecord->Header.Flags & KPROF_TRACE_RECORD_FLAG_CREATE) ?
                ROSPROF_LIFECYCLE_START : ROSPROF_LIFECYCLE_END;
            PublicRecord->Process.ParentProcessId =
                (ULONG)PrivateRecord->Data.Lifecycle.ParentProcessId;
            PublicRecord->Process.UniqueProcessKey =
                PrivateRecord->Data.Lifecycle.SubjectProcessKey;
            break;

        case KprofTraceRecordThread:
            *PublicSize = sizeof(ROSPROF_THREAD_RECORD_V1);
            Header = &PublicRecord->Thread.Header;
            RosprofInitializeRecordHeader(Header, &PrivateRecord->Header, *PublicSize, ROSPROF_RECORD_THREAD);
            PublicRecord->Thread.Event =
                (PrivateRecord->Header.Flags & KPROF_TRACE_RECORD_FLAG_CREATE) ?
                ROSPROF_LIFECYCLE_START : ROSPROF_LIFECYCLE_END;
            PublicRecord->Thread.UniqueThreadKey =
                PrivateRecord->Data.Lifecycle.SubjectThreadKey;
            PublicRecord->Thread.UniqueProcessKey =
                PrivateRecord->Data.Lifecycle.SubjectProcessKey;
            break;

        case KprofTraceRecordImage:
            Status = RosprofImageMetadata(Context, PrivateRecord, &Metadata, &PathBytes);
            if (!NT_SUCCESS(Status)) return Status;
            Header = &PublicRecord->Image.Header;
            RosprofInitializeRecordHeader(Header, &PrivateRecord->Header, *PublicSize, ROSPROF_RECORD_IMAGE);
            PublicRecord->Image.Event =
                (PrivateRecord->Header.Flags & KPROF_TRACE_RECORD_FLAG_CREATE) ?
                ROSPROF_IMAGE_LOAD : ROSPROF_IMAGE_UNLOAD;
            PublicRecord->Image.ImageBase = RosprofFilterAddress(Context, PrivateRecord->Data.Image.ImageBase, &Header->Flags);
            PublicRecord->Image.ImageSize = PrivateRecord->Data.Image.ImageSize;
            PublicRecord->Image.ImageKey = PrivateRecord->Data.Image.ImageKey;
            if (!PublicRecord->Image.ImageKey)
                PublicRecord->Image.ImageKey = PublicRecord->Image.ImageBase;
            if ((PrivateRecord->Header.Flags &
                 KPROF_TRACE_RECORD_FLAG_SYSTEM) ||
                (Metadata.Flags & KPROF_TRACE_IMAGE_META_SYSTEM))
            {
                PublicRecord->Image.ImageFlags |= ROSPROF_IMAGE_FLAG_SYSTEM;
            }
            if (Metadata.Flags & KPROF_TRACE_IMAGE_META_RSDS)
            {
                PublicRecord->Image.ImageFlags |=
                    ROSPROF_IMAGE_FLAG_BUILD_ID_RSDS;
            }
            PublicRecord->Image.ImageFlags |=
                ((ULONG)Metadata.Machine << ROSPROF_IMAGE_MACHINE_SHIFT) &
                ROSPROF_IMAGE_MACHINE_MASK;
            PublicRecord->Image.Checksum = Metadata.Checksum;
            PublicRecord->Image.TimeDateStamp = Metadata.TimeDateStamp;
            if (Metadata.Flags & KPROF_TRACE_IMAGE_META_TRUNCATED)
                Header->Flags |= ROSPROF_RECORD_FLAG_TRUNCATED;

            Offset = sizeof(ROSPROF_IMAGE_RECORD_V1);
            if (PathBytes)
            {
                PublicRecord->Image.PathOffset = Offset;
                PublicRecord->Image.PathBytes = PathBytes;
                Status = KprofTraceQueryImageMetadata(Context->Session, PrivateRecord->Data.Image.MetadataId, &Metadata, (PUCHAR)PublicBuffer + Offset, PathBytes, &PathBytes);
                if (!NT_SUCCESS(Status)) return Status;
                Offset = RosProfAlignUpU32(Offset + PathBytes);
            }
            if (Metadata.BuildIdBytes)
            {
                PublicRecord->Image.BuildIdOffset = Offset;
                PublicRecord->Image.BuildIdBytes = Metadata.BuildIdBytes;
                RtlCopyMemory((PUCHAR)PublicBuffer + Offset, Metadata.BuildId, Metadata.BuildIdBytes);
            }
            break;

        case KprofTraceRecordScheduler:
            if (PrivateRecord->Header.Flags & KPROF_TRACE_RECORD_FLAG_READY)
            {
                *PublicSize = sizeof(ROSPROF_SCHED_WAKEUP_RECORD_V1);
                Header = &PublicRecord->Wakeup.Header;
                RosprofInitializeRecordHeader(Header, &PrivateRecord->Header, *PublicSize, ROSPROF_RECORD_SCHED_WAKEUP);
                PublicRecord->Wakeup.TargetProcessId =
                    (ULONG)PrivateRecord->Data.Scheduler.NewProcessId;
                PublicRecord->Wakeup.TargetThreadId =
                    (ULONG)PrivateRecord->Data.Scheduler.NewThreadId;
                PublicRecord->Wakeup.WakerProcessId = Header->ProcessId;
                PublicRecord->Wakeup.WakerThreadId = Header->ThreadId;
                PublicRecord->Wakeup.TargetProcessKey =
                    PrivateRecord->Data.Scheduler.NewProcessKey;
                PublicRecord->Wakeup.TargetThreadKey =
                    PrivateRecord->Data.Scheduler.NewThreadKey;
                PublicRecord->Wakeup.WakerProcessKey =
                    PrivateRecord->Header.ProcessKey;
                PublicRecord->Wakeup.WakerThreadKey =
                    PrivateRecord->Header.ThreadKey;
                PublicRecord->Wakeup.TargetProcessorNumber =
                    PrivateRecord->Data.Scheduler.TargetProcessor;
                PublicRecord->Wakeup.SourceProcessorNumber =
                    PrivateRecord->Data.Scheduler.SourceProcessor;
                if (PublicRecord->Wakeup.TargetProcessorNumber !=
                    PublicRecord->Wakeup.SourceProcessorNumber)
                {
                    PublicRecord->Wakeup.WakeFlags |=
                        ROSPROF_WAKE_FLAG_CROSS_CPU;
                }
            }
            else
            {
                *PublicSize = sizeof(ROSPROF_CONTEXT_SWITCH_RECORD_V1);
                Header = &PublicRecord->ContextSwitch.Header;
                RosprofInitializeRecordHeader(Header, &PrivateRecord->Header, *PublicSize, ROSPROF_RECORD_CONTEXT_SWITCH);
                PublicRecord->ContextSwitch.OldProcessId =
                    (ULONG)PrivateRecord->Data.Scheduler.OldProcessId;
                PublicRecord->ContextSwitch.OldThreadId =
                    (ULONG)PrivateRecord->Data.Scheduler.OldThreadId;
                PublicRecord->ContextSwitch.NewProcessId =
                    (ULONG)PrivateRecord->Data.Scheduler.NewProcessId;
                PublicRecord->ContextSwitch.NewThreadId =
                    (ULONG)PrivateRecord->Data.Scheduler.NewThreadId;
                PublicRecord->ContextSwitch.OldProcessKey =
                    PrivateRecord->Data.Scheduler.OldProcessKey;
                PublicRecord->ContextSwitch.OldThreadKey =
                    PrivateRecord->Data.Scheduler.OldThreadKey;
                PublicRecord->ContextSwitch.NewProcessKey =
                    PrivateRecord->Data.Scheduler.NewProcessKey;
                PublicRecord->ContextSwitch.NewThreadKey =
                    PrivateRecord->Data.Scheduler.NewThreadKey;
                PublicRecord->ContextSwitch.OldThreadState =
                    RosprofThreadState(PrivateRecord->Data.Scheduler.OldState);
                PublicRecord->ContextSwitch.OldWaitReason =
                    PrivateRecord->Data.Scheduler.Reason;
                PublicRecord->ContextSwitch.OldPriority =
                    PrivateRecord->Data.Scheduler.OldPriority;
                PublicRecord->ContextSwitch.NewPriority =
                    PrivateRecord->Data.Scheduler.NewPriority;
                PublicRecord->ContextSwitch.NewProcessorNumber =
                    PrivateRecord->Data.Scheduler.TargetProcessor;
                if (PrivateRecord->Data.Scheduler.SchedulerFlags &
                    KPROF_TRACE_SCHED_FLAG_PREEMPTED)
                {
                    PublicRecord->ContextSwitch.SchedulerFlags |=
                        ROSPROF_SCHED_FLAG_PREEMPTED;
                }
                if (PrivateRecord->Data.Scheduler.SchedulerFlags &
                    KPROF_TRACE_SCHED_FLAG_VOLUNTARY)
                {
                    PublicRecord->ContextSwitch.SchedulerFlags |=
                        ROSPROF_SCHED_FLAG_VOLUNTARY;
                }
                if (PrivateRecord->Data.Scheduler.SchedulerFlags &
                    KPROF_TRACE_SCHED_FLAG_MIGRATED)
                {
                    PublicRecord->ContextSwitch.SchedulerFlags |=
                        ROSPROF_SCHED_FLAG_MIGRATED;
                }
            }
            break;

        case KprofTraceRecordSession:
            *PublicSize = sizeof(ROSPROF_SESSION_RECORD_V1);
            Header = &PublicRecord->Session.Header;
            RosprofInitializeRecordHeader(Header, &PrivateRecord->Header, *PublicSize, ROSPROF_RECORD_SESSION);
            Header->Flags |= ROSPROF_RECORD_FLAG_SYNTHETIC;
            PublicRecord->Session.Event = PrivateRecord->Data.Session.Event;
            PublicRecord->Session.Reason = PrivateRecord->Data.Session.Reason;
            PublicRecord->Session.Status = PrivateRecord->Data.Session.Status;
            PublicRecord->Session.State =
                (PrivateRecord->Data.Session.Event == ROSPROF_SESSION_EVENT_END) ?
                ROSPROF_SESSION_STOPPED :
                RosprofMapState(PrivateRecord->Data.Session.State);
            PublicRecord->Session.Sources = PrivateRecord->Data.Session.Sources;
            PublicRecord->Session.ConfigGeneration =
                PrivateRecord->Data.Session.ConfigGeneration;
            PublicRecord->Session.LostRecords =
                PrivateRecord->Data.Session.LostRecords;
            break;

        case KprofTraceRecordSecurity:
            *PublicSize = sizeof(ROSPROF_SECURITY_RECORD_V1);
            Header = &PublicRecord->Security.Header;
            RosprofInitializeRecordHeader(Header, &PrivateRecord->Header, *PublicSize, ROSPROF_RECORD_SECURITY);
            Header->Flags |= ROSPROF_RECORD_FLAG_SYNTHETIC;
            PublicRecord->Security.PrincipalClass =
                PrivateRecord->Data.Security.PrincipalClass;
            PublicRecord->Security.RequestedFeatures =
                PrivateRecord->Data.Security.RequestedFeatures;
            PublicRecord->Security.GrantedFeatures =
                PrivateRecord->Data.Security.GrantedFeatures;
            PublicRecord->Security.DeniedFeatures =
                PrivateRecord->Data.Security.DeniedFeatures;
            PublicRecord->Security.RedactionMode =
                PrivateRecord->Data.Security.RedactionMode;
            PublicRecord->Security.Status =
                PrivateRecord->Data.Security.Status;
            break;

        case KprofTraceRecordClockSync:
            *PublicSize = sizeof(ROSPROF_CLOCK_SYNC_RECORD_V1);
            Header = &PublicRecord->ClockSync.Header;
            RosprofInitializeRecordHeader(Header, &PrivateRecord->Header, *PublicSize, ROSPROF_RECORD_CLOCK_SYNC);
            Header->Flags |= ROSPROF_RECORD_FLAG_SYNTHETIC;
            PublicRecord->ClockSync.SystemTime100ns =
                PrivateRecord->Data.ClockSync.SystemTime100ns;
            PublicRecord->ClockSync.PerformanceCounter =
                PrivateRecord->Data.ClockSync.PerformanceCounter;
            PublicRecord->ClockSync.PerformanceFrequency =
                PrivateRecord->Data.ClockSync.PerformanceFrequency;
            PublicRecord->ClockSync.InterruptTime100ns =
                PrivateRecord->Data.ClockSync.InterruptTime100ns;
            PublicRecord->ClockSync.ClockFlags =
                PrivateRecord->Data.ClockSync.ClockFlags;
            break;

        default:
            return STATUS_NOT_SUPPORTED;
    }

    ASSERT(RosProfIsAlignedU32(*PublicSize));
    return STATUS_SUCCESS;
}

static
VOID
RosprofFillSessionInfo(
    IN PROSPROF_FILE_CONTEXT Context,
    OUT PROSPROF_SESSION_INFO_V1 SessionInfo)
{
    ULONG Processor, ProcessorCount = 0;

    RosprofInitializeStructHeader(&SessionInfo->Header, sizeof(*SessionInfo));
    SessionInfo->SessionId = Context->SessionId;
    SessionInfo->ConfigGeneration = Context->ConfigGeneration;
    SessionInfo->AcceptedSources = Context->AcceptedSources;
    SessionInfo->AcceptedRecordTypes = Context->AcceptedRecordTypes;
    SessionInfo->ActualPeriod100ns =
        (Context->AcceptedSources & ROSPROF_SOURCE_TIMER) ?
        Context->InternalConfig.Interval : 0;
    SessionInfo->TimestampFrequency = 10000000;
    SessionInfo->EffectiveSecurity = Context->EffectiveSecurity;
    for (Processor = 0; Processor < MAXIMUM_PROCESSORS; Processor++)
    {
        if (Context->InternalConfig.Affinity & AFFINITY_MASK(Processor))
            ProcessorCount++;
    }
    SessionInfo->RingCapacityBytes =
        Context->InternalConfig.RingSize * ProcessorCount;
    SessionInfo->ReadWatermarkBytes = Context->ReadWatermark;
    SessionInfo->MaximumStackDepth = (USHORT)Context->InternalConfig.MaximumStackDepth;
    SessionInfo->StackFlags = Context->GrantedStackFlags;
    SessionInfo->State = ROSPROF_SESSION_CONFIGURED;
}

static
NTSTATUS
RosprofConfigure(
    IN OUT PROSPROF_FILE_CONTEXT Context,
    IN OUT PVOID Buffer,
    IN ULONG InputLength,
    IN ULONG OutputLength,
    OUT PULONG_PTR Information)
{
    ROSPROF_CONFIG_V1 PublicConfig;
    PEPROCESS TargetProcess;
    NTSTATUS Status;

    if ((InputLength < sizeof(PublicConfig)) ||
        (OutputLength < sizeof(ROSPROF_SESSION_INFO_V1)))
    {
        return STATUS_BUFFER_TOO_SMALL;
    }
    RtlCopyMemory(&PublicConfig, Buffer, sizeof(PublicConfig));
    if (!RosprofValidateStructHeader(&PublicConfig.Header, sizeof(PublicConfig), InputLength))
    {
        return STATUS_REVISION_MISMATCH;
    }

    KeAcquireGuardedMutex(&Context->Lock);
    if (Context->Configured)
    {
        KeReleaseGuardedMutex(&Context->Lock);
        return STATUS_ALREADY_INITIALIZED;
    }
    Status = RosprofBuildInternalConfig(Context, (PROSPROF_CONFIG_V1)Buffer, InputLength, &TargetProcess);
    if (!NT_SUCCESS(Status))
    {
        KeReleaseGuardedMutex(&Context->Lock);
        return Status;
    }

    Status = RosprofResolveTimerInterval(Context);
    if (!NT_SUCCESS(Status))
    {
        if (TargetProcess) ObDereferenceObject(TargetProcess);
        KeReleaseGuardedMutex(&Context->Lock);
        return Status;
    }

    Status = KprofTraceCreateSession(&Context->InternalConfig, ExGetPreviousMode(), &Context->Session);
    if (TargetProcess) ObDereferenceObject(TargetProcess);
    if (!NT_SUCCESS(Status))
    {
        KeReleaseGuardedMutex(&Context->Lock);
        return Status;
    }

    Context->SessionId = InterlockedIncrement64(&RosprofNextSessionId);
    Context->ConfigGeneration++;
    Context->Configured = TRUE;
    Context->EndOfStreamDelivered = FALSE;
    Context->StopReason = ROSPROF_REASON_NONE;
    Context->FinalStatus = STATUS_SUCCESS;
    RosprofFillSessionInfo(Context, (PROSPROF_SESSION_INFO_V1)Buffer);
    *Information = sizeof(ROSPROF_SESSION_INFO_V1);
    KeReleaseGuardedMutex(&Context->Lock);
    return STATUS_SUCCESS;
}

static
NTSTATUS
RosprofValidateCommand(
    IN PROSPROF_FILE_CONTEXT Context,
    IN const ROSPROF_SESSION_COMMAND_V1 *Command,
    IN ULONG InputLength)
{
    if (!RosprofValidateStructHeader(&Command->Header, sizeof(*Command), InputLength))
        return STATUS_REVISION_MISMATCH;
    if (!Context->Configured || !Context->Session)
        return STATUS_INVALID_DEVICE_STATE;
    if ((Command->SessionId != Context->SessionId) ||
        (Command->ConfigGeneration != Context->ConfigGeneration))
        return STATUS_OBJECT_NAME_NOT_FOUND;
    if (Command->CommandFlags) return STATUS_INVALID_PARAMETER;
    return STATUS_SUCCESS;
}

static
NTSTATUS
RosprofStart(
    IN OUT PROSPROF_FILE_CONTEXT Context,
    IN const ROSPROF_SESSION_COMMAND_V1 *Command,
    IN ULONG InputLength)
{
    NTSTATUS Status;

    KeAcquireGuardedMutex(&Context->Lock);
    Status = RosprofValidateCommand(Context, Command, InputLength);
    if (NT_SUCCESS(Status))
    {
        Status = KprofTraceStartSession(Context->Session);
        if (NT_SUCCESS(Status) && !Context->Started)
        {
            Context->EndOfStreamDelivered = FALSE;
            Context->StopReason = ROSPROF_REASON_NONE;
            Context->FinalStatus = STATUS_SUCCESS;
            if (Context->AcceptedRecordTypes &
                ROSPROF_RECORD_MASK(ROSPROF_RECORD_SESSION))
            {
                Status = KprofTraceEmitSessionRecord(Context->Session, ROSPROF_SESSION_EVENT_BEGIN, ROSPROF_REASON_NONE, STATUS_SUCCESS, Context->AcceptedSources, Context->ConfigGeneration);
            }
            if (NT_SUCCESS(Status) &&
                (Context->AcceptedRecordTypes &
                 ROSPROF_RECORD_MASK(ROSPROF_RECORD_SECURITY)))
            {
                Status = KprofTraceEmitSecurityRecord(Context->Session, Context->RequestedSources, Context->AcceptedSources, Context->SecurityMode, Context->PrincipalClass, STATUS_SUCCESS);
            }
            if (NT_SUCCESS(Status) &&
                (Context->AcceptedRecordTypes &
                 ROSPROF_RECORD_MASK(ROSPROF_RECORD_CLOCK_SYNC)))
            {
                Status = KprofTraceEmitClockSyncRecord(Context->Session);
            }
            if (!NT_SUCCESS(Status))
                KprofTraceStopSession(Context->Session);
            else
                Context->Started = TRUE;
        }
    }
    KeReleaseGuardedMutex(&Context->Lock);
    return Status;
}

static
NTSTATUS
RosprofStop(
    IN OUT PROSPROF_FILE_CONTEXT Context,
    IN const ROSPROF_SESSION_COMMAND_V1 *Command,
    IN ULONG InputLength)
{
    KPROF_TRACE_STATS Stats;
    ULONG StopReason;
    NTSTATUS Status;

    KeAcquireGuardedMutex(&Context->Lock);
    Status = RosprofValidateCommand(Context, Command, InputLength);
    if (NT_SUCCESS(Status))
    {
        StopReason = Command->Reason ? Command->Reason :
                                      ROSPROF_REASON_REQUESTED;
        Status = KprofTraceQueryStats(Context->Session, &Stats);
        if (NT_SUCCESS(Status) && Context->Started && (Stats.State == 1))
        {
            if (Context->AcceptedRecordTypes &
                ROSPROF_RECORD_MASK(ROSPROF_RECORD_CLOCK_SYNC))
            {
                KprofTraceEmitClockSyncRecord(Context->Session);
            }
            if (Context->AcceptedRecordTypes &
                ROSPROF_RECORD_MASK(ROSPROF_RECORD_SESSION))
            {
                KprofTraceEmitSessionRecord(Context->Session, ROSPROF_SESSION_EVENT_END, StopReason, STATUS_SUCCESS, Context->AcceptedSources, Context->ConfigGeneration);
            }
        }
        Status = KprofTraceStopSession(Context->Session);
        Context->FinalStatus = Status;
        if (NT_SUCCESS(Status))
        {
            Context->Started = FALSE;
            Context->StopReason = StopReason;
        }
    }
    KeReleaseGuardedMutex(&Context->Lock);
    return Status;
}

static
NTSTATUS
RosprofValidateQuery(
    IN PROSPROF_FILE_CONTEXT Context,
    IN const ROSPROF_SESSION_QUERY_V1 *Query,
    IN ULONG InputLength)
{
    if (!RosprofValidateStructHeader(&Query->Header, sizeof(*Query), InputLength))
        return STATUS_REVISION_MISMATCH;
    if (!Context->Configured || !Context->Session)
        return STATUS_INVALID_DEVICE_STATE;
    if ((Query->SessionId != Context->SessionId) ||
        (Query->ConfigGeneration != Context->ConfigGeneration))
        return STATUS_OBJECT_NAME_NOT_FOUND;
    return STATUS_SUCCESS;
}

static
NTSTATUS
RosprofQueryStatus(
    IN OUT PROSPROF_FILE_CONTEXT Context,
    IN OUT PVOID Buffer,
    IN ULONG InputLength,
    IN ULONG OutputLength,
    OUT PULONG_PTR Information)
{
    ROSPROF_SESSION_QUERY_V1 Query;
    PROSPROF_SESSION_STATUS_V1 PublicStatus = Buffer;
    KPROF_TRACE_STATS Stats;
    UCHAR NextRecordBuffer[KPROF_TRACE_MAX_RECORD_SIZE];
    PKPROF_TRACE_RECORD NextRecord = (PKPROF_TRACE_RECORD)NextRecordBuffer;
    NTSTATUS Status;
    ULONGLONG Capacity;
    ULONG NextProcessor, NextRecordSize;

    if ((InputLength < sizeof(Query)) ||
        (OutputLength < sizeof(*PublicStatus)))
        return STATUS_BUFFER_TOO_SMALL;
    RtlCopyMemory(&Query, Buffer, sizeof(Query));
    KeAcquireGuardedMutex(&Context->Lock);
    Status = RosprofValidateQuery(Context, &Query, InputLength);
    if (!NT_SUCCESS(Status)) goto Exit;
    Status = KprofTraceQueryStats(Context->Session, &Stats);
    if (!NT_SUCCESS(Status)) goto Exit;

    RosprofInitializeStructHeader(&PublicStatus->Header, sizeof(*PublicStatus));
    PublicStatus->SessionId = Context->SessionId;
    PublicStatus->ConfigGeneration = Context->ConfigGeneration;
    PublicStatus->State = RosprofMapState(Stats.State);
    PublicStatus->FinalStatus = Context->FinalStatus;
    PublicStatus->StopReason = (Stats.State == 3) ?
                               Context->StopReason : ROSPROF_REASON_NONE;
    PublicStatus->ActiveProcessorCount = Stats.ProcessorCount;
    PublicStatus->StartTimestamp = Stats.StartTimestamp;
    PublicStatus->StopTimestamp = Stats.StopTimestamp;
    PublicStatus->Samples = Stats.Samples;
    PublicStatus->FilteredSamples = Stats.FilteredSamples;
    PublicStatus->ContextSwitches = Stats.ContextSwitches;
    PublicStatus->Wakeups = Stats.Wakeups;
    PublicStatus->LifecycleRecords = Stats.LifecycleRecords;
    RosprofInitializeStructHeader(&PublicStatus->Ring.Header, sizeof(PublicStatus->Ring));
    PublicStatus->Ring.SessionId = Context->SessionId;
    PublicStatus->Ring.ProducedRecords = Stats.ProducedRecords;
    PublicStatus->Ring.LostRecords = Stats.LostRecords;
    PublicStatus->Ring.ProducerSequence =
        Stats.ProducedRecords + Stats.LostRecords;
    PublicStatus->Ring.ConsumedRecords =
        InterlockedCompareExchange64(&Context->ConsumedRecords, 0, 0);
    PublicStatus->Ring.ConsumerSequence =
        InterlockedCompareExchange64(&Context->ConsumerSequence, 0, 0);
    PublicStatus->Ring.LostBytes =
        Stats.LostRecords * sizeof(KPROF_TRACE_RECORD);
    PublicStatus->Ring.PendingLossRecords = Stats.PendingLostRecords;
    Capacity = (ULONGLONG)Context->InternalConfig.RingSize *
               Stats.ProcessorCount;
    PublicStatus->Ring.CapacityBytes =
        (Capacity > MAXULONG) ? MAXULONG : (ULONG)Capacity;
    PublicStatus->Ring.UsedBytes =
        (Stats.BytesAvailable > MAXULONG) ? MAXULONG :
        (ULONG)Stats.BytesAvailable;
    PublicStatus->Ring.HighWatermarkBytes =
        (Stats.HighWatermarkBytes > MAXULONG) ? MAXULONG :
        (ULONG)Stats.HighWatermarkBytes;
    if (NT_SUCCESS(RosprofPeekNextRecord(Context, NextRecord, sizeof(NextRecordBuffer), &NextProcessor)) &&
        NT_SUCCESS(RosprofTranslatedRecordSize(Context, NextRecord, &NextRecordSize)))
    {
        PublicStatus->Ring.NextRecordBytes = NextRecordSize;
    }
    *Information = sizeof(*PublicStatus);

Exit:
    KeReleaseGuardedMutex(&Context->Lock);
    return Status;
}

static
NTSTATUS
RosprofQueryAccess(
    IN OUT PROSPROF_FILE_CONTEXT Context,
    IN OUT PVOID Buffer,
    IN ULONG InputLength,
    IN ULONG OutputLength,
    OUT PULONG_PTR Information)
{
    ROSPROF_SESSION_QUERY_V1 Query;
    PROSPROF_ACCESS_INFO_V1 AccessInfo = Buffer;
    NTSTATUS Status;

    if ((InputLength < sizeof(Query)) ||
        (OutputLength < sizeof(*AccessInfo)))
        return STATUS_BUFFER_TOO_SMALL;
    RtlCopyMemory(&Query, Buffer, sizeof(Query));
    KeAcquireGuardedMutex(&Context->Lock);
    Status = RosprofValidateQuery(Context, &Query, InputLength);
    if (!NT_SUCCESS(Status)) goto Exit;

    RosprofInitializeStructHeader(&AccessInfo->Header, sizeof(*AccessInfo));
    AccessInfo->SessionId = Context->SessionId;
    AccessInfo->PolicyFlags = Context->EffectiveSecurity;
    AccessInfo->RequestedSources = Context->RequestedSources;
    AccessInfo->GrantedSources = Context->AcceptedSources;
    AccessInfo->RequestedRecordTypes = Context->RequestedRecordTypes;
    AccessInfo->GrantedRecordTypes = Context->AcceptedRecordTypes;
    AccessInfo->RequestedStackFlags = Context->RequestedStackFlags;
    AccessInfo->GrantedStackFlags = Context->GrantedStackFlags;
    AccessInfo->RedactionMode = Context->SecurityMode;
    AccessInfo->PrincipalClass = Context->PrincipalClass;
    AccessInfo->AccessStatus = STATUS_SUCCESS;
    *Information = sizeof(*AccessInfo);

Exit:
    KeReleaseGuardedMutex(&Context->Lock);
    return Status;
}

static
NTSTATUS
RosprofReset(
    IN OUT PROSPROF_FILE_CONTEXT Context,
    IN const ROSPROF_SESSION_COMMAND_V1 *Command,
    IN ULONG InputLength)
{
    PEPROCESS TargetProcess;
    PKPROF_TRACE_SESSION OldSession;
    KPROF_TRACE_STATS Stats;
    NTSTATUS Status;

    KeAcquireGuardedMutex(&Context->Lock);
    Status = RosprofValidateCommand(Context, Command, InputLength);
    if (!NT_SUCCESS(Status)) goto Exit;
    if (Context->ReadBusy)
    {
        Status = STATUS_DEVICE_BUSY;
        goto Exit;
    }
    Status = KprofTraceQueryStats(Context->Session, &Stats);
    if (!NT_SUCCESS(Status)) goto Exit;
    if (Stats.State != 3)
    {
        Status = STATUS_INVALID_DEVICE_STATE;
        goto Exit;
    }

    TargetProcess = Context->InternalConfig.TargetProcess;
    if (TargetProcess) ObReferenceObject(TargetProcess);
    OldSession = Context->Session;
    Status = KprofTraceDestroySession(OldSession);
    if (!NT_SUCCESS(Status))
    {
        if (TargetProcess) ObDereferenceObject(TargetProcess);
        goto Exit;
    }

    Context->Session = NULL;
    Context->InternalConfig.TargetProcess = TargetProcess;
    Status = KprofTraceCreateSession(&Context->InternalConfig, ExGetPreviousMode(), &Context->Session);
    if (TargetProcess) ObDereferenceObject(TargetProcess);
    if (NT_SUCCESS(Status))
    {
        Context->ConfigGeneration++;
        Context->EndOfStreamDelivered = FALSE;
        Context->NextProcessor = 0;
        Context->Started = FALSE;
        Context->StopReason = ROSPROF_REASON_NONE;
        Context->FinalStatus = STATUS_SUCCESS;
        InterlockedExchange64(&Context->ConsumerSequence, 0);
        InterlockedExchange64(&Context->ConsumedRecords, 0);
    }
    else
    {
        Context->InternalConfig.TargetProcess = NULL;
        Context->Configured = FALSE;
    }

Exit:
    KeReleaseGuardedMutex(&Context->Lock);
    return Status;
}

static
NTSTATUS
RosprofPeekNextRecord(
    IN OUT PROSPROF_FILE_CONTEXT Context,
    OUT PKPROF_TRACE_RECORD Record,
    IN ULONG Capacity,
    OUT PULONG Processor)
{
    UCHAR CandidateBuffer[KPROF_TRACE_MAX_RECORD_SIZE];
    PKPROF_TRACE_RECORD Candidate = (PKPROF_TRACE_RECORD)CandidateBuffer;
    ULONG Offset, CurrentProcessor, BestProcessor = 0;
    BOOLEAN Found = FALSE;
    NTSTATUS Status = STATUS_NO_MORE_ENTRIES;

    for (Offset = 0; Offset < MAXIMUM_PROCESSORS; Offset++)
    {
        CurrentProcessor = (Context->NextProcessor + Offset) % MAXIMUM_PROCESSORS;
        Status = KprofTracePeek(Context->Session, CurrentProcessor, Candidate, sizeof(CandidateBuffer), NULL);
        if (NT_SUCCESS(Status))
        {
            if (Candidate->Header.Size > Capacity) return STATUS_DATA_ERROR;
            if (!Found || (Candidate->Header.Sequence < Record->Header.Sequence))
            {
                RtlCopyMemory(Record, Candidate, Candidate->Header.Size);
                BestProcessor = CurrentProcessor;
                Found = TRUE;
            }
            continue;
        }
        if ((Status != STATUS_NOT_FOUND) && (Status != STATUS_NO_MORE_ENTRIES)) return Status;
    }
    if (Found)
    {
        *Processor = BestProcessor;
        return STATUS_SUCCESS;
    }
    return STATUS_NO_MORE_ENTRIES;
}

static
NTSTATUS
RosprofArmCancelWait(
    IN OUT PIRP Irp,
    IN PROSPROF_FILE_CONTEXT Context)
{
    KIRQL CancelIrql;

    KeClearEvent(&Context->CancelEvent);
    KeMemoryBarrier();
    if (Context->Closing) return STATUS_CANCELLED;
    Irp->Tail.Overlay.DriverContext[0] = Context;
    IoAcquireCancelSpinLock(&CancelIrql);
    if (Irp->Cancel)
    {
        IoReleaseCancelSpinLock(CancelIrql);
        return STATUS_CANCELLED;
    }
    IoSetCancelRoutine(Irp, RosprofCancelRead);
    IoReleaseCancelSpinLock(CancelIrql);
    return STATUS_SUCCESS;
}

static
VOID
RosprofDisarmCancelWait(
    IN OUT PIRP Irp)
{
    KIRQL CancelIrql;

    IoAcquireCancelSpinLock(&CancelIrql);
    IoSetCancelRoutine(Irp, NULL);
    IoReleaseCancelSpinLock(CancelIrql);
    Irp->Tail.Overlay.DriverContext[0] = NULL;
}

VOID
NTAPI
RosprofCancelRead(
    IN PDEVICE_OBJECT DeviceObject,
    IN OUT PIRP Irp)
{
    PROSPROF_FILE_CONTEXT Context;

    UNREFERENCED_PARAMETER(DeviceObject);
    Context = Irp->Tail.Overlay.DriverContext[0];
    if (Context) KeSetEvent(&Context->CancelEvent, IO_NO_INCREMENT, FALSE);
    IoReleaseCancelSpinLock(Irp->CancelIrql);
}

static
NTSTATUS
RosprofProduceReadBatch(
    IN OUT PROSPROF_FILE_CONTEXT Context,
    IN OUT PIRP Irp,
    OUT PVOID OutputBuffer,
    IN ULONG OutputLength,
    OUT PULONG_PTR Information)
{
    PROSPROF_READ_BATCH_V1 Batch = OutputBuffer;
    UCHAR PrivateBuffer[KPROF_TRACE_MAX_RECORD_SIZE];
    UCHAR ConsumedBuffer[KPROF_TRACE_MAX_RECORD_SIZE];
    PKPROF_TRACE_RECORD PrivateRecord = (PKPROF_TRACE_RECORD)PrivateBuffer;
    PKPROF_TRACE_RECORD ConsumedRecord = (PKPROF_TRACE_RECORD)ConsumedBuffer;
    KPROF_TRACE_STATS Stats;
    ULONG PublicSize, Processor, BytesRead;
    NTSTATUS Status;

    if (OutputLength < sizeof(*Batch)) return STATUS_BUFFER_TOO_SMALL;

Retry:
    RtlZeroMemory(OutputBuffer, OutputLength);
    Batch->Magic = ROSPROF_READ_MAGIC;
    Batch->HeaderSize = sizeof(*Batch);
    Batch->Version = ROSPROF_READ_VERSION;
    Batch->RecordAlignment = ROSPROF_RECORD_ALIGNMENT;
    Batch->SessionId = Context->SessionId;
    Batch->ConfigGeneration = Context->ConfigGeneration;
    Batch->RecordsOffset = sizeof(*Batch);

    Status = KprofTraceQueryStats(Context->Session, &Stats);
    if (!NT_SUCCESS(Status)) return Status;
    if ((Stats.State == 1) && Stats.TargetExited)
    {
        ROSPROF_SESSION_COMMAND_V1 Command;

        RosprofInitializeStructHeader(&Command.Header, sizeof(Command));
        Command.SessionId = Context->SessionId;
        Command.ConfigGeneration = Context->ConfigGeneration;
        Command.Reason = ROSPROF_REASON_TARGET_EXIT;
        Status = RosprofStop(Context, &Command, sizeof(Command));
        if (!NT_SUCCESS(Status)) return Status;
        goto Retry;
    }

    if (!Context->NonBlocking && Context->ReadWatermark)
    {
        if ((Stats.State == 1) &&
            (Stats.BytesAvailable < Context->ReadWatermark))
        {
            Status = RosprofArmCancelWait(Irp, Context);
            if (!NT_SUCCESS(Status)) return Status;
            Status = KprofTraceWaitForData(Context->Session, &Context->CancelEvent);
            RosprofDisarmCancelWait(Irp);
            if (!NT_SUCCESS(Status)) return Status;
            goto Retry;
        }
    }

    for (;;)
    {
        Status = RosprofPeekNextRecord(Context, PrivateRecord, sizeof(PrivateBuffer), &Processor);
        if (Status == STATUS_NO_MORE_ENTRIES) break;
        if (!NT_SUCCESS(Status)) return Status;

        Status = RosprofTranslatedRecordSize(Context, PrivateRecord, &PublicSize);
        if (!NT_SUCCESS(Status)) return Status;
        if (PublicSize > OutputLength -
                         (Batch->RecordsOffset + Batch->RecordsBytes))
        {
            Batch->Flags |= ROSPROF_READ_FLAG_MORE_DATA;
            Batch->RequiredSize = Batch->RecordsOffset +
                                  Batch->RecordsBytes + PublicSize;
            break;
        }

        Status = RosprofTranslateRecord(Context, PrivateRecord, (PUCHAR)OutputBuffer + Batch->RecordsOffset + Batch->RecordsBytes, OutputLength - (Batch->RecordsOffset + Batch->RecordsBytes), &PublicSize);
        if (!NT_SUCCESS(Status)) return Status;

        BytesRead = 0;
        Status = KprofTraceDrain(Context->Session, Processor, ConsumedRecord, PrivateRecord->Header.Size, &BytesRead, NULL);
        if (!NT_SUCCESS(Status)) return Status;
        if (BytesRead != PrivateRecord->Header.Size) return STATUS_DATA_ERROR;
        if (ConsumedRecord->Header.Sequence != PrivateRecord->Header.Sequence) return STATUS_DATA_ERROR;

        if (Batch->RecordCount == 0)
            Batch->FirstSequence = PrivateRecord->Header.Sequence;
        Batch->LastSequence = PrivateRecord->Header.Sequence;
        Batch->RecordCount++;
        Batch->RecordsBytes += PublicSize;
        InterlockedExchange64(&Context->ConsumerSequence, (LONG64)PrivateRecord->Header.Sequence + 1);
        InterlockedIncrement64(&Context->ConsumedRecords);
        if (PrivateRecord->Header.Type == KprofTraceRecordLost)
        {
            Batch->LostSinceLastRead += PrivateRecord->Data.Lost.Count;
            Batch->Flags |= ROSPROF_READ_FLAG_LOSS_PENDING;
        }
        Context->NextProcessor = (Processor + 1) % MAXIMUM_PROCESSORS;
    }

    if (Batch->RecordCount || (Batch->Flags & ROSPROF_READ_FLAG_MORE_DATA))
    {
        if (NT_SUCCESS(KprofTraceQueryStats(Context->Session, &Stats)) &&
            Stats.PendingLostRecords)
        {
            Batch->Flags |= ROSPROF_READ_FLAG_LOSS_PENDING;
        }
        *Information = Batch->RecordsOffset + Batch->RecordsBytes;
        return STATUS_SUCCESS;
    }

    Status = KprofTraceQueryStats(Context->Session, &Stats);
    if (!NT_SUCCESS(Status)) return Status;
    if (Stats.PendingLostRecords)
        Batch->Flags |= ROSPROF_READ_FLAG_LOSS_PENDING;
    if ((Stats.State == 3) && (Stats.BytesAvailable == 0))
    {
        if (Context->EndOfStreamDelivered) return STATUS_END_OF_FILE;
        Context->EndOfStreamDelivered = TRUE;
        Batch->Flags |= ROSPROF_READ_FLAG_END_OF_STREAM;
        *Information = sizeof(*Batch);
        return STATUS_SUCCESS;
    }

    if (Context->NonBlocking)
    {
        Batch->Flags |= ROSPROF_READ_FLAG_NONBLOCKING_EMPTY;
        *Information = sizeof(*Batch);
        return STATUS_SUCCESS;
    }

    Status = RosprofArmCancelWait(Irp, Context);
    if (!NT_SUCCESS(Status)) return Status;
    Status = KprofTraceWaitForData(Context->Session, &Context->CancelEvent);
    RosprofDisarmCancelWait(Irp);
    if (!NT_SUCCESS(Status)) return Status;
    goto Retry;
}

static
VOID
RosprofDestroyFileSession(
    IN OUT PROSPROF_FILE_CONTEXT Context)
{
    KAPC_STATE ApcState;
    BOOLEAN Attached = FALSE;

    if (!Context->Session) return;
    if (PsGetCurrentProcess() != Context->OwnerProcess)
    {
        KeStackAttachProcess(&Context->OwnerProcess->Pcb, &ApcState);
        Attached = TRUE;
    }
    KprofTraceDestroySession(Context->Session);
    if (Attached) KeUnstackDetachProcess(&ApcState);
    Context->Session = NULL;
    Context->Configured = FALSE;
}

NTSTATUS
NTAPI
RosprofCreate(
    IN PDEVICE_OBJECT DeviceObject,
    IN OUT PIRP Irp)
{
    PIO_STACK_LOCATION IrpSp;
    PROSPROF_FILE_CONTEXT Context;

    PAGED_CODE();
    UNREFERENCED_PARAMETER(DeviceObject);
    IrpSp = IoGetCurrentIrpStackLocation(Irp);
    Context = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Context), ROSPROF_FILE_TAG);
    if (!Context)
    {
        RosprofCompleteIrp(Irp, STATUS_INSUFFICIENT_RESOURCES, 0);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(Context, sizeof(*Context));
    KeInitializeGuardedMutex(&Context->Lock);
    KeInitializeEvent(&Context->CancelEvent, NotificationEvent, FALSE);
    KeInitializeEvent(&Context->ReadDoneEvent, NotificationEvent, TRUE);
    Context->OwnerProcess = PsGetCurrentProcess();
    ObReferenceObject(Context->OwnerProcess);

    /* Bind the handle to the authenticated identity that opened it, not just
     * the process object: later operations must come from the same principal
     * even if the caller starts impersonating someone else. */
    {
        SECURITY_SUBJECT_CONTEXT Subject;
        PACCESS_TOKEN Token;
        PTOKEN_USER User = NULL;
        NTSTATUS SidStatus;

        SeCaptureSubjectContext(&Subject);
        Token = SeQuerySubjectContextToken(&Subject);
        SidStatus = SeQueryInformationToken(Token, TokenUser, (PVOID *)&User);
        if (NT_SUCCESS(SidStatus) && (User != NULL))
        {
            if (RtlValidSid(User->User.Sid) && (RtlLengthSid(User->User.Sid) <= sizeof(Context->OwnerSid)))
            {
                RtlCopyMemory(Context->OwnerSid, User->User.Sid, RtlLengthSid(User->User.Sid));
                Context->OwnerSidValid = TRUE;
            }
            ExFreePool(User);
        }
        SeReleaseSubjectContext(&Subject);
    }

    IrpSp->FileObject->FsContext = Context;
    RosprofCompleteIrp(Irp, STATUS_SUCCESS, 0);
    return STATUS_SUCCESS;
}

static
BOOLEAN
RosprofCallerIsOwner(
    IN PROSPROF_FILE_CONTEXT Context)
{
    SECURITY_SUBJECT_CONTEXT Subject;
    PACCESS_TOKEN Token;
    PTOKEN_USER User = NULL;
    BOOLEAN Match = FALSE;
    NTSTATUS Status;

    if (PsGetCurrentProcess() != Context->OwnerProcess) return FALSE;
    if (!Context->OwnerSidValid) return TRUE;
    SeCaptureSubjectContext(&Subject);
    Token = SeQuerySubjectContextToken(&Subject);
    Status = SeQueryInformationToken(Token, TokenUser, (PVOID *)&User);
    if (NT_SUCCESS(Status) && (User != NULL))
    {
        if (RtlValidSid(User->User.Sid)) Match = RtlEqualSid(User->User.Sid, (PSID)Context->OwnerSid);
        ExFreePool(User);
    }
    SeReleaseSubjectContext(&Subject);
    return Match;
}

NTSTATUS
NTAPI
RosprofCleanup(
    IN PDEVICE_OBJECT DeviceObject,
    IN OUT PIRP Irp)
{
    PIO_STACK_LOCATION IrpSp;
    PROSPROF_FILE_CONTEXT Context;

    PAGED_CODE();
    UNREFERENCED_PARAMETER(DeviceObject);
    IrpSp = IoGetCurrentIrpStackLocation(Irp);
    Context = IrpSp->FileObject->FsContext;
    if (Context)
    {
        InterlockedExchange(&Context->Closing, 1);
        KeSetEvent(&Context->CancelEvent, IO_NO_INCREMENT, FALSE);
        if (Context->ReadBusy)
        {
            KeWaitForSingleObject(&Context->ReadDoneEvent, Executive, KernelMode, FALSE, NULL);
        }
        KeAcquireGuardedMutex(&Context->Lock);
        RosprofDestroyFileSession(Context);
        KeReleaseGuardedMutex(&Context->Lock);
    }
    RosprofCompleteIrp(Irp, STATUS_SUCCESS, 0);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
RosprofClose(
    IN PDEVICE_OBJECT DeviceObject,
    IN OUT PIRP Irp)
{
    PIO_STACK_LOCATION IrpSp;
    PROSPROF_FILE_CONTEXT Context;

    PAGED_CODE();
    UNREFERENCED_PARAMETER(DeviceObject);
    IrpSp = IoGetCurrentIrpStackLocation(Irp);
    Context = IrpSp->FileObject->FsContext;
    if (Context)
    {
        InterlockedExchange(&Context->Closing, 1);
        KeSetEvent(&Context->CancelEvent, IO_NO_INCREMENT, FALSE);
        if (Context->ReadBusy)
        {
            KeWaitForSingleObject(&Context->ReadDoneEvent, Executive, KernelMode, FALSE, NULL);
        }
        KeAcquireGuardedMutex(&Context->Lock);
        RosprofDestroyFileSession(Context);
        KeReleaseGuardedMutex(&Context->Lock);
        IrpSp->FileObject->FsContext = NULL;
        ObDereferenceObject(Context->OwnerProcess);
        RtlSecureZeroMemory(Context, sizeof(*Context));
        ExFreePoolWithTag(Context, ROSPROF_FILE_TAG);
    }
    RosprofCompleteIrp(Irp, STATUS_SUCCESS, 0);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
RosprofDeviceControl(
    IN PDEVICE_OBJECT DeviceObject,
    IN OUT PIRP Irp)
{
    PIO_STACK_LOCATION IrpSp;
    PROSPROF_FILE_CONTEXT Context;
    PVOID Buffer;
    ULONG InputLength, OutputLength, ControlCode;
    ULONG_PTR Information = 0;
    NTSTATUS Status;

    PAGED_CODE();
    UNREFERENCED_PARAMETER(DeviceObject);
    IrpSp = IoGetCurrentIrpStackLocation(Irp);
    Context = IrpSp->FileObject->FsContext;
    Buffer = Irp->AssociatedIrp.SystemBuffer;
    InputLength = IrpSp->Parameters.DeviceIoControl.InputBufferLength;
    OutputLength = IrpSp->Parameters.DeviceIoControl.OutputBufferLength;
    ControlCode = IrpSp->Parameters.DeviceIoControl.IoControlCode;
    if (!Context || Context->Closing)
    {
        Status = Context ? STATUS_FILE_CLOSED : STATUS_INVALID_HANDLE;
        goto Complete;
    }
    if (!RosprofCallerIsOwner(Context))
    {
        Status = STATUS_ACCESS_DENIED;
        goto Complete;
    }

    switch (ControlCode)
    {
        case IOCTL_ROSPROF_QUERY_CAPABILITIES:
            if (OutputLength < sizeof(ROSPROF_CAPABILITIES_V1))
            {
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }
            RosprofQueryCapabilities((PROSPROF_CAPABILITIES_V1)Buffer);
            Information = sizeof(ROSPROF_CAPABILITIES_V1);
            Status = STATUS_SUCCESS;
            break;

        case IOCTL_ROSPROF_CONFIGURE:
            Status = RosprofConfigure(Context, Buffer, InputLength, OutputLength, &Information);
            break;

        case IOCTL_ROSPROF_START:
            if (InputLength < sizeof(ROSPROF_SESSION_COMMAND_V1))
                Status = STATUS_BUFFER_TOO_SMALL;
            else
                Status = RosprofStart(Context, (PROSPROF_SESSION_COMMAND_V1)Buffer, InputLength);
            break;

        case IOCTL_ROSPROF_STOP:
            if (InputLength < sizeof(ROSPROF_SESSION_COMMAND_V1))
                Status = STATUS_BUFFER_TOO_SMALL;
            else
                Status = RosprofStop(Context, (PROSPROF_SESSION_COMMAND_V1)Buffer, InputLength);
            break;

        case IOCTL_ROSPROF_QUERY_STATUS:
            Status = RosprofQueryStatus(Context, Buffer, InputLength, OutputLength, &Information);
            break;

        case IOCTL_ROSPROF_QUERY_ACCESS:
            Status = RosprofQueryAccess(Context, Buffer, InputLength, OutputLength, &Information);
            break;

        case IOCTL_ROSPROF_RESET:
            if (InputLength < sizeof(ROSPROF_SESSION_COMMAND_V1))
                Status = STATUS_BUFFER_TOO_SMALL;
            else
                Status = RosprofReset(Context, (PROSPROF_SESSION_COMMAND_V1)Buffer, InputLength);
            break;

        case IOCTL_ROSPROF_ENUM_PMU_EVENTS:
            if ((InputLength < sizeof(ROSPROF_PMU_ENUM_REQUEST_V1)) ||
                (OutputLength < sizeof(ROSPROF_PMU_EVENT_LIST_V1)))
            {
                Status = STATUS_BUFFER_TOO_SMALL;
                break;
            }
            if (!RosprofValidateStructHeader(&((PROSPROF_PMU_ENUM_REQUEST_V1)Buffer)->Header, sizeof(ROSPROF_PMU_ENUM_REQUEST_V1), InputLength))
            {
                Status = STATUS_REVISION_MISMATCH;
                break;
            }
            RosprofInitializeStructHeader(&((PROSPROF_PMU_EVENT_LIST_V1)Buffer)->Header, sizeof(ROSPROF_PMU_EVENT_LIST_V1));
            Information = sizeof(ROSPROF_PMU_EVENT_LIST_V1);
            Status = STATUS_SUCCESS;
            break;

        default:
            Status = STATUS_INVALID_DEVICE_REQUEST;
            break;
    }

Complete:
    RosprofCompleteIrp(Irp, Status, Information);
    return Status;
}

NTSTATUS
NTAPI
RosprofRead(
    IN PDEVICE_OBJECT DeviceObject,
    IN OUT PIRP Irp)
{
    PIO_STACK_LOCATION IrpSp;
    PROSPROF_FILE_CONTEXT Context;
    PVOID OutputBuffer;
    ULONG OutputLength;
    ULONG_PTR Information = 0;
    NTSTATUS Status;

    PAGED_CODE();
    UNREFERENCED_PARAMETER(DeviceObject);
    IrpSp = IoGetCurrentIrpStackLocation(Irp);
    Context = IrpSp->FileObject->FsContext;
    OutputLength = IrpSp->Parameters.Read.Length;
    if (!Context || Context->Closing || !RosprofCallerIsOwner(Context))
    {
        Status = !Context ? STATUS_INVALID_HANDLE :
                 Context->Closing ? STATUS_FILE_CLOSED :
                 STATUS_ACCESS_DENIED;
        goto Complete;
    }
    if (InterlockedCompareExchange(&Context->ReadBusy, 1, 0) != 0)
    {
        Status = STATUS_DEVICE_BUSY;
        goto Complete;
    }
    KeClearEvent(&Context->ReadDoneEvent);
    KeMemoryBarrier();
    if (Context->Closing)
    {
        Status = STATUS_FILE_CLOSED;
        goto ReleaseReader;
    }
    if (!Irp->MdlAddress)
    {
        Status = STATUS_INVALID_USER_BUFFER;
        goto ReleaseReader;
    }
    OutputBuffer = MmGetSystemAddressForMdlSafe(Irp->MdlAddress, NormalPagePriority);
    if (!OutputBuffer)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto ReleaseReader;
    }

    if (!Context->Configured || !Context->Session)
        Status = STATUS_INVALID_DEVICE_STATE;
    else
        Status = RosprofProduceReadBatch(Context, Irp, OutputBuffer, OutputLength, &Information);

ReleaseReader:
    InterlockedExchange(&Context->ReadBusy, 0);
    KeSetEvent(&Context->ReadDoneEvent, IO_NO_INCREMENT, FALSE);
Complete:
    RosprofCompleteIrp(Irp, Status, Information);
    return Status;
}

_Function_class_(DRIVER_INITIALIZE)
NTSTATUS
NTAPI
RosprofDriverEntry(
    IN PDRIVER_OBJECT DriverObject,
    IN PUNICODE_STRING RegistryPath)
{
    UNICODE_STRING DeviceName = RTL_CONSTANT_STRING(ROSPROF_NT_DEVICE_NAME);
    UNICODE_STRING DosDeviceName = RTL_CONSTANT_STRING(ROSPROF_DOS_DEVICE_NAME);
    NTSTATUS Status;

    PAGED_CODE();
    UNREFERENCED_PARAMETER(RegistryPath);
    Status = IoCreateDevice(DriverObject, 0, &DeviceName, FILE_DEVICE_ROSPROF, FILE_DEVICE_SECURE_OPEN, FALSE, &RosprofDeviceObject);
    if (!NT_SUCCESS(Status)) return Status;
    Status = IoCreateSymbolicLink(&DosDeviceName, &DeviceName);
    if (!NT_SUCCESS(Status))
    {
        IoDeleteDevice(RosprofDeviceObject);
        RosprofDeviceObject = NULL;
        return Status;
    }

    DriverObject->MajorFunction[IRP_MJ_CREATE] = RosprofCreate;
    DriverObject->MajorFunction[IRP_MJ_CLEANUP] = RosprofCleanup;
    DriverObject->MajorFunction[IRP_MJ_CLOSE] = RosprofClose;
    DriverObject->MajorFunction[IRP_MJ_DEVICE_CONTROL] = RosprofDeviceControl;
    DriverObject->MajorFunction[IRP_MJ_READ] = RosprofRead;
    RosprofDeviceObject->Flags |= DO_DIRECT_IO;
    RosprofDeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;
    return STATUS_SUCCESS;
}

BOOLEAN
NTAPI
RosprofInitialize(VOID)
{
    UNICODE_STRING DriverName = RTL_CONSTANT_STRING(L"\\Driver\\RosProf");
    NTSTATUS Status;

    PAGED_CODE();
    Status = IoCreateDriver(&DriverName, RosprofDriverEntry);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("RosProf: IoCreateDriver failed: 0x%08lx\n", Status);
        return FALSE;
    }
    return TRUE;
}
