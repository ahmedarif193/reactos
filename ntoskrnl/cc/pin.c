/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS kernel
 * FILE:            ntoskrnl/cc/pin.c
 * PURPOSE:         Implements cache managers pinning interface
 *
 * PROGRAMMERS:     ?
                    Pierre Schweitzer (pierre@reactos.org)
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* GLOBALS *******************************************************************/

extern NPAGED_LOOKASIDE_LIST iBcbLookasideList;

/* Counters:
 * - Number of calls to CcMapData that could wait
 * - Number of calls to CcMapData that couldn't wait
 * - Number of calls to CcPinRead that could wait
 * - Number of calls to CcPinRead that couldn't wait
 * - Number of calls to CcPinMappedDataCount
 */
ULONG CcMapDataWait = 0;
ULONG CcMapDataNoWait = 0;
ULONG CcPinReadWait = 0;
ULONG CcPinReadNoWait = 0;
ULONG CcPinMappedDataCount = 0;

/* FUNCTIONS *****************************************************************/

static
PINTERNAL_BCB
NTAPI
CcpFindBcb(
    IN PROS_SHARED_CACHE_MAP SharedCacheMap,
    IN PLARGE_INTEGER FileOffset,
    IN ULONG Length,
    IN BOOLEAN Pinned,
    IN BOOLEAN AllowMapped)
{
    PINTERNAL_BCB Bcb;
    BOOLEAN Found = FALSE;
    PLIST_ENTRY NextEntry;

    for (NextEntry = SharedCacheMap->BcbList.Flink;
         NextEntry != &SharedCacheMap->BcbList;
         NextEntry = NextEntry->Flink)
    {
        Bcb = CONTAINING_RECORD(NextEntry, INTERNAL_BCB, BcbEntry);

        if (!AllowMapped && Bcb->Vacb == NULL)
            continue;

        if (Bcb->PFCB.MappedFileOffset.QuadPart <= FileOffset->QuadPart &&
            (Bcb->PFCB.MappedFileOffset.QuadPart + Bcb->PFCB.MappedLength) >=
            (FileOffset->QuadPart + Length))
        {
            if ((Pinned && Bcb->PinCount > 0) || (!Pinned && Bcb->PinCount == 0))
            {
                Found = TRUE;
                break;
            }
        }
    }

    return (Found ? Bcb : NULL);
}

static
VOID
CcpDereferenceBcb(
    IN PROS_SHARED_CACHE_MAP SharedCacheMap,
    IN PINTERNAL_BCB Bcb)
{
    ULONG RefCount;
    KIRQL OldIrql;

    KeAcquireSpinLock(&SharedCacheMap->BcbSpinLock, &OldIrql);
    RefCount = --Bcb->RefCount;
    if (RefCount == 0)
    {
        if (Bcb->BcbEntry.Flink != NULL)
        {
            RemoveEntryList(&Bcb->BcbEntry);
        }
        KeReleaseSpinLock(&SharedCacheMap->BcbSpinLock, OldIrql);

        ASSERT(Bcb->PinCount == 0);
        if (Bcb->MappedBase != NULL)
        {
            MmUnmapViewInSystemSpace(Bcb->MappedBase);
        }
        else
        {
            /*
             * Don't mark dirty, if it was dirty,
             * the VACB was already marked as such
             * following the call to CcSetDirtyPinnedData
             */
            CcRosReleaseVacb(SharedCacheMap,
                             Bcb->Vacb,
                             FALSE,
                             FALSE);
        }

        ExDeleteResourceLite(&Bcb->Lock);
        ExFreeToNPagedLookasideList(&iBcbLookasideList, Bcb);
    }
    else
    {
        KeReleaseSpinLock(&SharedCacheMap->BcbSpinLock, OldIrql);
    }
}

static
BOOLEAN
CcpAcquireBcbForPin(
    IN PINTERNAL_BCB Bcb,
    IN ULONG PinFlags)
{
    BOOLEAN Result;

    if (BooleanFlagOn(PinFlags, PIN_EXCLUSIVE))
    {
        Result = ExAcquireResourceExclusiveLite(&Bcb->Lock, BooleanFlagOn(PinFlags, PIN_WAIT));
        if (!Result)
            return FALSE;

        Bcb->ExclusivePinCount++;
        Bcb->ExclusiveOwner = ExGetCurrentResourceThread();
    }
    else
    {
        Result = ExAcquireSharedStarveExclusive(&Bcb->Lock, BooleanFlagOn(PinFlags, PIN_WAIT));
        if (!Result)
            return FALSE;
    }

    Bcb->PinCount++;
    return TRUE;
}

