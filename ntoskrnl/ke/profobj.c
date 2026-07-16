/*
 * COPYRIGHT:       GPL - See COPYING in the top level directory
 * PROJECT:         ReactOS Kernel
 * FILE:            ntoskrnl/ke/profobj.c
 * PURPOSE:         Kernel Profiling
 * PROGRAMMERS:     Alex Ionescu (alex@relsoft.net)
 */

/* INCLUDES *****************************************************************/

#include <ntoskrnl.h>
#include <internal/proftrace.h>
#define NDEBUG
#include <debug.h>

/* GLOBALS *******************************************************************/

KIRQL KiProfileIrql = PROFILE_LEVEL;
LIST_ENTRY KiProfileListHead;
LIST_ENTRY KiProfileSourceListHead;
KSPIN_LOCK KiProfileLock;
/* Serializes physical profile-source transitions at passive level: the HAL
   start/stop broadcasts an IPI rendezvous, which must never run under
   KiProfileLock at PROFILE_LEVEL (above IPI_LEVEL, deadlocks the machine) */
KGUARDED_MUTEX KiProfileSourceMutex;
ULONG KiProfileTimeInterval = 78125; /* Default resolution 7.8ms (sysinternals) */
ULONG KiProfileAlignmentFixupInterval;

/* FUNCTIONS *****************************************************************/

static
BOOLEAN
KiAcquireProfileSourceLocked(
    IN KPROFILE_SOURCE Source,
    IN PKPROFILE_SOURCE_OBJECT NewSource,
    OUT PBOOLEAN ConsumedNewSource)
{
    PLIST_ENTRY NextEntry;
    PKPROFILE_SOURCE_OBJECT CurrentSource;

    *ConsumedNewSource = FALSE;
    for (NextEntry = KiProfileSourceListHead.Flink;
         NextEntry != &KiProfileSourceListHead;
         NextEntry = NextEntry->Flink)
    {
        CurrentSource = CONTAINING_RECORD(NextEntry, KPROFILE_SOURCE_OBJECT, ListEntry);
        if (CurrentSource->Source == Source)
        {
            ASSERT(CurrentSource->References != MAXULONG);
            CurrentSource->References++;
            return FALSE;
        }
    }

    NewSource->Source = Source;
    NewSource->References = 1;
    InsertHeadList(&KiProfileSourceListHead, &NewSource->ListEntry);
    *ConsumedNewSource = TRUE;
    return TRUE;
}

static
PKPROFILE_SOURCE_OBJECT
KiReleaseProfileSourceLocked(
    IN KPROFILE_SOURCE Source)
{
    PLIST_ENTRY NextEntry;
    PKPROFILE_SOURCE_OBJECT CurrentSource;

    for (NextEntry = KiProfileSourceListHead.Flink;
         NextEntry != &KiProfileSourceListHead;
         NextEntry = NextEntry->Flink)
    {
        CurrentSource = CONTAINING_RECORD(NextEntry, KPROFILE_SOURCE_OBJECT, ListEntry);
        if (CurrentSource->Source == Source)
        {
            ASSERT(CurrentSource->References != 0);
            CurrentSource->References--;
            if (CurrentSource->References == 0)
            {
                RemoveEntryList(&CurrentSource->ListEntry);
                return CurrentSource;
            }

            return NULL;
        }
    }

    ASSERT(FALSE);
    return NULL;
}

VOID
NTAPI
KeInitializeProfile(PKPROFILE Profile,
                    PKPROCESS Process,
                    PVOID ImageBase,
                    SIZE_T ImageSize,
                    ULONG BucketSize,
                    KPROFILE_SOURCE ProfileSource,
                    KAFFINITY Affinity)
{
    /* Initialize the Header */
    Profile->Type = ProfileObject;
    Profile->Size = sizeof(KPROFILE);

    /* Copy all the settings we were given */
    Profile->Process = Process;
    Profile->RangeBase = ImageBase;
    Profile->BucketShift = BucketSize - 2; /* See ntinternals.net -- Alex */
    Profile->RangeLimit = (PVOID)((ULONG_PTR)ImageBase + ImageSize);
    Profile->Started = FALSE;
    Profile->Source = ProfileSource;
    Profile->Affinity = Affinity;
}

BOOLEAN
NTAPI
KeStartProfile(IN PKPROFILE Profile,
               IN PVOID Buffer)
{
    KIRQL OldIrql;
    PKPROFILE_SOURCE_OBJECT SourceBuffer;
    BOOLEAN FreeBuffer = TRUE, StartedProfile = FALSE;
    BOOLEAN StartSource = FALSE, ConsumedSource = FALSE;
    PKPROCESS ProfileProcess;

    /* Callers must be at IRQL <= APC_LEVEL: starting blocks on the source
       transition mutex and an all-processor timer rendezvous */
    ASSERT(KeGetCurrentIrql() <= APC_LEVEL);

    /* Do not create a profile which the HAL cannot actually deliver. */
    if (!Buffer || !KeQueryIntervalProfile(Profile->Source)) return FALSE;

    /* Allocate a buffer first, before we raise IRQL */
    SourceBuffer = ExAllocatePoolWithTag(NonPagedPool,
                                         sizeof(KPROFILE_SOURCE_OBJECT),
                                         'forP');
    if (!SourceBuffer) return FALSE;
    RtlZeroMemory(SourceBuffer, sizeof(KPROFILE_SOURCE_OBJECT));

    /* Serialize the physical source transition with other transitions */
    KeAcquireGuardedMutex(&KiProfileSourceMutex);

    /* Raise to profile IRQL and acquire the profile lock */
    KeRaiseIrql(KiProfileIrql, &OldIrql);
    KeAcquireSpinLockAtDpcLevel(&KiProfileLock);

    /* Make sure it's not running. */
    if (!Profile->Started)
    {
        StartSource = KiAcquireProfileSourceLocked(Profile->Source, SourceBuffer, &ConsumedSource);
        if (ConsumedSource) FreeBuffer = FALSE;

        Profile->Buffer = Buffer;
        Profile->Started = TRUE;
        ProfileProcess = Profile->Process;
        if (ProfileProcess)
        {
            InsertTailList(&ProfileProcess->ProfileListHead, &Profile->ProfileListEntry);
        }
        else
        {
            InsertTailList(&KiProfileListHead, &Profile->ProfileListEntry);
        }

        StartedProfile = TRUE;
    }

    /* Release the profile lock */
    KeReleaseSpinLockFromDpcLevel(&KiProfileLock);

    /* Lower back to original IRQL */
    KeLowerIrql(OldIrql);

    /* Arm the timer outside the profile lock: the HAL rendezvous IPIs every
       processor, and a concurrent profile interrupt spinning on the lock at
       PROFILE_LEVEL would keep that rendezvous from ever completing */
    if (StartSource) HalStartProfileInterrupt(Profile->Source);
    KeReleaseGuardedMutex(&KiProfileSourceMutex);

    /* Free the pool */
    if (FreeBuffer) ExFreePoolWithTag(SourceBuffer, 'forP');

    /* Return whether we could start the profile */
    return StartedProfile;
}

BOOLEAN
NTAPI
KeStopProfile(IN PKPROFILE Profile)
{
    KIRQL OldIrql;
    PKPROFILE_SOURCE_OBJECT CurrentSource = NULL;
    BOOLEAN FreeSource = FALSE, StoppedProfile = FALSE;

    /* Callers must be at IRQL <= APC_LEVEL: stopping blocks on the source
       transition mutex and an all-processor timer rendezvous */
    ASSERT(KeGetCurrentIrql() <= APC_LEVEL);

    /* Serialize the physical source transition with other transitions */
    KeAcquireGuardedMutex(&KiProfileSourceMutex);

    /* Raise to profile IRQL and acquire the profile lock */
    KeRaiseIrql(KiProfileIrql, &OldIrql);
    KeAcquireSpinLockAtDpcLevel(&KiProfileLock);

    /* Make sure it's running */
    if (Profile->Started)
    {
        /* Remove it from the list and disable it before dropping the source. */
        RemoveEntryList(&Profile->ProfileListEntry);
        Profile->Started = FALSE;
        StoppedProfile = TRUE;

        CurrentSource = KiReleaseProfileSourceLocked(Profile->Source);
        if (CurrentSource) FreeSource = TRUE;
    }

    /* Release the profile lock */
    KeReleaseSpinLockFromDpcLevel(&KiProfileLock);

    /* Lower back to original IRQL */
    KeLowerIrql(OldIrql);

    /* Disarm the timer outside the profile lock: the HAL rendezvous IPIs
       every processor and must not wait behind PROFILE_LEVEL spinners */
    if (CurrentSource) HalStopProfileInterrupt(Profile->Source);
    KeReleaseGuardedMutex(&KiProfileSourceMutex);

    /* Free the Source Object */
    if (FreeSource) ExFreePoolWithTag(CurrentSource, 'forP');

    /* Return whether we could stop the profile */
    return StoppedProfile;
}

ULONG
NTAPI
KeQueryIntervalProfile(IN KPROFILE_SOURCE ProfileSource)
{
    HAL_PROFILE_SOURCE_INFORMATION ProfileSourceInformation;
    ULONG ReturnLength, Interval;
    NTSTATUS Status;

    /* Check what profile this is */
    if (ProfileSource == ProfileTime)
    {
#if defined(_M_ARM) && !defined(_M_ARM64)
        /* The ARM32 HAL has no profile interrupt provider. */
        Interval = 0;
#else
        /* Return the time interval */
        Interval = KiProfileTimeInterval;
#endif
    }
    else if (ProfileSource == ProfileAlignmentFixup)
    {
        /* Return the alignment interval */
        Interval = KiProfileAlignmentFixupInterval;
    }
    else
    {
        /* Request it from HAL */
        ProfileSourceInformation.Source = ProfileSource;
        Status = HalQuerySystemInformation(HalProfileSourceInformation,
                                           sizeof(HAL_PROFILE_SOURCE_INFORMATION),
                                           &ProfileSourceInformation,
                                           &ReturnLength);

        /* Check if HAL handled it and supports this profile */
        if (NT_SUCCESS(Status) && (ProfileSourceInformation.Supported))
        {
            /* Get the interval */
            Interval = ProfileSourceInformation.Interval;
        }
        else
        {
            /* Unsupported or invalid source, fail */
            Interval = 0;
        }
    }

    /* Return the interval we got */
    return Interval;
}

VOID
NTAPI
KeSetIntervalProfile(IN ULONG Interval,
                     IN KPROFILE_SOURCE ProfileSource)
{
    HAL_PROFILE_SOURCE_INTERVAL ProfileSourceInterval;

    /* Check what profile this is */
    if (ProfileSource == ProfileTime)
    {
        /* Set the interval through HAL */
        KiProfileTimeInterval = (ULONG)HalSetProfileInterval(Interval);
    }
    else if (ProfileSource == ProfileAlignmentFixup)
    {
        /* Set the alignment interval */
        KiProfileAlignmentFixupInterval = Interval;
    }
    else
    {
        /* HAL handles any other interval */
        ProfileSourceInterval.Source = ProfileSource;
        ProfileSourceInterval.Interval = Interval;
        HalSetSystemInformation(HalProfileSourceInterval,
                                sizeof(HAL_PROFILE_SOURCE_INTERVAL),
                                &ProfileSourceInterval);
    }
}

/*
 * @implemented
 */
VOID
NTAPI
KeProfileInterrupt(IN PKTRAP_FRAME TrapFrame)
{
    /* Called from HAL for Timer Profiling */
    KeProfileInterruptWithSource(TrapFrame, ProfileTime);
}

VOID
NTAPI
KiParseProfileList(IN PKTRAP_FRAME TrapFrame,
                   IN KPROFILE_SOURCE Source,
                   IN PLIST_ENTRY ListHead)
{
    PULONG BucketValue;
    PKPROFILE Profile;
    PLIST_ENTRY NextEntry;
    ULONG_PTR ProgramCounter;
    ULONG Processor;

    /* Get the Program Counter */
    ProgramCounter = KeGetTrapFramePc(TrapFrame);
    Processor = KeGetCurrentProcessorNumber();

    /* Loop the List */
    for (NextEntry = ListHead->Flink;
         NextEntry != ListHead;
         NextEntry = NextEntry->Flink)
    {
        /* Get the entry */
        Profile = CONTAINING_RECORD(NextEntry, KPROFILE, ProfileListEntry);

        /* Check if the source is good, and if it's within the range */
        if ((Profile->Source != Source) ||
            !(Profile->Affinity & AFFINITY_MASK(Processor)) ||
            (ProgramCounter < (ULONG_PTR)Profile->RangeBase) ||
            (ProgramCounter >= (ULONG_PTR)Profile->RangeLimit))
        {
            continue;
        }

        /* Get the Pointer to the Bucket Value representing this Program Counter */
        BucketValue = (PULONG)((ULONG_PTR)Profile->Buffer +
                               (((ProgramCounter - (ULONG_PTR)Profile->RangeBase)
                                >> Profile->BucketShift) &~ 0x3));

        /* Increment the value */
        InterlockedIncrement((PLONG)BucketValue);
    }
}

/*
 * @implemented
 *
 * Remarks:
 *         Called from HAL, this function looks up the process
 *         entries, finds the proper source object, verifies the
 *         ranges with the trapframe data, and inserts the information
 *         from the trap frame into the buffer, while using buckets and
 *         shifting like we specified. -- Alex
 */
VOID
NTAPI
KeProfileInterruptWithSource(IN PKTRAP_FRAME TrapFrame,
                             IN KPROFILE_SOURCE Source)
{
    PKPROCESS Process = KeGetCurrentThread()->ApcState.Process;
    KIRQL OldIrql;
    BOOLEAN RaisedIrql = FALSE;

    /* Feed the loss-aware trace engine from the same physical interrupt. */
    KprofTraceSampleEvent(TrapFrame, Source);

    if (KeGetCurrentIrql() < KiProfileIrql)
    {
        KeRaiseIrql(KiProfileIrql, &OldIrql);
        RaisedIrql = TRUE;
    }

    /* Start/stop and interrupt traversal share this lifetime lock. */
    KeAcquireSpinLockAtDpcLevel(&KiProfileLock);
    KiParseProfileList(TrapFrame, Source, &Process->ProfileListHead);
    KiParseProfileList(TrapFrame, Source, &KiProfileListHead);
    KeReleaseSpinLockFromDpcLevel(&KiProfileLock);

    if (RaisedIrql) KeLowerIrql(OldIrql);
}

