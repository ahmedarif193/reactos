/*
 * PROJECT:     ReactOS Performance Analyzer
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     ReactOS RosProf kernel trace-session adapter
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "profiler_recorder_internal.h"

#include <reactos/rosprof.h>

typedef struct _RPERF_KERNEL_ID
{
    ULONG NumericId;
    ULONGLONG StableId;
    ULONGLONG KernelId;
    ULONGLONG PendingStableId;
    ULONGLONG PendingKernelId;
    BOOL Active;
    BOOL KernelIdValid;
    BOOL KernelIdConfirmed;
    BOOL PendingKernelIdValid;
    BOOL PendingValid;
} RPERF_KERNEL_ID;

typedef struct _RPERF_KERNEL_STATE
{
    RPERF_CAPTURE_CONFIGURATION Config;
    HANDLE Device;
    HANDLE ReaderThread;
    HANDLE DurationThread;
    HANDLE StopEvent;
    HANDLE StopCompleteEvent;
    volatile LONG StopIssued;
    volatile LONG StopError;
    ROSPROF_CAPABILITIES_V1 Capabilities;
    ROSPROF_SESSION_INFO_V1 Session;
    RPERF_RECORDING *Recording;
    RPERF_KERNEL_ID *Processes;
    SIZE_T ProcessCount;
    SIZE_T ProcessCapacity;
    RPERF_KERNEL_ID *Threads;
    SIZE_T ThreadCount;
    SIZE_T ThreadCapacity;
    PUCHAR ReadBuffer;
    ULONG ReadCapacity;
    ULONG MaximumReadBytes;
    ULONGLONG LastSequence;
    BOOL HaveLastSequence;
    ULONGLONG NextSequence;
    volatile LONG DurationExpired;
    volatile LONG TargetExited;
    RPERF_BASELINE_RESULT Baseline;
    DWORD Error;
} RPERF_KERNEL_STATE;

static VOID
RperfRosProfInitializeHeader(PROSPROF_STRUCT_HEADER Header,
                            ULONG Size)
{
    ZeroMemory(Header, Size);
    Header->Size = Size;
    Header->MajorVersion = ROSPROF_ABI_VERSION_MAJOR;
    Header->MinorVersion = ROSPROF_ABI_VERSION_MINOR;
}

static BOOL
RperfRosProfStructValid(const ROSPROF_STRUCT_HEADER *Header,
                       ULONG Returned,
                       ULONG Minimum)
{
    return Header != NULL && Returned >= Minimum &&
           Header->Size >= Minimum && Header->Size <= Returned &&
           Header->MajorVersion == ROSPROF_ABI_VERSION_MAJOR;
}

static BOOL
RperfRosProfIoctl(HANDLE Device,
                  DWORD Code,
                  PVOID Input,
                  DWORD InputBytes,
                  PVOID Output,
                  DWORD OutputBytes,
                  PDWORD Returned)
{
    DWORD Local = 0;
    if (Returned == NULL)
        Returned = &Local;
    return DeviceIoControl(Device, Code,
                           Input, InputBytes,
                           Output, OutputBytes,
                           Returned, NULL);
}

static ULONG
RperfRosProfFeatureMap(ULONGLONG Capabilities)
{
    ULONG Features = 0;
    if (Capabilities & ROSPROF_CAP_TIMER_SAMPLE)
        Features |= RPERF_CAP_TIMER;
    if (Capabilities & ROSPROF_CAP_USER_STACK)
        Features |= RPERF_CAP_USER_STACKS | RPERF_CAP_STACK_WALK;
    if (Capabilities & ROSPROF_CAP_KERNEL_STACK)
        Features |= RPERF_CAP_KERNEL_STACKS | RPERF_CAP_STACK_WALK;
    if (Capabilities & ROSPROF_CAP_PMU_SAMPLE)
        Features |= RPERF_CAP_PMU;
    if (Capabilities & ROSPROF_CAP_PROCESS_LIFECYCLE)
        Features |= RPERF_CAP_PROCESS_EVENTS;
    if (Capabilities & ROSPROF_CAP_THREAD_LIFECYCLE)
        Features |= RPERF_CAP_THREAD_EVENTS;
    if (Capabilities & ROSPROF_CAP_IMAGE_LIFECYCLE)
        Features |= RPERF_CAP_IMAGE_EVENTS;
    if (Capabilities & (ROSPROF_CAP_CONTEXT_SWITCH |
                        ROSPROF_CAP_SCHED_WAKEUP))
        Features |= RPERF_CAP_SCHEDULER_EVENTS;
    if (Capabilities & ROSPROF_CAP_LOSS_RECORDS)
        Features |= RPERF_CAP_LOSS_ACCOUNTING;
    if (Capabilities & ROSPROF_CAP_SYSTEM_WIDE)
        Features |= RPERF_CAP_SYSTEM_WIDE;
    /* PROCESS_SCOPED does not imply child inheritance/rundown. */
    return Features;
}

BOOL
RperfKernelQueryCapabilities(RPERF_RECORDER_CAPABILITIES *Result)
{
    HANDLE Device;
    ROSPROF_CAPABILITIES_V1 Capabilities;
    DWORD Returned, Error;

    Device = CreateFileW(ROSPROF_WIN32_DEVICE_NAME,
                         GENERIC_READ,
                         FILE_SHARE_READ | FILE_SHARE_WRITE,
                         NULL,
                         OPEN_EXISTING,
                         FILE_ATTRIBUTE_NORMAL,
                         NULL);
    if (Device == INVALID_HANDLE_VALUE)
    {
        Error = GetLastError();
        Result->Available = FALSE;
        Result->RequiresPrivilege = Error == ERROR_ACCESS_DENIED;
        Result->Status = Error;
        if (Error == ERROR_ACCESS_DENIED)
            lstrcpyW(Result->Description,
                     L"RosProf is installed, but this user is not allowed to "
                     L"open the profiling device.");
        else
            lstrcpyW(Result->Description,
                     L"The ReactOS RosProf profiling device is unavailable.");
        SetLastError(Error);
        return TRUE;
    }
    ZeroMemory(&Capabilities, sizeof(Capabilities));
    if (!RperfRosProfIoctl(Device,
                          IOCTL_ROSPROF_QUERY_CAPABILITIES,
                          NULL, 0,
                          &Capabilities, sizeof(Capabilities),
                          &Returned))
    {
        Error = GetLastError();
        CloseHandle(Device);
        Result->Status = Error;
        SetLastError(Error);
        return TRUE;
    }
    CloseHandle(Device);
    if (!RperfRosProfStructValid(&Capabilities.Header,
                                 Returned,
                                 sizeof(Capabilities)) ||
        Capabilities.RecordAlignment != ROSPROF_RECORD_ALIGNMENT ||
        Capabilities.ReadHeaderSize < sizeof(ROSPROF_READ_BATCH_V1) ||
        Capabilities.MaximumRecordBytes < sizeof(ROSPROF_RECORD_HEADER))
    {
        Result->Status = ERROR_REVISION_MISMATCH;
        lstrcpyW(Result->Description,
                 L"RosProf returned an incompatible capability contract.");
        SetLastError(Result->Status);
        return TRUE;
    }
    Result->Available = TRUE;
    Result->RequiresPrivilege = TRUE;
    Result->Features = RperfRosProfFeatureMap(Capabilities.Capabilities);
    if (Capabilities.MaximumStackDepth <= 1)
        Result->Features &= ~RPERF_CAP_STACK_WALK;
    Result->MinimumIntervalUs =
        max(1, (Capabilities.MinimumPeriod100ns + 9) / 10);
    Result->MaximumStackDepth = Capabilities.MaximumStackDepth;
    Result->AbiVersion = (ROSPROF_ABI_VERSION_MAJOR << 16) |
                         ROSPROF_ABI_VERSION_MINOR;
    Result->Status = ERROR_SUCCESS;
    if (Capabilities.MaximumStackDepth <= 1 &&
        (Capabilities.Capabilities & ROSPROF_CAP_PMU_SAMPLE) == 0)
    {
        lstrcpyW(Result->Description,
                 L"ReactOS RosProf timer sampling; this kernel currently "
                 L"provides one instruction-pointer frame and no PMU events.");
    }
    else if (Capabilities.MaximumStackDepth <= 1)
    {
        lstrcpyW(Result->Description,
                 L"ReactOS RosProf sampling; this kernel currently provides "
                 L"one instruction-pointer frame rather than full call chains.");
    }
    else
    {
        lstrcpyW(Result->Description,
                 L"ReactOS RosProf non-intrusive kernel trace session.");
    }
    return TRUE;
}