static
VOID
CcpReleaseBcbPin(
    IN PINTERNAL_BCB Bcb,
    IN ERESOURCE_THREAD ResourceThreadId)
{
    ASSERT(Bcb->PinCount != 0);

    if (Bcb->ExclusivePinCount != 0 &&
        Bcb->ExclusiveOwner == ResourceThreadId)
    {
        ExReleaseResourceForThreadLite(&Bcb->Lock, ResourceThreadId);
        Bcb->ExclusivePinCount--;
        if (Bcb->ExclusivePinCount == 0)
        {
            Bcb->ExclusiveOwner = 0;
        }
    }
    else
    {
        ExReleaseResourceForThreadLite(&Bcb->Lock, ResourceThreadId);
    }

    Bcb->PinCount--;
}

static
PVOID
CcpGetBcbBuffer(
    IN PINTERNAL_BCB Bcb,
    IN PLARGE_INTEGER FileOffset)
{
    if (Bcb->MappedBase != NULL)
    {
        return (PVOID)((ULONG_PTR)Bcb->MappedBase +
                       (FileOffset->QuadPart - Bcb->MappedBaseOffset.QuadPart));
    }

    ASSERT(Bcb->Vacb != NULL);
    return (PVOID)((ULONG_PTR)Bcb->Vacb->BaseAddress +
                   (FileOffset->QuadPart - Bcb->Vacb->FileOffset.QuadPart));
}

static
PVOID
CcpGetAppropriateBcb(
    IN PROS_SHARED_CACHE_MAP SharedCacheMap,
    IN PROS_VACB Vacb,
    IN PLARGE_INTEGER FileOffset,
    IN ULONG Length,
    IN ULONG PinFlags,
    IN BOOLEAN ToPin)
{
    KIRQL OldIrql;
    BOOLEAN Result;
    PINTERNAL_BCB iBcb, DupBcb;
    LARGE_INTEGER BcbFileOffset;
    ULONG BcbLength;

    iBcb = ExAllocateFromNPagedLookasideList(&iBcbLookasideList);
    if (iBcb == NULL)
    {
        return NULL;
    }

    BcbFileOffset.QuadPart = PAGE_ROUND_DOWN_64(FileOffset->QuadPart);
    BcbLength = (ULONG)(PAGE_ROUND_UP_64(FileOffset->QuadPart + Length) - BcbFileOffset.QuadPart);

    RtlZeroMemory(iBcb, sizeof(*iBcb));
    iBcb->PFCB.NodeTypeCode = 0x2FD; /* As per KMTests */
    iBcb->PFCB.NodeByteSize = 0;
    iBcb->PFCB.MappedLength = BcbLength;
    iBcb->PFCB.MappedFileOffset = BcbFileOffset;
    iBcb->SharedCacheMap = SharedCacheMap;
    iBcb->Vacb = Vacb;
    iBcb->MappedBase = NULL;
    iBcb->MappedBaseOffset.QuadPart = 0;
    iBcb->PinCount = 0;
    iBcb->RefCount = 1;
    ExInitializeResourceLite(&iBcb->Lock);

    KeAcquireSpinLock(&SharedCacheMap->BcbSpinLock, &OldIrql);

    /* Check if we raced with another BCB creation */
    DupBcb = CcpFindBcb(SharedCacheMap, FileOffset, Length, ToPin, ToPin);
    /* Yes, and we've lost */
    if (DupBcb != NULL)
    {
        /* We will return that BCB */
        ++DupBcb->RefCount;
        Result = TRUE;
        KeReleaseSpinLock(&SharedCacheMap->BcbSpinLock, OldIrql);

        if (ToPin)
        {
            Result = CcpAcquireBcbForPin(DupBcb, PinFlags);
            if (!Result)
            {
                CcpDereferenceBcb(SharedCacheMap, DupBcb);
                CcRosReleaseVacb(SharedCacheMap, Vacb, FALSE, FALSE);
                ExDeleteResourceLite(&iBcb->Lock);
                ExFreeToNPagedLookasideList(&iBcbLookasideList, iBcb);
                return NULL;
            }
        }

        /* Delete the loser */
        CcRosReleaseVacb(SharedCacheMap, Vacb, FALSE, FALSE);
        ExDeleteResourceLite(&iBcb->Lock);
        ExFreeToNPagedLookasideList(&iBcbLookasideList, iBcb);

        /* Return the winner - no need to update buffer address, it's
         * relative to the VACB, which is unchanged.
         */
        iBcb = DupBcb;
    }
    /* Nope, insert ourselves */
    else
    {
        if (ToPin)
        {
            Result = CcpAcquireBcbForPin(iBcb, PinFlags);
            if (!Result)
            {
                KeReleaseSpinLock(&SharedCacheMap->BcbSpinLock, OldIrql);
                ExDeleteResourceLite(&iBcb->Lock);
                ExFreeToNPagedLookasideList(&iBcbLookasideList, iBcb);
                return NULL;
            }
        }

        InsertTailList(&SharedCacheMap->BcbList, &iBcb->BcbEntry);
        KeReleaseSpinLock(&SharedCacheMap->BcbSpinLock, OldIrql);
    }

    return iBcb;
}

