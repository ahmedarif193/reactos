/*
 * PROJECT:     ReactOS host-native tests
 * FILE:        submodules/host-tests/mmcc/nvs/core/t_ntpaging.c
 * PURPOSE:     NT paging I/O host-native regression tests
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "mmharness.h"
#include "ntpagingshim.h"

typedef struct _PAGING_FILE
{
    ULONG Calls;
    ULONG Waits;
    ULONG Unmaps;
    ULONG Length;
    ULONG64 Offset;
    ULONG Information;
    NTSTATUS DispatchStatus;
    NTSTATUS CompletionStatus;
    BOOLEAN Map;
    BOOLEAN Write;
    PMDL PendingMdl;
    PIO_STATUS_BLOCK PendingIosb;
    PFN_NUMBER Frames[MI_MAX_FILE_WRITE_PAGES];
} PAGING_FILE;

static _Thread_local PAGING_FILE *ActivePaging;

VOID
KeInitializeEvent(KEVENT *Event, ULONG Type, BOOLEAN State)
{
    CHECK(Type == NotificationEvent);
    Event->State = State;
    Event->Context = NULL;
}

NTSTATUS
KeWaitForSingleObject(PVOID Object, ULONG Reason, ULONG Mode, BOOLEAN Alertable, PLARGE_INTEGER Timeout)
{
    KEVENT *Event = Object;
    PAGING_FILE *File = Event->Context;

    CHECK(File != NULL && File->PendingMdl != NULL && File->PendingIosb != NULL);
    CHECK(Reason == WrPageIn && Mode == KernelMode && !Alertable && Timeout == NULL);
    CHECK(MiHostIrql == (File->Write ? 0 : APC_LEVEL));
    CHECK(File->PendingMdl->MdlFlags & MDL_PAGES_LOCKED);
    CHECK(memcmp(MmGetMdlPfnArray(File->PendingMdl), File->Frames,
                 sizeof(PFN_NUMBER) * (File->Length / PAGE_SIZE)) == 0);
    File->Waits++;
    File->PendingIosb->Status = File->CompletionStatus;
    File->PendingIosb->Information = File->Information;
    File->PendingMdl = NULL;
    File->PendingIosb = NULL;
    Event->State = TRUE;
    return STATUS_SUCCESS;
}

static
NTSTATUS
Submit(PFILE_OBJECT Object, PMDL Mdl, PLARGE_INTEGER Offset, KEVENT *Event, PIO_STATUS_BLOCK Iosb, BOOLEAN Write)
{
    PAGING_FILE *File = Object->FsContext;

    CHECK(MiHostIrql == (Write ? 0 : APC_LEVEL));
    CHECK(Mdl->Next == NULL && Mdl->Process == NULL);
    CHECK(Mdl->StartVa == NULL && Mdl->MappedSystemVa == NULL && Mdl->ByteOffset == 0);
    CHECK(Mdl->MdlFlags == (MDL_PAGES_LOCKED | (Write ? 0 : MDL_IO_PAGE_READ)));
    CHECK(Mdl->ByteCount > 0 && Mdl->ByteCount <=
          (Write ? MI_MAX_FILE_WRITE_PAGES : MI_MAX_FILE_IO_PAGES) * PAGE_SIZE);
    CHECK(Mdl->Size == sizeof(MDL) + sizeof(PFN_NUMBER) * (Mdl->ByteCount / PAGE_SIZE));
    File->Length = Mdl->ByteCount;
    File->Offset = Offset->QuadPart;
    File->Write = Write;
    File->Calls++;
    memcpy(File->Frames, MmGetMdlPfnArray(Mdl), sizeof(PFN_NUMBER) * (File->Length / PAGE_SIZE));
    if (File->Map)
    {
        Mdl->MappedSystemVa = (PVOID)(ULONG_PTR)0x123000;
        Mdl->MdlFlags |= MDL_MAPPED_TO_SYSTEM_VA;
    }
    ActivePaging = File;
    if (File->DispatchStatus == STATUS_PENDING)
    {
        Event->Context = File;
        File->PendingMdl = Mdl;
        File->PendingIosb = Iosb;
    }
    else
    {
        Iosb->Status = File->CompletionStatus;
        Iosb->Information = File->Information;
    }
    return File->DispatchStatus;
}

NTSTATUS
IoSynchronousPageWrite(PFILE_OBJECT File, PMDL Mdl, PLARGE_INTEGER Offset, KEVENT *Event, PIO_STATUS_BLOCK Iosb)
{
    return Submit(File, Mdl, Offset, Event, Iosb, TRUE);
}

NTSTATUS
IoPageRead(PFILE_OBJECT File, PMDL Mdl, PLARGE_INTEGER Offset, KEVENT *Event, PIO_STATUS_BLOCK Iosb)
{
    return Submit(File, Mdl, Offset, Event, Iosb, FALSE);
}

VOID
MmUnmapLockedPages(PVOID Base, PMDL Mdl)
{
    CHECK(ActivePaging != NULL && ActivePaging->PendingMdl == NULL);
    CHECK(Base == (PVOID)(ULONG_PTR)0x123000 && Base == Mdl->MappedSystemVa);
    CHECK(Mdl->MdlFlags & MDL_MAPPED_TO_SYSTEM_VA);
    Mdl->MappedSystemVa = NULL;
    Mdl->MdlFlags &= ~MDL_MAPPED_TO_SYSTEM_VA;
    ActivePaging->Unmaps++;
}

void
TestNtPaging(void)
{
    PAGING_FILE File = {0};
    FILE_OBJECT Object = { .FsContext = &File };
    MI_CONTROL_AREA Control = { .FileObject = &Object };
    ULONG Frames[MI_MAX_FILE_WRITE_PAGES];
    ULONG Transferred;
    ULONG Count, i, Pending, Map, Write;

    for (i = 0; i < RTL_NUMBER_OF(Frames); i++)
        Frames[i] = 300 + i * 7;

    for (Pending = 0; Pending < 2; Pending++)
    for (Map = 0; Map < 2; Map++)
    for (Write = 0; Write < 2; Write++)
    for (Count = 1; Count <= (Write ? MI_MAX_FILE_WRITE_PAGES : MI_MAX_FILE_IO_PAGES); Count++)
    {
        memset(&File, 0, sizeof(File));
        File.DispatchStatus = Pending ? STATUS_PENDING : STATUS_SUCCESS;
        File.Map = (BOOLEAN)Map;
        File.Information = Count * PAGE_SIZE - 17;
        CHECK(NT_SUCCESS(MiPagingIoFrames(&Object, 3 * PAGE_SIZE, Frames, Count, (BOOLEAN)Write, &Transferred)));
        CHECK(File.Calls == 1 && File.Waits == Pending && File.Unmaps == Map);
        CHECK(File.Offset == 3 * PAGE_SIZE && File.Length == Count * PAGE_SIZE);
        CHECK(Transferred == File.Information && MiHostIrql == 0);
        for (i = 0; i < Count; i++)
            CHECK(File.Frames[i] == Frames[i]);

        File.CompletionStatus = STATUS_UNEXPECTED_IO_ERROR;
        if (!Pending)
            File.DispatchStatus = File.CompletionStatus;
        CHECK(MiPagingIoFrames(&Object, 0, Frames, Count, (BOOLEAN)Write, &Transferred) == STATUS_UNEXPECTED_IO_ERROR);
        CHECK(Transferred == 0 && MiHostIrql == 0);
        CHECK(File.Calls == 2 && File.Waits == 2 * Pending && File.Unmaps == 2 * Map);
    }

    memset(&File, 0, sizeof(File));
    CHECK(NT_SUCCESS(MiControlWriteFrames(&Control, PAGE_SIZE, PAGE_SIZE + 317, Frames, 2)));
    CHECK(File.Calls == 1 && File.Length == 2 * PAGE_SIZE && File.Write);
    CHECK(MiControlWriteFrames(&Control, 0, PAGE_SIZE, Frames, 2) == STATUS_INVALID_PARAMETER);
    CHECK(MiControlWriteFrames(&Control, 0, 2 * PAGE_SIZE, Frames, 1) == STATUS_INVALID_PARAMETER);
    CHECK(MiPagingIoFrames(&Object, 1, Frames, 1, TRUE, &Transferred) == STATUS_INVALID_PARAMETER);
    CHECK(MiPagingIoFrames(&Object, 0, Frames, 0, TRUE, &Transferred) == STATUS_INVALID_PARAMETER);
    CHECK(MiPagingIoFrames(&Object, 0, Frames, MI_MAX_FILE_WRITE_PAGES + 1, TRUE, &Transferred) == STATUS_INVALID_PARAMETER);
    CHECK(MiPagingIoFrames(&Object, 0, Frames, MI_MAX_FILE_IO_PAGES + 1, FALSE, &Transferred) == STATUS_INVALID_PARAMETER);
    CHECK(MiPagingIoFrames(&Object, 0, NULL, 1, TRUE, &Transferred) == STATUS_INVALID_PARAMETER);
    CHECK(File.Calls == 1 && Transferred == 0);

    /* PE raw sections are sector aligned, not necessarily page aligned. */
    memset(&File, 0, sizeof(File));
    File.Information = 2 * PAGE_SIZE;
    CHECK(NT_SUCCESS(MiControlReadPages(&Control, 1024, Frames, 2)));
    CHECK(File.Calls == 1 && File.Offset == 1024 && File.Length == 2 * PAGE_SIZE && !File.Write);
    File.Information--;
    CHECK(MiControlReadPages(&Control, 1024, Frames, 2) == STATUS_END_OF_FILE);
    CHECK(MiPagingIoFrames(&Object, 1024, Frames, 2, TRUE, &Transferred) == STATUS_INVALID_PARAMETER);
    CHECK(MiPagingIoFrames(&Object, 1025, Frames, 2, FALSE, &Transferred) == STATUS_INVALID_PARAMETER);
    ActivePaging = NULL;
}
