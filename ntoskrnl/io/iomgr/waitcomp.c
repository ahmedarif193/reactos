/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 Ahmed ARIF
 *
 * Wait-completion associations, based on the public Nt* contracts and native
 * Windows 11 black-box tests. No worker thread blocks on the target object.
 */
#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

#define WAIT_PACKET_ACCESS 1
#define TAG_WAIT_PACKET 'pWcI'

C_ASSERT(WaitDpc != WaitAll && WaitDpc != WaitAny && WaitDpc != WaitNotification && WaitDpc != WaitDequeue);

typedef struct _IOP_WAIT_ASSOCIATION IOP_WAIT_ASSOCIATION, *PIOP_WAIT_ASSOCIATION;

typedef struct _IOP_WAIT_COMPLETION_PACKET
{
    FAST_MUTEX ApiLock;
    KSPIN_LOCK StateLock;
    PIOP_WAIT_ASSOCIATION Association;
} IOP_WAIT_COMPLETION_PACKET, *PIOP_WAIT_COMPLETION_PACKET;

struct _IOP_WAIT_ASSOCIATION
{
    IOP_MINI_COMPLETION_PACKET Completion;
    KWAIT_BLOCK WaitBlock;
    KDPC DeliveryDpc;
    KEVENT DeliveryDone;
    volatile LONG References;
    PIOP_WAIT_COMPLETION_PACKET Packet;
    PKQUEUE Port;
    PVOID Target;
    PDISPATCHER_HEADER WaitObject;
};

static VOID
IopDereferenceWaitAssociation(PIOP_WAIT_ASSOCIATION Association)
{
    if (InterlockedDecrement(&Association->References)) return;
    ObDereferenceObjectDeferDelete(Association->Target);
    ObDereferenceObjectDeferDelete(Association->Port);
    ObDereferenceObjectDeferDelete(Association->Packet);
    ExFreePoolWithTag(Association, TAG_WAIT_PACKET);
}

/* The caller owns a separate reference. Detaching makes the handle reusable. */
static VOID
IopDetachWaitAssociation(PIOP_WAIT_ASSOCIATION Association)
{
    PIOP_WAIT_COMPLETION_PACKET Packet = Association->Packet;
    KIRQL OldIrql;
    BOOLEAN Detached = FALSE;

    KeAcquireSpinLock(&Packet->StateLock, &OldIrql);
    if (Packet->Association == Association)
    {
        Packet->Association = NULL;
        Detached = TRUE;
    }
    KeReleaseSpinLock(&Packet->StateLock, OldIrql);
    if (Detached) IopDereferenceWaitAssociation(Association);
}

VOID NTAPI
IopReleaseWaitCompletionPacket(PIOP_MINI_COMPLETION_PACKET Completion)
{
    PIOP_WAIT_ASSOCIATION Association = CONTAINING_RECORD(Completion, IOP_WAIT_ASSOCIATION, Completion);
    IopDetachWaitAssociation(Association);
    IopDereferenceWaitAssociation(Association); /* queue ownership */
}

static VOID NTAPI
IopDeliverWaitCompletion(PKDPC Dpc, PVOID Context, PVOID Argument1, PVOID Argument2)
{
    PIOP_WAIT_ASSOCIATION Association = Context;
    PKQUEUE Port = Association->Port;
    KIRQL OldIrql;
    BOOLEAN Abandoned;
    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(Argument1);
    UNREFERENCED_PARAMETER(Argument2);

    /* Do not nest two dispatcher-object locks: the target may itself be used
     * by another association. Delivery owns its own reference and storage. */
    OldIrql = KeRaiseIrqlToSynchLevel();
    KiAcquireDispatcherObject(&Port->Header);
    Abandoned = Port->Header.Abandoned;
    if (!Abandoned)
    {
        InterlockedIncrement(&Association->References); /* queue ownership */
        KiInsertQueue(Port, &Association->Completion.ListEntry, FALSE);
    }
    KiReleaseDispatcherObject(&Port->Header);
    KiExitDispatcher(OldIrql);
    if (Abandoned) IopDetachWaitAssociation(Association);
    KeSetEvent(&Association->DeliveryDone, IO_NO_INCREMENT, FALSE);
    IopDereferenceWaitAssociation(Association); /* DPC ownership */
}