static
PINTERNAL_BCB
CcpMapDataAcrossVacbs(
    IN PROS_SHARED_CACHE_MAP SharedCacheMap,
    IN PLARGE_INTEGER FileOffset,
    IN ULONG Length,
    IN ULONG PinFlags,
    IN BOOLEAN ToPin,
    OUT PVOID *Buffer)
{
    PINTERNAL_BCB Bcb;
    LARGE_INTEGER SectionOffset;
    SIZE_T ViewSize;
    NTSTATUS Status;
    KIRQL OldIrql;
    PINTERNAL_BCB DupBcb;
    BOOLEAN Result;

    Bcb = ExAllocateFromNPagedLookasideList(&iBcbLookasideList);
    if (Bcb == NULL)
        return NULL;

    RtlZeroMemory(Bcb, sizeof(*Bcb));
    Bcb->PFCB.NodeTypeCode = 0x2FD;
    Bcb->PFCB.NodeByteSize = 0;
    Bcb->PFCB.MappedLength = Length;
    Bcb->PFCB.MappedFileOffset = *FileOffset;
    Bcb->SharedCacheMap = SharedCacheMap;
    Bcb->RefCount = 1;
    ExInitializeResourceLite(&Bcb->Lock);

    SectionOffset = *FileOffset;
    ViewSize = Length;
    Status = MmMapViewInSystemSpaceEx(SharedCacheMap->Section,
                                      &Bcb->MappedBase,
                                      &ViewSize,
                                      &SectionOffset,
                                      0);
    if (!NT_SUCCESS(Status))
    {
        ExDeleteResourceLite(&Bcb->Lock);
        ExFreeToNPagedLookasideList(&iBcbLookasideList, Bcb);
        ExRaiseStatus(Status);
        return NULL;
    }

    Bcb->MappedBaseOffset = SectionOffset;

    /*
     * Only pinned cross-VACB BCBs are shared through the BCB list. Plain
     * CcMapData callers get a private system-space view and drop it on unmap.
     */
    if (ToPin)
    {
        Result = CcpAcquireBcbForPin(Bcb, PinFlags);
        if (!Result)
        {
            MmUnmapViewInSystemSpace(Bcb->MappedBase);
            ExDeleteResourceLite(&Bcb->Lock);
            ExFreeToNPagedLookasideList(&iBcbLookasideList, Bcb);
            return NULL;
        }

        KeAcquireSpinLock(&SharedCacheMap->BcbSpinLock, &OldIrql);

        DupBcb = CcpFindBcb(SharedCacheMap, FileOffset, Length, TRUE, TRUE);
        if (DupBcb != NULL)
        {
            ++DupBcb->RefCount;
            KeReleaseSpinLock(&SharedCacheMap->BcbSpinLock, OldIrql);

            CcpReleaseBcbPin(Bcb, ExGetCurrentResourceThread());
            MmUnmapViewInSystemSpace(Bcb->MappedBase);
            ExDeleteResourceLite(&Bcb->Lock);
            ExFreeToNPagedLookasideList(&iBcbLookasideList, Bcb);

            Bcb = DupBcb;

            Result = CcpAcquireBcbForPin(Bcb, PinFlags);
            if (!Result)
            {
                CcpDereferenceBcb(SharedCacheMap, Bcb);
                return NULL;
            }
        }
        else
        {
            InsertTailList(&SharedCacheMap->BcbList, &Bcb->BcbEntry);
            KeReleaseSpinLock(&SharedCacheMap->BcbSpinLock, OldIrql);
        }
    }

    *Buffer = CcpGetBcbBuffer(Bcb, FileOffset);

    return Bcb;
}