static BOOL
RperfKernelGrowIds(RPERF_KERNEL_ID **Ids,
                   SIZE_T *Capacity,
                   SIZE_T Required)
{
    SIZE_T NewCapacity;
    PVOID NewBuffer;
    if (Required <= *Capacity)
        return TRUE;
    NewCapacity = *Capacity != 0 ? *Capacity * 2 : 64;
    if (NewCapacity < Required)
        NewCapacity = Required;
    if (NewCapacity > ((SIZE_T)-1) / sizeof(**Ids))
        return FALSE;
    if (*Ids != NULL)
        NewBuffer = HeapReAlloc(GetProcessHeap(), 0, *Ids,
                                NewCapacity * sizeof(**Ids));
    else
        NewBuffer = HeapAlloc(GetProcessHeap(), 0,
                              NewCapacity * sizeof(**Ids));
    if (NewBuffer == NULL)
        return FALSE;
    *Ids = NewBuffer;
    *Capacity = NewCapacity;
    return TRUE;
}

static SIZE_T
RperfKernelFindIdIndex(const RPERF_KERNEL_ID *Ids,
                       SIZE_T Count,
                       ULONG NumericId)
{
    SIZE_T Index;
    for (Index = Count; Index != 0; --Index)
    {
        if (Ids[Index - 1].NumericId == NumericId)
            return Index - 1;
    }
    return Count;
}

static ULONGLONG
RperfKernelFindId(const RPERF_KERNEL_ID *Ids,
                  SIZE_T Count,
                  ULONG NumericId)
{
    SIZE_T Index = RperfKernelFindIdIndex(Ids, Count, NumericId);

    if (Index != Count)
        return Ids[Index].StableId;
    return NumericId;
}

static ULONGLONG
RperfKernelResolveId(const RPERF_KERNEL_ID *Ids,
                     SIZE_T Count,
                     ULONG NumericId,
                     ULONGLONG KernelId)
{
    SIZE_T Index;
    const RPERF_KERNEL_ID *Entry;

    if (KernelId == 0)
        return RperfKernelFindId(Ids, Count, NumericId);
    Index = RperfKernelFindIdIndex(Ids, Count, NumericId);
    if (Index == Count)
        return KernelId;
    Entry = &Ids[Index];
    if (Entry->KernelIdValid && Entry->KernelId == KernelId)
        return Entry->StableId;
    if (Entry->PendingValid && Entry->PendingKernelIdValid &&
        Entry->PendingKernelId == KernelId)
    {
        return Entry->PendingStableId;
    }
    return KernelId;
}

static BOOL
RperfKernelSetBaselineId(RPERF_KERNEL_ID **Ids,
                         SIZE_T *Count,
                         SIZE_T *Capacity,
                         ULONG NumericId,
                         ULONGLONG StableId,
                         ULONGLONG KernelId,
                         BOOL KernelIdValid)
{
    SIZE_T Index = RperfKernelFindIdIndex(*Ids, *Count, NumericId);
    RPERF_KERNEL_ID *Entry;

    if (Index == *Count)
    {
        if (!RperfKernelGrowIds(Ids, Capacity, *Count + 1))
            return FALSE;
        (*Count)++;
    }
    Entry = &(*Ids)[Index];
    Entry->NumericId = NumericId;
    Entry->StableId = StableId;
    Entry->KernelId = KernelId;
    Entry->Active = TRUE;
    Entry->KernelIdValid = KernelIdValid;
    Entry->KernelIdConfirmed = FALSE;
    Entry->PendingValid = FALSE;
    return TRUE;
}

static BOOL
RperfKernelStartId(RPERF_KERNEL_ID **Ids,
                   SIZE_T *Count,
                   SIZE_T *Capacity,
                   ULONG NumericId,
                   ULONGLONG KernelId,
                   ULONGLONG *StableId)
{
    SIZE_T Index = RperfKernelFindIdIndex(*Ids, *Count, NumericId);
    RPERF_KERNEL_ID *Entry;

    if (Index == *Count)
    {
        if (!RperfKernelGrowIds(Ids, Capacity, *Count + 1))
            return FALSE;
        (*Count)++;
        Entry = &(*Ids)[Index];
        Entry->NumericId = NumericId;
        Entry->StableId = KernelId != 0 ? KernelId : NumericId;
        Entry->KernelIdValid = FALSE;
        Entry->KernelIdConfirmed = FALSE;
        Entry->PendingValid = FALSE;
        Entry->Active = FALSE;
    }
    else
    {
        Entry = &(*Ids)[Index];
    }
    if (Entry->PendingValid && Entry->PendingKernelIdValid &&
        Entry->PendingKernelId == KernelId)
    {
        /* A loss may have hidden the older lifetime's END.  The expected
         * baseline START is still authoritative when it arrives. */
        Entry->StableId = Entry->PendingStableId;
        Entry->KernelId = Entry->PendingKernelId;
        Entry->KernelIdValid = TRUE;
        Entry->KernelIdConfirmed = TRUE;
        Entry->PendingValid = FALSE;
        Entry->Active = TRUE;
        *StableId = Entry->StableId;
        return TRUE;
    }
    if (Entry->Active && Entry->KernelIdValid &&
        !Entry->KernelIdConfirmed && Entry->KernelId != KernelId)
    {
        /* This queued START predates the object captured by the baseline.
         * Make it current for intervening samples while the baseline waits
         * for the older lifetime's END. */
        Entry->PendingStableId = Entry->StableId;
        Entry->PendingKernelId = Entry->KernelId;
        Entry->PendingKernelIdValid = Entry->KernelIdValid;
        Entry->PendingValid = TRUE;
        Entry->StableId = KernelId != 0 ? KernelId : NumericId;
        Entry->KernelId = KernelId;
        Entry->KernelIdValid = TRUE;
        Entry->KernelIdConfirmed = TRUE;
        Entry->Active = TRUE;
        *StableId = Entry->StableId;
        return TRUE;
    }
    if (!Entry->Active ||
        (Entry->KernelIdValid && Entry->KernelIdConfirmed &&
         Entry->KernelId != KernelId))
    {
        Entry->StableId = KernelId != 0 ? KernelId : NumericId;
    }
    Entry->KernelId = KernelId;
    Entry->KernelIdValid = TRUE;
    Entry->KernelIdConfirmed = TRUE;
    Entry->Active = TRUE;
    *StableId = Entry->StableId;
    return TRUE;
}

static ULONGLONG
RperfKernelEndId(RPERF_KERNEL_ID *Ids,
                 SIZE_T Count,
                 ULONG NumericId,
                 ULONGLONG KernelId)
{
    SIZE_T Index = RperfKernelFindIdIndex(Ids, Count, NumericId);
    RPERF_KERNEL_ID *Entry;

    if (Index == Count)
        return KernelId != 0 ? KernelId : NumericId;
    Entry = &Ids[Index];
    if (!Entry->KernelIdValid || Entry->KernelId == KernelId)
    {
        ULONGLONG StableId = Entry->StableId;

        if (Entry->PendingValid)
        {
            Entry->StableId = Entry->PendingStableId;
            Entry->KernelId = Entry->PendingKernelId;
            Entry->KernelIdValid = Entry->PendingKernelIdValid;
            Entry->KernelIdConfirmed = FALSE;
            Entry->PendingValid = FALSE;
            Entry->Active = TRUE;
        }
        else
        {
            Entry->KernelId = KernelId;
            Entry->KernelIdValid = TRUE;
            Entry->KernelIdConfirmed = TRUE;
            Entry->Active = FALSE;
        }
        return StableId;
    }
    return KernelId != 0 ? KernelId : NumericId;
}

static ULONGLONG
RperfKernelObjectKey(ULONGLONG CreationTime100ns,
                     ULONG NumericId,
                     BOOL Thread)
{
    /* Keep this mixer in sync with KiKprofTraceMixObjectKey so a Toolhelp
     * baseline can validate queued kernel lifecycle records across ID reuse. */
    ULONGLONG Domain = Thread ? 0x5448524541440001ULL :
                                0x50524f4345535301ULL;
    ULONGLONG Key = CreationTime100ns ^
                    ((ULONGLONG)NumericId << 32) ^ NumericId ^ Domain;

    Key ^= Key >> 30;
    Key *= 0xbf58476d1ce4e5b9ULL;
    Key ^= Key >> 27;
    Key *= 0x94d049bb133111ebULL;
    Key ^= Key >> 31;
    return Key != 0 ? Key : Domain;
}