/*
 * @implemented
 */
VOID
NTAPI
KeSetProfileIrql(IN KIRQL ProfileIrql)
{
    /* Set the IRQL at which Profiling will run */
    KiProfileIrql = ProfileIrql;
}

/* REACTOS TRACE PROFILING **************************************************/

#define KPROF_TRACE_SESSION_TAG 'sTrP'
#define KPROF_TRACE_RING_TAG    'rTrP'
#define KPROF_TRACE_IMAGE_TAG   'iTrP'
#define KPROF_TRACE_MAX_GLOBAL_RING_BYTES  (256ULL * 1024 * 1024)
#define KPROF_TRACE_MAX_IMAGE_METADATA_BYTES (4 * 1024 * 1024)
#define KPROF_TRACE_MAX_IMAGE_METADATA_COUNT 4096

#define KPROF_TRACE_STATE_CREATED   0
#define KPROF_TRACE_STATE_RUNNING   1
#define KPROF_TRACE_STATE_STOPPING  2
#define KPROF_TRACE_STATE_STOPPED   3
#define KPROF_TRACE_STATE_DESTROYED 4

/*
 * Per-CPU record ring ownership contract.
 *
 * Head is advanced only by the producer, Tail only by the consumer, and both
 * are monotonically increasing byte counters; the buffer index is the value
 * masked by (Size - 1), Size being a power of two.  Every access to Head,
 * Tail, and the buffer happens under Lock, which producers take at their own
 * IRQL (raised to at least DISPATCH_LEVEL) and consumers take from passive
 * level after the session ControlLock; the spinlock's acquire/release
 * barriers order the buffer contents against the counter updates, so no
 * additional memory barriers are required.  Records never straddle Head and
 * Tail: writers reserve whole records, readers copy whole records, and
 * wraparound splits only the byte copy, never the accounting.  Loss counters
 * are producer-owned; the consumer reads them under the same lock while
 * draining.
 */
typedef struct _KPROF_TRACE_RING
{
    KSPIN_LOCK Lock;
    PUCHAR Buffer;
    ULONG Size;
    ULONGLONG Head;
    ULONGLONG Tail;
    ULONGLONG ProducedRecords;
    ULONGLONG HighWatermark;
    ULONGLONG LostRecords;
    ULONGLONG PendingLostRecords;
    ULONGLONG FirstLostSequence;
    ULONGLONG LastLostSequence;
    ULONGLONG FirstLostTimestamp;
    ULONGLONG LastLostTimestamp;
    ULONG PendingLostReason;
} KPROF_TRACE_RING, *PKPROF_TRACE_RING;

typedef struct _KPROF_TRACE_IMAGE_ENTRY
{
    LIST_ENTRY ListEntry;
    ULONGLONG ImageKey;
    ULONG_PTR ProcessKey;
    ULONGLONG ImageBase;
    ULONG AllocationSize;
    ULONG Flags;
    ULONG PathBytes;
    ULONG BuildIdBytes;
    ULONG Checksum;
    ULONG TimeDateStamp;
    USHORT Machine;
    BOOLEAN Active;
    UCHAR Reserved;
    UCHAR BuildId[KPROF_TRACE_IMAGE_BUILD_ID_BYTES];
    UCHAR Path[ANYSIZE_ARRAY];
} KPROF_TRACE_IMAGE_ENTRY, *PKPROF_TRACE_IMAGE_ENTRY;

typedef struct _KPROF_TRACE_IMAGE_SOURCE
{
    ULONG Flags;
    ULONG PathBytes;
    ULONG BuildIdBytes;
    ULONG Checksum;
    ULONG TimeDateStamp;
    USHORT Machine;
    USHORT Reserved;
    UCHAR BuildId[KPROF_TRACE_IMAGE_BUILD_ID_BYTES];
    PCUCHAR Path;
} KPROF_TRACE_IMAGE_SOURCE, *PKPROF_TRACE_IMAGE_SOURCE;

typedef struct _KPROF_IMAGE_DEBUG_DIRECTORY
{
    ULONG Characteristics;
    ULONG TimeDateStamp;
    USHORT MajorVersion;
    USHORT MinorVersion;
    ULONG Type;
    ULONG SizeOfData;
    ULONG AddressOfRawData;
    ULONG PointerToRawData;
} KPROF_IMAGE_DEBUG_DIRECTORY, *PKPROF_IMAGE_DEBUG_DIRECTORY;

#define KPROF_IMAGE_DEBUG_TYPE_CODEVIEW 2

C_ASSERT(sizeof(KPROF_TRACE_RECORD) == 96);
C_ASSERT(sizeof(KPROF_IMAGE_DEBUG_DIRECTORY) == 28);

struct _KPROF_TRACE_SESSION
{
    LIST_ENTRY ListEntry;
    EX_RUNDOWN_REF ProducerRundown;
    KGUARDED_MUTEX ControlLock;
    KGUARDED_MUTEX ImageMetadataLock;
    LIST_ENTRY ImageMetadataList;
    KEVENT DataEvent;
    KDPC NotifyDpc;
    volatile LONG NotifyQueued;
    KDPC ReleaseDpc;
    volatile LONG PendingProducerReleases;
    PEPROCESS OwnerProcess;
    PEPROCESS TargetProcess;
    KPROF_TRACE_CONFIG Config;
    volatile LONG State;
    volatile LONG64 NextSequence;
    ULONGLONG NextImageKey;
    ULONG ImageMetadataBytes;
    ULONG ImageMetadataCount;
    ULONGLONG StartTimestamp;
    ULONGLONG StopTimestamp;
    volatile LONG64 Samples;
    volatile LONG64 ContextSwitches;
    volatile LONG64 Wakeups;
    volatile LONG64 LifecycleRecords;
    volatile LONG64 FilteredSamples;
    KPROFILE_SOURCE Source;
    BOOLEAN SourceAcquired;
    ULONGLONG AllocatedRingBytes;
    KPROF_TRACE_RING Rings[MAXIMUM_PROCESSORS];
};

static LIST_ENTRY KiKprofTraceSessionList =
{
    &KiKprofTraceSessionList,
    &KiKprofTraceSessionList
};
static volatile LONG KiKprofTraceSessionCount;
static volatile LONG64 KiKprofTraceAllocatedRingBytes;
/* Lockless negative gates only; KiProfileLock remains the session-list authority. */
static volatile LONG KiKprofTraceActiveContextSwitchSessions;
static volatile LONG KiKprofTraceActiveWakeupSessions;

#if defined(_M_AMD64)

/*
 * Interrupt-safe kernel unwind support.
 *
 * The profile interrupt runs above DISPATCH_LEVEL, so it can take neither
 * PsLoadedModuleSpinLock (RtlLookupFunctionEntry does) nor any mutex.  A
 * fixed-size, seqlock-published image table lets the sampler locate .pdata
 * without locks: writers serialize on KiKprofUnwindLock at DISPATCH_LEVEL
 * and bump the generation around every mutation; the interrupt reader bails
 * out and drops the chain whenever it observes a write in progress.
 *
 * Known hazard until runtime-proven: a reader that validated the table just
 * before an image unload can still dereference .pdata while the image is
 * being unmapped.  The window is one interrupt; closing it needs a grace
 * period (IPI rendezvous) after removal.
 */

#define KPROF_UNWIND_MAX_IMAGES 128

typedef struct _KPROF_UNWIND_IMAGE
{
    ULONG_PTR Base;
    SIZE_T Size;
    PRUNTIME_FUNCTION FunctionTable;
    ULONG FunctionCount;
} KPROF_UNWIND_IMAGE, *PKPROF_UNWIND_IMAGE;

static KPROF_UNWIND_IMAGE KiKprofUnwindImages[KPROF_UNWIND_MAX_IMAGES];
static ULONG KiKprofUnwindImageCount;
static volatile LONG KiKprofUnwindGeneration;
static KSPIN_LOCK KiKprofUnwindLock;
static volatile LONG KiKprofUnwindSeeded;
static volatile LONG KiKprofChainActive[MAXIMUM_PROCESSORS];

static
VOID
KiKprofUnwindInsertImage(
    IN PVOID ImageBase,
    IN SIZE_T ImageSize)
{
    PRUNTIME_FUNCTION FunctionTable;
    ULONG DirectorySize = 0;
    ULONG FunctionCount;
    ULONG Index, Position;
    KIRQL OldIrql;

    FunctionTable = RtlImageDirectoryEntryToData(ImageBase, TRUE, IMAGE_DIRECTORY_ENTRY_EXCEPTION, &DirectorySize);
    FunctionCount = DirectorySize / sizeof(RUNTIME_FUNCTION);
    if (!FunctionTable || !FunctionCount) return;

    KeAcquireSpinLock(&KiKprofUnwindLock, &OldIrql);

    for (Position = 0; Position < KiKprofUnwindImageCount; Position++)
    {
        if (KiKprofUnwindImages[Position].Base >= (ULONG_PTR)ImageBase) break;
    }
    if ((Position < KiKprofUnwindImageCount) && (KiKprofUnwindImages[Position].Base == (ULONG_PTR)ImageBase))
    {
        InterlockedIncrement(&KiKprofUnwindGeneration);
        KiKprofUnwindImages[Position].Size = ImageSize;
        KiKprofUnwindImages[Position].FunctionTable = FunctionTable;
        KiKprofUnwindImages[Position].FunctionCount = FunctionCount;
        InterlockedIncrement(&KiKprofUnwindGeneration);
        KeReleaseSpinLock(&KiKprofUnwindLock, OldIrql);
        return;
    }
    if (KiKprofUnwindImageCount == KPROF_UNWIND_MAX_IMAGES)
    {
        KeReleaseSpinLock(&KiKprofUnwindLock, OldIrql);
        return;
    }

    InterlockedIncrement(&KiKprofUnwindGeneration);
    for (Index = KiKprofUnwindImageCount; Index > Position; Index--)
    {
        KiKprofUnwindImages[Index] = KiKprofUnwindImages[Index - 1];
    }
    KiKprofUnwindImages[Position].Base = (ULONG_PTR)ImageBase;
    KiKprofUnwindImages[Position].Size = ImageSize;
    KiKprofUnwindImages[Position].FunctionTable = FunctionTable;
    KiKprofUnwindImages[Position].FunctionCount = FunctionCount;
    KiKprofUnwindImageCount++;
    InterlockedIncrement(&KiKprofUnwindGeneration);

    KeReleaseSpinLock(&KiKprofUnwindLock, OldIrql);
}

static
VOID
KiKprofUnwindRemoveImage(
    IN PVOID ImageBase)
{
    ULONG Index, Position;
    KIRQL OldIrql;

    KeAcquireSpinLock(&KiKprofUnwindLock, &OldIrql);
    for (Position = 0; Position < KiKprofUnwindImageCount; Position++)
    {
        if (KiKprofUnwindImages[Position].Base == (ULONG_PTR)ImageBase) break;
    }
    if (Position == KiKprofUnwindImageCount)
    {
        KeReleaseSpinLock(&KiKprofUnwindLock, OldIrql);
        return;
    }

    InterlockedIncrement(&KiKprofUnwindGeneration);
    for (Index = Position; Index + 1 < KiKprofUnwindImageCount; Index++)
    {
        KiKprofUnwindImages[Index] = KiKprofUnwindImages[Index + 1];
    }
    KiKprofUnwindImageCount--;
    InterlockedIncrement(&KiKprofUnwindGeneration);
    KeReleaseSpinLock(&KiKprofUnwindLock, OldIrql);
}

static
VOID
KiKprofUnwindSeedSystemImages(VOID)
{
    struct
    {
        PVOID Base;
        SIZE_T Size;
    } Snapshot[KPROF_UNWIND_MAX_IMAGES];
    PLIST_ENTRY Entry;
    PLDR_DATA_TABLE_ENTRY LdrEntry;
    ULONG Count = 0, Index;
    KIRQL OldIrql;

    if (InterlockedCompareExchange(&KiKprofUnwindSeeded, 1, 0) != 0) return;

    /* Collect bases under the loader lock, parse directories after
     * releasing it: image headers may be pageable. */
    KeAcquireSpinLock(&PsLoadedModuleSpinLock, &OldIrql);
    for (Entry = PsLoadedModuleList.Flink; (Entry != &PsLoadedModuleList) && (Count < KPROF_UNWIND_MAX_IMAGES); Entry = Entry->Flink)
    {
        LdrEntry = CONTAINING_RECORD(Entry, LDR_DATA_TABLE_ENTRY, InLoadOrderLinks);
        Snapshot[Count].Base = LdrEntry->DllBase;
        Snapshot[Count].Size = LdrEntry->SizeOfImage;
        Count++;
    }
    KeReleaseSpinLock(&PsLoadedModuleSpinLock, OldIrql);

    for (Index = 0; Index < Count; Index++)
    {
        KiKprofUnwindInsertImage(Snapshot[Index].Base, Snapshot[Index].Size);
    }
}

static
BOOLEAN
KiKprofUnwindLookup(
    IN ULONG64 ControlPc,
    OUT PULONG64 ImageBase,
    OUT PRUNTIME_FUNCTION *FunctionEntry)
{
    KPROF_UNWIND_IMAGE Image;
    PRUNTIME_FUNCTION Table, Candidate;
    LONG Generation;
    ULONG Lo, Hi, Mid, Count;
    ULONG Rva;
    BOOLEAN Found = FALSE;

    *ImageBase = 0;
    *FunctionEntry = NULL;

    Generation = KiKprofUnwindGeneration;
    if (Generation & 1) return FALSE;
    KeMemoryBarrier();

    Count = KiKprofUnwindImageCount;
    if (Count > KPROF_UNWIND_MAX_IMAGES) return FALSE;
    Lo = 0;
    Hi = Count;
    while (Lo < Hi)
    {
        Mid = Lo + (Hi - Lo) / 2;
        if (ControlPc < KiKprofUnwindImages[Mid].Base)
        {
            Hi = Mid;
        }
        else if (ControlPc >= KiKprofUnwindImages[Mid].Base + KiKprofUnwindImages[Mid].Size)
        {
            Lo = Mid + 1;
        }
        else
        {
            Image = KiKprofUnwindImages[Mid];
            Found = TRUE;
            break;
        }
    }

    KeMemoryBarrier();
    if (KiKprofUnwindGeneration != Generation) return FALSE;
    if (!Found) return FALSE;
    if (!Image.FunctionTable || !Image.FunctionCount) return FALSE;

    *ImageBase = Image.Base;
    Table = Image.FunctionTable;
    Rva = (ULONG)(ControlPc - Image.Base);

    Lo = 0;
    Hi = Image.FunctionCount;
    while (Lo < Hi)
    {
        Mid = Lo + (Hi - Lo) / 2;
        Candidate = &Table[Mid];
        if (!MmIsAddressValid(Candidate)) return FALSE;
        if (Rva < Candidate->BeginAddress)
        {
            Hi = Mid;
        }
        else if (Rva >= Candidate->EndAddress)
        {
            Lo = Mid + 1;
        }
        else
        {
            *FunctionEntry = Candidate;
            return TRUE;
        }
    }

    /* In-image address without a function entry: a leaf function. */
    return TRUE;
}

