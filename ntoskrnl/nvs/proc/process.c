/*
 * PROJECT:     ReactOS NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/proc/process.c
 * PURPOSE:     Process address-space lifecycle management
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include <nvs/include/miproc.h>

NTSTATUS
MiProcessManagerInitialize(
    _Inout_ PMI_SYSTEM System,
    _Out_ PMI_PROCESS_MANAGER Manager)
{
    return MiProcessManagerInitializeEx(System, Manager, MI_FRAME_INVALID, 0);
}

NTSTATUS
MiProcessManagerInitializeEx(
    _Inout_ PMI_SYSTEM System,
    _Out_ PMI_PROCESS_MANAGER Manager,
    _In_ ULONG AdoptedFrame,
    _In_ ULONG64 AdoptedSystemVa)
{
    ULONG64 ViewSize = PAGE_SIZE;
    ULONG64 PageableView = 0;
    NTSTATUS Status;

    RtlZeroMemory(Manager, sizeof(*Manager));
    MI_MUTEX_INIT(&Manager->Lock);
    InitializeListHead(&Manager->ProcessList);
    Manager->TrimCursor = &Manager->ProcessList;
    Manager->AvailableLow = System->Pfn.FrameCount / 16;
    Manager->AvailableHigh = System->Pfn.FrameCount / 8;

    Status = MiSegmentCreate(System, MiSegmentPageFileBacked, PAGE_SIZE, MI_PROT_READWRITE, NULL, NULL, NULL, 0,
                             &Manager->UserSharedSegment);
    if (!NT_SUCCESS(Status))
        return Status;

    if (AdoptedFrame != MI_FRAME_INVALID)
    {
        Status = MiSegmentAdoptFrame(Manager->UserSharedSegment, 0, AdoptedFrame);
        if (NT_SUCCESS(Status))
        {
            Manager->UserSharedSystemVa = AdoptedSystemVa;
            Manager->UserSharedAdopted = TRUE;
        }
        else
            MiProcessManagerUninitialize(System, Manager);

        return Status;
    }

    Status = MiMapView(&System->SystemSpace, Manager->UserSharedSegment, &PageableView, 0, &ViewSize,
                       MI_PROT_READWRITE, 0);
    if (NT_SUCCESS(Status))
    {
        Manager->UserSharedMdl = MiMdlAllocate(&System->SystemSpace, PageableView, PAGE_SIZE);
        Status = (Manager->UserSharedMdl != NULL) ? MiProbeAndLockPages(Manager->UserSharedMdl, FALSE, TRUE)
                                                      : STATUS_INSUFFICIENT_RESOURCES;
        if (NT_SUCCESS(Status))
            Status = MiMapLockedPages(Manager->UserSharedMdl, MiCacheFull, &Manager->UserSharedSystemVa);

        MiUnmapView(&System->SystemSpace, PageableView);
    }

    if (!NT_SUCCESS(Status))
        MiProcessManagerUninitialize(System, Manager);

    return Status;
}

VOID
MiProcessManagerUninitialize(
    _Inout_ PMI_SYSTEM System,
    _Inout_ PMI_PROCESS_MANAGER Manager)
{
    MI_ASSERT(IsListEmpty(&Manager->ProcessList));

    if (Manager->UserSharedMdl != NULL)
    {
        if (Manager->UserSharedMdl->Locked)
            MiUnlockPages(Manager->UserSharedMdl);

        MiMdlFree(Manager->UserSharedMdl);
        Manager->UserSharedMdl = NULL;
    }

    Manager->UserSharedSystemVa = 0;

    if (Manager->UserSharedSegment != NULL)
    {
        if (Manager->UserSharedAdopted)
            MiSegmentReleaseAdoptedFrame(Manager->UserSharedSegment, 0);

        MiSegmentDereference(Manager->UserSharedSegment);
        Manager->UserSharedSegment = NULL;
    }
}

NTSTATUS
MiProcessCreate(
    _Inout_ PMI_SYSTEM System,
    _Inout_ PMI_PROCESS_MANAGER Manager,
    _Out_ PMI_PROCESS Process)
{
    ULONG64 Base = MI_SHARED_USER_DATA_VA;
    ULONG64 ViewSize = PAGE_SIZE;
    NTSTATUS Status;

    RtlZeroMemory(Process, sizeof(*Process));
    Process->WorkingSetMinimum = MI_DEFAULT_WS_MINIMUM;
    Process->WorkingSetMaximum = MI_DEFAULT_WS_MAXIMUM;

    Status = MiAddressSpaceCreate(System, &Process->Space);
    if (!NT_SUCCESS(Status))
        return Status;

    Status = MiMapView(&Process->Space, Manager->UserSharedSegment, &Base, 0, &ViewSize, MI_PROT_READONLY, 0);
    if (!NT_SUCCESS(Status))
    {
        MiAddressSpaceDestroy(&Process->Space);
        return Status;
    }

    MI_MUTEX_ACQUIRE(&Manager->Lock);
    InsertTailList(&Manager->ProcessList, &Process->SystemLink);
    Manager->ProcessCount++;
    MI_MUTEX_RELEASE(&Manager->Lock);
    return STATUS_SUCCESS;
}

NTSTATUS
MiProcessAdopt(
    _Inout_ PMI_SYSTEM System,
    _Inout_ PMI_PROCESS_MANAGER Manager,
    _Out_ PMI_PROCESS Process,
    _In_ ULONG RootFrame)
{
    NTSTATUS Status;

    RtlZeroMemory(Process, sizeof(*Process));
    Process->WorkingSetMinimum = MI_DEFAULT_WS_MINIMUM;
    Process->WorkingSetMaximum = MI_DEFAULT_WS_MAXIMUM;

    Status = MiAddressSpaceAdopt(System, &Process->Space, RootFrame, FALSE);
    if (!NT_SUCCESS(Status))
        return Status;

    MI_MUTEX_ACQUIRE(&Manager->Lock);
    InsertTailList(&Manager->ProcessList, &Process->SystemLink);
    Manager->ProcessCount++;
    MI_MUTEX_RELEASE(&Manager->Lock);
    return STATUS_SUCCESS;
}

VOID
MiProcessDelete(
    _Inout_ PMI_PROCESS_MANAGER Manager,
    _Inout_ PMI_PROCESS Process)
{
    MI_MUTEX_ACQUIRE(&Manager->Lock);
    if (Manager->TrimCursor == &Process->SystemLink)
        Manager->TrimCursor = Process->SystemLink.Flink;
    RemoveEntryList(&Process->SystemLink);
    Manager->ProcessCount--;
    MI_MUTEX_RELEASE(&Manager->Lock);

    MiCleanAddressSpace(&Process->Space);
    MiAddressSpaceDestroy(&Process->Space);
}

static
NTSTATUS
MiProcessAllocateTopDown(
    _Inout_ PMI_PROCESS Process,
    _In_ ULONG64 Size,
    _Out_ PULONG64 Base)
{
    ULONG64 RegionSize = Size;

    *Base = 0;
    return MiAllocateVirtualMemory(&Process->Space, Base, &RegionSize,
                                   MI_MEM_RESERVE | MI_MEM_COMMIT | MI_MEM_TOP_DOWN, MI_PROT_READWRITE);
}

NTSTATUS
MiProcessCreatePeb(
    _Inout_ PMI_PROCESS Process,
    _In_ ULONG64 Size,
    _Out_ PULONG64 Peb)
{
    NTSTATUS Status;

    if (Process->Peb != 0)
        return STATUS_INVALID_PARAMETER;

    Status = MiProcessAllocateTopDown(Process, Size, Peb);
    if (NT_SUCCESS(Status))
        Process->Peb = *Peb;

    return Status;
}

NTSTATUS
MiProcessCreateTeb(
    _Inout_ PMI_PROCESS Process,
    _In_ ULONG64 Size,
    _Out_ PULONG64 Teb)
{
    return MiProcessAllocateTopDown(Process, Size, Teb);
}

NTSTATUS
MiProcessDeleteTeb(
    _Inout_ PMI_PROCESS Process,
    _In_ ULONG64 Teb)
{
    ULONG64 Size = 0;

    return MiFreeVirtualMemory(&Process->Space, &Teb, &Size, MI_MEM_RELEASE);
}

NTSTATUS
MiProcessSetWorkingSetLimits(
    _Inout_ PMI_PROCESS Process,
    _In_ ULONG64 Minimum,
    _In_ ULONG64 Maximum,
    _In_ BOOLEAN HardMaximum)
{
    if (Minimum == ~0ULL && Maximum == ~0ULL)
    {
        MiProcessEmptyWorkingSet(Process);
        return STATUS_SUCCESS;
    }

    if (Minimum == 0 || Maximum < Minimum)
        return STATUS_INVALID_PARAMETER;

    Process->WorkingSetMinimum = Minimum;
    Process->WorkingSetMaximum = Maximum;
    Process->HardWorkingSetMaximum = HardMaximum;

    if (HardMaximum)
    {
        LONG64 Resident = MI_ATOMIC_READ64(&Process->Space.ResidentPages);

        if ((ULONG64)Resident > Maximum)
            MiTrimAddressSpace(&Process->Space, (ULONG)((ULONG64)Resident - Maximum), TRUE);
    }

    return STATUS_SUCCESS;
}

static
VOID
MiProcessUpdatePeaks(
    _Inout_ PMI_PROCESS Process)
{
    LONG64 Resident = MI_ATOMIC_READ64(&Process->Space.ResidentPages);
    LONG64 Commit = MI_ATOMIC_READ64(&Process->Space.CommittedPages);

    if (Resident > MI_ATOMIC_READ64(&Process->PeakWorkingSet))
        MI_ATOMIC_WRITE64(&Process->PeakWorkingSet, Resident);

    if (Commit > MI_ATOMIC_READ64(&Process->PeakCommit))
        MI_ATOMIC_WRITE64(&Process->PeakCommit, Commit);
}

VOID
MiProcessQueryCounters(
    _Inout_ PMI_PROCESS Process,
    _Out_ PMI_PROCESS_COUNTERS Counters)
{
    MiProcessUpdatePeaks(Process);

    Counters->PageFaultCount = (ULONG64)MI_ATOMIC_READ64(&Process->Space.Faults);
    Counters->WorkingSetSize = (ULONG64)MI_ATOMIC_READ64(&Process->Space.ResidentPages) << PAGE_SHIFT;
    Counters->PeakWorkingSetSize = (ULONG64)MI_ATOMIC_READ64(&Process->PeakWorkingSet) << PAGE_SHIFT;
    Counters->PagefileUsage = (ULONG64)MI_ATOMIC_READ64(&Process->Space.CommittedPages) << PAGE_SHIFT;
    Counters->PeakPagefileUsage = (ULONG64)MI_ATOMIC_READ64(&Process->PeakCommit) << PAGE_SHIFT;
    Counters->PageTablePages = (ULONG64)MI_ATOMIC_READ64(&Process->Space.PageTablePages);
}

ULONG
MiProcessEmptyWorkingSet(
    _Inout_ PMI_PROCESS Process)
{
    MiProcessUpdatePeaks(Process);
    return MiTrimAddressSpace(&Process->Space, ~0u, TRUE);
}

ULONG
MiBalanceMemory(
    _Inout_ PMI_SYSTEM System,
    _Inout_ PMI_PROCESS_MANAGER Manager)
{
    ULONG64 Available = MiPfnAvailablePages(&System->Pfn);
    ULONG Trimmed = 0;
    ULONG Visited = 0;
    ULONG Pass;

    MI_ATOMIC_ADD64(&Manager->BalancePasses, 1);

    if (Available >= Manager->AvailableLow)
    {
        MiWriteModifiedPages(System, 64);
        return 0;
    }

    MI_MUTEX_ACQUIRE(&Manager->Lock);

    for (Pass = 0; Pass < 2 && Available + Trimmed < Manager->AvailableHigh; Pass++)
    {
        for (Visited = 0; Visited < Manager->ProcessCount && Available + Trimmed < Manager->AvailableHigh; Visited++)
        {
            PMI_PROCESS Process;
            LONG64 Resident;
            ULONG64 Floor;
            ULONG Want;

            if (Manager->TrimCursor == &Manager->ProcessList)
                Manager->TrimCursor = Manager->ProcessList.Flink;

            if (Manager->TrimCursor == &Manager->ProcessList)
                break;

            Process = CONTAINING_RECORD(Manager->TrimCursor, MI_PROCESS, SystemLink);
            Manager->TrimCursor = Manager->TrimCursor->Flink;

            MiProcessUpdatePeaks(Process);
            Resident = MI_ATOMIC_READ64(&Process->Space.ResidentPages);
            Floor = (Pass == 0) ? Process->WorkingSetMinimum : 0;

            if ((ULONG64)Resident <= Floor)
                continue;

            Want = (ULONG)((ULONG64)Resident - Floor);
            if (Want > Manager->AvailableHigh - (Available + Trimmed))
                Want = (ULONG)(Manager->AvailableHigh - (Available + Trimmed));

            Trimmed += MiTrimAddressSpace(&Process->Space, Want, (BOOLEAN)(Pass != 0));
        }
    }

    MI_MUTEX_RELEASE(&Manager->Lock);

    if (Available + Trimmed < Manager->AvailableHigh)
        Trimmed += MiTrimAddressSpace(&System->SystemSpace, (ULONG)(Manager->AvailableHigh - (Available + Trimmed)),
                                      TRUE);

    MI_ATOMIC_ADD64(&Manager->PagesTrimmed, Trimmed);
    MiWriteModifiedPages(System, Trimmed + 64);
    return Trimmed;
}

NTSTATUS
MiCopyVirtualMemory(
    _Inout_ PMI_ADDRESS_SPACE SourceSpace,
    _In_ ULONG64 SourceAddress,
    _Inout_ PMI_ADDRESS_SPACE TargetSpace,
    _In_ ULONG64 TargetAddress,
    _In_ ULONG64 Size,
    _In_ BOOLEAN UserMode,
    _Out_ PULONG64 BytesCopied)
{
    NTSTATUS Status = STATUS_SUCCESS;

    *BytesCopied = 0;

    while (*BytesCopied < Size && NT_SUCCESS(Status))
    {
        ULONG64 Source = SourceAddress + *BytesCopied;
        ULONG64 Target = TargetAddress + *BytesCopied;
        ULONG Chunk = PAGE_SIZE - (ULONG)(Source & (PAGE_SIZE - 1));
        PMI_MDL SourceMdl;
        PMI_MDL TargetMdl;

        if (Chunk > PAGE_SIZE - (ULONG)(Target & (PAGE_SIZE - 1)))
            Chunk = PAGE_SIZE - (ULONG)(Target & (PAGE_SIZE - 1));

        if (Chunk > Size - *BytesCopied)
            Chunk = (ULONG)(Size - *BytesCopied);

        SourceMdl = MiMdlAllocate(SourceSpace, Source, Chunk);
        TargetMdl = MiMdlAllocate(TargetSpace, Target, Chunk);

        if (SourceMdl == NULL || TargetMdl == NULL)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
        }
        else
        {
            Status = MiProbeAndLockPages(SourceMdl, UserMode, FALSE);
            if (NT_SUCCESS(Status))
            {
                Status = MiProbeAndLockPages(TargetMdl, UserMode, TRUE);
                if (NT_SUCCESS(Status))
                {
                    PUCHAR From = MiArchMapFrame(SourceMdl->Frames[0]);
                    PUCHAR To = MiArchMapFrame(TargetMdl->Frames[0]);

                    RtlCopyMemory(To + TargetMdl->ByteOffset, From + SourceMdl->ByteOffset, Chunk);
                    MiArchUnmapFrame(To);
                    MiArchUnmapFrame(From);
                    MiUnlockPages(TargetMdl);
                    *BytesCopied += Chunk;
                }

                MiUnlockPages(SourceMdl);
            }
        }

        if (SourceMdl != NULL)
            MiMdlFree(SourceMdl);

        if (TargetMdl != NULL)
            MiMdlFree(TargetMdl);
    }

    if (!NT_SUCCESS(Status) && *BytesCopied != 0 && Status == STATUS_ACCESS_VIOLATION)
        Status = STATUS_PARTIAL_COPY;

    return Status;
}