static BOOL CALLBACK
RperfKernelBaselineId(PVOID Opaque,
                      BOOL Thread,
                      ULONG NumericId,
                      ULONGLONG StableId,
                      ULONGLONG CreationTime100ns,
                      BOOL CreationTimeValid)
{
    RPERF_KERNEL_STATE *State = Opaque;
    ULONGLONG KernelId = 0;

    if (CreationTimeValid)
        KernelId = RperfKernelObjectKey(CreationTime100ns,
                                        NumericId,
                                        Thread);

    if (Thread)
    {
        return RperfKernelSetBaselineId(&State->Threads,
                                        &State->ThreadCount,
                                        &State->ThreadCapacity,
                                        NumericId, StableId,
                                        KernelId, CreationTimeValid);
    }
    return RperfKernelSetBaselineId(&State->Processes,
                                    &State->ProcessCount,
                                    &State->ProcessCapacity,
                                    NumericId, StableId,
                                    KernelId, CreationTimeValid);
}

static ULONGLONG
RperfKernelTimestampNs(const RPERF_KERNEL_STATE *State,
                       ULONGLONG Timestamp)
{
    ULONGLONG Frequency = State->Capabilities.TimestampFrequency;
    ULONGLONG Whole, Remainder;
    if (Frequency == 0)
        return Timestamp;
    Whole = Timestamp / Frequency;
    Remainder = Timestamp % Frequency;
    if (Whole > (ULONGLONG)-1 / 1000000000ULL)
        return (ULONGLONG)-1;
    return Whole * 1000000000ULL +
           (Remainder * 1000000000ULL) / Frequency;
}

static BOOL
RperfKernelUtf8Path(const UCHAR *Record,
                    ULONG RecordBytes,
                    ULONG Offset,
                    ULONG Bytes,
                    ULONG MaximumBytes,
                    PWSTR *Path)
{
    int Characters;
    PWSTR Buffer;

    *Path = NULL;
    if (Bytes == 0)
        return Offset == 0 || Offset <= RecordBytes;
    if (!RosProfRangeValidU32(RecordBytes, Offset, Bytes) ||
        Bytes >= MaximumBytes)
        return FALSE;
    Characters = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                                     (PCSTR)(Record + Offset),
                                     Bytes, NULL, 0);
    if (Characters <= 0 ||
        (SIZE_T)Characters + 1 > ((SIZE_T)-1) / sizeof(WCHAR))
        return FALSE;
    Buffer = HeapAlloc(GetProcessHeap(), 0,
                       ((SIZE_T)Characters + 1) * sizeof(WCHAR));
    if (Buffer == NULL)
        return FALSE;
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                             (PCSTR)(Record + Offset), Bytes,
                             Buffer, Characters))
    {
        HeapFree(GetProcessHeap(), 0, Buffer);
        return FALSE;
    }
    Buffer[Characters] = UNICODE_NULL;
    *Path = Buffer;
    return TRUE;
}

static ULONGLONG
RperfKernelModuleForAddress(const RPERF_RECORDING *Recording,
                            ULONGLONG ProcessKey,
                            ULONGLONG Address)
{
    SIZE_T Index;
    for (Index = Recording->ModuleCount; Index != 0; --Index)
    {
        const RPERF_MODULE *Module = &Recording->Modules[Index - 1];
        if (Module->ProcessKey != 0 && ProcessKey != 0 &&
            Module->ProcessKey != ProcessKey)
            continue;
        if (Address >= Module->Base &&
            Address - Module->Base < Module->Size)
            return Module->Id;
    }
    return RPERF_MODEL_INVALID_ID;
}

static ULONG
RperfKernelImageArchitecture(ULONG ImageFlags,
                             ULONG Fallback)
{
    switch (ROSPROF_IMAGE_MACHINE(ImageFlags))
    {
        case IMAGE_FILE_MACHINE_I386: return ROSPROF_ARCH_X86;
        case IMAGE_FILE_MACHINE_AMD64: return ROSPROF_ARCH_AMD64;
        case IMAGE_FILE_MACHINE_ARM:
        case IMAGE_FILE_MACHINE_ARMNT: return ROSPROF_ARCH_ARM;
        case IMAGE_FILE_MACHINE_ARM64: return ROSPROF_ARCH_ARM64;
        default: return Fallback;
    }
}

BOOL
RperfKernelDecodeImageRecord(const ROSPROF_RECORD_HEADER *Header,
                             ULONG MaximumStringBytes,
                             ULONG FallbackArchitecture,
                             ULONGLONG ProcessKey,
                             RPERF_MODULE *Module,
                             PWSTR *Path)
{
    const ROSPROF_IMAGE_RECORD_V1 *Source;
    const UCHAR *BuildId;
    ULONGLONG ModuleId;
    ULONG PathEnd, BuildIdEnd;

    if (!Header || !Module || !Path ||
        Header->Type != ROSPROF_RECORD_IMAGE ||
        Header->Size < sizeof(*Source))
    {
        SetLastError(ERROR_BAD_FORMAT);
        return FALSE;
    }
    Source = (const ROSPROF_IMAGE_RECORD_V1 *)Header;
    if ((Source->Event != ROSPROF_IMAGE_LOAD &&
         Source->Event != ROSPROF_IMAGE_UNLOAD) ||
        (Source->PathBytes != 0 &&
         (Source->PathOffset < sizeof(*Source) ||
          !RosProfIsAlignedU32(Source->PathOffset))) ||
        (Source->BuildIdBytes != 0 &&
         (Source->BuildIdOffset < sizeof(*Source) ||
          !RosProfIsAlignedU32(Source->BuildIdOffset))) ||
        !RosProfRangeValidU32(Header->Size,
                              Source->PathOffset,
                              Source->PathBytes) ||
        !RosProfRangeValidU32(Header->Size,
                              Source->BuildIdOffset,
                              Source->BuildIdBytes))
    {
        SetLastError(ERROR_BAD_FORMAT);
        return FALSE;
    }
    PathEnd = Source->PathOffset + Source->PathBytes;
    BuildIdEnd = Source->BuildIdOffset + Source->BuildIdBytes;
    if (Source->PathBytes != 0 && Source->BuildIdBytes != 0 &&
        Source->PathOffset < BuildIdEnd &&
        Source->BuildIdOffset < PathEnd)
    {
        SetLastError(ERROR_BAD_FORMAT);
        return FALSE;
    }
    if ((Source->ImageFlags & ROSPROF_IMAGE_FLAG_BUILD_ID_RSDS) &&
        Source->BuildIdBytes != 20)
    {
        SetLastError(ERROR_BAD_FORMAT);
        return FALSE;
    }
    if (!RperfKernelUtf8Path((const UCHAR *)Header,
                             Header->Size,
                             Source->PathOffset,
                             Source->PathBytes,
                             MaximumStringBytes,
                             Path))
    {
        SetLastError(ERROR_BAD_FORMAT);
        return FALSE;
    }

    ZeroMemory(Module, sizeof(*Module));
    ModuleId = Source->ImageKey != 0 ? Source->ImageKey :
               Source->ImageBase;
    if (ModuleId == RPERF_MODEL_INVALID_ID)
    {
        if (*Path != NULL)
            HeapFree(GetProcessHeap(), 0, *Path);
        *Path = NULL;
        SetLastError(ERROR_BAD_FORMAT);
        return FALSE;
    }
    Module->Id = ModuleId;
    Module->ProcessKey = ProcessKey;
    Module->Base = Source->ImageBase;
    Module->Size = Source->ImageSize;
    Module->Architecture = RperfKernelImageArchitecture(
        Source->ImageFlags,
        FallbackArchitecture);
    Module->TimeDateStamp = Source->TimeDateStamp;
    Module->Checksum = Source->Checksum;
    Module->Path = *Path;
    if (Source->ImageFlags & ROSPROF_IMAGE_FLAG_BUILD_ID_RSDS)
    {
        BuildId = (const UCHAR *)Header + Source->BuildIdOffset;
        CopyMemory(Module->DebugId, BuildId, sizeof(Module->DebugId));
        Module->DebugAge = (ULONG)BuildId[16] |
            ((ULONG)BuildId[17] << 8) |
            ((ULONG)BuildId[18] << 16) |
            ((ULONG)BuildId[19] << 24);
    }
    return TRUE;
}

static VOID
RperfKernelCommonHeader(RPERF_KERNEL_STATE *State,
                        const ROSPROF_RECORD_HEADER *Source,
                        RPERF_RECORD_HEADER *Target)
{
    ZeroMemory(Target, sizeof(*Target));
    Target->Flags = 0;
    if (Source->Flags & ROSPROF_RECORD_FLAG_TRUNCATED)
        Target->Flags |= RPERF_MODEL_RECORD_FLAG_TRUNCATED;
    if (Source->Flags & ROSPROF_RECORD_FLAG_REDACTED)
        Target->Flags |= RPERF_MODEL_RECORD_FLAG_REDACTED;
    Target->TimestampNs = RperfKernelTimestampNs(State, Source->Timestamp);
    Target->Sequence = State->NextSequence++;
    Target->ProcessId = Source->ProcessId;
    Target->ThreadId = Source->ThreadId;
    Target->ProcessKey = RperfKernelResolveId(State->Processes,
                                              State->ProcessCount,
                                              Source->ProcessId,
                                              Source->UniqueProcessKey);
    Target->ThreadKey = RperfKernelResolveId(State->Threads,
                                             State->ThreadCount,
                                             Source->ThreadId,
                                             Source->UniqueThreadKey);
    Target->Cpu = Source->ProcessorNumber;
}