static
ULONG
KiKprofTraceCaptureKernelChain(
    IN PKTRAP_FRAME TrapFrame,
    OUT PULONGLONG Frames,
    IN ULONG Limit,
    OUT PULONG ChainFlags)
{
    CONTEXT Context;
    PRUNTIME_FUNCTION FunctionEntry;
    PVOID HandlerData;
    PKTHREAD Thread;
    ULONG64 ControlPc, ImageBase, EstablisherFrame;
    ULONG64 StackLow, StackHigh, NewPc;
    ULONG Depth = 0;
    ULONG Processor;

    *ChainFlags = 0;
    if (!Limit) return 0;

    ControlPc = KeGetTrapFramePc(TrapFrame);
    Frames[Depth++] = ControlPc;
    if (ControlPc <= (ULONG64)MmHighestUserAddress)
    {
        /* User-mode interrupt: deferred user unwinding is not implemented,
         * so the chain is the interrupted instruction alone. */
        *ChainFlags |= KPROF_TRACE_CHAIN_USER_BOUNDARY;
        return Depth;
    }

    Processor = KeGetCurrentProcessorNumber();
    if (Processor >= MAXIMUM_PROCESSORS) return Depth;
    if (InterlockedCompareExchange(&KiKprofChainActive[Processor], 1, 0) != 0) return Depth;

    RtlZeroMemory(&Context, sizeof(Context));
    Context.Rip = TrapFrame->Rip;
    Context.Rsp = TrapFrame->Rsp;
    Context.Rbp = TrapFrame->Rbp;
    Context.Rax = TrapFrame->Rax;
    Context.Rcx = TrapFrame->Rcx;
    Context.Rdx = TrapFrame->Rdx;
    Context.R8 = TrapFrame->R8;
    Context.R9 = TrapFrame->R9;
    Context.R10 = TrapFrame->R10;
    Context.R11 = TrapFrame->R11;

    Thread = KeGetCurrentThread();
    StackLow = (ULONG64)Thread->StackLimit;
    StackHigh = (ULONG64)Thread->StackBase;
    if ((Context.Rsp < StackLow) || (Context.Rsp >= StackHigh))
    {
        /* Interrupted on a DPC or interrupt stack: bound the walk to a
         * window above the interrupted stack pointer instead. */
        StackLow = Context.Rsp;
        StackHigh = Context.Rsp + KERNEL_STACK_SIZE;
    }

    while (Depth < Limit)
    {
        if ((Context.Rsp & 7) || (Context.Rsp < StackLow) || (Context.Rsp >= StackHigh))
        {
            *ChainFlags |= KPROF_TRACE_CHAIN_STOPPED;
            break;
        }
        if (!KiKprofUnwindLookup(ControlPc, &ImageBase, &FunctionEntry))
        {
            *ChainFlags |= KPROF_TRACE_CHAIN_STOPPED;
            break;
        }
        if (!FunctionEntry)
        {
            /* Leaf function: the return address sits at RSP. */
            if ((Context.Rsp + sizeof(ULONG64) > StackHigh) || !MmIsAddressValid((PVOID)Context.Rsp))
            {
                *ChainFlags |= KPROF_TRACE_CHAIN_STOPPED;
                break;
            }
            Context.Rip = *(PULONG64)Context.Rsp;
            Context.Rsp += sizeof(ULONG64);
        }
        else
        {
            if (!MmIsAddressValid((PVOID)(ImageBase + FunctionEntry->UnwindData)))
            {
                *ChainFlags |= KPROF_TRACE_CHAIN_STOPPED;
                break;
            }
            RtlVirtualUnwind(UNW_FLAG_NHANDLER, ImageBase, ControlPc, FunctionEntry, &Context, &HandlerData, &EstablisherFrame, NULL);
        }

        NewPc = Context.Rip;
        if (!NewPc) break;
        if (NewPc <= (ULONG64)MmHighestUserAddress)
        {
            *ChainFlags |= KPROF_TRACE_CHAIN_USER_BOUNDARY;
            break;
        }
        Frames[Depth++] = NewPc;
        ControlPc = NewPc;
    }

    if (Depth == Limit) *ChainFlags |= KPROF_TRACE_CHAIN_TRUNCATED;

    InterlockedExchange(&KiKprofChainActive[Processor], 0);
    return Depth;
}

#endif /* _M_AMD64 */

static
BOOLEAN
KiKprofTraceReserveRingBytes(
    IN ULONGLONG Bytes)
{
    LONG64 Current, NewValue;

    if (Bytes > KPROF_TRACE_MAX_GLOBAL_RING_BYTES) return FALSE;
    for (;;)
    {
        Current = InterlockedCompareExchange64(&KiKprofTraceAllocatedRingBytes, 0, 0);
        if (Current < 0) return FALSE;
        if ((ULONGLONG)Current > KPROF_TRACE_MAX_GLOBAL_RING_BYTES - Bytes)
            return FALSE;
        NewValue = Current + (LONG64)Bytes;
        if (InterlockedCompareExchange64(&KiKprofTraceAllocatedRingBytes, NewValue, Current) == Current)
        {
            return TRUE;
        }
    }
}

static
VOID
NTAPI
KiKprofTraceNotifyDpc(
    IN PKDPC Dpc,
    IN PVOID DeferredContext,
    IN PVOID SystemArgument1,
    IN PVOID SystemArgument2)
{
    PKPROF_TRACE_SESSION Session = DeferredContext;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    KeSetEvent(&Session->DataEvent, IO_NO_INCREMENT, FALSE);
    InterlockedExchange(&Session->NotifyQueued, 0);
    ExReleaseRundownProtection(&Session->ProducerRundown);
}

static
VOID
NTAPI
KiKprofTraceReleaseDpc(
    IN PKDPC Dpc,
    IN PVOID DeferredContext,
    IN PVOID SystemArgument1,
    IN PVOID SystemArgument2)
{
    PKPROF_TRACE_SESSION Session = DeferredContext;
    LONG Pending;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);

    Pending = InterlockedExchange(&Session->PendingProducerReleases, 0);
    while (Pending-- > 0) ExReleaseRundownProtection(&Session->ProducerRundown);
}

static
VOID
KiKprofTraceReleaseProducer(
    IN PKPROF_TRACE_SESSION Session)
{
    /* Releasing the last producer reference while the stop path waits on the
     * rundown signals an event, and KeSetEvent is limited to DISPATCH_LEVEL.
     * Producers running above that (PROFILE_LEVEL samples, SYNCH_LEVEL
     * scheduler hooks) defer the release to a DPC instead of signaling from
     * the interrupt. */
    if (KeGetCurrentIrql() <= DISPATCH_LEVEL)
    {
        ExReleaseRundownProtection(&Session->ProducerRundown);
        return;
    }
    InterlockedIncrement(&Session->PendingProducerReleases);
    KeInsertQueueDpc(&Session->ReleaseDpc, NULL, NULL);
}

static
VOID
KiKprofTraceNotifyData(
    IN PKPROF_TRACE_SESSION Session)
{
    if (InterlockedCompareExchange(&Session->NotifyQueued, 1, 0) != 0) return;
    if (!ExAcquireRundownProtection(&Session->ProducerRundown))
    {
        InterlockedExchange(&Session->NotifyQueued, 0);
        return;
    }

    if (!KeInsertQueueDpc(&Session->NotifyDpc, NULL, NULL))
    {
        InterlockedExchange(&Session->NotifyQueued, 0);
        KiKprofTraceReleaseProducer(Session);
    }
}

static
BOOLEAN
KiKprofTraceTimerSupported(VOID)
{
#if defined(_M_IX86) || defined(_M_AMD64) || defined(_M_ARM64)
    return TRUE;
#else
    return FALSE;
#endif
}

static
BOOLEAN
KiKprofTraceIsOwner(
    IN PKPROF_TRACE_SESSION Session)
{
    return Session && (PsGetCurrentProcess() == Session->OwnerProcess);
}

static
ULONGLONG
KiKprofTraceProcessId(
    IN PEPROCESS Process)
{
    return Process ? (ULONGLONG)(ULONG_PTR)Process->UniqueProcessId : 0;
}

static
ULONGLONG
KiKprofTraceThreadId(
    IN PETHREAD Thread)
{
    return Thread ? (ULONGLONG)(ULONG_PTR)Thread->Cid.UniqueThread : 0;
}

static
ULONGLONG
KiKprofTraceMixObjectKey(
    IN ULONGLONG CreateTime,
    IN ULONGLONG ObjectId,
    IN ULONGLONG Domain)
{
    ULONGLONG Key;

    /* SplitMix64 finalization hides kernel object addresses entirely while
     * distinguishing reused numeric IDs with the object's creation time. */
    Key = CreateTime ^ (ObjectId << 32) ^ ObjectId ^ Domain;
    Key ^= Key >> 30;
    Key *= 0xbf58476d1ce4e5b9ULL;
    Key ^= Key >> 27;
    Key *= 0x94d049bb133111ebULL;
    Key ^= Key >> 31;
    return Key ? Key : Domain;
}

static
ULONGLONG
KiKprofTraceProcessKey(
    IN PEPROCESS Process)
{
    if (!Process) return 0;
    return KiKprofTraceMixObjectKey((ULONGLONG)Process->CreateTime.QuadPart, KiKprofTraceProcessId(Process), 0x50524f4345535301ULL);
}

static
ULONGLONG
KiKprofTraceThreadKey(
    IN PETHREAD Thread)
{
    if (!Thread) return 0;
    return KiKprofTraceMixObjectKey((ULONGLONG)Thread->CreateTime.QuadPart, KiKprofTraceThreadId(Thread), 0x5448524541440001ULL);
}

static
PEPROCESS
KiKprofTraceThreadProcess(
    IN PKTHREAD Thread)
{
    if (!Thread || !Thread->ApcState.Process) return NULL;
    return CONTAINING_RECORD(Thread->ApcState.Process, EPROCESS, Pcb);
}

static
PETHREAD
KiKprofTraceExecutiveThread(
    IN PKTHREAD Thread)
{
    return Thread ? CONTAINING_RECORD(Thread, ETHREAD, Tcb) : NULL;
}

static
BOOLEAN
KiKprofTraceScopeMatches(
    IN PKPROF_TRACE_SESSION Session,
    IN PEPROCESS Process)
{
    if (Session->Config.Scope == KprofTraceScopeSystem) return TRUE;
    return Process && (Process == Session->TargetProcess);
}

static
VOID
KiKprofTraceAcquireRing(
    IN PKPROF_TRACE_RING Ring,
    OUT PKIRQL OldIrql,
    OUT PBOOLEAN RaisedIrql)
{
    *RaisedIrql = FALSE;
    if (KeGetCurrentIrql() < KiProfileIrql)
    {
        KeRaiseIrql(KiProfileIrql, OldIrql);
        *RaisedIrql = TRUE;
    }

    KeAcquireSpinLockAtDpcLevel(&Ring->Lock);
}

static
VOID
KiKprofTraceReleaseRing(
    IN PKPROF_TRACE_RING Ring,
    IN KIRQL OldIrql,
    IN BOOLEAN RaisedIrql)
{
    KeReleaseSpinLockFromDpcLevel(&Ring->Lock);
    if (RaisedIrql) KeLowerIrql(OldIrql);
}

static
BOOLEAN
KiKprofTraceWriteRawLocked(
    IN PKPROF_TRACE_RING Ring,
    IN const KPROF_TRACE_RECORD *Record)
{
    ULONG FirstLength, SecondLength, Offset;
    ULONGLONG Used;

    ASSERT((Record->Header.Size >= sizeof(*Record)) && (Record->Header.Size <= KPROF_TRACE_MAX_RECORD_SIZE));
    Used = Ring->Head - Ring->Tail;
    ASSERT(Used <= Ring->Size);
    if ((ULONGLONG)Record->Header.Size > (Ring->Size - Used)) return FALSE;

    Offset = (ULONG)(Ring->Head & (Ring->Size - 1));
    FirstLength = min((ULONG)Record->Header.Size, Ring->Size - Offset);
    SecondLength = Record->Header.Size - FirstLength;
    RtlCopyMemory(Ring->Buffer + Offset, Record, FirstLength);
    if (SecondLength)
    {
        RtlCopyMemory(Ring->Buffer, (PUCHAR)Record + FirstLength, SecondLength);
    }

    Ring->Head += Record->Header.Size;
    Used = Ring->Head - Ring->Tail;
    if (Used > Ring->HighWatermark) Ring->HighWatermark = Used;
    Ring->ProducedRecords++;
    return TRUE;
}

static
USHORT
KiKprofTraceRingPeekSizeLocked(
    IN const KPROF_TRACE_RING *Ring,
    IN ULONGLONG Position)
{
    ULONGLONG SizePosition = Position + FIELD_OFFSET(KPROF_TRACE_RECORD_HEADER, Size);
    ULONG Low = (ULONG)(SizePosition & (Ring->Size - 1));
    ULONG High = (ULONG)((SizePosition + 1) & (Ring->Size - 1));

    return (USHORT)(Ring->Buffer[Low] | ((USHORT)Ring->Buffer[High] << 8));
}