static
BOOLEAN
CcpEnsureBcbResident(
    IN PROS_SHARED_CACHE_MAP SharedCacheMap,
    IN PINTERNAL_BCB Bcb,
    IN BOOLEAN Wait,
    IN BOOLEAN NoRead)
{
    LONGLONG CurrentOffset;
    ULONG Remaining;
    ULONG VacbOffset;

    CurrentOffset = Bcb->PFCB.MappedFileOffset.QuadPart;
    Remaining = Bcb->PFCB.MappedLength;

    while (Remaining != 0)
    {
        PROS_VACB Vacb;
        ULONG ChunkLength;
        BOOLEAN Result;
        BOOLEAN ExtraReference = FALSE;

        VacbOffset = (ULONG)(CurrentOffset % VACB_MAPPING_GRANULARITY);
        ChunkLength = min(Remaining, VACB_MAPPING_GRANULARITY - VacbOffset);

        if ((Bcb->Vacb != NULL) &&
            (ROUND_DOWN(CurrentOffset, VACB_MAPPING_GRANULARITY) == Bcb->Vacb->FileOffset.QuadPart))
        {
            Vacb = Bcb->Vacb;
        }
        else
        {
            NTSTATUS Status;

            Status = CcRosGetVacb(SharedCacheMap,
                                  ROUND_DOWN(CurrentOffset, VACB_MAPPING_GRANULARITY),
                                  &Vacb);
            if (!NT_SUCCESS(Status))
            {
                ExRaiseStatus(Status);
                return FALSE;
            }

            ExtraReference = TRUE;
        }

        Result = CcRosEnsureVacbResident(Vacb, Wait, NoRead, VacbOffset, ChunkLength);

        if (ExtraReference)
        {
            CcRosReleaseVacb(SharedCacheMap, Vacb, FALSE, FALSE);
        }

        if (!Result)
        {
            return FALSE;
        }

        CurrentOffset += ChunkLength;
        Remaining -= ChunkLength;
    }

    return TRUE;
}

static
BOOLEAN
CcpPinData(
    IN PROS_SHARED_CACHE_MAP SharedCacheMap,
    IN PLARGE_INTEGER FileOffset,
    IN ULONG Length,
    IN ULONG Flags,
    OUT	PVOID * Bcb,
    OUT	PVOID * Buffer)
{
    PINTERNAL_BCB NewBcb;
    KIRQL OldIrql;
    ULONG VacbOffset;
    NTSTATUS Status;
    _SEH2_VOLATILE BOOLEAN Result;

    VacbOffset = (ULONG)(FileOffset->QuadPart % VACB_MAPPING_GRANULARITY);

    KeAcquireSpinLock(&SharedCacheMap->BcbSpinLock, &OldIrql);
    NewBcb = CcpFindBcb(SharedCacheMap, FileOffset, Length, TRUE, TRUE);
    if ((NewBcb == NULL) && BooleanFlagOn(Flags, PIN_IF_BCB))
    {
        NewBcb = CcpFindBcb(SharedCacheMap, FileOffset, Length, FALSE, TRUE);
    }

    if (NewBcb != NULL)
    {
        BOOLEAN Result;

        ++NewBcb->RefCount;
        KeReleaseSpinLock(&SharedCacheMap->BcbSpinLock, OldIrql);

        Result = CcpAcquireBcbForPin(NewBcb, Flags);
        if (!Result)
        {
            CcpDereferenceBcb(SharedCacheMap, NewBcb);
            return FALSE;
        }
    }
    else
    {
        LONGLONG ROffset;
        PROS_VACB Vacb;

        KeReleaseSpinLock(&SharedCacheMap->BcbSpinLock, OldIrql);

        if (BooleanFlagOn(Flags, PIN_IF_BCB))
        {
            return FALSE;
        }

        if ((VacbOffset + Length) > VACB_MAPPING_GRANULARITY)
        {
            NewBcb = CcpMapDataAcrossVacbs(SharedCacheMap, FileOffset, Length, Flags, TRUE, Buffer);
            if (NewBcb == NULL)
            {
                return FALSE;
            }
        }
        else
        {
            /* Properly round offset and call internal helper for getting a VACB */
            ROffset = ROUND_DOWN(FileOffset->QuadPart, VACB_MAPPING_GRANULARITY);
            Status = CcRosGetVacb(SharedCacheMap, ROffset, &Vacb);
            if (!NT_SUCCESS(Status))
            {
                CCTRACE(CC_API_DEBUG, "FileObject=%p FileOffset=%p Length=%lu Flags=0x%lx -> FALSE\n",
                    SharedCacheMap->FileObject, FileOffset, Length, Flags);
                ExRaiseStatus(Status);
                return FALSE;
            }

            NewBcb = CcpGetAppropriateBcb(SharedCacheMap, Vacb, FileOffset, Length, Flags, TRUE);
            if (NewBcb == NULL)
            {
                CcRosReleaseVacb(SharedCacheMap, Vacb, FALSE, FALSE);
                return FALSE;
            }
        }
    }

    Result = FALSE;
    _SEH2_TRY
    {
        /* Ensure the pages are resident */
        Result = CcpEnsureBcbResident(SharedCacheMap,
                                      NewBcb,
                                      BooleanFlagOn(Flags, PIN_WAIT),
                                      BooleanFlagOn(Flags, PIN_NO_READ));
    }
    _SEH2_FINALLY
    {
        if (!Result)
        {
            CCTRACE(CC_API_DEBUG, "FileObject=%p FileOffset=%p Length=%lu Flags=0x%lx -> FALSE\n",
                            SharedCacheMap->FileObject, FileOffset, Length, Flags);
            CcUnpinData(&NewBcb->PFCB);
            *Bcb = NULL;
            *Buffer = NULL;
        }
    }
    _SEH2_END;

    if (Result)
    {
        *Bcb = &NewBcb->PFCB;
        *Buffer = CcpGetBcbBuffer(NewBcb, FileOffset);
    }

    return Result;
}