static RPERF_LOSS_REASON
RperfKernelLossReason(ULONG Reason)
{
    switch (Reason)
    {
        case ROSPROF_LOSS_RING_FULL: return RperfLossBufferFull;
        case ROSPROF_LOSS_ALLOCATION: return RperfLossAllocation;
        case ROSPROF_LOSS_STACK_WALK: return RperfLossUnsafeUnwind;
        case ROSPROF_LOSS_CORRUPT_RECORD: return RperfLossSequenceGap;
        default: return RperfLossUnknown;
    }
}

static BOOL
RperfKernelNormalizeSample(RPERF_KERNEL_STATE *State,
                           const ROSPROF_SAMPLE_RECORD_V1 *Source)
{
    RPERF_RECORD Record;
    const ULONGLONG *Frames;
    ULONG Depth = Source->UserDepth + Source->KernelDepth;
    ULONG ExpectedBytes;
    ULONG Index;

    if (Source->Header.Size < sizeof(*Source) ||
        Depth > State->Config.Limits.MaxFrames ||
        Depth > RPERF_MODEL_MAX_FRAMES ||
        Depth > (MAXDWORD / sizeof(ULONGLONG)))
        return FALSE;
    ExpectedBytes = Depth * sizeof(ULONGLONG);
    if (Source->FramesBytes != ExpectedBytes ||
        !RosProfRangeValidU32(Source->Header.Size,
                              Source->FramesOffset,
                              Source->FramesBytes) ||
        (Source->FramesOffset & (sizeof(ULONGLONG) - 1)) != 0)
        return FALSE;
    ZeroMemory(&Record, sizeof(Record));
    RperfKernelCommonHeader(State, &Source->Header, &Record.Header);
    Record.Header.Kind = RperfRecordSample;
    Record.Header.EventId = Source->PmuEventId != 0 ?
                            Source->PmuEventId : Source->Source;
    Record.Data.Sample.Weight = Source->Weight != 0 ? Source->Weight : 1;
    Record.Data.Sample.Period = State->Session.ActualPeriod100ns;
    Frames = (const ULONGLONG *)((const UCHAR *)Source +
                                 Source->FramesOffset);
    if (Depth == 0 && Source->InstructionPointer != 0)
        Depth = 1;
    Record.Data.Sample.Depth = (USHORT)Depth;
    for (Index = 0; Index < Depth; ++Index)
    {
        RPERF_FRAME *Frame = &Record.Data.Sample.Frames[Index];
        Frame->Address = ExpectedBytes != 0 ?
                         Frames[Index] : Source->InstructionPointer;
        Frame->FunctionAddress = Frame->Address;
        Frame->ModuleId = RperfKernelModuleForAddress(State->Recording,
                                                      Record.Header.ProcessKey,
                                                      Frame->Address);
        Frame->Context = Index < Source->UserDepth ?
                         RperfContextUser : RperfContextKernel;
        Frame->Resolution = RperfResolutionAddress;
    }
    return RperfRecordingAddRecord(State->Recording, &Record);
}

