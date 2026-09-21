/*
 * PROJECT:     ReactOS NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/nt/ntfileio.c
 * PURPOSE:     File I/O integration for memory manager backing
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifdef MM_HOST_TEST
#include <mmcc/nvs/core/ntfileshim.h>
#else
#include <nvs/nt/mint.h>
#endif

NTSTATUS
MiControlRead(
    _In_opt_ PVOID Context,
    _In_ ULONG64 Offset,
    _In_ ULONG Length,
    _Out_ PVOID Buffer)
{
    PMI_CONTROL_AREA Control = Context;
    ULONG Transferred;
    NTSTATUS Status;

    ASSERT(Length <= PAGE_SIZE);
    if (Length == 0)
        return STATUS_SUCCESS;

    Status = MiPagingIo(Control->FileObject, Offset, PAGE_SIZE, Buffer, FALSE, &Transferred);

    if (Status == STATUS_END_OF_FILE)
    {
        Status = STATUS_SUCCESS;
        Transferred = 0;
    }

    if (NT_SUCCESS(Status) && Transferred < Length)
        RtlZeroMemory((PUCHAR)Buffer + Transferred, Length - Transferred);

    return Status;
}

NTSTATUS
MiControlWrite(
    _In_opt_ PVOID Context,
    _In_ ULONG64 Offset,
    _In_ ULONG Length,
    _In_ PVOID Buffer)
{
    PMI_CONTROL_AREA Control = Context;
    ULONG Transferred;

    ASSERT(Length <= PAGE_SIZE);
    if (Length == 0)
        return STATUS_SUCCESS;

    return MiPagingIo(Control->FileObject, Offset, PAGE_SIZE, Buffer, TRUE, &Transferred);
}

NTSTATUS
MiControlWriteFrames(
    _In_opt_ PVOID Context,
    _In_ ULONG64 Offset,
    _In_ ULONG Length,
    _In_ const ULONG *Frames,
    _In_ ULONG PageCount)
{
    PMI_CONTROL_AREA Control = Context;
    ULONG Transferred;

    if (PageCount == 0 || PageCount > MI_MAX_FILE_IO_PAGES ||
        Length <= (PageCount - 1) * PAGE_SIZE || Length > PageCount * PAGE_SIZE)
        return STATUS_INVALID_PARAMETER;

    return MiPagingIoFrames(Control->FileObject, Offset, Frames, PageCount, TRUE, &Transferred);
}