/*
 * @implemented
 */
BOOLEAN
NTAPI
CcMapData (
    IN PFILE_OBJECT FileObject,
    IN PLARGE_INTEGER FileOffset,
    IN ULONG Length,
    IN ULONG Flags,
    OUT PVOID *pBcb,
    OUT PVOID *pBuffer)
{
    KIRQL OldIrql;
    PINTERNAL_BCB iBcb;
    PROS_VACB Vacb;
    PROS_SHARED_CACHE_MAP SharedCacheMap;
    ULONG VacbOffset;
    NTSTATUS Status;
    _SEH2_VOLATILE BOOLEAN Result;

    CCTRACE(CC_API_DEBUG, "CcMapData(FileObject 0x%p, FileOffset 0x%I64x, Length %lu, Flags 0x%lx,"
           " pBcb 0x%p, pBuffer 0x%p)\n", FileObject, FileOffset->QuadPart,
           Length, Flags, pBcb, pBuffer);

    ASSERT(FileObject);
    ASSERT(FileObject->SectionObjectPointer);
    ASSERT(FileObject->SectionObjectPointer->SharedCacheMap);

    SharedCacheMap = FileObject->SectionObjectPointer->SharedCacheMap;
    ASSERT(SharedCacheMap);

    if (Flags & MAP_WAIT)
    {
        ++CcMapDataWait;
    }
    else
    {
        ++CcMapDataNoWait;
    }

    VacbOffset = (ULONG)(FileOffset->QuadPart % VACB_MAPPING_GRANULARITY);
    if ((VacbOffset + Length) > VACB_MAPPING_GRANULARITY)
    {
        iBcb = CcpMapDataAcrossVacbs(SharedCacheMap, FileOffset, Length, 0, FALSE, pBuffer);
        if (iBcb == NULL)
        {
            *pBcb = NULL;
            *pBuffer = NULL;
            return FALSE;
        }

        Result = FALSE;
        _SEH2_TRY
        {
            Result = CcpEnsureBcbResident(SharedCacheMap,
                                          iBcb,
                                          BooleanFlagOn(Flags, MAP_WAIT),
                                          BooleanFlagOn(Flags, MAP_NO_READ));
        }
        _SEH2_FINALLY
        {
            if (!Result)
            {
                CcpDereferenceBcb(SharedCacheMap, iBcb);
                *pBcb = NULL;
                *pBuffer = NULL;
            }
        }
        _SEH2_END;

        if (!Result)
        {
            return FALSE;
        }

        *pBcb = &iBcb->PFCB;
        *pBuffer = CcpGetBcbBuffer(iBcb, FileOffset);

        CCTRACE(CC_API_DEBUG, "FileObject=%p FileOffset=%p Length=%lu Flags=0x%lx -> TRUE Bcb=%p, Buffer %p\n",
            FileObject, FileOffset, Length, Flags, *pBcb, *pBuffer);
        return TRUE;
    }

    KeAcquireSpinLock(&SharedCacheMap->BcbSpinLock, &OldIrql);
    iBcb = CcpFindBcb(SharedCacheMap, FileOffset, Length, FALSE, FALSE);

    if (iBcb == NULL)
    {
        KeReleaseSpinLock(&SharedCacheMap->BcbSpinLock, OldIrql);

        /* Call internal helper for getting a VACB */
        Status = CcRosGetVacb(SharedCacheMap, FileOffset->QuadPart, &Vacb);
        if (!NT_SUCCESS(Status))
        {
            CCTRACE(CC_API_DEBUG, "FileObject=%p FileOffset=%p Length=%lu Flags=0x%lx -> FALSE\n",
                SharedCacheMap->FileObject, FileOffset, Length, Flags);
            ExRaiseStatus(Status);
        }

        iBcb = CcpGetAppropriateBcb(SharedCacheMap, Vacb, FileOffset, Length, 0, FALSE);
        if (iBcb == NULL)
        {
            CcRosReleaseVacb(SharedCacheMap, Vacb, FALSE, FALSE);
            CCTRACE(CC_API_DEBUG, "FileObject=%p FileOffset=%p Length=%lu Flags=0x%lx -> FALSE\n",
                SharedCacheMap->FileObject, FileOffset, Length, Flags);
            *pBcb = NULL; // If you ever remove this for compat, make sure to review all callers for using an unititialized value
            ExRaiseStatus(STATUS_INSUFFICIENT_RESOURCES);
        }
    }
    else
    {
        ++iBcb->RefCount;
        KeReleaseSpinLock(&SharedCacheMap->BcbSpinLock, OldIrql);
    }

    _SEH2_TRY
    {
        Result = FALSE;
        /* Ensure the pages are resident */
        Result = CcRosEnsureVacbResident(iBcb->Vacb, BooleanFlagOn(Flags, MAP_WAIT),
                BooleanFlagOn(Flags, MAP_NO_READ), VacbOffset, Length);
    }
    _SEH2_FINALLY
    {
        if (!Result)
        {
            CcpDereferenceBcb(SharedCacheMap, iBcb);
            *pBcb = NULL;
            *pBuffer = NULL;
        }
    }
    _SEH2_END;

    if (Result)
    {
        *pBcb = &iBcb->PFCB;
        if (iBcb->MappedBase != NULL)
        {
            *pBuffer = (PVOID)((ULONG_PTR)iBcb->MappedBase +
                               (FileOffset->QuadPart - iBcb->MappedBaseOffset.QuadPart));
        }
        else
        {
            *pBuffer = (PVOID)((ULONG_PTR)iBcb->Vacb->BaseAddress + VacbOffset);
        }
    }

    CCTRACE(CC_API_DEBUG, "FileObject=%p FileOffset=%p Length=%lu Flags=0x%lx -> TRUE Bcb=%p, Buffer %p\n",
        FileObject, FileOffset, Length, Flags, *pBcb, *pBuffer);
    return Result;
}