static BOOL
RperfKernelNormalizeRecord(RPERF_KERNEL_STATE *State,
                           const ROSPROF_RECORD_HEADER *Header)
{
    RPERF_RECORD Record;

    if (Header->Version > 1)
        return TRUE; /* compatible unknown version */
    if ((Header->Flags & ROSPROF_RECORD_FLAG_COMMITTED) == 0)
        return FALSE;
    if (Header->Type == ROSPROF_RECORD_SAMPLE)
        return RperfKernelNormalizeSample(
            State, (const ROSPROF_SAMPLE_RECORD_V1 *)Header);

    ZeroMemory(&Record, sizeof(Record));
    RperfKernelCommonHeader(State, Header, &Record.Header);
    switch (Header->Type)
    {
        case ROSPROF_RECORD_SESSION:
        {
            const ROSPROF_SESSION_RECORD_V1 *Source =
                (const ROSPROF_SESSION_RECORD_V1 *)Header;
            if (Header->Size < sizeof(*Source))
                return FALSE;
            Record.Header.Kind = Source->Event == ROSPROF_SESSION_EVENT_END ?
                                 RperfRecordSessionEnd :
                                 RperfRecordSessionInfo;
            Record.Data.Lifecycle.ObjectId = State->Session.SessionId;
            Record.Data.Lifecycle.ExitStatus = Source->Status;
            break;
        }
        case ROSPROF_RECORD_LOSS:
        {
            const ROSPROF_LOSS_RECORD_V1 *Source =
                (const ROSPROF_LOSS_RECORD_V1 *)Header;
            if (Header->Size < sizeof(*Source))
                return FALSE;
            Record.Header.Kind = RperfRecordLost;
            Record.Data.Lost.Reason = RperfKernelLossReason(Source->Reason);
            Record.Data.Lost.FirstSequence = Source->FirstLostSequence;
            Record.Data.Lost.LastSequence = Source->LastLostSequence;
            Record.Data.Lost.Count = Source->LostRecords;
            Record.Data.Lost.Weight = Source->LostBytes;
            break;
        }
        case ROSPROF_RECORD_PROCESS:
        {
            const ROSPROF_PROCESS_RECORD_V1 *Source =
                (const ROSPROF_PROCESS_RECORD_V1 *)Header;
            ULONGLONG ProcessKey;
            if (Header->Size < sizeof(*Source))
                return FALSE;
            if (Source->Event == ROSPROF_LIFECYCLE_END)
            {
                ProcessKey = RperfKernelEndId(State->Processes,
                                              State->ProcessCount,
                                              Header->ProcessId,
                                              Source->UniqueProcessKey);
            }
            else
            {
                if (!RperfKernelStartId(&State->Processes,
                                        &State->ProcessCount,
                                        &State->ProcessCapacity,
                                        Header->ProcessId,
                                        Source->UniqueProcessKey,
                                        &ProcessKey))
                    return FALSE;
            }
            Record.Header.ProcessKey = ProcessKey;
            Record.Header.Kind = Source->Event == ROSPROF_LIFECYCLE_END ?
                                 RperfRecordProcessEnd :
                                 RperfRecordProcessStart;
            if (Source->Event == ROSPROF_LIFECYCLE_END &&
                State->Config.Scope != RperfScopeSystem &&
                Header->ProcessId == State->Config.ProcessId)
            {
                InterlockedExchange(&State->TargetExited, 1);
            }
            Record.Data.Lifecycle.ObjectId = ProcessKey;
            /* The persisted process schema stores a numeric parent PID;
             * thread lifecycle ParentId uses the stable process key. */
            Record.Data.Lifecycle.ParentId = Source->ParentProcessId;
            Record.Data.Lifecycle.ImageBase = Source->ImageBase;
            Record.Data.Lifecycle.ExitStatus = Source->ExitStatus;
            break;
        }
        case ROSPROF_RECORD_THREAD:
        {
            const ROSPROF_THREAD_RECORD_V1 *Source =
                (const ROSPROF_THREAD_RECORD_V1 *)Header;
            ULONGLONG ProcessKey, ThreadKey;
            if (Header->Size < sizeof(*Source))
                return FALSE;
            ProcessKey = RperfKernelResolveId(State->Processes,
                                              State->ProcessCount,
                                              Header->ProcessId,
                                              Source->UniqueProcessKey);
            if (Source->Event == ROSPROF_LIFECYCLE_END)
            {
                ThreadKey = RperfKernelEndId(State->Threads,
                                             State->ThreadCount,
                                             Header->ThreadId,
                                             Source->UniqueThreadKey);
            }
            else
            {
                if (!RperfKernelStartId(&State->Threads,
                                        &State->ThreadCount,
                                        &State->ThreadCapacity,
                                        Header->ThreadId,
                                        Source->UniqueThreadKey,
                                        &ThreadKey))
                    return FALSE;
            }
            Record.Header.ThreadKey = ThreadKey;
            Record.Header.ProcessKey = ProcessKey;
            Record.Header.Kind = Source->Event == ROSPROF_LIFECYCLE_END ?
                                 RperfRecordThreadEnd :
                                 RperfRecordThreadStart;
            Record.Data.Lifecycle.ObjectId = ThreadKey;
            Record.Data.Lifecycle.ParentId = ProcessKey;
            Record.Data.Lifecycle.ImageBase = Source->StartAddress;
            Record.Data.Lifecycle.ExitStatus = Source->ExitStatus;
            break;
        }
        case ROSPROF_RECORD_IMAGE:
        {
            const ROSPROF_IMAGE_RECORD_V1 *Source =
                (const ROSPROF_IMAGE_RECORD_V1 *)Header;
            PWSTR Path = NULL;
            RPERF_MODULE Module;
            if (!RperfKernelDecodeImageRecord(
                    Header,
                    State->Config.Limits.MaxStringBytes,
                    State->Capabilities.Architecture,
                    Record.Header.ProcessKey,
                    &Module,
                    &Path))
            {
                return FALSE;
            }
            if (Source->Event == ROSPROF_IMAGE_LOAD)
            {
                if (!RperfRecordingAddModule(State->Recording, &Module))
                {
                    if (Path != NULL)
                        HeapFree(GetProcessHeap(), 0, Path);
                    return FALSE;
                }
            }
            if (Path != NULL)
                HeapFree(GetProcessHeap(), 0, Path);
            Record.Header.Kind = Source->Event == ROSPROF_IMAGE_UNLOAD ?
                                 RperfRecordImageUnload :
                                 RperfRecordImageLoad;
            Record.Data.Lifecycle.ObjectId = Source->ImageKey;
            Record.Data.Lifecycle.ModuleId = Source->ImageKey;
            Record.Data.Lifecycle.ImageBase = Source->ImageBase;
            Record.Data.Lifecycle.ImageSize = Source->ImageSize;
            break;
        }
        case ROSPROF_RECORD_CONTEXT_SWITCH:
        {
            const ROSPROF_CONTEXT_SWITCH_RECORD_V1 *Source =
                (const ROSPROF_CONTEXT_SWITCH_RECORD_V1 *)Header;
            if (Header->Size < sizeof(*Source))
                return FALSE;
            Record.Header.Kind = RperfRecordContextSwitch;
            Record.Data.Scheduler.OldProcessId = Source->OldProcessId;
            Record.Data.Scheduler.OldThreadId = Source->OldThreadId;
            Record.Data.Scheduler.NewProcessId = Source->NewProcessId;
            Record.Data.Scheduler.NewThreadId = Source->NewThreadId;
            Record.Data.Scheduler.OldProcessKey =
                RperfKernelResolveId(State->Processes, State->ProcessCount,
                                     Source->OldProcessId,
                                     Source->OldProcessKey);
            Record.Data.Scheduler.OldThreadKey =
                RperfKernelResolveId(State->Threads, State->ThreadCount,
                                     Source->OldThreadId,
                                     Source->OldThreadKey);
            Record.Data.Scheduler.NewProcessKey =
                RperfKernelResolveId(State->Processes, State->ProcessCount,
                                     Source->NewProcessId,
                                     Source->NewProcessKey);
            Record.Data.Scheduler.NewThreadKey =
                RperfKernelResolveId(State->Threads, State->ThreadCount,
                                     Source->NewThreadId,
                                     Source->NewThreadKey);
            Record.Data.Scheduler.State = Source->OldThreadState;
            Record.Data.Scheduler.Reason = Source->OldWaitReason;
            Record.Data.Scheduler.TargetCpu = Source->NewProcessorNumber;
            Record.Data.Scheduler.Flags = Source->SchedulerFlags;
            Record.Data.Scheduler.DurationNs = Source->WaitDuration;
            break;
        }
        case ROSPROF_RECORD_SCHED_WAKEUP:
        {
            const ROSPROF_SCHED_WAKEUP_RECORD_V1 *Source =
                (const ROSPROF_SCHED_WAKEUP_RECORD_V1 *)Header;
            if (Header->Size < sizeof(*Source))
                return FALSE;
            Record.Header.Kind = RperfRecordWakeup;
            Record.Data.Scheduler.OldProcessId = Source->WakerProcessId;
            Record.Data.Scheduler.OldThreadId = Source->WakerThreadId;
            Record.Data.Scheduler.NewProcessId = Source->TargetProcessId;
            Record.Data.Scheduler.NewThreadId = Source->TargetThreadId;
            Record.Data.Scheduler.OldProcessKey =
                RperfKernelResolveId(State->Processes, State->ProcessCount,
                                     Source->WakerProcessId,
                                     Source->WakerProcessKey);
            Record.Data.Scheduler.OldThreadKey =
                RperfKernelResolveId(State->Threads, State->ThreadCount,
                                     Source->WakerThreadId,
                                     Source->WakerThreadKey);
            Record.Data.Scheduler.NewProcessKey =
                RperfKernelResolveId(State->Processes, State->ProcessCount,
                                     Source->TargetProcessId,
                                     Source->TargetProcessKey);
            Record.Data.Scheduler.NewThreadKey =
                RperfKernelResolveId(State->Threads, State->ThreadCount,
                                     Source->TargetThreadId,
                                     Source->TargetThreadKey);
            Record.Data.Scheduler.TargetCpu = Source->TargetProcessorNumber;
            Record.Data.Scheduler.Flags = Source->WakeFlags;
            Record.Data.Scheduler.DurationNs = Source->WakeLatency;
            break;
        }
        case ROSPROF_RECORD_PMU:
        {
            const ROSPROF_PMU_RECORD_V1 *Source =
                (const ROSPROF_PMU_RECORD_V1 *)Header;
            if (Header->Size < sizeof(*Source) ||
                Source->InstructionPointer == 0)
                return FALSE;
            Record.Header.Kind = RperfRecordSample;
            Record.Header.EventId = Source->EventId;
            Record.Data.Sample.Weight = Source->CounterValue;
            Record.Data.Sample.Period = Source->SamplePeriod;
            Record.Data.Sample.Depth = 1;
            Record.Data.Sample.Frames[0].Address = Source->InstructionPointer;
            Record.Data.Sample.Frames[0].FunctionAddress =
                Source->InstructionPointer;
            Record.Data.Sample.Frames[0].ModuleId =
                RperfKernelModuleForAddress(State->Recording,
                                            Record.Header.ProcessKey,
                                            Source->InstructionPointer);
            Record.Data.Sample.Frames[0].Context =
                (Source->PmuFlags & ROSPROF_PMU_FLAG_KERNEL) ?
                RperfContextKernel : RperfContextUser;
            Record.Data.Sample.Frames[0].Resolution = RperfResolutionAddress;
            break;
        }
        case ROSPROF_RECORD_CLOCK_SYNC:
        {
            const ROSPROF_CLOCK_SYNC_RECORD_V1 *Source =
                (const ROSPROF_CLOCK_SYNC_RECORD_V1 *)Header;
            if (Header->Size < sizeof(*Source))
                return FALSE;
            Record.Header.Kind = RperfRecordClockSync;
            Record.Data.Clock.SystemTime100ns = Source->SystemTime100ns;
            Record.Data.Clock.PerformanceCounter = Source->PerformanceCounter;
            Record.Data.Clock.PerformanceFrequency =
                Source->PerformanceFrequency;
            Record.Data.Clock.InterruptTime100ns = Source->InterruptTime100ns;
            Record.Data.Clock.Flags = Source->ClockFlags;
            break;
        }
        case ROSPROF_RECORD_SECURITY:
            Record.Header.Kind = RperfRecordSecurity;
            break;
        default:
            return TRUE; /* skip unknown record type by validated size */
    }
    return RperfRecordingAddRecord(State->Recording, &Record);
}

BOOL
RperfKernelSequenceAfter(ULONGLONG Sequence,
                         ULONGLONG Previous)
{
    ULONGLONG Delta = Sequence - Previous;
    return Delta != 0 && Delta <= MAXLONGLONG;
}

