/*
 * COPYRIGHT:       GPL - See COPYING in the top level directory
 * PROJECT:         ReactOS Kernel
 * FILE:            ntoskrnl/ke/profobj.c
 * PURPOSE:         Kernel Profiling
 * PROGRAMMERS:     Alex Ionescu (alex@relsoft.net)
 */

/* INCLUDES *****************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* GLOBALS *******************************************************************/

KIRQL KiProfileIrql = PROFILE_LEVEL;
LIST_ENTRY KiProfileListHead;
LIST_ENTRY KiProfileSourceListHead;
KSPIN_LOCK KiProfileLock;
/* Serializes physical profile-source transitions at passive level: the HAL
   start/stop broadcasts an IPI rendezvous, which must never run under
   KiProfileLock at an interrupt level that can block the rendezvous */
KGUARDED_MUTEX KiProfileSourceMutex;
ULONG KiProfileTimeInterval = 78125; /* Default resolution 7.8ms (sysinternals) */
ULONG KiProfileAlignmentFixupInterval;

/* FUNCTIONS *****************************************************************/

static
BOOLEAN
KiAcquireProfileSourceLocked(
    IN KPROFILE_SOURCE Source,
    IN PKPROFILE_SOURCE_OBJECT NewSource)
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
            ASSERT(CurrentSource->References != MAXULONG);
            CurrentSource->References++;
            return FALSE;
        }
    }

    NewSource->Source = Source;
    NewSource->References = 1;
    InsertHeadList(&KiProfileSourceListHead, &NewSource->ListEntry);
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
    BOOLEAN StartSource = FALSE;
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
        StartSource = KiAcquireProfileSourceLocked(Profile->Source, SourceBuffer);
        if (StartSource) FreeBuffer = FALSE;

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
    BOOLEAN StoppedProfile = FALSE;

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
    if (CurrentSource) ExFreePoolWithTag(CurrentSource, 'forP');

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
        /* Keep the published interval and per-processor HAL broadcast in the
           same order as profile source start/stop transitions. */
        ASSERT(KeGetCurrentIrql() <= APC_LEVEL);
        KeAcquireGuardedMutex(&KiProfileSourceMutex);
        KiProfileTimeInterval = (ULONG)HalSetProfileInterval(Interval);
        KeReleaseGuardedMutex(&KiProfileSourceMutex);
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

/*
 * @implemented
 */
NTSTATUS
NTAPI
KeQueryHardwareCounterConfiguration(
    _Out_writes_to_(MaximumCount, *Count) PHARDWARE_COUNTER CounterArray,
    _In_ ULONG MaximumCount,
    _Out_ PULONG Count)
{
    UNREFERENCED_PARAMETER(CounterArray);
    UNREFERENCED_PARAMETER(MaximumCount);

    /* No hardware counters are configured for thread profiling;
     * STATUS_NOT_SUPPORTED is the documented return for this case */
    *Count = 0;
    return STATUS_NOT_SUPPORTED;
}

/*
 * @implemented
 */
NTSTATUS
NTAPI
KeSetHardwareCounterConfiguration(
    _In_reads_(Count) PHARDWARE_COUNTER CounterArray,
    _In_ ULONG Count)
{
    UNREFERENCED_PARAMETER(CounterArray);
    UNREFERENCED_PARAMETER(Count);

    /* Hardware counter multiplexing for thread profiling is not supported */
    return STATUS_NOT_SUPPORTED;
}