/*
 * @unimplemented
 */
BOOLEAN
NTAPI
CcPinMappedData (
    IN	PFILE_OBJECT FileObject,
    IN	PLARGE_INTEGER FileOffset,
    IN	ULONG Length,
    IN	ULONG Flags,
    OUT	PVOID * Bcb)
{
    BOOLEAN Result;
    PVOID Buffer;
    PINTERNAL_BCB iBcb;
    PROS_SHARED_CACHE_MAP SharedCacheMap;

    CCTRACE(CC_API_DEBUG, "FileObject=%p FileOffset=%p Length=%lu Flags=0x%lx\n",
        FileObject, FileOffset, Length, Flags);

    ASSERT(FileObject);
    ASSERT(FileObject->SectionObjectPointer);
    ASSERT(FileObject->SectionObjectPointer->SharedCacheMap);

    SharedCacheMap = FileObject->SectionObjectPointer->SharedCacheMap;
    ASSERT(SharedCacheMap);
    if (!SharedCacheMap->PinAccess)
    {
        SetFlag(Flags, PIN_NO_READ);
    }

    iBcb = *Bcb ? CONTAINING_RECORD(*Bcb, INTERNAL_BCB, PFCB) : NULL;

    ++CcPinMappedDataCount;

    Result = CcpPinData(SharedCacheMap, FileOffset, Length, Flags, Bcb, &Buffer);
    if (Result && iBcb != NULL)
    {
        CcUnpinData(&iBcb->PFCB);
    }

    return Result;
}

/*
 * @unimplemented
 */