static BOOL
RperfKernelParseBatch(RPERF_KERNEL_STATE *State,
                      ULONG Bytes,
                      BOOL *EndOfStream)
{
    const ROSPROF_READ_BATCH_V1 *Batch;
    ULONG Offset, End, Index;
    ULONGLONG FirstObserved = 0, LastObserved = 0;

    *EndOfStream = FALSE;
    if (Bytes < sizeof(*Batch))
        return FALSE;
    Batch = (const ROSPROF_READ_BATCH_V1 *)State->ReadBuffer;
    if (Batch->Magic != ROSPROF_READ_MAGIC ||
        Batch->Version != ROSPROF_READ_VERSION ||
        Batch->HeaderSize < sizeof(*Batch) || Batch->HeaderSize > Bytes ||
        Batch->RecordAlignment != ROSPROF_RECORD_ALIGNMENT ||
        Batch->SessionId != State->Session.SessionId ||
        Batch->ConfigGeneration != State->Session.ConfigGeneration ||
        !RosProfRangeValidU32(Bytes,
                              Batch->RecordsOffset,
                              Batch->RecordsBytes) ||
        !RosProfIsAlignedU32(Batch->RecordsOffset) ||
        (Batch->RecordCount == 0 && Batch->RecordsBytes != 0) ||
        (Batch->RecordCount != 0 && Batch->RecordsBytes == 0))
        return FALSE;
    if ((Batch->Flags & ROSPROF_READ_FLAG_MORE_DATA) != 0 &&
        Batch->RequiredSize > State->ReadCapacity)
    {
        PUCHAR NewBuffer;
        if (Batch->RequiredSize > State->MaximumReadBytes)
            return FALSE;
        NewBuffer = HeapReAlloc(GetProcessHeap(), 0,
                                State->ReadBuffer, Batch->RequiredSize);
        if (NewBuffer == NULL)
            return FALSE;
        State->ReadBuffer = NewBuffer;
        State->ReadCapacity = Batch->RequiredSize;
    }
    Offset = Batch->RecordsOffset;
    End = Offset + Batch->RecordsBytes;
    for (Index = 0; Index < Batch->RecordCount; ++Index)
    {
        const ROSPROF_RECORD_HEADER *Header;
        if (Offset > End || End - Offset < sizeof(*Header))
            return FALSE;
        Header = (const ROSPROF_RECORD_HEADER *)(State->ReadBuffer + Offset);
        if (!RosProfRecordHeaderValid(Header, End - Offset) ||
            Header->Version == 0 ||
            (Index != 0 &&
             !RperfKernelSequenceAfter(Header->Sequence, LastObserved)) ||
            (State->HaveLastSequence &&
             !RperfKernelSequenceAfter(Header->Sequence,
                                       State->LastSequence)))
            return FALSE;
        if (Index == 0)
            FirstObserved = Header->Sequence;
        LastObserved = Header->Sequence;
        /* Sequence gaps are accounted by the authoritative loss records
         * emitted by the kernel.  Synthesizing another record here would
         * count the same dropped records twice. */
        if (!RperfKernelNormalizeRecord(State, Header))
            return FALSE;
        State->LastSequence = Header->Sequence;
        State->HaveLastSequence = TRUE;
        Offset += Header->Size;
    }
    if (Offset != End ||
        (Batch->RecordCount != 0 &&
         (Batch->FirstSequence != FirstObserved ||
          Batch->LastSequence != LastObserved)))
        return FALSE;
    *EndOfStream = (Batch->Flags & ROSPROF_READ_FLAG_END_OF_STREAM) != 0;
    return TRUE;
}

static BOOL
RperfKernelStopDevice(RPERF_KERNEL_STATE *State)
{
    ROSPROF_SESSION_COMMAND_V1 Command;
    DWORD Returned;
    DWORD CancelError = ERROR_SUCCESS;
    BOOL Stopped;

    SetEvent(State->StopEvent);
    if (InterlockedCompareExchange(&State->StopIssued, 1, 0) != 0)
        return TRUE;
    if (!CancelIoEx(State->Device, NULL) &&
        GetLastError() != ERROR_NOT_FOUND)
        CancelError = GetLastError();
    RperfRosProfInitializeHeader(&Command.Header, sizeof(Command));
    Command.SessionId = State->Session.SessionId;
    Command.ConfigGeneration = State->Session.ConfigGeneration;
    Command.Reason = InterlockedCompareExchange(&State->TargetExited, 0, 0) ?
                     ROSPROF_REASON_TARGET_EXIT : ROSPROF_REASON_REQUESTED;
    Stopped = RperfRosProfIoctl(State->Device,
                               IOCTL_ROSPROF_STOP,
                               &Command, sizeof(Command),
                               NULL, 0, &Returned);
    InterlockedExchange(&State->StopError,
                        Stopped ? ERROR_SUCCESS : (LONG)GetLastError());
    SetEvent(State->StopCompleteEvent);
    if (!Stopped)
    {
        SetLastError((DWORD)State->StopError);
        return FALSE;
    }
    if (CancelError != ERROR_SUCCESS &&
        CancelError != ERROR_CALL_NOT_IMPLEMENTED)
        SetLastError(CancelError);
    return TRUE;
}

static DWORD WINAPI
RperfKernelDurationThread(PVOID Opaque)
{
    RPERF_KERNEL_STATE *State = Opaque;
    if (WaitForSingleObject(State->StopEvent,
                            State->Config.DurationMs) == WAIT_TIMEOUT)
    {
        InterlockedExchange(&State->DurationExpired, 1);
        RperfKernelStopDevice(State);
    }
    return ERROR_SUCCESS;
}

static DWORD WINAPI
RperfKernelReader(PVOID Opaque)
{
    RPERF_KERNEL_STATE *State = Opaque;
    BOOL EndOfStream = FALSE;

    while (!EndOfStream)
    {
        DWORD Bytes = 0;
        LONG StopError;

        /*
         * A synchronous read and the STOP IOCTL share one file object.  Once
         * cancellation releases an in-flight read, do not race STOP by
         * issuing a replacement read before the control operation completes.
         */
        if (WaitForSingleObject(State->StopEvent, 0) == WAIT_OBJECT_0 &&
            WaitForSingleObject(State->StopCompleteEvent, 0) != WAIT_OBJECT_0)
        {
            WaitForSingleObject(State->StopCompleteEvent, INFINITE);
        }
        StopError = InterlockedCompareExchange(&State->StopError,
                                               ERROR_SUCCESS,
                                               ERROR_SUCCESS);
        if (StopError != ERROR_SUCCESS)
        {
            State->Error = (DWORD)StopError;
            return State->Error;
        }
        if (!ReadFile(State->Device,
                      State->ReadBuffer,
                      State->ReadCapacity,
                      &Bytes,
                      NULL))
        {
            State->Error = GetLastError();
            if (State->Error == ERROR_HANDLE_EOF)
                break;
            if (State->Error == ERROR_OPERATION_ABORTED &&
                WaitForSingleObject(State->StopEvent, 0) == WAIT_OBJECT_0)
            {
                State->Error = ERROR_SUCCESS;
                continue; /* STOP follows cancellation; drain through EOS. */
            }
            return State->Error;
        }
        if (!RperfKernelParseBatch(State, Bytes, &EndOfStream))
        {
            State->Error = ERROR_BAD_FORMAT;
            return State->Error;
        }
        if (InterlockedCompareExchange(&State->TargetExited, 0, 0) != 0 &&
            InterlockedCompareExchange(&State->StopIssued, 0, 0) == 0 &&
            !RperfKernelStopDevice(State))
        {
            State->Error = GetLastError();
            return State->Error;
        }
    }
    return ERROR_SUCCESS;
}

static BOOL
RperfKernelStart(PVOID Opaque)
{
    RPERF_KERNEL_STATE *State = Opaque;
    ROSPROF_SESSION_COMMAND_V1 Command;
    DWORD Returned;

    RperfRosProfInitializeHeader(&Command.Header, sizeof(Command));
    Command.SessionId = State->Session.SessionId;
    Command.ConfigGeneration = State->Session.ConfigGeneration;
    if (!RperfRosProfIoctl(State->Device,
                          IOCTL_ROSPROF_START,
                          &Command, sizeof(Command),
                          NULL, 0, &Returned))
        return FALSE;
    if (State->Config.Scope != RperfScopeSystem &&
        State->Config.ProcessId != 0)
    {
        if (!RperfCaptureBaseline(&State->Config,
                                  State->Recording,
                                  0,
                                  &State->NextSequence,
                                  RperfKernelBaselineId,
                                  State,
                                  &State->Baseline))
        {
            DWORD Error = GetLastError();
            RperfKernelStopDevice(State);
            SetLastError(Error);
            return FALSE;
        }
    }
    if (!RperfCaptureSystemModuleBaseline(State->Recording,
                                          0,
                                          &State->NextSequence,
                                          &State->Baseline))
    {
        DWORD Error = GetLastError();
        RperfKernelStopDevice(State);
        SetLastError(Error);
        return FALSE;
    }
    if (State->Baseline.Partial)
    {
        RPERF_RECORD Loss;
        ZeroMemory(&Loss, sizeof(Loss));
        Loss.Header.Kind = RperfRecordLost;
        Loss.Header.Flags = RPERF_MODEL_RECORD_FLAG_SYNTHETIC;
        Loss.Header.Sequence = State->NextSequence++;
        Loss.Header.Cpu = RPERF_MODEL_ALL_CPUS;
        Loss.Data.Lost.Reason = RperfLossUserspaceSnapshot;
        Loss.Data.Lost.Count = 1;
        if (!RperfRecordingAddRecord(State->Recording, &Loss))
        {
            DWORD Error = GetLastError();
            RperfKernelStopDevice(State);
            SetLastError(Error);
            return FALSE;
        }
    }
    State->ReaderThread = CreateThread(NULL, 0, RperfKernelReader,
                                       State, 0, NULL);
    if (State->ReaderThread == NULL)
    {
        DWORD Error = GetLastError();
        RperfKernelStopDevice(State);
        SetLastError(Error);
        return FALSE;
    }
    if (State->Config.DurationMs != 0)
    {
        State->DurationThread = CreateThread(NULL, 0,
                                             RperfKernelDurationThread,
                                             State, 0, NULL);
        if (State->DurationThread == NULL)
        {
            DWORD Error = GetLastError();
            RperfKernelStopDevice(State);
            WaitForSingleObject(State->ReaderThread, INFINITE);
            SetLastError(Error);
            return FALSE;
        }
    }
    return TRUE;
}

