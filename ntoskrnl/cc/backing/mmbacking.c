/*
 * PROJECT:     ReactOS Cache Manager
 * FILE:        ntoskrnl/cc/backing/mmbacking.c
 * PURPOSE:     Memory manager backing for cached file views
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "../api/ccnt.h"

static
NTSTATUS
CcMmMapView(
    _In_ PVOID Context,
    _In_ ULONG64 Offset,
    _In_ SIZE_T Length,
    _Out_ PVOID *Base)
{
    PCC_NT_MAP NtMap = Context;
    PMI_SEGMENT Segment = NtMap->Control->Segment;
    ULONG64 Limit = ((ULONG64)MI_ATOMIC_READ64(&Segment->PageCount) << PAGE_SHIFT);
    ULONG Attempts = 0;
    NTSTATUS Status;

    *Base = NULL;

    if (Offset >= Limit)
        return STATUS_INVALID_VIEW_SIZE;

    do
    {
        ULONG64 Address = 0;
        ULONG64 Size = Length;

        Status = MiMapViewEx(&Segment->System->SystemSpace, Segment, &Address, Offset, &Size, MI_PROT_READWRITE,
                             MI_MEM_RESERVE, ~0ULL, 0, FALSE);
        if (NT_SUCCESS(Status))
            *Base = (PVOID)(ULONG_PTR)Address;
    } while (NT_SUCCESS(MiWaitForMemory(Status, &Attempts)) && Status == STATUS_NO_MEMORY);

    return Status;
}

static
VOID
CcMmUnmapView(
    _In_ PVOID Context,
    _In_ PVOID Base)
{
    PCC_NT_MAP NtMap = Context;

    MiUnmapView(&NtMap->Control->Segment->System->SystemSpace, (ULONG64)(ULONG_PTR)Base);
}

static
BOOLEAN
CcMmIsResident(
    _In_ PVOID Context,
    _In_ ULONG64 Offset,
    _In_ ULONG Length)
{
    PCC_NT_MAP NtMap = Context;

    return MiSegmentIsResident(NtMap->Control->Segment, Offset, Length);
}

static
NTSTATUS
CcMmMakeResident(
    _In_ PVOID Context,
    _In_ ULONG64 Offset,
    _In_ ULONG Length,
    _In_ ULONG64 ValidDataLength)
{
    PCC_NT_MAP NtMap = Context;
    ULONG Attempts = 0;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(ValidDataLength);

    do
    {
        Status = MiSegmentMakeResident(NtMap->Control->Segment, Offset, Length);
    } while (NT_SUCCESS(MiWaitForMemory(Status, &Attempts)) && Status == STATUS_NO_MEMORY);

    return Status;
}

static
NTSTATUS
CcMmMarkDirty(
    _In_ PVOID Context,
    _In_ ULONG64 Offset,
    _In_ ULONG Length)
{
    PCC_NT_MAP NtMap = Context;
    ULONG Attempts = 0;
    NTSTATUS Status;

    do
    {
        Status = MiSegmentMarkDirty(NtMap->Control->Segment, Offset, Length);
    } while (NT_SUCCESS(MiWaitForMemory(Status, &Attempts)) && Status == STATUS_NO_MEMORY);

    return Status;
}

static
NTSTATUS
CcMmFlush(
    _In_ PVOID Context,
    _In_ ULONG64 Offset,
    _In_ ULONG Length)
{
    PCC_NT_MAP NtMap = Context;

    while (Length != 0)
    {
        CC_VIEW_RANGE Range;
        ULONG64 Address;
        ULONG64 Size;
        NTSTATUS Status = CcViewAcquire(&NtMap->Map, Offset, Length, &Range);

        if (!NT_SUCCESS(Status))
            return Status;

        Address = (ULONG64)(ULONG_PTR)Range.Address;
        Size = Range.Length;
        Status = MiFlushVirtualMemory(&NtMap->Control->Segment->System->SystemSpace, &Address, &Size);
        Offset += Range.Length;
        Length -= Range.Length;
        CcViewRelease(&Range);

        if (!NT_SUCCESS(Status))
            return Status;
    }

    return STATUS_SUCCESS;
}

static
BOOLEAN
CcMmPurge(
    _In_ PVOID Context,
    _In_ ULONG64 Offset,
    _In_ ULONG64 Length)
{
    PCC_NT_MAP NtMap = Context;

    return MiSegmentPurge(NtMap->Control->Segment, Offset, Length);
}

static
NTSTATUS
CcMmExtend(
    _In_ PVOID Context,
    _In_ ULONG64 Size)
{
    PCC_NT_MAP NtMap = Context;
    ULONG Attempts = 0;
    NTSTATUS Status;

    do
    {
        Status = MiSegmentExtend(NtMap->Control->Segment, Size);
    } while (NT_SUCCESS(MiWaitForMemory(Status, &Attempts)) && Status == STATUS_NO_MEMORY);

    return Status;
}

static
NTSTATUS
CcMmPrefetch(
    _In_ PVOID Context,
    _In_ ULONG64 Offset,
    _In_ ULONG Length)
{
    PCC_NT_MAP NtMap = Context;

    return MiSegmentPrefetch(NtMap->Control->Segment, Offset, Length);
}

static
NTSTATUS
CcMmMakeViewResident(
    _In_ PVOID Context,
    _In_ PVOID Base,
    _In_ ULONG Length)
{
    ULONG_PTR Address = (ULONG_PTR)Base;
    ULONG_PTR End = Address + Length;
    NTSTATUS Status = STATUS_SUCCESS;

    UNREFERENCED_PARAMETER(Context);

    _SEH2_TRY
    {
        while (Address < End)
        {
            (VOID)*(volatile UCHAR *)Address;
            Address = (Address | (PAGE_SIZE - 1)) + 1;
        }
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    return Status;
}

CC_BACKING_OPS CcNtBackingOps =
{
    CcMmMapView, CcMmUnmapView, CcMmIsResident, CcMmMakeResident, CcMmMarkDirty, CcMmFlush, CcMmPurge, CcMmExtend,
    CcMmPrefetch, CcMmMakeViewResident
};