static
BOOLEAN
KiKprofTraceEmitPendingLossLocked(
    IN PKPROF_TRACE_SESSION Session,
    IN PKPROF_TRACE_RING Ring,
    IN ULONG Processor)
{
    KPROF_TRACE_RECORD LostRecord;
    ULONGLONG Used;

    if (!Ring->PendingLostRecords) return FALSE;
    Used = Ring->Head - Ring->Tail;
    ASSERT(Used <= Ring->Size);
    if ((ULONGLONG)sizeof(LostRecord) > (Ring->Size - Used)) return FALSE;

    RtlZeroMemory(&LostRecord, sizeof(LostRecord));
    LostRecord.Header.Type = KprofTraceRecordLost;
    LostRecord.Header.Size = sizeof(LostRecord);
    LostRecord.Header.Processor = Processor;
    LostRecord.Header.Timestamp = KeQueryInterruptTime();
    LostRecord.Header.Sequence =
        (ULONGLONG)InterlockedIncrement64(&Session->NextSequence) - 1;
    LostRecord.Data.Lost.FirstSequence = Ring->FirstLostSequence;
    LostRecord.Data.Lost.LastSequence = Ring->LastLostSequence;
    LostRecord.Data.Lost.Count = Ring->PendingLostRecords;
    LostRecord.Data.Lost.FirstTimestamp = Ring->FirstLostTimestamp;
    LostRecord.Data.Lost.LastTimestamp = Ring->LastLostTimestamp;
    LostRecord.Data.Lost.Reason = Ring->PendingLostReason;
    if (!KiKprofTraceWriteRawLocked(Ring, &LostRecord)) return FALSE;

    Ring->PendingLostRecords = 0;
    Ring->FirstLostSequence = 0;
    Ring->LastLostSequence = 0;
    Ring->FirstLostTimestamp = 0;
    Ring->LastLostTimestamp = 0;
    Ring->PendingLostReason = 0;
    return TRUE;
}

static
VOID
KiKprofTraceAccountLossLocked(
    IN PKPROF_TRACE_RING Ring,
    IN ULONGLONG Sequence,
    IN ULONGLONG Timestamp,
    IN ULONG Reason)
{
    if (Ring->PendingLostRecords == 0)
    {
        Ring->FirstLostSequence = Sequence;
        Ring->FirstLostTimestamp = Timestamp;
        Ring->PendingLostReason = Reason;
    }

    Ring->LastLostSequence = Sequence;
    Ring->LastLostTimestamp = Timestamp;
    Ring->PendingLostRecords++;
    Ring->LostRecords++;
}

static
BOOLEAN
KiKprofTraceWriteRecord(
    IN PKPROF_TRACE_SESSION Session,
    IN ULONG Processor,
    IN OUT PKPROF_TRACE_RECORD Record)
{
    PKPROF_TRACE_RING Ring;
    KIRQL OldIrql = PASSIVE_LEVEL;
    BOOLEAN RaisedIrql;
    BOOLEAN WroteRecord = FALSE;
    ULONGLONG Sequence;

    if (Processor >= MAXIMUM_PROCESSORS) return FALSE;
    Ring = &Session->Rings[Processor];
    if (!Ring->Buffer) return FALSE;

    KiKprofTraceAcquireRing(Ring, &OldIrql, &RaisedIrql);

    KiKprofTraceEmitPendingLossLocked(Session, Ring, Processor);

    Sequence = (ULONGLONG)InterlockedIncrement64(&Session->NextSequence) - 1;
    Record->Header.Sequence = Sequence;
    if (!KiKprofTraceWriteRawLocked(Ring, Record))
    {
        KiKprofTraceAccountLossLocked(Ring, Sequence, Record->Header.Timestamp, KPROF_TRACE_LOSS_RING_FULL);
    }
    else
    {
        WroteRecord = TRUE;
    }

    KiKprofTraceReleaseRing(Ring, OldIrql, RaisedIrql);
    if (WroteRecord)
    {
        switch (Record->Header.Type)
        {
            case KprofTraceRecordSample:
                InterlockedIncrement64(&Session->Samples);
                break;
            case KprofTraceRecordProcess:
            case KprofTraceRecordThread:
            case KprofTraceRecordImage:
                InterlockedIncrement64(&Session->LifecycleRecords);
                break;
            case KprofTraceRecordScheduler:
                if (Record->Header.Flags & KPROF_TRACE_RECORD_FLAG_READY)
                    InterlockedIncrement64(&Session->Wakeups);
                else
                    InterlockedIncrement64(&Session->ContextSwitches);
                break;
            default:
                break;
        }
        KiKprofTraceNotifyData(Session);
    }
    return WroteRecord;
}

static
ULONG
KiKprofTraceSnapshotSessions(
    IN ULONG RequiredFlag,
    IN PEPROCESS Process,
    IN OPTIONAL PEPROCESS AlternateProcess,
    OUT PKPROF_TRACE_SESSION *Sessions)
{
    PLIST_ENTRY NextEntry;
    PKPROF_TRACE_SESSION Session;
    ULONG Count = 0;
    KIRQL OldIrql = PASSIVE_LEVEL;
    BOOLEAN RaisedIrql = FALSE;
    ULONG Processor;

    Processor = KeGetCurrentProcessorNumber();
    if (Processor >= MAXIMUM_PROCESSORS) return 0;

    if (KeGetCurrentIrql() < KiProfileIrql)
    {
        KeRaiseIrql(KiProfileIrql, &OldIrql);
        RaisedIrql = TRUE;
    }

    KeAcquireSpinLockAtDpcLevel(&KiProfileLock);
    for (NextEntry = KiKprofTraceSessionList.Flink;
         NextEntry != &KiKprofTraceSessionList;
         NextEntry = NextEntry->Flink)
    {
        Session = CONTAINING_RECORD(NextEntry, KPROF_TRACE_SESSION, ListEntry);
        if (!(Session->Config.Flags & RequiredFlag)) continue;
        if (!(Session->Config.Affinity & AFFINITY_MASK(Processor)))
        {
            if (RequiredFlag == KPROF_TRACE_FLAG_SAMPLE) InterlockedIncrement64(&Session->FilteredSamples);
            continue;
        }
        if ((RequiredFlag == KPROF_TRACE_FLAG_SAMPLE) &&
            (Session->Config.Flags & KPROF_TRACE_FLAG_EXCLUDE_OWNER) &&
            (Process == Session->OwnerProcess))
        {
            InterlockedIncrement64(&Session->FilteredSamples);
            continue;
        }
        if (!KiKprofTraceScopeMatches(Session, Process) &&
            !KiKprofTraceScopeMatches(Session, AlternateProcess))
        {
            continue;
        }

        if (ExAcquireRundownProtection(&Session->ProducerRundown))
        {
            Sessions[Count++] = Session;
            if (Count == KPROF_TRACE_MAX_SESSIONS) break;
        }
    }
    KeReleaseSpinLockFromDpcLevel(&KiProfileLock);

    if (RaisedIrql) KeLowerIrql(OldIrql);
    return Count;
}

static
VOID
KiKprofTraceReleaseSessions(
    IN PKPROF_TRACE_SESSION *Sessions,
    IN ULONG Count)
{
    ULONG Index;

    for (Index = 0; Index < Count; Index++)
    {
        KiKprofTraceReleaseProducer(Sessions[Index]);
    }
}

static
ULONGLONG
KiKprofTraceNextImageKeyLocked(
    IN PKPROF_TRACE_SESSION Session)
{
    ULONGLONG Key;

    do
    {
        Key = ++Session->NextImageKey;
    } while ((Key == 0) || (Key == MAXULONGLONG));
    return Key;
}

static
BOOLEAN
KiKprofTraceCaptureImageIdentity(
    IN PVOID ImageBase,
    IN SIZE_T ImageSize,
    IN OUT PKPROF_TRACE_IMAGE_SOURCE Source)
{
    PIMAGE_NT_HEADERS NtHeaders = NULL;
    PKPROF_IMAGE_DEBUG_DIRECTORY DebugDirectory;
    PUCHAR CodeView;
    ULONG DebugBytes = 0, Index, Count;
    ULONG_PTR Base, DebugOffset, CodeViewOffset;
    NTSTATUS Status;
    BOOLEAN Valid = FALSE;

    if (!ImageBase || (ImageSize < sizeof(IMAGE_DOS_HEADER)))
        return FALSE;

    _SEH2_TRY
    {
        Status = RtlImageNtHeaderEx(0, ImageBase, (ULONGLONG)ImageSize, &NtHeaders);
        if (!NT_SUCCESS(Status) || !NtHeaders)
            _SEH2_LEAVE;

        Source->Machine = NtHeaders->FileHeader.Machine;
        Source->TimeDateStamp = NtHeaders->FileHeader.TimeDateStamp;
        if (NtHeaders->OptionalHeader.Magic == IMAGE_NT_OPTIONAL_HDR64_MAGIC)
        {
            PIMAGE_OPTIONAL_HEADER64 OptionalHeader =
                (PIMAGE_OPTIONAL_HEADER64)&NtHeaders->OptionalHeader;
            Source->Checksum = OptionalHeader->CheckSum;
        }
        else if (NtHeaders->OptionalHeader.Magic ==
                 IMAGE_NT_OPTIONAL_HDR32_MAGIC)
        {
            PIMAGE_OPTIONAL_HEADER32 OptionalHeader =
                (PIMAGE_OPTIONAL_HEADER32)&NtHeaders->OptionalHeader;
            Source->Checksum = OptionalHeader->CheckSum;
        }
        else
        {
            _SEH2_LEAVE;
        }
        Valid = TRUE;

        DebugDirectory = RtlImageDirectoryEntryToData(ImageBase, TRUE, IMAGE_DIRECTORY_ENTRY_DEBUG, &DebugBytes);
        if (!DebugDirectory ||
            (DebugBytes < sizeof(*DebugDirectory)))
        {
            _SEH2_LEAVE;
        }

        Base = (ULONG_PTR)ImageBase;
        if ((ULONG_PTR)DebugDirectory < Base)
            _SEH2_LEAVE;
        DebugOffset = (ULONG_PTR)DebugDirectory - Base;
        if ((DebugOffset > ImageSize) ||
            (DebugBytes > ImageSize - DebugOffset))
        {
            _SEH2_LEAVE;
        }

        Count = DebugBytes / sizeof(*DebugDirectory);
        for (Index = 0; Index < Count; Index++)
        {
            if ((DebugDirectory[Index].Type !=
                 KPROF_IMAGE_DEBUG_TYPE_CODEVIEW) ||
                (DebugDirectory[Index].SizeOfData <
                 KPROF_TRACE_IMAGE_BUILD_ID_BYTES + 4))
            {
                continue;
            }
            CodeViewOffset = DebugDirectory[Index].AddressOfRawData;
            if ((CodeViewOffset > ImageSize) ||
                (DebugDirectory[Index].SizeOfData >
                 ImageSize - CodeViewOffset))
            {
                continue;
            }
            CodeView = (PUCHAR)ImageBase + CodeViewOffset;
            if ((CodeView[0] != 'R') || (CodeView[1] != 'S') ||
                (CodeView[2] != 'D') || (CodeView[3] != 'S'))
            {
                continue;
            }
            RtlCopyMemory(Source->BuildId, CodeView + 4, KPROF_TRACE_IMAGE_BUILD_ID_BYTES);
            Source->BuildIdBytes = KPROF_TRACE_IMAGE_BUILD_ID_BYTES;
            Source->Flags |= KPROF_TRACE_IMAGE_META_RSDS;
            break;
        }
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Valid = FALSE;
    }
    _SEH2_END;

    return Valid;
}

static
NTSTATUS
KiKprofTraceImagePathUtf8(
    IN PCUNICODE_STRING ImageName,
    OUT PUCHAR *Path,
    OUT PULONG PathBytes)
{
    PUCHAR Buffer;
    ULONG Required = 0, Written = 0;
    NTSTATUS Status;

    *Path = NULL;
    *PathBytes = 0;
    if (!ImageName || !ImageName->Buffer || !ImageName->Length)
        return STATUS_OBJECT_NAME_NOT_FOUND;

    Status = RtlUnicodeToUTF8N(NULL, 0, &Required, ImageName->Buffer, ImageName->Length);
    if (!NT_SUCCESS(Status) || !Required)
        return NT_SUCCESS(Status) ? STATUS_OBJECT_NAME_NOT_FOUND : Status;
    if (Required > KPROF_TRACE_MAX_IMAGE_PATH_BYTES)
        return STATUS_NAME_TOO_LONG;

    Buffer = ExAllocatePoolWithTag(PagedPool, Required, KPROF_TRACE_IMAGE_TAG);
    if (!Buffer)
        return STATUS_INSUFFICIENT_RESOURCES;
    Status = RtlUnicodeToUTF8N((PCHAR)Buffer, Required, &Written, ImageName->Buffer, ImageName->Length);
    if (!NT_SUCCESS(Status) || (Written != Required))
    {
        ExFreePoolWithTag(Buffer, KPROF_TRACE_IMAGE_TAG);
        return NT_SUCCESS(Status) ? STATUS_DATA_ERROR : Status;
    }

    *Path = Buffer;
    *PathBytes = Written;
    return STATUS_SUCCESS;
}

static
BOOLEAN
KiKprofTraceAddImageMetadata(
    IN PKPROF_TRACE_SESSION Session,
    IN OPTIONAL PEPROCESS Process,
    IN PVOID ImageBase,
    IN const KPROF_TRACE_IMAGE_SOURCE *Source,
    OUT PULONGLONG ImageKey,
    OUT PULONGLONG MetadataId,
    OUT PBOOLEAN Truncated)
{
    PKPROF_TRACE_IMAGE_ENTRY Entry;
    ULONG AllocationSize, PathBytes;

    *MetadataId = 0;
    *Truncated = FALSE;
    KeAcquireGuardedMutex(&Session->ImageMetadataLock);
    *ImageKey = KiKprofTraceNextImageKeyLocked(Session);
    PathBytes = Source->PathBytes;
    AllocationSize = FIELD_OFFSET(KPROF_TRACE_IMAGE_ENTRY, Path) + PathBytes;
    if ((Session->ImageMetadataCount >=
         KPROF_TRACE_MAX_IMAGE_METADATA_COUNT) ||
        (AllocationSize > KPROF_TRACE_MAX_IMAGE_METADATA_BYTES -
         Session->ImageMetadataBytes))
    {
        PathBytes = 0;
        AllocationSize = FIELD_OFFSET(KPROF_TRACE_IMAGE_ENTRY, Path);
        *Truncated = TRUE;
    }
    if ((Session->ImageMetadataCount >=
         KPROF_TRACE_MAX_IMAGE_METADATA_COUNT) ||
        (AllocationSize > KPROF_TRACE_MAX_IMAGE_METADATA_BYTES -
         Session->ImageMetadataBytes))
    {
        KeReleaseGuardedMutex(&Session->ImageMetadataLock);
        return FALSE;
    }

    Entry = ExAllocatePoolWithTag(PagedPool, AllocationSize, KPROF_TRACE_IMAGE_TAG);
    if (!Entry)
    {
        KeReleaseGuardedMutex(&Session->ImageMetadataLock);
        return FALSE;
    }
    RtlZeroMemory(Entry, AllocationSize);
    Entry->ImageKey = *ImageKey;
    Entry->ProcessKey = (ULONG_PTR)Process;
    Entry->ImageBase = (ULONGLONG)(ULONG_PTR)ImageBase;
    Entry->AllocationSize = AllocationSize;
    Entry->Flags = Source->Flags;
    if (*Truncated)
        Entry->Flags |= KPROF_TRACE_IMAGE_META_TRUNCATED;
    Entry->PathBytes = PathBytes;
    Entry->BuildIdBytes = Source->BuildIdBytes;
    Entry->Checksum = Source->Checksum;
    Entry->TimeDateStamp = Source->TimeDateStamp;
    Entry->Machine = Source->Machine;
    Entry->Active = TRUE;
    RtlCopyMemory(Entry->BuildId, Source->BuildId, sizeof(Entry->BuildId));
    if (PathBytes)
        RtlCopyMemory(Entry->Path, Source->Path, PathBytes);
    InsertTailList(&Session->ImageMetadataList, &Entry->ListEntry);
    Session->ImageMetadataBytes += AllocationSize;
    Session->ImageMetadataCount++;
    *MetadataId = Entry->ImageKey;
    KeReleaseGuardedMutex(&Session->ImageMetadataLock);
    return TRUE;
}

