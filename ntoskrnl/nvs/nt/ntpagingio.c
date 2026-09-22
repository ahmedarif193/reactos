/*
 * PROJECT:     ReactOS NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/nt/ntpagingio.c
 * PURPOSE:     NT paging I/O integration
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifdef MM_HOST_TEST
#include <mmcc/nvs/core/ntpagingshim.h>
#else
#include <nvs/nt/mint.h>
#endif

typedef struct _MI_ASYNC_READ
{
    WORK_QUEUE_ITEM Work;
    PIRP Irp;
    PFILE_OBJECT File;
    PETHREAD Thread;
    PVOID Buffer;
    MI_READ_COMPLETION Completion;
    PVOID Context;
    MDL Mdl;
    PFN_NUMBER Frame;
} MI_ASYNC_READ, *PMI_ASYNC_READ;

static
VOID
NTAPI
MiAsyncReadWorker(
    _In_ PVOID Context)
{
    PMI_ASYNC_READ Read = Context;
    NTSTATUS Status = Read->Irp->IoStatus.Status;
    ULONG_PTR Transferred = Read->Irp->IoStatus.Information;
    MI_READ_COMPLETION Completion = Read->Completion;
    PVOID CompletionContext = Read->Context;

    if (Status == STATUS_END_OF_FILE)
    {
        Status = STATUS_SUCCESS;
        Transferred = 0;
    }
    if (NT_SUCCESS(Status) && Transferred < PAGE_SIZE)
        RtlZeroMemory((PUCHAR)Read->Buffer + Transferred, PAGE_SIZE - Transferred);
    Read->Irp->MdlAddress = NULL;
    IoFreeIrp(Read->Irp);
    ObDereferenceObject(Read->Thread);
    ObDereferenceObject(Read->File);
    ExFreePoolWithTag(Read, 'rAmM');
    Completion(CompletionContext, Status);
}

static
NTSTATUS
NTAPI
MiAsyncReadComplete(
    _In_ PDEVICE_OBJECT Device,
    _In_ PIRP Irp,
    _In_ PVOID Context)
{
    PMI_ASYNC_READ Read = Context;

    UNREFERENCED_PARAMETER(Device);
    UNREFERENCED_PARAMETER(Irp);
    ExQueueWorkItem(&Read->Work, DelayedWorkQueue);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

NTSTATUS
MiControlReadAsync(
    _In_opt_ PVOID Context,
    _In_ ULONG64 Offset,
    _In_ ULONG Frame,
    _In_ PVOID Buffer,
    _In_ MI_READ_COMPLETION Completion,
    _In_opt_ PVOID CompletionContext)
{
    PMI_CONTROL_AREA Control = Context;
    PDEVICE_OBJECT Device = IoGetRelatedDeviceObject(Control->FileObject);
    PMI_ASYNC_READ Read;
    PIO_STACK_LOCATION Stack;
    KIRQL OldIrql;

    Read = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Read), 'rAmM');
    if (Read == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(Read, sizeof(*Read));
    Read->Irp = IoAllocateIrp(Device->StackSize, FALSE);
    if (Read->Irp == NULL)
    {
        ExFreePoolWithTag(Read, 'rAmM');
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Read->File = Control->FileObject;
    Read->Thread = PsGetCurrentThread();
    Read->Buffer = Buffer;
    Read->Completion = Completion;
    Read->Context = CompletionContext;
    ObReferenceObject(Read->File);
    ObReferenceObject(Read->Thread);
    MmInitializeMdl(&Read->Mdl, Buffer, PAGE_SIZE);
    ASSERT(MmGetMdlPfnArray(&Read->Mdl) == &Read->Frame);
    Read->Frame = Frame;
    Read->Mdl.MdlFlags = MDL_PAGES_LOCKED | MDL_IO_PAGE_READ | MDL_MAPPED_TO_SYSTEM_VA;
    Read->Mdl.MappedSystemVa = Buffer;
    Read->Irp->MdlAddress = &Read->Mdl;
    Read->Irp->UserBuffer = Buffer;
    Read->Irp->RequestorMode = KernelMode;
    Read->Irp->Flags = IRP_PAGING_IO | IRP_NOCACHE | IRP_INPUT_OPERATION;
    Read->Irp->Tail.Overlay.OriginalFileObject = Read->File;
    Read->Irp->Tail.Overlay.Thread = Read->Thread;
    Stack = IoGetNextIrpStackLocation(Read->Irp);
    Stack->MajorFunction = IRP_MJ_READ;
    Stack->FileObject = Read->File;
    Stack->Parameters.Read.Length = PAGE_SIZE;
    Stack->Parameters.Read.ByteOffset.QuadPart = Offset;
    ExInitializeWorkItem(&Read->Work, MiAsyncReadWorker, Read);
    IoSetCompletionRoutine(Read->Irp, MiAsyncReadComplete, Read, TRUE, TRUE, TRUE);
    OldIrql = KeGetCurrentIrql();
    if (OldIrql < APC_LEVEL)
        KeRaiseIrql(APC_LEVEL, &OldIrql);
    IoCallDriver(Device, Read->Irp);
    if (OldIrql < APC_LEVEL)
        KeLowerIrql(OldIrql);
    return STATUS_PENDING;
}

NTSTATUS
MiSubmitPagingMdl(
    _In_ PFILE_OBJECT FileObject,
    _Inout_ PMDL Mdl,
    _In_ ULONG64 Offset,
    _In_ BOOLEAN Write,
    _Out_ PULONG Transferred)
{
    IO_STATUS_BLOCK Iosb = {0};
    LARGE_INTEGER FileOffset;
    KEVENT Event;
    KIRQL OldIrql;
    NTSTATUS Status;

    *Transferred = 0;
    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    FileOffset.QuadPart = (LONGLONG)Offset;
    OldIrql = KeGetCurrentIrql();
    if (!Write && OldIrql < APC_LEVEL)
        KeRaiseIrql(APC_LEVEL, &OldIrql);

    Status = Write ? IoSynchronousPageWrite(FileObject, Mdl, &FileOffset, &Event, &Iosb)
                   : IoPageRead(FileObject, Mdl, &FileOffset, &Event, &Iosb);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, WrPageIn, KernelMode, FALSE, NULL);
        Status = Iosb.Status;
    }

    if (!Write && OldIrql < APC_LEVEL)
        KeLowerIrql(OldIrql);
    if (NT_SUCCESS(Status))
        *Transferred = (ULONG)Iosb.Information;
    return Status;
}

NTSTATUS
MiPagingIoFrames(
    _In_ PFILE_OBJECT FileObject,
    _In_ ULONG64 Offset,
    _In_ const ULONG *Frames,
    _In_ ULONG PageCount,
    _In_ BOOLEAN Write,
    _Out_ PULONG Transferred)
{
    struct
    {
        MDL Mdl;
        PFN_NUMBER Pages[MI_MAX_FILE_WRITE_PAGES];
    } Storage = {0};
    PMDL Mdl = &Storage.Mdl;
    PPFN_NUMBER Pages;
    NTSTATUS Status;
    ULONG i;

    *Transferred = 0;
    if (PageCount == 0 || PageCount > (Write ? MI_MAX_FILE_WRITE_PAGES : MI_MAX_FILE_IO_PAGES) || Frames == NULL ||
        (Offset & (PAGE_SIZE - 1)) != 0)
        return STATUS_INVALID_PARAMETER;

    MmInitializeMdl(Mdl, NULL, PageCount * PAGE_SIZE);
    Pages = MmGetMdlPfnArray(Mdl);
    ASSERT(Pages == Storage.Pages);
    for (i = 0; i < PageCount; i++)
        Pages[i] = Frames[i];
    Mdl->MdlFlags = MDL_PAGES_LOCKED | (Write ? 0 : MDL_IO_PAGE_READ);

    Status = MiSubmitPagingMdl(FileObject, Mdl, Offset, Write, Transferred);
    if (Mdl->MdlFlags & MDL_MAPPED_TO_SYSTEM_VA)
        MmUnmapLockedPages(Mdl->MappedSystemVa, Mdl);
    return Status;
}