static BOOL
RperfKernelStop(PVOID Opaque)
{
    return RperfKernelStopDevice((RPERF_KERNEL_STATE *)Opaque);
}

static BOOL
RperfKernelJoin(PVOID Opaque,
                DWORD Timeout)
{
    RPERF_KERNEL_STATE *State = Opaque;
    ROSPROF_SESSION_QUERY_V1 Query;
    ROSPROF_SESSION_STATUS_V1 Status;
    DWORD Wait, Returned;

    Wait = WaitForSingleObject(State->ReaderThread, Timeout);
    if (Wait == WAIT_TIMEOUT)
    {
        SetLastError(ERROR_TIMEOUT);
        return FALSE;
    }
    if (Wait != WAIT_OBJECT_0)
        return FALSE;
    SetEvent(State->StopEvent);
    if (State->DurationThread != NULL)
        WaitForSingleObject(State->DurationThread, INFINITE);
    if (State->Error != ERROR_SUCCESS)
    {
        SetLastError(State->Error);
        return FALSE;
    }
    RperfRosProfInitializeHeader(&Query.Header, sizeof(Query));
    Query.SessionId = State->Session.SessionId;
    Query.ConfigGeneration = State->Session.ConfigGeneration;
    ZeroMemory(&Status, sizeof(Status));
    if (!RperfRosProfIoctl(State->Device,
                          IOCTL_ROSPROF_QUERY_STATUS,
                          &Query, sizeof(Query),
                          &Status, sizeof(Status),
                          &Returned) ||
        !RperfRosProfStructValid(&Status.Header,
                                 Returned,
                                 sizeof(Status)) ||
        Status.SessionId != State->Session.SessionId ||
        Status.ConfigGeneration != State->Session.ConfigGeneration)
        return FALSE;
    State->Recording->Info.StartTimeNs =
        RperfKernelTimestampNs(State, Status.StartTimestamp);
    State->Recording->Info.EndTimeNs =
        RperfKernelTimestampNs(State, Status.StopTimestamp);
    if (InterlockedCompareExchange(&State->DurationExpired, 0, 0) != 0)
    {
        State->Recording->Info.CompletionReason = RperfCompletionDuration;
    }
    else if (InterlockedCompareExchange(&State->TargetExited, 0, 0) != 0)
    {
        State->Recording->Info.CompletionReason = RperfCompletionTargetExit;
    }
    else if (Status.StopReason == ROSPROF_REASON_REQUESTED)
    {
        State->Recording->Info.CompletionReason = RperfCompletionUserStop;
    }
    else if (Status.StopReason == ROSPROF_REASON_TARGET_EXIT)
    {
        State->Recording->Info.CompletionReason = RperfCompletionTargetExit;
    }
    else if (Status.StopReason == ROSPROF_REASON_NONE)
    {
        State->Recording->Info.CompletionReason = RperfCompletionIncomplete;
    }
    else
    {
        State->Recording->Info.CompletionReason = RperfCompletionError;
    }
    State->Recording->Info.CompletionError = Status.FinalStatus;
    State->Recording->Info.Complete =
        Status.State == ROSPROF_SESSION_STOPPED &&
        Status.FinalStatus == ERROR_SUCCESS;
    State->Recording->Counters.AttemptedSamples =
        max(State->Recording->Counters.SuccessfulSamples, Status.Samples) +
        Status.Ring.LostRecords;
    if (Status.Ring.LostRecords > State->Recording->Counters.LostRecords)
        State->Recording->Counters.LostRecords = Status.Ring.LostRecords;
    if (!RperfRecordingFreeze(State->Recording))
        return FALSE;
    return TRUE;
}

static BOOL
RperfKernelCounters(PVOID Opaque,
                    RPERF_CAPTURE_COUNTERS *Counters)
{
    RPERF_KERNEL_STATE *State = Opaque;
    if (State->Recording == NULL)
        ZeroMemory(Counters, sizeof(*Counters));
    else
        *Counters = State->Recording->Counters;
    return TRUE;
}

static RPERF_RECORDING *
RperfKernelTake(PVOID Opaque)
{
    RPERF_KERNEL_STATE *State = Opaque;
    if (State->Recording != NULL)
        RperfRecordingAddRef(State->Recording);
    return State->Recording;
}

static VOID
RperfKernelDestroy(PVOID Opaque)
{
    RPERF_KERNEL_STATE *State = Opaque;
    if (State->ReaderThread != NULL)
        CloseHandle(State->ReaderThread);
    if (State->DurationThread != NULL)
        CloseHandle(State->DurationThread);
    if (State->StopEvent != NULL)
        CloseHandle(State->StopEvent);
    if (State->StopCompleteEvent != NULL)
        CloseHandle(State->StopCompleteEvent);
    if (State->Device != INVALID_HANDLE_VALUE)
        CloseHandle(State->Device);
    if (State->Recording != NULL)
        RperfRecordingRelease(State->Recording);
    if (State->Processes != NULL)
        HeapFree(GetProcessHeap(), 0, State->Processes);
    if (State->Threads != NULL)
        HeapFree(GetProcessHeap(), 0, State->Threads);
    if (State->ReadBuffer != NULL)
        HeapFree(GetProcessHeap(), 0, State->ReadBuffer);
    HeapFree(GetProcessHeap(), 0, State);
}

static const RPERF_RECORDER_OPS RperfKernelOps =
{
    RperfKernelStart,
    RperfKernelStop,
    RperfKernelJoin,
    RperfKernelCounters,
    RperfKernelTake,
    RperfKernelDestroy
};

