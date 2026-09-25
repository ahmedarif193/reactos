/*
 * PROJECT:     ReactOS NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/sys/mdl.c
 * PURPOSE:     Memory descriptor list management
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <nvs/include/misys.h>

#define MI_MDL_LOCK_ATTEMPTS 64

PMI_MDL
MiMdlAllocate(
    _In_ PMI_ADDRESS_SPACE Space,
    _In_ ULONG64 VirtualAddress,
    _In_ ULONG ByteCount)
{
    ULONG ByteOffset = (ULONG)(VirtualAddress & (PAGE_SIZE - 1));
    ULONG Pages = (ULONG)(((ULONG64)ByteOffset + ByteCount + PAGE_SIZE - 1) >> PAGE_SHIFT);
    PMI_MDL Mdl;

    if (ByteCount == 0)
        return NULL;

    Mdl = MI_ALLOCATE(FIELD_OFFSET(MI_MDL, Frames) + (SIZE_T)Pages * sizeof(MI_FRAME_NUMBER));
    if (Mdl == NULL)
        return NULL;

    RtlZeroMemory(Mdl, FIELD_OFFSET(MI_MDL, Frames));
    Mdl->Space = Space;
    Mdl->StartVa = VirtualAddress - ByteOffset;
    Mdl->ByteOffset = ByteOffset;
    Mdl->ByteCount = ByteCount;
    Mdl->PageCount = Pages;
    return Mdl;
}

VOID
MiMdlFree(
    _Inout_ PMI_MDL Mdl)
{
    MI_ASSERT(!Mdl->Locked && Mdl->MappedSystemVa == 0);
    MI_FREE(Mdl);
}

VOID
MiUnlockFrames(
    _Inout_ PMI_SYSTEM System,
    _In_ const MI_FRAME_NUMBER *Frames,
    _In_ ULONG PageCount,
    _In_ BOOLEAN WriteAccess)
{
    PMI_PFN_DATABASE Db = &System->Pfn;
    ULONG i;

    for (i = 0; i < PageCount; i++)
    {
        if (Frames[i] >= Db->FrameCount)
            continue;

        if (WriteAccess)
            MiPfnSetModified(Db, (ULONG)Frames[i]);

        MiPfnDereference(Db, (ULONG)Frames[i]);
    }
}

NTSTATUS
MiLockPages(
    _Inout_ PMI_ADDRESS_SPACE Space,
    _In_ ULONG64 StartVa,
    _In_ ULONG PageCount,
    _In_ BOOLEAN UserMode,
    _In_ BOOLEAN WriteAccess,
    _Out_ PMI_FRAME_NUMBER Frames)
{
    PMI_PFN_DATABASE Db = &Space->System->Pfn;
    ULONG i;

    StartVa &= ~((ULONG64)PAGE_SIZE - 1);

    if (PageCount == 0 || StartVa < Space->LowestVa || StartVa > Space->HighestVa ||
        ((ULONG64)PageCount << PAGE_SHIFT) - 1 > Space->HighestVa - StartVa)
    {
        return STATUS_ACCESS_VIOLATION;
    }

    for (i = 0; i < PageCount; i++)
    {
        ULONG64 Va = StartVa + (ULONG64)i * PAGE_SIZE;
        NTSTATUS Status = STATUS_WORKING_SET_QUOTA;
        ULONG Attempt;

        for (Attempt = 0; Attempt < MI_MDL_LOCK_ATTEMPTS; Attempt++)
        {
            MI_PTE Pte;
            ULONG64 Physical;

            MI_RW_ACQUIRE_EXCLUSIVE(&Space->Lock);

            if (MiPtTranslate(Space, Va, &Physical, &Pte) && (!UserMode || MiArchPteIsUser(Pte)) &&
                (!WriteAccess || (MiArchPteIsWritable(Pte) && MiArchPteIsDirty(Pte))))
            {
                ULONG64 Frame = Physical >> PAGE_SHIFT;

                if (Frame < Db->FrameCount)
                {
                    KIRQL OldIrql = MiPfnLock(Db, (ULONG)Frame);

                    if (!(MI_PFN_FLAGS(&Db->Pfn[Frame]) & MI_PFN_FLAG_PAGE_TABLE))
                        MI_ATOMIC_WRITE32(&Db->Pfn[Frame].CacheFlags,
                                          MiArchPteLeafFlags(Pte) & MI_LEAF_CACHE_MASK);
                    MiPfnReferenceLocked(Db, (ULONG)Frame);
                    MiPfnUnlock(Db, (ULONG)Frame, OldIrql);
                }

                Frames[i] = (MI_FRAME_NUMBER)Frame;
                MI_RW_RELEASE_EXCLUSIVE(&Space->Lock);
                Status = STATUS_SUCCESS;
                break;
            }

            MI_RW_RELEASE_EXCLUSIVE(&Space->Lock);

            Status = MiFaultWithWriteAllowance(Space, Va, WriteAccess ? MiFaultWrite : MiFaultRead,
                                               UserMode, TRUE);
            if (!NT_SUCCESS(Status))
                break;

            Status = STATUS_WORKING_SET_QUOTA;
        }

        if (!NT_SUCCESS(Status))
        {
            MiUnlockFrames(Space->System, Frames, i, WriteAccess);
            return Status;
        }
    }

    return STATUS_SUCCESS;
}

NTSTATUS
MiLockSelectedPages(
    _Inout_ PMI_ADDRESS_SPACE Space,
    _In_ const ULONG64 *Addresses,
    _In_ ULONG PageCount,
    _In_ BOOLEAN UserMode,
    _In_ BOOLEAN WriteAccess,
    _Out_ PMI_FRAME_NUMBER Frames)
{
    ULONG i;

    if (Addresses == NULL || Frames == NULL || PageCount == 0)
        return STATUS_INVALID_PARAMETER;

    for (i = 0; i < PageCount; i++)
    {
        PMI_ADDRESS_SPACE Target = Space;
        ULONG64 Va = Addresses[i];
        NTSTATUS Status;

        if (!UserMode && Va >= Space->System->SystemSpace.LowestVa)
            Target = &Space->System->SystemSpace;
        Status = (Va & (PAGE_SIZE - 1)) ? STATUS_INVALID_PARAMETER
            : MiLockPages(Target, Va, 1, UserMode, WriteAccess, &Frames[i]);
        if (!NT_SUCCESS(Status))
        {
            MiUnlockFrames(Space->System, Frames, i, WriteAccess);
            return Status;
        }
    }
    return STATUS_SUCCESS;
}

NTSTATUS
MiMapFrames(
    _Inout_ PMI_SYSTEM System,
    _In_ const MI_FRAME_NUMBER *Frames,
    _In_ ULONG PageCount,
    _In_ MI_CACHE_TYPE CacheType,
    _In_ ULONG Protection,
    _Out_ PULONG64 Base)
{
    ULONG Flags = (CacheType == MiCacheNone) ? MI_LEAF_NOCACHE
                                             : ((CacheType == MiCacheWriteCombined) ? MI_LEAF_WRITECOMBINE : 0);
    NTSTATUS Status;

    *Base = MiReserveSystemPtes(System, PageCount);
    if (*Base == 0)
        return STATUS_INSUFFICIENT_RESOURCES;

    Status = MiSystemMapFrames(System, *Base, Frames, PageCount, Protection, Flags | MI_LEAF_PFN_CACHE, FALSE);
    if (!NT_SUCCESS(Status))
    {
        MiReleaseSystemPtes(System, *Base, PageCount);
        *Base = 0;
    }

    return Status;
}

VOID
MiUnmapFrames(
    _Inout_ PMI_SYSTEM System,
    _In_ ULONG64 Base,
    _In_ ULONG PageCount)
{
    MiSystemUnmap(System, Base, PageCount, FALSE);
    MiReleaseSystemPtes(System, Base, PageCount);
}

NTSTATUS
MiProbeAndLockPages(
    _Inout_ PMI_MDL Mdl,
    _In_ BOOLEAN UserMode,
    _In_ BOOLEAN WriteAccess)
{
    NTSTATUS Status;

    if (Mdl->Locked)
        return STATUS_INVALID_PARAMETER;

    Mdl->WriteAccess = WriteAccess;
    Status = MiLockPages(Mdl->Space, Mdl->StartVa, Mdl->PageCount, UserMode, WriteAccess, Mdl->Frames);
    if (NT_SUCCESS(Status))
        Mdl->Locked = TRUE;

    return Status;
}

VOID
MiUnlockPages(
    _Inout_ PMI_MDL Mdl)
{
    MI_ASSERT(Mdl->Locked);

    if (Mdl->MappedSystemVa != 0)
        MiUnmapLockedPages(Mdl);

    MiUnlockFrames(Mdl->Space->System, Mdl->Frames, Mdl->PageCount, Mdl->WriteAccess);
    Mdl->Locked = FALSE;
}

NTSTATUS
MiBuildMdlForSystemRange(
    _Inout_ PMI_MDL Mdl)
{
    ULONG i;

    for (i = 0; i < Mdl->PageCount; i++)
    {
        ULONG64 Physical = MiGetPhysicalAddress(Mdl->Space, Mdl->StartVa + (ULONG64)i * PAGE_SIZE);

        if (Physical == 0)
            return STATUS_ACCESS_VIOLATION;

        Mdl->Frames[i] = (MI_FRAME_NUMBER)(Physical >> PAGE_SHIFT);
    }

    return STATUS_SUCCESS;
}

NTSTATUS
MiMapLockedPages(
    _Inout_ PMI_MDL Mdl,
    _In_ MI_CACHE_TYPE CacheType,
    _Out_ PULONG64 SystemVa)
{
    NTSTATUS Status;

    *SystemVa = 0;

    if (Mdl->MappedSystemVa == 0)
    {
        Status = MiMapFrames(Mdl->Space->System, Mdl->Frames, Mdl->PageCount, CacheType, MI_PROT_READWRITE,
                             &Mdl->MappedSystemVa);
        if (!NT_SUCCESS(Status))
            return Status;
    }

    *SystemVa = Mdl->MappedSystemVa + Mdl->ByteOffset;
    return STATUS_SUCCESS;
}

VOID
MiUnmapLockedPages(
    _Inout_ PMI_MDL Mdl)
{
    if (Mdl->MappedSystemVa == 0)
        return;

    MiUnmapFrames(Mdl->Space->System, Mdl->MappedSystemVa, Mdl->PageCount);
    Mdl->MappedSystemVa = 0;
}
