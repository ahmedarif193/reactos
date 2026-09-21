/*
 * PROJECT:     ReactOS host-native tests
 * FILE:        submodules/host-tests/mmcc/nvs/core/ntpagingshim.h
 * PURPOSE:     NT paging I/O host-test compatibility definitions
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once

#include "ntfileshim.h"

typedef ULONG_PTR PFN_NUMBER, *PPFN_NUMBER;
typedef struct _MDL
{
    struct _MDL *Next;
    short Size;
    short MdlFlags;
    PVOID Process;
    PVOID MappedSystemVa;
    PVOID StartVa;
    ULONG ByteCount;
    ULONG ByteOffset;
} MDL, *PMDL;

C_ASSERT(sizeof(MDL) == 48);
#define MDL_MAPPED_TO_SYSTEM_VA 1
#define MDL_PAGES_LOCKED 2
#define MDL_SOURCE_IS_NONPAGED_POOL 4
#define MDL_IO_PAGE_READ 0x40
#define APC_LEVEL 1
#define NotificationEvent 0
#define WrPageIn 9
#define MmGetMdlPfnArray(Mdl) ((PPFN_NUMBER)((Mdl) + 1))
#define KeGetCurrentIrql() MiHostIrql
#define KeRaiseIrql(Level, Old) (*(Old) = MiHostRaiseIrql(Level))
#define KeLowerIrql(Level) MiHostLowerIrql(Level)

static inline void MmInitializeMdl(PMDL Mdl, PVOID Base, ULONG Length)
{
    ULONG Offset = (ULONG)((ULONG_PTR)Base & (PAGE_SIZE - 1));

    Mdl->Next = NULL;
    Mdl->Size = (short)(sizeof(MDL) + sizeof(PFN_NUMBER) * ((Offset + Length + PAGE_SIZE - 1) / PAGE_SIZE));
    Mdl->MdlFlags = 0;
    Mdl->StartVa = (PVOID)((ULONG_PTR)Base & ~(ULONG_PTR)(PAGE_SIZE - 1));
    Mdl->ByteOffset = Offset;
    Mdl->ByteCount = Length;
}

VOID KeInitializeEvent(KEVENT *Event, ULONG Type, BOOLEAN State);
NTSTATUS KeWaitForSingleObject(PVOID Object, ULONG Reason, ULONG Mode, BOOLEAN Alertable, PLARGE_INTEGER Timeout);
NTSTATUS IoSynchronousPageWrite(PFILE_OBJECT File, PMDL Mdl, PLARGE_INTEGER Offset, KEVENT *Event, PIO_STATUS_BLOCK Iosb);
NTSTATUS IoPageRead(PFILE_OBJECT File, PMDL Mdl, PLARGE_INTEGER Offset, KEVENT *Event, PIO_STATUS_BLOCK Iosb);
VOID MmUnmapLockedPages(PVOID Base, PMDL Mdl);
NTSTATUS MiSubmitPagingMdl(PFILE_OBJECT File, PMDL Mdl, ULONG64 Offset, BOOLEAN Write, PULONG Transferred);

#define IRP_PAGING_IO 2
#define IRP_NOCACHE 1
#define IRP_INPUT_OPERATION 0x40
#define IRP_MJ_READ 3
#define DelayedWorkQueue 1
#define STATUS_MORE_PROCESSING_REQUIRED ((NTSTATUS)0xC0000016)

typedef struct _DEVICE_OBJECT { UCHAR StackSize; } DEVICE_OBJECT, *PDEVICE_OBJECT;
typedef struct _IO_STACK_LOCATION
{
    UCHAR MajorFunction;
    PFILE_OBJECT FileObject;
    struct { struct { ULONG Length; LARGE_INTEGER ByteOffset; } Read; } Parameters;
} IO_STACK_LOCATION, *PIO_STACK_LOCATION;
typedef struct _IRP
{
    PMDL MdlAddress;
    PVOID UserBuffer;
    ULONG RequestorMode;
    ULONG Flags;
    struct { struct { PFILE_OBJECT OriginalFileObject; PETHREAD Thread; } Overlay; } Tail;
    IO_STATUS_BLOCK IoStatus;
    IO_STACK_LOCATION Stack;
    NTSTATUS (*Completion)(PDEVICE_OBJECT, struct _IRP *, PVOID);
    PVOID Context;
} IRP, *PIRP;
typedef struct _WORK_QUEUE_ITEM
{
    VOID (*Routine)(PVOID);
    PVOID Context;
    struct _WORK_QUEUE_ITEM *Next;
} WORK_QUEUE_ITEM, *PWORK_QUEUE_ITEM;

PDEVICE_OBJECT IoGetRelatedDeviceObject(PFILE_OBJECT File);
PIRP IoAllocateIrp(UCHAR StackSize, BOOLEAN ChargeQuota);
VOID IoFreeIrp(PIRP Irp);
PETHREAD PsGetCurrentThread(VOID);
VOID ObReferenceObject(PVOID Object);
NTSTATUS IoCallDriver(PDEVICE_OBJECT Device, PIRP Irp);
VOID ExQueueWorkItem(PWORK_QUEUE_ITEM Work, ULONG Queue);
#define IoGetNextIrpStackLocation(Irp) (&(Irp)->Stack)
#define ExInitializeWorkItem(Work, Function, Argument) do { (Work)->Routine = (Function); (Work)->Context = (Argument); } while (0)
#define IoSetCompletionRoutine(Irp, Function, Argument, Success, Error, Cancel) do { \
    (Irp)->Completion = (Function); (Irp)->Context = (Argument); } while (0)
NTSTATUS MiControlReadAsync(PVOID Context, ULONG64 Offset, ULONG Frame, PVOID Buffer,
                            MI_READ_COMPLETION Completion, PVOID CompletionContext);