BOOL
RperfKernelCreate(const RPERF_CAPTURE_CONFIGURATION *Configuration,
                  const RPERF_RECORDER_OPS **Ops,
                  PVOID *Opaque)
{
    RPERF_KERNEL_STATE *State;
    ROSPROF_CONFIG_V1 Config;
    ROSPROF_ACCESS_INFO_V1 Access;
    ROSPROF_SESSION_QUERY_V1 Query;
    DWORD Returned;
    ULONG RingBytes;

    State = HeapAlloc(GetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*State));
    if (State == NULL)
        return FALSE;
    State->Device = INVALID_HANDLE_VALUE;
    State->Config = *Configuration;
    State->NextSequence = 1;
    if (Configuration->EventId != 0 ||
        Configuration->Scope == RperfScopeSelectedThreads ||
        Configuration->Scope == RperfScopeProcessTree ||
        Configuration->FollowChildren)
    {
        SetLastError(ERROR_NOT_SUPPORTED);
        goto Failure;
    }
    State->StopEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (State->StopEvent == NULL)
        goto Failure;
    State->StopCompleteEvent = CreateEventW(NULL, TRUE, FALSE, NULL);
    if (State->StopCompleteEvent == NULL)
        goto Failure;
    State->Device = CreateFileW(ROSPROF_WIN32_DEVICE_NAME,
                                GENERIC_READ | GENERIC_WRITE,
                                0, NULL, OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL, NULL);
    if (State->Device == INVALID_HANDLE_VALUE)
        goto Failure;
    ZeroMemory(&State->Capabilities, sizeof(State->Capabilities));
    if (!RperfRosProfIoctl(State->Device,
                          IOCTL_ROSPROF_QUERY_CAPABILITIES,
                          NULL, 0,
                          &State->Capabilities,
                          sizeof(State->Capabilities),
                          &Returned) ||
        !RperfRosProfStructValid(&State->Capabilities.Header,
                                 Returned,
                                 sizeof(State->Capabilities)))
        goto Failure;
    if (!(State->Capabilities.Capabilities & ROSPROF_CAP_TIMER_SAMPLE))
    {
        SetLastError(ERROR_NOT_SUPPORTED);
        goto Failure;
    }
    RperfRosProfInitializeHeader(&Config.Header, sizeof(Config));
    Config.Sources = ROSPROF_SOURCE_TIMER;
    if (State->Capabilities.Capabilities & ROSPROF_CAP_PROCESS_LIFECYCLE)
        Config.Sources |= ROSPROF_SOURCE_PROCESS;
    if (State->Capabilities.Capabilities & ROSPROF_CAP_THREAD_LIFECYCLE)
        Config.Sources |= ROSPROF_SOURCE_THREAD;
    if (State->Capabilities.Capabilities & ROSPROF_CAP_IMAGE_LIFECYCLE)
        Config.Sources |= ROSPROF_SOURCE_IMAGE;
    Config.RecordTypes = ROSPROF_RECORD_MASK(ROSPROF_RECORD_SESSION) |
                         ROSPROF_RECORD_MASK(ROSPROF_RECORD_SAMPLE);
    if (State->Capabilities.Capabilities & ROSPROF_CAP_LOSS_RECORDS)
        Config.RecordTypes |= ROSPROF_RECORD_MASK(ROSPROF_RECORD_LOSS);
    if (Config.Sources & ROSPROF_SOURCE_PROCESS)
        Config.RecordTypes |= ROSPROF_RECORD_MASK(ROSPROF_RECORD_PROCESS);
    if (Config.Sources & ROSPROF_SOURCE_THREAD)
        Config.RecordTypes |= ROSPROF_RECORD_MASK(ROSPROF_RECORD_THREAD);
    if (Config.Sources & ROSPROF_SOURCE_IMAGE)
        Config.RecordTypes |= ROSPROF_RECORD_MASK(ROSPROF_RECORD_IMAGE);
    if (State->Capabilities.SupportedRecordTypes &
        ROSPROF_RECORD_MASK(ROSPROF_RECORD_SECURITY))
        Config.RecordTypes |= ROSPROF_RECORD_MASK(ROSPROF_RECORD_SECURITY);
    if ((State->Capabilities.Capabilities & ROSPROF_CAP_CLOCK_SYNC) &&
        (State->Capabilities.SupportedSources & ROSPROF_SOURCE_CLOCK_SYNC))
    {
        Config.Sources |= ROSPROF_SOURCE_CLOCK_SYNC;
        Config.RecordTypes |= ROSPROF_RECORD_MASK(ROSPROF_RECORD_CLOCK_SYNC);
    }
    Config.RecordTypes &= State->Capabilities.SupportedRecordTypes;
    if (Configuration->Scope == RperfScopeSystem)
        Config.ConfigFlags |= ROSPROF_CONFIG_FLAG_SYSTEM_WIDE;
    Config.ConfigFlags |= ROSPROF_CONFIG_FLAG_PER_CPU_BUFFER |
                          ROSPROF_CONFIG_FLAG_DROP_NEW |
                          ROSPROF_CONFIG_FLAG_EXCLUDE_PROFILER;
    Config.SamplePeriod100ns = (ULONGLONG)Configuration->IntervalUs * 10;
    Config.TargetProcessId = Configuration->ProcessId;
    RingBytes = 4 * 1024 * 1024;
    RingBytes = max(RingBytes, State->Capabilities.MinimumRingBytes);
    RingBytes = min(RingBytes, State->Capabilities.MaximumRingBytes);
    Config.RingSizeBytes = RingBytes;
    Config.ReadWatermarkBytes = min(RingBytes / 4, 1024 * 1024UL);
    Config.MaximumStackDepth = 1;
    Config.StackFlags = ROSPROF_STACK_INCLUDE_IP;
    if (State->Capabilities.MaximumStackDepth > 1 &&
        Configuration->Limits.MaxFrames > 1)
    {
        /* The kernel offers interrupt-time call chains; request them bounded
         * by what the analysis model can hold. */
        Config.MaximumStackDepth = (USHORT)min(State->Capabilities.MaximumStackDepth,
                                               min(Configuration->Limits.MaxFrames, RPERF_MODEL_MAX_FRAMES));
        Config.StackFlags |= ROSPROF_STACK_KERNEL | ROSPROF_STACK_TRUNCATE_ALLOWED;
    }
    Config.SecurityMode = ROSPROF_SECURITY_STRICT;
    ZeroMemory(&State->Session, sizeof(State->Session));
    if (!RperfRosProfIoctl(State->Device,
                          IOCTL_ROSPROF_CONFIGURE,
                          &Config, sizeof(Config),
                          &State->Session, sizeof(State->Session),
                          &Returned) ||
        !RperfRosProfStructValid(&State->Session.Header,
                                 Returned,
                                 sizeof(State->Session)) ||
        (State->Session.AcceptedSources & Config.Sources) != Config.Sources ||
        (State->Session.AcceptedRecordTypes & Config.RecordTypes) !=
            Config.RecordTypes ||
        State->Session.MaximumStackDepth != Config.MaximumStackDepth ||
        (State->Session.StackFlags & Config.StackFlags) != Config.StackFlags)
    {
        if (GetLastError() == ERROR_SUCCESS)
            SetLastError(ERROR_NOT_SUPPORTED);
        goto Failure;
    }
    RperfRosProfInitializeHeader(&Query.Header, sizeof(Query));
    Query.SessionId = State->Session.SessionId;
    Query.ConfigGeneration = State->Session.ConfigGeneration;
    ZeroMemory(&Access, sizeof(Access));
    if (!RperfRosProfIoctl(State->Device,
                          IOCTL_ROSPROF_QUERY_ACCESS,
                          &Query, sizeof(Query),
                          &Access, sizeof(Access),
                          &Returned) ||
        !RperfRosProfStructValid(&Access.Header,
                                 Returned,
                                 sizeof(Access)) ||
        Access.AccessStatus != ERROR_SUCCESS ||
        (Access.GrantedSources & Config.Sources) != Config.Sources ||
        (Access.GrantedStackFlags & Config.StackFlags) != Config.StackFlags)
    {
        if (GetLastError() == ERROR_SUCCESS)
            SetLastError(ERROR_ACCESS_DENIED);
        goto Failure;
    }
    if (Configuration->Limits.MaxRecordBytes >
        16UL * 1024 * 1024 - sizeof(ROSPROF_READ_BATCH_V1))
        State->MaximumReadBytes = 16UL * 1024 * 1024;
    else
        State->MaximumReadBytes = Configuration->Limits.MaxRecordBytes +
                                  sizeof(ROSPROF_READ_BATCH_V1);
    if (State->MaximumReadBytes < State->Capabilities.ReadHeaderSize)
    {
        SetLastError(ERROR_BUFFER_OVERFLOW);
        goto Failure;
    }
    State->ReadCapacity = max(State->Capabilities.ReadHeaderSize,
                              min(Config.ReadWatermarkBytes,
                                  State->MaximumReadBytes));
    State->ReadBuffer = HeapAlloc(GetProcessHeap(), 0, State->ReadCapacity);
    if (State->ReadBuffer == NULL)
        goto Failure;
    State->Recording = RperfRecordingCreate(&Configuration->Limits);
    if (State->Recording == NULL)
        goto Failure;
    RperfRecordingSetSink(State->Recording, Configuration->RecordSink, Configuration->RecordSinkContext);
    State->Recording->Info.Backend = RperfBackendKernel;
    State->Recording->Info.Metric = Configuration->EventId != 0 ?
                                    RperfMetricEventWeight :
                                    RperfMetricCpuSamples;
    State->Recording->Info.ProducerArchitecture =
        State->Capabilities.Architecture;
    State->Recording->Info.AddressWidth = State->Capabilities.PointerWidth;
    State->Recording->Info.ClockId = State->Capabilities.ClockType;
    State->Recording->Info.ClockFrequency =
        State->Capabilities.TimestampFrequency;
    State->Recording->Info.IntervalMs =
        (Configuration->IntervalUs + 999) / 1000;
    State->Recording->Info.RequestedDurationMs = Configuration->DurationMs;
    State->Recording->Info.SessionId.Data1 =
        (ULONG)State->Session.SessionId;
    State->Recording->Info.SessionId.Data2 =
        (USHORT)(State->Session.SessionId >> 32);
    State->Recording->Info.SessionId.Data3 =
        (USHORT)(State->Session.SessionId >> 48);
    if (!RperfRecordingSetTargetName(State->Recording,
                                     Configuration->TargetName))
        goto Failure;
    *Ops = &RperfKernelOps;
    *Opaque = State;
    return TRUE;

Failure:
    RperfKernelDestroy(State);
    return FALSE;
}