static
ULONGLONG
KiKprofTraceUnloadImageKey(
    IN PKPROF_TRACE_SESSION Session,
    IN OPTIONAL PEPROCESS Process,
    IN PVOID ImageBase,
    OUT PBOOLEAN Found)
{
    PLIST_ENTRY NextEntry;
    PKPROF_TRACE_IMAGE_ENTRY Entry;
    ULONGLONG Key;

    *Found = FALSE;
    KeAcquireGuardedMutex(&Session->ImageMetadataLock);
    for (NextEntry = Session->ImageMetadataList.Blink;
         NextEntry != &Session->ImageMetadataList;
         NextEntry = NextEntry->Blink)
    {
        Entry = CONTAINING_RECORD(NextEntry, KPROF_TRACE_IMAGE_ENTRY, ListEntry);
        if (Entry->Active &&
            (Entry->ProcessKey == (ULONG_PTR)Process) &&
            (Entry->ImageBase == (ULONGLONG)(ULONG_PTR)ImageBase))
        {
            Entry->Active = FALSE;
            Key = Entry->ImageKey;
            *Found = TRUE;
            KeReleaseGuardedMutex(&Session->ImageMetadataLock);
            return Key;
        }
    }
    Key = KiKprofTraceNextImageKeyLocked(Session);
    KeReleaseGuardedMutex(&Session->ImageMetadataLock);
    return Key;
}

static
VOID
KiKprofTraceDiscardImageMetadata(
    IN PKPROF_TRACE_SESSION Session,
    IN ULONGLONG MetadataId)
{
    PLIST_ENTRY NextEntry;
    PKPROF_TRACE_IMAGE_ENTRY Entry = NULL;

    KeAcquireGuardedMutex(&Session->ImageMetadataLock);
    for (NextEntry = Session->ImageMetadataList.Flink;
         NextEntry != &Session->ImageMetadataList;
         NextEntry = NextEntry->Flink)
    {
        Entry = CONTAINING_RECORD(NextEntry, KPROF_TRACE_IMAGE_ENTRY, ListEntry);
        if (Entry->ImageKey == MetadataId)
        {
            RemoveEntryList(&Entry->ListEntry);
            Session->ImageMetadataBytes -= Entry->AllocationSize;
            Session->ImageMetadataCount--;
            break;
        }
        Entry = NULL;
    }
    KeReleaseGuardedMutex(&Session->ImageMetadataLock);
    if (Entry)
        ExFreePoolWithTag(Entry, KPROF_TRACE_IMAGE_TAG);
}

static
VOID
KiKprofTraceFreeImageMetadata(
    IN PKPROF_TRACE_SESSION Session)
{
    PLIST_ENTRY NextEntry;
    PKPROF_TRACE_IMAGE_ENTRY Entry;

    KeAcquireGuardedMutex(&Session->ImageMetadataLock);
    while (!IsListEmpty(&Session->ImageMetadataList))
    {
        NextEntry = RemoveHeadList(&Session->ImageMetadataList);
        Entry = CONTAINING_RECORD(NextEntry, KPROF_TRACE_IMAGE_ENTRY, ListEntry);
        ExFreePoolWithTag(Entry, KPROF_TRACE_IMAGE_TAG);
    }
    Session->ImageMetadataBytes = 0;
    Session->ImageMetadataCount = 0;
    KeReleaseGuardedMutex(&Session->ImageMetadataLock);
}

static
VOID
KiKprofTraceInitializeRecord(
    OUT PKPROF_TRACE_RECORD Record,
    IN KPROF_TRACE_RECORD_TYPE Type,
    IN ULONG Flags,
    IN ULONG Processor,
    IN OPTIONAL PEPROCESS Process,
    IN OPTIONAL PETHREAD Thread)
{
    RtlZeroMemory(Record, sizeof(*Record));
    Record->Header.Type = (USHORT)Type;
    Record->Header.Size = sizeof(*Record);
    Record->Header.Flags = Flags;
    Record->Header.Processor = Processor;
    Record->Header.Timestamp = KeQueryInterruptTime();
    Record->Header.ProcessId = KiKprofTraceProcessId(Process);
    Record->Header.ThreadId = KiKprofTraceThreadId(Thread);
}

VOID
NTAPI
KprofTraceQueryCapabilities(
    OUT PKPROF_TRACE_CAPABILITIES Capabilities)
{
    ULONG CapabilitiesMask;

    RtlZeroMemory(Capabilities, sizeof(*Capabilities));
    CapabilitiesMask = KPROF_TRACE_CAP_PROCESS |
                       KPROF_TRACE_CAP_THREAD |
                       KPROF_TRACE_CAP_IMAGE |
                       KPROF_TRACE_CAP_SCHEDULER |
                       KPROF_TRACE_CAP_PER_CPU_RING |
                       KPROF_TRACE_CAP_LOSS_RECORDS;
    if (KiKprofTraceTimerSupported()) CapabilitiesMask |= KPROF_TRACE_CAP_TIMER;
#if defined(_M_AMD64)
    CapabilitiesMask |= KPROF_TRACE_CAP_KERNEL_CHAIN;
#endif

    Capabilities->Version = KPROF_TRACE_CAPABILITIES_VERSION;
    Capabilities->Size = sizeof(*Capabilities);
    Capabilities->Capabilities = CapabilitiesMask;
    Capabilities->UnsupportedFlags = KPROF_TRACE_FLAG_PMU;
#if !defined(_M_AMD64)
    Capabilities->UnsupportedFlags |= KPROF_TRACE_FLAG_CALLCHAIN;
#endif
    if (!KiKprofTraceTimerSupported())
        Capabilities->UnsupportedFlags |= KPROF_TRACE_FLAG_SAMPLE;
    Capabilities->MaximumSessions = KPROF_TRACE_MAX_SESSIONS;
    Capabilities->MaximumProcessors = KeNumberProcessors;
    Capabilities->MinimumRingSize = KPROF_TRACE_MIN_RING_SIZE;
    Capabilities->MaximumRingSize = KPROF_TRACE_MAX_RING_SIZE;
    Capabilities->TimerSource = ProfileTime;
    Capabilities->TimerInterval = KeQueryIntervalProfile(ProfileTime);
}

NTSTATUS
NTAPI
KprofTraceCreateSession(
    IN const KPROF_TRACE_CONFIG *Config,
    IN KPROCESSOR_MODE RequestorMode,
    OUT PKPROF_TRACE_SESSION *SessionOut)
{
    PKPROF_TRACE_SESSION Session;
    KAFFINITY ActiveAffinity;
    ULONG Processor, AllocatedProcessors = 0;
    ULONG RequestedProcessors = 0;
    ULONGLONG RequestedRingBytes;
    LONG Count;
    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();

    if (!Config || !SessionOut) return STATUS_INVALID_PARAMETER;
    *SessionOut = NULL;
    if ((Config->Version != KPROF_TRACE_CONFIG_VERSION) ||
        (Config->Size < sizeof(*Config)))
    {
        return STATUS_REVISION_MISMATCH;
    }

    if (!Config->Flags || (Config->Flags & ~KPROF_TRACE_SUPPORTED_FLAGS))
        return STATUS_NOT_SUPPORTED;
#if !defined(_M_AMD64)
    if (Config->Flags & KPROF_TRACE_FLAG_CALLCHAIN)
        return STATUS_NOT_SUPPORTED;
#endif
    if ((Config->Scope != KprofTraceScopeSystem) &&
        (Config->Scope != KprofTraceScopeProcess))
        return STATUS_INVALID_PARAMETER;
    if ((Config->Scope == KprofTraceScopeProcess) && !Config->TargetProcess)
        return STATUS_INVALID_PARAMETER;
    if ((Config->Scope == KprofTraceScopeSystem) && Config->TargetProcess)
        return STATUS_INVALID_PARAMETER;
    if ((Config->Scope == KprofTraceScopeSystem) &&
        (RequestorMode != KernelMode) &&
        !SeSinglePrivilegeCheck(SeSystemProfilePrivilege, RequestorMode))
    {
        return STATUS_PRIVILEGE_NOT_HELD;
    }

    if ((Config->RingSize < KPROF_TRACE_MIN_RING_SIZE) ||
        (Config->RingSize > KPROF_TRACE_MAX_RING_SIZE) ||
        (Config->RingSize & (Config->RingSize - 1)))
    {
        return STATUS_INVALID_BUFFER_SIZE;
    }

    ActiveAffinity = Config->Affinity & KeActiveProcessors;
    if (!ActiveAffinity) return STATUS_INVALID_PARAMETER;
    for (Processor = 0; Processor < MAXIMUM_PROCESSORS; Processor++)
    {
        if (ActiveAffinity & AFFINITY_MASK(Processor)) RequestedProcessors++;
    }
    RequestedRingBytes = (ULONGLONG)Config->RingSize * RequestedProcessors;
    if (RequestedRingBytes > KPROF_TRACE_MAX_SESSION_RING_BYTES)
        return STATUS_QUOTA_EXCEEDED;
    if (Config->Flags & KPROF_TRACE_FLAG_SAMPLE)
    {
        if (!KiKprofTraceTimerSupported() ||
            ((KPROFILE_SOURCE)Config->Source != ProfileTime))
        {
            return STATUS_NOT_SUPPORTED;
        }
    }
    else if (Config->Source != 0)
    {
        return STATUS_INVALID_PARAMETER;
    }

    for (;;)
    {
        Count = KiKprofTraceSessionCount;
        if (Count >= KPROF_TRACE_MAX_SESSIONS) return STATUS_TOO_MANY_SESSIONS;
        if (InterlockedCompareExchange(&KiKprofTraceSessionCount, Count + 1, Count) == Count)
        {
            break;
        }
    }

    if (!KiKprofTraceReserveRingBytes(RequestedRingBytes))
    {
        InterlockedDecrement(&KiKprofTraceSessionCount);
        return STATUS_QUOTA_EXCEEDED;
    }

    Session = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Session), KPROF_TRACE_SESSION_TAG);
    if (!Session)
    {
        InterlockedExchangeAdd64(&KiKprofTraceAllocatedRingBytes, -(LONG64)RequestedRingBytes);
        InterlockedDecrement(&KiKprofTraceSessionCount);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(Session, sizeof(*Session));
    Session->Config = *Config;
    Session->Config.Affinity = ActiveAffinity;
    Session->Config.TargetProcess = Config->TargetProcess;
    Session->OwnerProcess = PsGetCurrentProcess();
    Session->TargetProcess = Config->TargetProcess;
    Session->Source = (KPROFILE_SOURCE)Config->Source;
    Session->AllocatedRingBytes = RequestedRingBytes;
    Session->State = KPROF_TRACE_STATE_CREATED;
    ExInitializeRundownProtection(&Session->ProducerRundown);
    KeInitializeGuardedMutex(&Session->ControlLock);
    KeInitializeGuardedMutex(&Session->ImageMetadataLock);
    InitializeListHead(&Session->ImageMetadataList);
    KeInitializeEvent(&Session->DataEvent, SynchronizationEvent, FALSE);
    KeInitializeDpc(&Session->NotifyDpc, KiKprofTraceNotifyDpc, Session);
    KeInitializeDpc(&Session->ReleaseDpc, KiKprofTraceReleaseDpc, Session);
    InitializeListHead(&Session->ListEntry);
    ObReferenceObject(Session->OwnerProcess);
    if (Session->TargetProcess) ObReferenceObject(Session->TargetProcess);

    for (Processor = 0; Processor < MAXIMUM_PROCESSORS; Processor++)
    {
        PKPROF_TRACE_RING Ring;

        if (!(ActiveAffinity & AFFINITY_MASK(Processor))) continue;
        Ring = &Session->Rings[Processor];
        Ring->Buffer = ExAllocatePoolWithTag(NonPagedPool, Config->RingSize, KPROF_TRACE_RING_TAG);
        if (!Ring->Buffer)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            break;
        }

        RtlZeroMemory(Ring->Buffer, Config->RingSize);
        Ring->Size = Config->RingSize;
        KeInitializeSpinLock(&Ring->Lock);
        AllocatedProcessors++;
    }

    if (!NT_SUCCESS(Status))
    {
        for (Processor = 0; Processor < MAXIMUM_PROCESSORS; Processor++)
        {
            if (Session->Rings[Processor].Buffer)
            {
                RtlSecureZeroMemory(Session->Rings[Processor].Buffer, Session->Rings[Processor].Size);
                ExFreePoolWithTag(Session->Rings[Processor].Buffer, KPROF_TRACE_RING_TAG);
            }
        }
        if (Session->TargetProcess) ObDereferenceObject(Session->TargetProcess);
        ObDereferenceObject(Session->OwnerProcess);
        RtlSecureZeroMemory(Session, sizeof(*Session));
        ExFreePoolWithTag(Session, KPROF_TRACE_SESSION_TAG);
        InterlockedExchangeAdd64(&KiKprofTraceAllocatedRingBytes, -(LONG64)RequestedRingBytes);
        InterlockedDecrement(&KiKprofTraceSessionCount);
        return Status;
    }

    ASSERT(AllocatedProcessors != 0);
    *SessionOut = Session;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