/* Called with the target dispatcher object locked at SYNCH_LEVEL. */
VOID FASTCALL
IopSignalWaitCompletionPacket(PKWAIT_BLOCK WaitBlock)
{
    PIOP_WAIT_ASSOCIATION Association = CONTAINING_RECORD(WaitBlock, IOP_WAIT_ASSOCIATION, WaitBlock);
    BOOLEAN Inserted;

    ASSERT(WaitBlock->WaitType == WaitDpc);

    RemoveEntryList(&WaitBlock->WaitListEntry);
    WaitBlock->WaitListEntry.Flink = NULL;
    WaitBlock->BlockState = WaitBlockInactive;
    KiSatisfyNonMutantWait((PKMUTANT)Association->WaitObject);
    InterlockedIncrement(&Association->References);
    Inserted = KeInsertQueueDpc(&Association->DeliveryDpc, NULL, NULL);
    ASSERT(Inserted);
    UNREFERENCED_PARAMETER(Inserted);
}

static NTSTATUS
IopCancelWaitAssociation(PIOP_WAIT_COMPLETION_PACKET Packet, BOOLEAN RemoveSignaledPacket)
{
    PIOP_WAIT_ASSOCIATION Association;
    PLIST_ENTRY Entry;
    KIRQL OldIrql;
    BOOLEAN Removed = FALSE;
    NTSTATUS Status = STATUS_CANCELLED;

    ExAcquireFastMutex(&Packet->ApiLock);
    KeAcquireSpinLock(&Packet->StateLock, &OldIrql);
    Association = Packet->Association;
    if (Association) InterlockedIncrement(&Association->References);
    KeReleaseSpinLock(&Packet->StateLock, OldIrql);
    if (!Association) goto Done;

    OldIrql = KeRaiseIrqlToSynchLevel();
    KiAcquireDispatcherObject(Association->WaitObject);
    if (Association->WaitBlock.WaitListEntry.Flink)
    {
        RemoveEntryList(&Association->WaitBlock.WaitListEntry);
        Association->WaitBlock.WaitListEntry.Flink = NULL;
        Association->WaitBlock.BlockState = WaitBlockInactive;
        Removed = TRUE;
    }
    KiReleaseDispatcherObject(Association->WaitObject);
    KiExitDispatcher(OldIrql);
    if (Removed)
    {
        IopDetachWaitAssociation(Association);
        Status = STATUS_SUCCESS;
    }
    else if (!RemoveSignaledPacket)
    {
        Status = STATUS_PENDING;
    }
    else
    {
        /* The target signaled. Wait only for the bounded delivery DPC, never
         * for the target. The packet remains referenced across this wait. */
        KeWaitForSingleObject(&Association->DeliveryDone, Executive, KernelMode, FALSE, NULL);
        OldIrql = KeRaiseIrqlToSynchLevel();
        KiAcquireDispatcherObject(&Association->Port->Header);
        if (!Association->Port->Header.Abandoned)
        {
            for (Entry = Association->Port->EntryListHead.Flink; Entry != &Association->Port->EntryListHead; Entry = Entry->Flink)
            {
                if (Entry != &Association->Completion.ListEntry) continue;
                RemoveEntryList(Entry);
                Entry->Flink = NULL;
                Association->Port->Header.SignalState--;
                Removed = TRUE;
                break;
            }
        }
        KiReleaseDispatcherObject(&Association->Port->Header);
        KiExitDispatcher(OldIrql);
        if (Removed)
        {
            IopReleaseWaitCompletionPacket(&Association->Completion);
            Status = STATUS_SUCCESS;
        }
        else Status = STATUS_PENDING;
    }
    IopDereferenceWaitAssociation(Association); /* this call's reference */
Done:
    ExReleaseFastMutex(&Packet->ApiLock);
    return Status;
}

static VOID NTAPI
IopCloseWaitCompletionPacket(PEPROCESS Process, PVOID Object, ULONG_PTR ProcessHandleCount, ULONG_PTR SystemHandleCount)
{
    UNREFERENCED_PARAMETER(Process);
    UNREFERENCED_PARAMETER(ProcessHandleCount);
    if (SystemHandleCount == 1) IopCancelWaitAssociation(Object, TRUE);
}