BOOLEAN
NTAPI
CcPinRead (
    IN	PFILE_OBJECT FileObject,
    IN	PLARGE_INTEGER FileOffset,
    IN	ULONG Length,
    IN	ULONG Flags,
    OUT	PVOID * Bcb,
    OUT	PVOID * Buffer)
{
    PROS_SHARED_CACHE_MAP SharedCacheMap;

    CCTRACE(CC_API_DEBUG, "FileOffset=%p FileOffset=%p Length=%lu Flags=0x%lx\n",
        FileObject, FileOffset, Length, Flags);

    ASSERT(FileObject);
    ASSERT(FileObject->SectionObjectPointer);
    ASSERT(FileObject->SectionObjectPointer->SharedCacheMap);

    SharedCacheMap = FileObject->SectionObjectPointer->SharedCacheMap;
    ASSERT(SharedCacheMap);
    if (!SharedCacheMap->PinAccess)
    {
        SetFlag(Flags, PIN_NO_READ);
    }

    if (Flags & PIN_WAIT)
    {
        ++CcPinReadWait;
    }
    else
    {
        ++CcPinReadNoWait;
    }

    return CcpPinData(SharedCacheMap, FileOffset, Length, Flags, Bcb, Buffer);
}

/*
 * @unimplemented
 */
BOOLEAN
NTAPI
CcPreparePinWrite (
    IN	PFILE_OBJECT FileObject,
    IN	PLARGE_INTEGER FileOffset,
    IN	ULONG Length,
    IN	BOOLEAN Zero,
    IN	ULONG Flags,
    OUT	PVOID * Bcb,
    OUT	PVOID * Buffer)
{
    CCTRACE(CC_API_DEBUG, "FileOffset=%p FileOffset=%p Length=%lu Zero=%d Flags=0x%lx\n",
        FileObject, FileOffset, Length, Zero, Flags);

    /*
     * FIXME: This is function is similar to CcPinRead, but doesn't
     * read the data if they're not present. Instead it should just
     * prepare the VACBs and zero them out if Zero != FALSE.
     *
     * For now calling CcPinRead is better than returning error or
     * just having UNIMPLEMENTED here.
     */
    return CcPinRead(FileObject, FileOffset, Length, Flags, Bcb, Buffer);
}

static
VOID
CcpMarkDirtyVacbsForBcb(
    IN PINTERNAL_BCB Bcb)
{
    PROS_SHARED_CACHE_MAP SharedCacheMap = Bcb->SharedCacheMap;
    LONGLONG CurrentOffset = Bcb->PFCB.MappedFileOffset.QuadPart;
    LONGLONG EndOffset;
    NTSTATUS Status;

    Status = RtlLongLongAdd(CurrentOffset, Bcb->PFCB.MappedLength, &EndOffset);
    if (!NT_SUCCESS(Status))
    {
        return;
    }

    while (CurrentOffset < EndOffset)
    {
        PROS_VACB Vacb;
        LONGLONG VacbOffset = ROUND_DOWN(CurrentOffset, VACB_MAPPING_GRANULARITY);

        Status = CcRosGetVacb(SharedCacheMap, VacbOffset, &Vacb);
        if (NT_SUCCESS(Status))
        {
            CcRosReleaseVacb(SharedCacheMap, Vacb, TRUE, FALSE);
        }

        CurrentOffset = VacbOffset + VACB_MAPPING_GRANULARITY;
    }
}

/*
 * @implemented
 */
VOID NTAPI
CcSetDirtyPinnedData (
    IN PVOID Bcb,
    IN PLARGE_INTEGER Lsn)
{
    PINTERNAL_BCB iBcb = CONTAINING_RECORD(Bcb, INTERNAL_BCB, PFCB);
    PROS_VACB Vacb = iBcb->Vacb;

    CCTRACE(CC_API_DEBUG, "Bcb=%p Lsn=%p\n", Bcb, Lsn);

    iBcb->Dirty = TRUE;

    /* Tell Mm */
    MmMakeSegmentDirty(iBcb->SharedCacheMap->FileObject->SectionObjectPointer,
                       iBcb->PFCB.MappedFileOffset.QuadPart,
                       iBcb->PFCB.MappedLength);

    if ((Vacb != NULL) && !Vacb->Dirty)
    {
        CcRosMarkDirtyVacb(Vacb);
    }
    else if (iBcb->MappedBase != NULL)
    {
        CcpMarkDirtyVacbsForBcb(iBcb);
    }
}


/*
 * @implemented
 */
VOID NTAPI
CcUnpinData (
    IN PVOID Bcb)
{
    CCTRACE(CC_API_DEBUG, "Bcb=%p\n", Bcb);

    CcUnpinDataForThread(Bcb, (ERESOURCE_THREAD)PsGetCurrentThread());
}