KprofTraceStartSession(
    IN OUT PKPROF_TRACE_SESSION Session)
{
    PKPROFILE_SOURCE_OBJECT NewSource = NULL;
    BOOLEAN ConsumedSource = FALSE, StartSource = FALSE;
    KIRQL OldIrql;
    NTSTATUS Status = STATUS_SUCCESS;

    PAGED_CODE();
    if (!KiKprofTraceIsOwner(Session)) return STATUS_ACCESS_DENIED;

#if defined(_M_AMD64)
    /* Publish already-loaded system images to the interrupt-safe unwind
     * table before the first chain can be captured. */
    if (Session->Config.Flags & KPROF_TRACE_FLAG_CALLCHAIN)
        KiKprofUnwindSeedSystemImages();
#endif

    if (Session->Config.Flags & KPROF_TRACE_FLAG_SAMPLE)
    {
        NewSource = ExAllocatePoolWithTag(NonPagedPool, sizeof(*NewSource), 'forP');
        if (!NewSource) return STATUS_INSUFFICIENT_RESOURCES;
        RtlZeroMemory(NewSource, sizeof(*NewSource));
    }

    KeAcquireGuardedMutex(&Session->ControlLock);
    if (Session->State == KPROF_TRACE_STATE_RUNNING)
    {
        goto Exit;
    }
    if (Session->State != KPROF_TRACE_STATE_CREATED)
    {
        Status = STATUS_INVALID_DEVICE_STATE;
        goto Exit;
    }

    if ((Session->Config.Flags & KPROF_TRACE_FLAG_SAMPLE) &&
        Session->Config.Interval)
    {
        KeSetIntervalProfile(Session->Config.Interval, Session->Source);
        Session->Config.Interval = KeQueryIntervalProfile(Session->Source);
        if (!Session->Config.Interval)
        {
            Status = STATUS_NOT_SUPPORTED;
            goto Exit;
        }
    }

    /* Serialize the physical source transition with other transitions */
    KeAcquireGuardedMutex(&KiProfileSourceMutex);
    KeRaiseIrql(KiProfileIrql, &OldIrql);
    KeAcquireSpinLockAtDpcLevel(&KiProfileLock);
    if (Session->Config.Flags & KPROF_TRACE_FLAG_SAMPLE)
    {
        StartSource = KiAcquireProfileSourceLocked(Session->Source, NewSource, &ConsumedSource);
        Session->SourceAcquired = TRUE;
    }
    InsertTailList(&KiKprofTraceSessionList, &Session->ListEntry);
    Session->State = KPROF_TRACE_STATE_RUNNING;
    Session->StartTimestamp = KeQueryInterruptTime();
    if (Session->Config.Flags & KPROF_TRACE_FLAG_CONTEXT_SWITCH)
        InterlockedIncrement(&KiKprofTraceActiveContextSwitchSessions);
    if (Session->Config.Flags & KPROF_TRACE_FLAG_SCHED_WAKEUP)
        InterlockedIncrement(&KiKprofTraceActiveWakeupSessions);
    KeReleaseSpinLockFromDpcLevel(&KiProfileLock);
    KeLowerIrql(OldIrql);

    /* Arm the timer outside the profile lock: the HAL rendezvous IPIs every
       processor and must not wait behind PROFILE_LEVEL spinners */
    if (StartSource) HalStartProfileInterrupt(Session->Source);
    KeReleaseGuardedMutex(&KiProfileSourceMutex);

Exit:
    KeReleaseGuardedMutex(&Session->ControlLock);
    if (NewSource && !ConsumedSource) ExFreePoolWithTag(NewSource, 'forP');
    return Status;
}