BOOLEAN NTAPI
IopInitializeWaitCompletionPacketType(VOID)
{
    OBJECT_TYPE_INITIALIZER Initializer = {0};
    UNICODE_STRING Name = RTL_CONSTANT_STRING(L"WaitCompletionPacket");

    Initializer.Length = sizeof(Initializer);
    Initializer.DefaultNonPagedPoolCharge = sizeof(IOP_WAIT_COMPLETION_PACKET);
    Initializer.GenericMapping.GenericRead = STANDARD_RIGHTS_READ | WAIT_PACKET_ACCESS;
    Initializer.GenericMapping.GenericWrite = STANDARD_RIGHTS_WRITE | WAIT_PACKET_ACCESS;
    Initializer.GenericMapping.GenericExecute = STANDARD_RIGHTS_EXECUTE | WAIT_PACKET_ACCESS;
    Initializer.GenericMapping.GenericAll = STANDARD_RIGHTS_REQUIRED | WAIT_PACKET_ACCESS;
    Initializer.PoolType = NonPagedPool;
    Initializer.ValidAccessMask = STANDARD_RIGHTS_REQUIRED | SYNCHRONIZE | WAIT_PACKET_ACCESS;
    Initializer.InvalidAttributes = OBJ_OPENLINK;
    Initializer.UseDefaultObject = TRUE;
    Initializer.CloseProcedure = IopCloseWaitCompletionPacket;
    return NT_SUCCESS(ObCreateObjectType(&Name, &Initializer, NULL, &IopWaitCompletionPacketType));
}

NTSTATUS NTAPI
NtCreateWaitCompletionPacket(PHANDLE WaitCompletionPacketHandle, ACCESS_MASK DesiredAccess, POBJECT_ATTRIBUTES ObjectAttributes)
{
    PIOP_WAIT_COMPLETION_PACKET Packet;
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    HANDLE Handle;
    NTSTATUS Status;
    PAGED_CODE();

    _SEH2_TRY
    {
        if (PreviousMode != KernelMode) ProbeForWriteHandle(WaitCompletionPacketHandle);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        _SEH2_YIELD(return _SEH2_GetExceptionCode());
    }
    _SEH2_END;
    Status = ObCreateObject(PreviousMode, IopWaitCompletionPacketType, ObjectAttributes, PreviousMode, NULL, sizeof(*Packet), 0, 0, (PVOID *)&Packet);
    if (!NT_SUCCESS(Status)) return Status;
    RtlZeroMemory(Packet, sizeof(*Packet));
    ExInitializeFastMutex(&Packet->ApiLock);
    KeInitializeSpinLock(&Packet->StateLock);
    Status = ObInsertObject(Packet, NULL, DesiredAccess, 0, NULL, &Handle);
    if (!NT_SUCCESS(Status)) return Status;
    _SEH2_TRY
    {
        *WaitCompletionPacketHandle = Handle;
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
        ObCloseHandle(Handle, PreviousMode);
    }
    _SEH2_END;
    return Status;
}