/*
 * @unimplemented
 */
VOID
NTAPI
CcUnpinDataForThread (
    IN	PVOID Bcb,
    IN	ERESOURCE_THREAD ResourceThreadId)
{
    PINTERNAL_BCB iBcb = CONTAINING_RECORD(Bcb, INTERNAL_BCB, PFCB);

    CCTRACE(CC_API_DEBUG, "Bcb=%p ResourceThreadId=%lu\n", Bcb, ResourceThreadId);

    if (iBcb->PinCount != 0)
    {
        CcpReleaseBcbPin(iBcb, ResourceThreadId);
    }

    CcpDereferenceBcb(iBcb->SharedCacheMap, iBcb);
}

/*
 * @implemented
 */
VOID
NTAPI
CcRepinBcb (
    IN	PVOID Bcb)
{
    PINTERNAL_BCB iBcb = CONTAINING_RECORD(Bcb, INTERNAL_BCB, PFCB);

    CCTRACE(CC_API_DEBUG, "Bcb=%p\n", Bcb);

    iBcb->RefCount++;
}

/*
 * @unimplemented
 */
VOID
NTAPI
CcUnpinRepinnedBcb (
    IN	PVOID Bcb,
    IN	BOOLEAN WriteThrough,
    IN	PIO_STATUS_BLOCK IoStatus)
{
    PINTERNAL_BCB iBcb = CONTAINING_RECORD(Bcb, INTERNAL_BCB, PFCB);
    KIRQL OldIrql;
    PROS_SHARED_CACHE_MAP SharedCacheMap;

    CCTRACE(CC_API_DEBUG, "Bcb=%p WriteThrough=%d\n", Bcb, WriteThrough);

    SharedCacheMap = iBcb->SharedCacheMap;
    IoStatus->Status = STATUS_SUCCESS;

    if (WriteThrough)
    {
        CcFlushCache(SharedCacheMap->FileObject->SectionObjectPointer,
                     &iBcb->PFCB.MappedFileOffset,
                     iBcb->PFCB.MappedLength,
                     IoStatus);
        if (NT_SUCCESS(IoStatus->Status))
        {
            iBcb->Dirty = FALSE;
        }
    }
    else
    {
        IoStatus->Status = STATUS_SUCCESS;
        IoStatus->Information = 0;
    }

    KeAcquireSpinLock(&SharedCacheMap->BcbSpinLock, &OldIrql);
    if (--iBcb->RefCount == 0)
    {
        if (iBcb->Dirty && iBcb->BcbEntry.Flink != NULL)
        {
            KeReleaseSpinLock(&SharedCacheMap->BcbSpinLock, OldIrql);
            if (iBcb->PinCount != 0)
            {
                ExReleaseResourceLite(&iBcb->Lock);
                iBcb->PinCount--;
                iBcb->ExclusivePinCount = 0;
                iBcb->ExclusiveOwner = 0;
                ASSERT(iBcb->PinCount == 0);
            }

            OldIrql = KeAcquireQueuedSpinLock(LockQueueMasterLock);
            CcScheduleLazyWriteScan(TRUE);
            KeReleaseQueuedSpinLock(LockQueueMasterLock, OldIrql);
            return;
        }

        if (iBcb->BcbEntry.Flink != NULL)
        {
            RemoveEntryList(&iBcb->BcbEntry);
        }
        KeReleaseSpinLock(&SharedCacheMap->BcbSpinLock, OldIrql);

        if (iBcb->PinCount != 0)
        {
            ExReleaseResourceLite(&iBcb->Lock);
            iBcb->PinCount--;
            iBcb->ExclusivePinCount = 0;
            iBcb->ExclusiveOwner = 0;
            ASSERT(iBcb->PinCount == 0);
        }

        /*
         * Don't mark dirty, if it was dirty,
         * the VACB was already marked as such
         * following the call to CcSetDirtyPinnedData
         */
        if (iBcb->MappedBase != NULL)
        {
            MmUnmapViewInSystemSpace(iBcb->MappedBase);
        }
        else
        {
            CcRosReleaseVacb(iBcb->SharedCacheMap,
                             iBcb->Vacb,
                             FALSE,
                             FALSE);
        }

        ExDeleteResourceLite(&iBcb->Lock);
        ExFreeToNPagedLookasideList(&iBcbLookasideList, iBcb);
    }
    else
    {
        KeReleaseSpinLock(&SharedCacheMap->BcbSpinLock, OldIrql);
    }
}