NTSTATUS
NTAPI
KprofTraceStopSession(
    IN OUT PKPROF_TRACE_SESSION Session)
{
    PKPROFILE_SOURCE_OBJECT SourceObject = NULL;
    KIRQL OldIrql;

    PAGED_CODE();
    if (!KiKprofTraceIsOwner(Session)) return STATUS_ACCESS_DENIED;

    KeAcquireGuardedMutex(&Session->ControlLock);
    if ((Session->State == KPROF_TRACE_STATE_STOPPED) ||
        (Session->State == KPROF_TRACE_STATE_STOPPING))
    {
        KeReleaseGuardedMutex(&Session->ControlLock);
        return STATUS_SUCCESS;
    }
    if (Session->State == KPROF_TRACE_STATE_CREATED)
    {
        Session->State = KPROF_TRACE_STATE_STOPPED;
        ExWaitForRundownProtectionRelease(&Session->ProducerRundown);
        Session->StopTimestamp = KeQueryInterruptTime();
        KeSetEvent(&Session->DataEvent, IO_NO_INCREMENT, FALSE);
        KeReleaseGuardedMutex(&Session->ControlLock);
        return STATUS_SUCCESS;
    }
    if (Session->State != KPROF_TRACE_STATE_RUNNING)
    {
        KeReleaseGuardedMutex(&Session->ControlLock);
        return STATUS_INVALID_DEVICE_STATE;
    }

    /* Serialize the physical source transition with other transitions */
    KeAcquireGuardedMutex(&KiProfileSourceMutex);
    KeRaiseIrql(KiProfileIrql, &OldIrql);
    KeAcquireSpinLockAtDpcLevel(&KiProfileLock);
    Session->State = KPROF_TRACE_STATE_STOPPING;
    RemoveEntryList(&Session->ListEntry);
    InitializeListHead(&Session->ListEntry);
    if (Session->Config.Flags & KPROF_TRACE_FLAG_CONTEXT_SWITCH)
        InterlockedDecrement(&KiKprofTraceActiveContextSwitchSessions);
    if (Session->Config.Flags & KPROF_TRACE_FLAG_SCHED_WAKEUP)
        InterlockedDecrement(&KiKprofTraceActiveWakeupSessions);
    if (Session->SourceAcquired)
    {
        SourceObject = KiReleaseProfileSourceLocked(Session->Source);
        Session->SourceAcquired = FALSE;
    }
    KeReleaseSpinLockFromDpcLevel(&KiProfileLock);
    KeLowerIrql(OldIrql);

    /* Disarm the timer outside the profile lock: the HAL rendezvous IPIs
       every processor and must not wait behind PROFILE_LEVEL spinners */
    if (SourceObject) HalStopProfileInterrupt(Session->Source);
    KeReleaseGuardedMutex(&KiProfileSourceMutex);

    /* This is the no-writes-after-stop barrier for all producers. */
    ExWaitForRundownProtectionRelease(&Session->ProducerRundown);
    Session->StopTimestamp = KeQueryInterruptTime();
    Session->State = KPROF_TRACE_STATE_STOPPED;
    KeSetEvent(&Session->DataEvent, IO_NO_INCREMENT, FALSE);
    KeReleaseGuardedMutex(&Session->ControlLock);
    if (SourceObject) ExFreePoolWithTag(SourceObject, 'forP');
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
KprofTraceDrain(
    IN OUT PKPROF_TRACE_SESSION Session,
    IN ULONG Processor,
    OUT PVOID Buffer,
    IN ULONG BufferSize,
    OUT PULONG BytesWritten,
    OUT OPTIONAL PKPROF_TRACE_CPU_STATS CpuStats)
{
    PKPROF_TRACE_RING Ring;
    ULONG Available, CopyLength, FirstLength, Offset;
    KIRQL OldIrql = PASSIVE_LEVEL;
    BOOLEAN RaisedIrql;
    BOOLEAN EmittedLoss;

    PAGED_CODE();
    if (!KiKprofTraceIsOwner(Session)) return STATUS_ACCESS_DENIED;
    if (!Buffer || !BytesWritten) return STATUS_INVALID_PARAMETER;
    *BytesWritten = 0;
    if (Processor >= MAXIMUM_PROCESSORS) return STATUS_INVALID_PARAMETER;

    KeAcquireGuardedMutex(&Session->ControlLock);
    if ((Session->State == KPROF_TRACE_STATE_DESTROYED) ||
        (Session->State == KPROF_TRACE_STATE_CREATED))
    {
        KeReleaseGuardedMutex(&Session->ControlLock);
        return STATUS_INVALID_DEVICE_STATE;
    }

    Ring = &Session->Rings[Processor];
    if (!Ring->Buffer)
    {
        KeReleaseGuardedMutex(&Session->ControlLock);
        return STATUS_NOT_FOUND;
    }

    KiKprofTraceAcquireRing(Ring, &OldIrql, &RaisedIrql);
    Available = (ULONG)(Ring->Head - Ring->Tail);

    /* Records are variable-sized; walk record boundaries so the copy never
     * splits a record. */
    CopyLength = 0;
    while (CopyLength < Available)
    {
        USHORT RecordSize = KiKprofTraceRingPeekSizeLocked(Ring, Ring->Tail + CopyLength);
        if (RecordSize < sizeof(KPROF_TRACE_RECORD)) break;
        if (RecordSize > KPROF_TRACE_MAX_RECORD_SIZE) break;
        if ((ULONG)RecordSize > Available - CopyLength) break;
        if (CopyLength + RecordSize > BufferSize) break;
        CopyLength += RecordSize;
    }
    Offset = (ULONG)(Ring->Tail & (Ring->Size - 1));
    FirstLength = min(CopyLength, Ring->Size - Offset);
    if (FirstLength) RtlCopyMemory(Buffer, Ring->Buffer + Offset, FirstLength);
    if (CopyLength > FirstLength)
    {
        RtlCopyMemory((PUCHAR)Buffer + FirstLength, Ring->Buffer, CopyLength - FirstLength);
    }
    Ring->Tail += CopyLength;
    EmittedLoss = KiKprofTraceEmitPendingLossLocked(Session, Ring, Processor);
    *BytesWritten = CopyLength;
    if (CpuStats)
    {
        CpuStats->ProducedRecords = Ring->ProducedRecords;
        CpuStats->LostRecords = Ring->LostRecords;
        CpuStats->PendingLostRecords = Ring->PendingLostRecords;
        CpuStats->BytesAvailable = Ring->Head - Ring->Tail;
        CpuStats->HighWatermarkBytes = Ring->HighWatermark;
    }
    KiKprofTraceReleaseRing(Ring, OldIrql, RaisedIrql);
    KeReleaseGuardedMutex(&Session->ControlLock);
    if (EmittedLoss) KiKprofTraceNotifyData(Session);
    return (Available && !CopyLength) ? STATUS_BUFFER_TOO_SMALL : STATUS_SUCCESS;
}

NTSTATUS
NTAPI
KprofTracePeek(
    IN PKPROF_TRACE_SESSION Session,
    IN ULONG Processor,
    OUT PKPROF_TRACE_RECORD Record,
    IN ULONG Capacity,
    OUT OPTIONAL PKPROF_TRACE_CPU_STATS CpuStats)
{
    PKPROF_TRACE_RING Ring;
    ULONG Available, FirstLength, Offset, RecordSize;
    KIRQL OldIrql = PASSIVE_LEVEL;
    BOOLEAN RaisedIrql;

    PAGED_CODE();
    if (!KiKprofTraceIsOwner(Session)) return STATUS_ACCESS_DENIED;
    if (!Record || (Capacity < sizeof(*Record)) || (Processor >= MAXIMUM_PROCESSORS))
        return STATUS_INVALID_PARAMETER;

    KeAcquireGuardedMutex(&Session->ControlLock);
    if ((Session->State == KPROF_TRACE_STATE_DESTROYED) ||
        (Session->State == KPROF_TRACE_STATE_CREATED))
    {
        KeReleaseGuardedMutex(&Session->ControlLock);
        return STATUS_INVALID_DEVICE_STATE;
    }
    Ring = &Session->Rings[Processor];
    if (!Ring->Buffer)
    {
        KeReleaseGuardedMutex(&Session->ControlLock);
        return STATUS_NOT_FOUND;
    }

    KiKprofTraceAcquireRing(Ring, &OldIrql, &RaisedIrql);
    Available = (ULONG)(Ring->Head - Ring->Tail);
    if (CpuStats)
    {
        CpuStats->ProducedRecords = Ring->ProducedRecords;
        CpuStats->LostRecords = Ring->LostRecords;
        CpuStats->PendingLostRecords = Ring->PendingLostRecords;
        CpuStats->BytesAvailable = Available;
        CpuStats->HighWatermarkBytes = Ring->HighWatermark;
    }
    if (Available < sizeof(*Record))
    {
        KiKprofTraceReleaseRing(Ring, OldIrql, RaisedIrql);
        KeReleaseGuardedMutex(&Session->ControlLock);
        return STATUS_NO_MORE_ENTRIES;
    }

    RecordSize = KiKprofTraceRingPeekSizeLocked(Ring, Ring->Tail);
    if ((RecordSize < sizeof(*Record)) || (RecordSize > KPROF_TRACE_MAX_RECORD_SIZE) || (RecordSize > Available))
    {
        KiKprofTraceReleaseRing(Ring, OldIrql, RaisedIrql);
        KeReleaseGuardedMutex(&Session->ControlLock);
        return STATUS_DATA_ERROR;
    }
    if (RecordSize > Capacity)
    {
        KiKprofTraceReleaseRing(Ring, OldIrql, RaisedIrql);
        KeReleaseGuardedMutex(&Session->ControlLock);
        return STATUS_BUFFER_TOO_SMALL;
    }

    Offset = (ULONG)(Ring->Tail & (Ring->Size - 1));
    FirstLength = min(RecordSize, Ring->Size - Offset);
    RtlCopyMemory(Record, Ring->Buffer + Offset, FirstLength);
    if (FirstLength < RecordSize)
    {
        RtlCopyMemory((PUCHAR)Record + FirstLength, Ring->Buffer, RecordSize - FirstLength);
    }
    KiKprofTraceReleaseRing(Ring, OldIrql, RaisedIrql);
    KeReleaseGuardedMutex(&Session->ControlLock);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
KprofTraceWaitForData(
    IN PKPROF_TRACE_SESSION Session,
    IN OPTIONAL PKEVENT CancelEvent)
{
    PVOID Objects[2];
    NTSTATUS Status;

    PAGED_CODE();
    if (!KiKprofTraceIsOwner(Session)) return STATUS_ACCESS_DENIED;
    Objects[0] = &Session->DataEvent;
    if (!CancelEvent)
    {
        return KeWaitForSingleObject(Objects[0], Executive, KernelMode, FALSE, NULL);
    }

    Objects[1] = CancelEvent;
    Status = KeWaitForMultipleObjects(2, Objects, WaitAny, Executive, KernelMode, FALSE, NULL, NULL);
    if (Status == STATUS_WAIT_1) return STATUS_CANCELLED;
    return Status;
}

NTSTATUS
NTAPI
KprofTraceQueryStats(
    IN PKPROF_TRACE_SESSION Session,
    OUT PKPROF_TRACE_STATS Stats)
{
    ULONG Processor;

    PAGED_CODE();
    if (!KiKprofTraceIsOwner(Session)) return STATUS_ACCESS_DENIED;
    if (!Stats) return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(Stats, sizeof(*Stats));
    Stats->Version = KPROF_TRACE_STATS_VERSION;
    Stats->Size = sizeof(*Stats);
    KeAcquireGuardedMutex(&Session->ControlLock);
    Stats->State = Session->State;
    Stats->StartTimestamp = Session->StartTimestamp;
    Stats->StopTimestamp = Session->StopTimestamp;
    Stats->Samples = InterlockedCompareExchange64(&Session->Samples, 0, 0);
    Stats->ContextSwitches =
        InterlockedCompareExchange64(&Session->ContextSwitches, 0, 0);
    Stats->Wakeups = InterlockedCompareExchange64(&Session->Wakeups, 0, 0);
    Stats->LifecycleRecords =
        InterlockedCompareExchange64(&Session->LifecycleRecords, 0, 0);
    Stats->FilteredSamples = InterlockedCompareExchange64(&Session->FilteredSamples, 0, 0);
    for (Processor = 0; Processor < MAXIMUM_PROCESSORS; Processor++)
    {
        PKPROF_TRACE_RING Ring;
        KIRQL OldIrql = PASSIVE_LEVEL;
        BOOLEAN RaisedIrql;

        Ring = &Session->Rings[Processor];
        if (!Ring->Buffer) continue;
        Stats->ProcessorCount++;
        KiKprofTraceAcquireRing(Ring, &OldIrql, &RaisedIrql);
        Stats->ProducedRecords += Ring->ProducedRecords;
        Stats->LostRecords += Ring->LostRecords;
        Stats->PendingLostRecords += Ring->PendingLostRecords;
        Stats->BytesAvailable += Ring->Head - Ring->Tail;
        Stats->HighWatermarkBytes += Ring->HighWatermark;
        KiKprofTraceReleaseRing(Ring, OldIrql, RaisedIrql);
    }
    KeReleaseGuardedMutex(&Session->ControlLock);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
KprofTraceQueryImageMetadata(
    IN PKPROF_TRACE_SESSION Session,
    IN ULONGLONG MetadataId,
    OUT PKPROF_TRACE_IMAGE_METADATA Metadata,
    OUT OPTIONAL PVOID Path,
    IN ULONG PathCapacity,
    OUT PULONG PathBytes)
{
    PLIST_ENTRY NextEntry;
    PKPROF_TRACE_IMAGE_ENTRY Entry;
    NTSTATUS Status = STATUS_NOT_FOUND;

    PAGED_CODE();
    if (!KiKprofTraceIsOwner(Session)) return STATUS_ACCESS_DENIED;
    if (!Metadata || !PathBytes || !MetadataId ||
        (!Path && PathCapacity))
    {
        return STATUS_INVALID_PARAMETER;
    }

    RtlZeroMemory(Metadata, sizeof(*Metadata));
    *PathBytes = 0;
    KeAcquireGuardedMutex(&Session->ImageMetadataLock);
    for (NextEntry = Session->ImageMetadataList.Flink;
         NextEntry != &Session->ImageMetadataList;
         NextEntry = NextEntry->Flink)
    {
        Entry = CONTAINING_RECORD(NextEntry, KPROF_TRACE_IMAGE_ENTRY, ListEntry);
        if (Entry->ImageKey != MetadataId)
            continue;

        Metadata->Flags = Entry->Flags;
        Metadata->PathBytes = Entry->PathBytes;
        Metadata->BuildIdBytes = Entry->BuildIdBytes;
        Metadata->Checksum = Entry->Checksum;
        Metadata->TimeDateStamp = Entry->TimeDateStamp;
        Metadata->Machine = Entry->Machine;
        RtlCopyMemory(Metadata->BuildId, Entry->BuildId, sizeof(Metadata->BuildId));
        *PathBytes = Entry->PathBytes;
        if (Entry->PathBytes > PathCapacity)
        {
            Status = STATUS_BUFFER_TOO_SMALL;
        }
        else
        {
            if (Entry->PathBytes)
                RtlCopyMemory(Path, Entry->Path, Entry->PathBytes);
            Status = STATUS_SUCCESS;
        }
        break;
    }
    KeReleaseGuardedMutex(&Session->ImageMetadataLock);
    return Status;
}

NTSTATUS
NTAPI
KprofTraceDestroySession(
    IN OUT PKPROF_TRACE_SESSION Session)
{
    PEPROCESS OwnerProcess, TargetProcess;
    ULONGLONG AllocatedRingBytes;
    ULONG Processor;
    NTSTATUS Status;

    PAGED_CODE();
    if (!KiKprofTraceIsOwner(Session)) return STATUS_ACCESS_DENIED;
    Status = KprofTraceStopSession(Session);
    if (!NT_SUCCESS(Status)) return Status;

    KeAcquireGuardedMutex(&Session->ControlLock);
    if (Session->State != KPROF_TRACE_STATE_STOPPED)
    {
        KeReleaseGuardedMutex(&Session->ControlLock);
        return STATUS_INVALID_DEVICE_STATE;
    }
    Session->State = KPROF_TRACE_STATE_DESTROYED;
    OwnerProcess = Session->OwnerProcess;
    TargetProcess = Session->TargetProcess;
    AllocatedRingBytes = Session->AllocatedRingBytes;
    for (Processor = 0; Processor < MAXIMUM_PROCESSORS; Processor++)
    {
        if (Session->Rings[Processor].Buffer)
        {
            RtlSecureZeroMemory(Session->Rings[Processor].Buffer, Session->Rings[Processor].Size);
            ExFreePoolWithTag(Session->Rings[Processor].Buffer, KPROF_TRACE_RING_TAG);
            Session->Rings[Processor].Buffer = NULL;
        }
    }
    KeReleaseGuardedMutex(&Session->ControlLock);

    KiKprofTraceFreeImageMetadata(Session);
    if (TargetProcess) ObDereferenceObject(TargetProcess);
    ObDereferenceObject(OwnerProcess);
    RtlSecureZeroMemory(Session, sizeof(*Session));
    ExFreePoolWithTag(Session, KPROF_TRACE_SESSION_TAG);
    InterlockedExchangeAdd64(&KiKprofTraceAllocatedRingBytes, -(LONG64)AllocatedRingBytes);
    InterlockedDecrement(&KiKprofTraceSessionCount);
    return STATUS_SUCCESS;
}

static
ULONG
KiKprofTraceControlProcessor(
    IN PKPROF_TRACE_SESSION Session)
{
    ULONG Processor, CurrentProcessor;

    CurrentProcessor = KeGetCurrentProcessorNumber();
    if ((CurrentProcessor < MAXIMUM_PROCESSORS) &&
        Session->Rings[CurrentProcessor].Buffer)
    {
        return CurrentProcessor;
    }
    for (Processor = 0; Processor < MAXIMUM_PROCESSORS; Processor++)
    {
        if (Session->Rings[Processor].Buffer) return Processor;
    }
    return MAXIMUM_PROCESSORS;
}

static
NTSTATUS
KiKprofTraceWriteControlRecord(
    IN PKPROF_TRACE_SESSION Session,
    IN OUT PKPROF_TRACE_RECORD Record)
{
    ULONG Processor;
    BOOLEAN Written;

    if (!KiKprofTraceIsOwner(Session)) return STATUS_ACCESS_DENIED;
    if (!ExAcquireRundownProtection(&Session->ProducerRundown))
        return STATUS_INVALID_DEVICE_STATE;
    Processor = KiKprofTraceControlProcessor(Session);
    if (Processor == MAXIMUM_PROCESSORS)
    {
        ExReleaseRundownProtection(&Session->ProducerRundown);
        return STATUS_INVALID_DEVICE_STATE;
    }
    Record->Header.Processor = Processor;
    Written = KiKprofTraceWriteRecord(Session, Processor, Record);
    ExReleaseRundownProtection(&Session->ProducerRundown);
    return Written ? STATUS_SUCCESS : STATUS_BUFFER_OVERFLOW;
}

NTSTATUS
NTAPI
KprofTraceEmitSessionRecord(
    IN OUT PKPROF_TRACE_SESSION Session,
    IN ULONG Event,
    IN ULONG Reason,
    IN NTSTATUS Status,
    IN ULONGLONG Sources,
    IN ULONGLONG ConfigGeneration)
{
    KPROF_TRACE_RECORD Record;
    ULONG Processor;

    PAGED_CODE();
    if (!Session) return STATUS_INVALID_PARAMETER;
    Processor = KeGetCurrentProcessorNumber();
    KiKprofTraceInitializeRecord(&Record, KprofTraceRecordSession, KPROF_TRACE_RECORD_FLAG_SYSTEM, Processor, PsGetCurrentProcess(), PsGetCurrentThread());
    Record.Data.Session.Event = Event;
    Record.Data.Session.Reason = Reason;
    Record.Data.Session.Status = Status;
    Record.Data.Session.State = Session->State;
    Record.Data.Session.Sources = Sources;
    Record.Data.Session.ConfigGeneration = ConfigGeneration;
    return KiKprofTraceWriteControlRecord(Session, &Record);
}

NTSTATUS
NTAPI
KprofTraceEmitSecurityRecord(
    IN OUT PKPROF_TRACE_SESSION Session,
    IN ULONGLONG RequestedFeatures,
    IN ULONGLONG GrantedFeatures,
    IN ULONG RedactionMode,
    IN ULONG PrincipalClass,
    IN NTSTATUS Status)
{
    KPROF_TRACE_RECORD Record;
    ULONG Processor;

    PAGED_CODE();
    if (!Session) return STATUS_INVALID_PARAMETER;
    Processor = KeGetCurrentProcessorNumber();
    KiKprofTraceInitializeRecord(&Record, KprofTraceRecordSecurity, KPROF_TRACE_RECORD_FLAG_SYSTEM, Processor, PsGetCurrentProcess(), PsGetCurrentThread());
    Record.Data.Security.RequestedFeatures = RequestedFeatures;
    Record.Data.Security.GrantedFeatures = GrantedFeatures;
    Record.Data.Security.DeniedFeatures =
        RequestedFeatures & ~GrantedFeatures;
    Record.Data.Security.RedactionMode = RedactionMode;
    Record.Data.Security.PrincipalClass = PrincipalClass;
    Record.Data.Security.Status = Status;
    return KiKprofTraceWriteControlRecord(Session, &Record);
}

NTSTATUS
NTAPI
KprofTraceEmitClockSyncRecord(
    IN OUT PKPROF_TRACE_SESSION Session)
{
    KPROF_TRACE_RECORD Record;
    LARGE_INTEGER SystemTime, Frequency, Counter;
    ULONG Processor;

    PAGED_CODE();
    if (!Session) return STATUS_INVALID_PARAMETER;
    KeQuerySystemTime(&SystemTime);
    Counter = KeQueryPerformanceCounter(&Frequency);
    Processor = KeGetCurrentProcessorNumber();
    KiKprofTraceInitializeRecord(&Record, KprofTraceRecordClockSync, KPROF_TRACE_RECORD_FLAG_SYSTEM, Processor, PsGetCurrentProcess(), PsGetCurrentThread());
    Record.Data.ClockSync.SystemTime100ns = SystemTime.QuadPart;
    Record.Data.ClockSync.PerformanceCounter = Counter.QuadPart;
    Record.Data.ClockSync.PerformanceFrequency = Frequency.QuadPart;
    Record.Data.ClockSync.InterruptTime100ns = KeQueryInterruptTime();
    return KiKprofTraceWriteControlRecord(Session, &Record);
}

VOID
NTAPI
KprofTraceSampleEvent(
    IN PKTRAP_FRAME TrapFrame,
    IN KPROFILE_SOURCE Source)
{
    PKPROF_TRACE_SESSION Sessions[KPROF_TRACE_MAX_SESSIONS];
    PETHREAD Thread;
    PEPROCESS Process;
    UCHAR RecordBuffer[KPROF_TRACE_MAX_RECORD_SIZE];
    PKPROF_TRACE_RECORD Record = (PKPROF_TRACE_RECORD)RecordBuffer;
    PULONGLONG Frames = (PULONGLONG)(RecordBuffer + sizeof(KPROF_TRACE_RECORD));
    ULONG Count, Index, Processor, RecordFlags;
    ULONG FrameCount = 0, ChainFlags = 0;
    BOOLEAN WantChain = FALSE;
    ULONG_PTR ProgramCounter;

    Thread = PsGetCurrentThread();
    Process = PsGetCurrentProcess();
    Processor = KeGetCurrentProcessorNumber();
    ProgramCounter = KeGetTrapFramePc(TrapFrame);
    Count = KiKprofTraceSnapshotSessions(KPROF_TRACE_FLAG_SAMPLE, Process, NULL, Sessions);
    if (!Count) return;

    RecordFlags = KPROF_TRACE_RECORD_FLAG_KERNEL;
    if (ProgramCounter <= (ULONG_PTR)MmHighestUserAddress) RecordFlags = KPROF_TRACE_RECORD_FLAG_USER;
    KiKprofTraceInitializeRecord(Record, KprofTraceRecordSample, RecordFlags, Processor, Process, Thread);
    Record->Data.Sample.ProgramCounter = ProgramCounter;
    Record->Data.Sample.Source = Source;
    Record->Data.Sample.Interval = KeQueryIntervalProfile(Source);

    for (Index = 0; Index < Count; Index++)
    {
        if (Sessions[Index]->Source != Source) continue;
        if (!(Sessions[Index]->Config.Affinity & AFFINITY_MASK(Processor))) continue;
        if (!(Sessions[Index]->Config.Flags & KPROF_TRACE_FLAG_CALLCHAIN)) continue;
        WantChain = TRUE;
        break;
    }

#if defined(_M_AMD64)
    if (WantChain)
    {
        FrameCount = KiKprofTraceCaptureKernelChain(TrapFrame, Frames, KPROF_TRACE_MAX_FRAMES, &ChainFlags);
    }
#else
    UNREFERENCED_PARAMETER(WantChain);
    UNREFERENCED_PARAMETER(Frames);
#endif
    Record->Data.Sample.FrameCount = FrameCount;
    Record->Data.Sample.ChainFlags = ChainFlags;

    for (Index = 0; Index < Count; Index++)
    {
        if ((Sessions[Index]->Source == Source) && (Sessions[Index]->Config.Affinity & AFFINITY_MASK(Processor)))
        {
            if ((Sessions[Index]->Config.Flags & KPROF_TRACE_FLAG_CALLCHAIN) && (FrameCount > 1))
            {
                Record->Header.Size = (USHORT)(sizeof(KPROF_TRACE_RECORD) + FrameCount * sizeof(ULONGLONG));
                Record->Header.Flags = RecordFlags;
                if (ChainFlags & (KPROF_TRACE_CHAIN_TRUNCATED | KPROF_TRACE_CHAIN_STOPPED))
                    Record->Header.Flags |= KPROF_TRACE_RECORD_FLAG_TRUNCATED;
            }
            else
            {
                Record->Header.Size = sizeof(KPROF_TRACE_RECORD);
                Record->Header.Flags = RecordFlags;
            }
            KiKprofTraceWriteRecord(Sessions[Index], Processor, Record);
        }
        else
        {
            /* The session subscribed to sampling but not to this profile
             * source: deliberately filtered, not lost. */
            InterlockedIncrement64(&Sessions[Index]->FilteredSamples);
        }
    }
    KiKprofTraceReleaseSessions(Sessions, Count);
}

VOID
NTAPI
KprofTraceProcessEvent(
    IN PEPROCESS Process,
    IN BOOLEAN Create)
{
    PKPROF_TRACE_SESSION Sessions[KPROF_TRACE_MAX_SESSIONS];
    KPROF_TRACE_RECORD Record;
    ULONG Count, Index, Processor, Flags;

    Processor = KeGetCurrentProcessorNumber();
    Count = KiKprofTraceSnapshotSessions(KPROF_TRACE_FLAG_PROCESS, Process, NULL, Sessions);
    if (!Count) return;
    Flags = Create ? KPROF_TRACE_RECORD_FLAG_CREATE :
                     KPROF_TRACE_RECORD_FLAG_DESTROY;
    KiKprofTraceInitializeRecord(&Record, KprofTraceRecordProcess, Flags, Processor, Process, NULL);
    Record.Data.Lifecycle.SubjectProcessId = KiKprofTraceProcessId(Process);
    Record.Data.Lifecycle.ParentProcessId =
        (ULONGLONG)(ULONG_PTR)Process->InheritedFromUniqueProcessId;
    Record.Data.Lifecycle.SubjectProcessKey = KiKprofTraceProcessKey(Process);
    for (Index = 0; Index < Count; Index++)
        KiKprofTraceWriteRecord(Sessions[Index], Processor, &Record);
    KiKprofTraceReleaseSessions(Sessions, Count);
}

VOID
NTAPI
KprofTraceThreadEvent(
    IN PETHREAD Thread,
    IN BOOLEAN Create)
{
    PKPROF_TRACE_SESSION Sessions[KPROF_TRACE_MAX_SESSIONS];
    PEPROCESS Process;
    KPROF_TRACE_RECORD Record;
    ULONG Count, Index, Processor, Flags;

    Process = PsGetThreadProcess(Thread);
    Processor = KeGetCurrentProcessorNumber();
    Count = KiKprofTraceSnapshotSessions(KPROF_TRACE_FLAG_THREAD, Process, NULL, Sessions);
    if (!Count) return;
    Flags = Create ? KPROF_TRACE_RECORD_FLAG_CREATE :
                     KPROF_TRACE_RECORD_FLAG_DESTROY;
    KiKprofTraceInitializeRecord(&Record, KprofTraceRecordThread, Flags, Processor, Process, Thread);
    Record.Data.Lifecycle.SubjectProcessId = KiKprofTraceProcessId(Process);
    Record.Data.Lifecycle.SubjectThreadId = KiKprofTraceThreadId(Thread);
    Record.Data.Lifecycle.SubjectProcessKey = KiKprofTraceProcessKey(Process);
    Record.Data.Lifecycle.SubjectThreadKey = KiKprofTraceThreadKey(Thread);
    for (Index = 0; Index < Count; Index++)
        KiKprofTraceWriteRecord(Sessions[Index], Processor, &Record);
    KiKprofTraceReleaseSessions(Sessions, Count);
}

VOID
NTAPI
KprofTraceImageEvent(
    IN OPTIONAL PEPROCESS Process,
    IN PVOID ImageBase,
    IN SIZE_T ImageSize,
    IN BOOLEAN Load,
    IN BOOLEAN SystemImage,
    IN OPTIONAL PCUNICODE_STRING ImageName)
{
    PKPROF_TRACE_SESSION Sessions[KPROF_TRACE_MAX_SESSIONS];
    KPROF_TRACE_RECORD Record;
    KPROF_TRACE_IMAGE_SOURCE Source;
    UNICODE_STRING QueriedName;
    PCUNICODE_STRING EffectiveName = ImageName;
    KAPC_STATE ApcState;
    PUCHAR Path = NULL;
    ULONGLONG ImageKey, MetadataId;
    ULONG Count, Index, Processor, Flags;
    BOOLEAN Attached = FALSE, NameAllocated = FALSE;
    BOOLEAN MetadataAdded, Truncated, Found, WroteRecord;
    NTSTATUS Status;

    PAGED_CODE();
#if defined(_M_AMD64)
    /* Keep the interrupt-safe unwind table current even when no session
     * subscribes to image records: a chain-capturing session may still be
     * sampling through this image. */
    if (SystemImage)
    {
        if (Load)
            KiKprofUnwindInsertImage(ImageBase, ImageSize);
        else
            KiKprofUnwindRemoveImage(ImageBase);
    }
#endif
    Processor = KeGetCurrentProcessorNumber();
    Count = KiKprofTraceSnapshotSessions(KPROF_TRACE_FLAG_IMAGE, Process, NULL, Sessions);
    if (!Count) return;

    RtlZeroMemory(&Source, sizeof(Source));
    RtlZeroMemory(&QueriedName, sizeof(QueriedName));
    if (SystemImage)
        Source.Flags |= KPROF_TRACE_IMAGE_META_SYSTEM;
    if (Load)
    {
        if (Process && (Process != PsGetCurrentProcess()))
        {
            KeStackAttachProcess(&Process->Pcb, &ApcState);
            Attached = TRUE;
        }
        if (!EffectiveName && Process)
        {
            Status = MmGetFileNameForAddress(ImageBase, &QueriedName);
            if (NT_SUCCESS(Status))
            {
                EffectiveName = &QueriedName;
                NameAllocated = TRUE;
            }
        }
        if (!KiKprofTraceCaptureImageIdentity(ImageBase, ImageSize, &Source))
        {
            Source.Flags |= KPROF_TRACE_IMAGE_META_TRUNCATED;
        }
        if (Attached)
        {
            KeUnstackDetachProcess(&ApcState);
            Attached = FALSE;
        }
        Status = KiKprofTraceImagePathUtf8(EffectiveName, &Path, &Source.PathBytes);
        if (NT_SUCCESS(Status))
        {
            Source.Path = Path;
        }
        else
        {
            Source.Flags |= KPROF_TRACE_IMAGE_META_TRUNCATED;
        }
    }

    Flags = Load ? KPROF_TRACE_RECORD_FLAG_CREATE :
                   KPROF_TRACE_RECORD_FLAG_DESTROY;
    if (SystemImage) Flags |= KPROF_TRACE_RECORD_FLAG_SYSTEM;
    for (Index = 0; Index < Count; Index++)
    {
        MetadataId = 0;
        MetadataAdded = FALSE;
        Truncated = FALSE;
        Found = FALSE;
        if (Load)
        {
            MetadataAdded = KiKprofTraceAddImageMetadata(Sessions[Index], Process, ImageBase, &Source, &ImageKey, &MetadataId, &Truncated);
            if (!MetadataAdded)
                Truncated = TRUE;
        }
        else
        {
            ImageKey = KiKprofTraceUnloadImageKey(Sessions[Index], Process, ImageBase, &Found);
            if (!Found)
                Truncated = TRUE;
        }

        KiKprofTraceInitializeRecord(&Record, KprofTraceRecordImage, Flags | ((Truncated || (Source.Flags & KPROF_TRACE_IMAGE_META_TRUNCATED)) ? KPROF_TRACE_RECORD_FLAG_TRUNCATED : 0), Processor, Process, NULL);
        Record.Data.Image.ImageBase = (ULONGLONG)(ULONG_PTR)ImageBase;
        Record.Data.Image.ImageSize = ImageSize;
        Record.Data.Image.ImageKey = ImageKey;
        Record.Data.Image.MetadataId = MetadataId;
        WroteRecord = KiKprofTraceWriteRecord(Sessions[Index], Processor, &Record);
        if (Load && MetadataAdded && !WroteRecord)
            KiKprofTraceDiscardImageMetadata(Sessions[Index], MetadataId);
    }
    KiKprofTraceReleaseSessions(Sessions, Count);
    if (Path)
        ExFreePoolWithTag(Path, KPROF_TRACE_IMAGE_TAG);
    if (NameAllocated)
        RtlFreeUnicodeString(&QueriedName);
}

static DECLSPEC_NOINLINE VOID
NTAPI
KiKprofTraceSchedulerSwitchActive(
    IN OPTIONAL PKTHREAD OldThread,
    IN OPTIONAL PKTHREAD NewThread,
    IN ULONG Reason)
{
    PKPROF_TRACE_SESSION Sessions[KPROF_TRACE_MAX_SESSIONS];
    PEPROCESS OldProcess, NewProcess;
    PETHREAD OldExecutiveThread, NewExecutiveThread;
    KPROF_TRACE_RECORD Record;
    ULONG Count, Index, Processor;

    OldProcess = KiKprofTraceThreadProcess(OldThread);
    NewProcess = KiKprofTraceThreadProcess(NewThread);
    OldExecutiveThread = KiKprofTraceExecutiveThread(OldThread);
    NewExecutiveThread = KiKprofTraceExecutiveThread(NewThread);
    Processor = KeGetCurrentProcessorNumber();
    Count = KiKprofTraceSnapshotSessions(KPROF_TRACE_FLAG_CONTEXT_SWITCH, OldProcess, NewProcess, Sessions);
    if (!Count) return;
    KiKprofTraceInitializeRecord(&Record, KprofTraceRecordScheduler, 0, Processor, NewProcess, NewExecutiveThread);
    Record.Data.Scheduler.OldProcessId = KiKprofTraceProcessId(OldProcess);
    Record.Data.Scheduler.OldThreadId = KiKprofTraceThreadId(OldExecutiveThread);
    Record.Data.Scheduler.NewProcessId = KiKprofTraceProcessId(NewProcess);
    Record.Data.Scheduler.NewThreadId = KiKprofTraceThreadId(NewExecutiveThread);
    Record.Data.Scheduler.Reason = Reason;
    Record.Data.Scheduler.OldState = OldThread ? OldThread->State : Initialized;
    Record.Data.Scheduler.OldPriority = OldThread ? OldThread->Priority : 0;
    Record.Data.Scheduler.NewPriority = NewThread ? NewThread->Priority : 0;
    Record.Data.Scheduler.SourceProcessor = (USHORT)Processor;
    Record.Data.Scheduler.TargetProcessor = (USHORT)Processor;
    if (OldThread && (OldThread->State == Waiting))
        Record.Data.Scheduler.SchedulerFlags = KPROF_TRACE_SCHED_FLAG_VOLUNTARY;
    else if (OldThread && ((OldThread->State == Ready) ||
                           (OldThread->State == Standby) ||
                           (OldThread->State == DeferredReady)))
        Record.Data.Scheduler.SchedulerFlags = KPROF_TRACE_SCHED_FLAG_PREEMPTED;
    for (Index = 0; Index < Count; Index++)
        KiKprofTraceWriteRecord(Sessions[Index], Processor, &Record);
    KiKprofTraceReleaseSessions(Sessions, Count);
}

VOID
NTAPI
KprofTraceSchedulerSwitch(
    IN OPTIONAL PKTHREAD OldThread,
    IN OPTIONAL PKTHREAD NewThread,
    IN ULONG Reason)
{
    if (!ReadNoFence(&KiKprofTraceActiveContextSwitchSessions)) return;
    KiKprofTraceSchedulerSwitchActive(OldThread, NewThread, Reason);
}

static DECLSPEC_NOINLINE VOID
NTAPI
KiKprofTraceSchedulerReadyActive(
    IN PKTHREAD Thread,
    IN ULONG TargetProcessor)
{
    PKPROF_TRACE_SESSION Sessions[KPROF_TRACE_MAX_SESSIONS];
    PEPROCESS Process;
    PETHREAD ExecutiveThread;
    PEPROCESS WakerProcess;
    PETHREAD WakerThread;
    KPROF_TRACE_RECORD Record;
    ULONG Count, Index, Processor;

    Process = KiKprofTraceThreadProcess(Thread);
    ExecutiveThread = KiKprofTraceExecutiveThread(Thread);
    WakerProcess = PsGetCurrentProcess();
    WakerThread = PsGetCurrentThread();
    Processor = KeGetCurrentProcessorNumber();
    Count = KiKprofTraceSnapshotSessions(KPROF_TRACE_FLAG_SCHED_WAKEUP, Process, NULL, Sessions);
    if (!Count) return;
    KiKprofTraceInitializeRecord(&Record, KprofTraceRecordScheduler, KPROF_TRACE_RECORD_FLAG_READY, Processor, WakerProcess, WakerThread);
    Record.Data.Scheduler.NewProcessId = KiKprofTraceProcessId(Process);
    Record.Data.Scheduler.NewThreadId = KiKprofTraceThreadId(ExecutiveThread);
    Record.Data.Scheduler.NewPriority = Thread->Priority;
    Record.Data.Scheduler.SourceProcessor = (USHORT)Processor;
    Record.Data.Scheduler.TargetProcessor = (USHORT)TargetProcessor;
    for (Index = 0; Index < Count; Index++)
        KiKprofTraceWriteRecord(Sessions[Index], Processor, &Record);
    KiKprofTraceReleaseSessions(Sessions, Count);
}

VOID
NTAPI
KprofTraceSchedulerReady(
    IN PKTHREAD Thread,
    IN ULONG TargetProcessor)
{
    if (!ReadNoFence(&KiKprofTraceActiveWakeupSessions)) return;
    KiKprofTraceSchedulerReadyActive(Thread, TargetProcessor);
}