NTSTATUS NTAPI
NtAssociateWaitCompletionPacket(HANDLE WaitCompletionPacketHandle, HANDLE IoCompletionHandle, HANDLE TargetObjectHandle, PVOID KeyContext, PVOID ApcContext, NTSTATUS IoStatus, ULONG_PTR IoStatusInformation, PBOOLEAN AlreadySignaled)
{
    PIOP_WAIT_COMPLETION_PACKET Packet;
    PIOP_WAIT_ASSOCIATION Association;
    PVOID Target;
    PKQUEUE Port;
    PDISPATCHER_HEADER WaitObject;
    KPROCESSOR_MODE PreviousMode = ExGetPreviousMode();
    KIRQL OldIrql;
    NTSTATUS Status;
    BOOLEAN Signaled;
    ULONG Type;
    PAGED_CODE();

    Status = ObReferenceObjectByHandle(WaitCompletionPacketHandle, WAIT_PACKET_ACCESS, IopWaitCompletionPacketType, PreviousMode, (PVOID *)&Packet, NULL);
    if (!NT_SUCCESS(Status)) return Status;
    ExAcquireFastMutex(&Packet->ApiLock);
    KeAcquireSpinLock(&Packet->StateLock, &OldIrql);
    Status = Packet->Association ? STATUS_INVALID_PARAMETER_1 : STATUS_SUCCESS;
    KeReleaseSpinLock(&Packet->StateLock, OldIrql);
    if (!NT_SUCCESS(Status)) goto Done;
    Status = ObReferenceObjectByHandle(IoCompletionHandle, IO_COMPLETION_MODIFY_STATE, IoCompletionType, PreviousMode, (PVOID *)&Port, NULL);
    if (!NT_SUCCESS(Status)) goto Done;
    Status = ObReferenceObjectByHandle(TargetObjectHandle, SYNCHRONIZE, NULL, PreviousMode, &Target, NULL);
    if (!NT_SUCCESS(Status)) goto ReleasePort;
    WaitObject = ObpGetObjectWaitObject(ObGetObjectType(Target), Target);
    if (!WaitObject)
    {
        Status = STATUS_INVALID_PARAMETER_3;
        goto ReleaseTarget;
    }
    Type = WaitObject->Type & KOBJECT_TYPE_MASK;
    if (Type != EventNotificationObject && Type != EventSynchronizationObject && Type != TimerNotificationObject && Type != TimerSynchronizationObject && Type != SemaphoreObject && Type != ProcessObject && Type != ThreadObject)
    {
        Status = STATUS_INVALID_PARAMETER_3;
        goto ReleaseTarget;
    }
    Association = ExAllocatePoolZero(NonPagedPool, sizeof(*Association), TAG_WAIT_PACKET);
    if (!Association)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto ReleaseTarget;
    }
    Association->References = 2; /* handle association and this registration */
    Association->Packet = Packet;
    Association->Port = Port;
    Association->Target = Target;
    Association->WaitObject = WaitObject;
    Association->Completion.PacketType = IopCompletionPacketWait;
    Association->Completion.KeyContext = KeyContext;
    Association->Completion.ApcContext = ApcContext;
    Association->Completion.IoStatus = IoStatus;
    Association->Completion.IoStatusInformation = IoStatusInformation;
    Association->WaitBlock.Object = WaitObject;
    Association->WaitBlock.WaitType = WaitDpc;
    Association->WaitBlock.BlockState = WaitBlockActive;
    KeInitializeDpc(&Association->DeliveryDpc, IopDeliverWaitCompletion, Association);
    KeInitializeEvent(&Association->DeliveryDone, NotificationEvent, FALSE);
    ObReferenceObject(Packet);
    KeAcquireSpinLock(&Packet->StateLock, &OldIrql);
    Packet->Association = Association;
    KeReleaseSpinLock(&Packet->StateLock, OldIrql);

    OldIrql = KeRaiseIrqlToSynchLevel();
    KiAcquireDispatcherObject(WaitObject);
    InsertTailList(&WaitObject->WaitListHead, &Association->WaitBlock.WaitListEntry);
    Signaled = WaitObject->SignalState > 0;
    if (Signaled) IopSignalWaitCompletionPacket(&Association->WaitBlock);
    KiReleaseDispatcherObject(WaitObject);
    KiExitDispatcher(OldIrql);
    /* Native keeps the association if this optional copy faults. Never write
     * a user-supplied kernel address, even inside an exception handler. */
    _SEH2_TRY
    {
        if (AlreadySignaled)
        {
            if (PreviousMode != KernelMode) ProbeForWrite(AlreadySignaled, sizeof(*AlreadySignaled), 1);
            *AlreadySignaled = Signaled;
        }
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        /* Optional output faults do not roll back native associations. */
    }
    _SEH2_END;
    IopDereferenceWaitAssociation(Association);
    Status = STATUS_SUCCESS;
    goto Done;
ReleaseTarget:
    ObDereferenceObject(Target);
ReleasePort:
    ObDereferenceObject(Port);
Done:
    ExReleaseFastMutex(&Packet->ApiLock);
    ObDereferenceObject(Packet);
    return Status;
}

NTSTATUS NTAPI
NtCancelWaitCompletionPacket(HANDLE WaitCompletionPacketHandle, BOOLEAN RemoveSignaledPacket)
{
    PIOP_WAIT_COMPLETION_PACKET Packet;
    NTSTATUS Status;
    PAGED_CODE();

    Status = ObReferenceObjectByHandle(WaitCompletionPacketHandle, WAIT_PACKET_ACCESS, IopWaitCompletionPacketType, ExGetPreviousMode(), (PVOID *)&Packet, NULL);
    if (!NT_SUCCESS(Status)) return Status;
    Status = IopCancelWaitAssociation(Packet, RemoveSignaledPacket);
    ObDereferenceObject(Packet);
    return Status;
}
