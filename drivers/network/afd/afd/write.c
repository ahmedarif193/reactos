/*
 * COPYRIGHT:        See COPYING in the top level directory
 * PROJECT:          ReactOS kernel
 * FILE:             drivers/net/afd/afd/write.c
 * PURPOSE:          Ancillary functions driver
 * PROGRAMMER:       Art Yerkes (ayerkes@speakeasy.net)
 * UPDATE HISTORY:
 * 20040708 Created
 */

#include "afd.h"
#include <tdikrnl.h>

static IO_COMPLETION_ROUTINE SendComplete;

static NTSTATUS
NTAPI
AfdBufferedSendComplete(PDEVICE_OBJECT DeviceObject, PIRP Irp, PVOID Context)
{
    /* Release the transport resources before SendComplete can signal FCB
     * rundown and allow the socket's paged send window to be freed. */
    MmUnlockPages(Irp->MdlAddress);
    IoFreeMdl(Irp->MdlAddress);
    Irp->MdlAddress = NULL;
    SendComplete(DeviceObject, Irp, Context);
    IoFreeIrp(Irp);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

static NTSTATUS
AfdStartBufferedSend(PAFD_FCB FCB)
{
    PDEVICE_OBJECT DeviceObject;
    PIRP Irp;
    PMDL Mdl;
    NTSTATUS Status = STATUS_SUCCESS;

    ASSERT(!FCB->SendIrp.InFlightRequest);
    ASSERT(FCB->Send.BytesUsed);
    DeviceObject = IoGetRelatedDeviceObject(FCB->Connection.Object);

    /* IoBuildDeviceIoControlRequest associates its IRP with the calling
     * thread. An accepted buffered send must survive that thread's exit. */
    Irp = IoAllocateIrp(DeviceObject->StackSize, FALSE);
    if (!Irp)
        return STATUS_INSUFFICIENT_RESOURCES;

    Mdl = IoAllocateMdl(FCB->Send.Window, FCB->Send.BytesUsed, FALSE, FALSE, Irp);
    if (!Mdl)
    {
        IoFreeIrp(Irp);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    _SEH2_TRY
    {
        MmProbeAndLockPages(Mdl, KernelMode, IoReadAccess);
    }
    _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER)
    {
        Status = _SEH2_GetExceptionCode();
    }
    _SEH2_END;

    if (!NT_SUCCESS(Status))
    {
        IoFreeMdl(Mdl);
        IoFreeIrp(Irp);
        return Status;
    }

    Irp->RequestorMode = KernelMode;
    TdiBuildSend(Irp, DeviceObject, FCB->Connection.Object, AfdBufferedSendComplete, FCB, Mdl, 0, FCB->Send.BytesUsed);
    FCB->SendIrp.InFlightRequest = Irp;
    return IoCallDriver(DeviceObject, Irp);
}

/* The send window owns buffered bytes independently of the caller's IRP. */
static VOID
AfdFailSendQueue(PAFD_FCB FCB, NTSTATUS Status)
{
    PLIST_ENTRY Entry;
    PIRP Irp;
    PIO_STACK_LOCATION IrpSp;
    PAFD_SEND_INFO SendReq;

    FCB->Send.BytesUsed = 0;
    FCB->PollState &= ~AFD_EVENT_SEND;
    FCB->PollState |= AFD_EVENT_ABORT;
    FCB->PollStatus[FD_CLOSE_BIT] = Status;

    while (!IsListEmpty(&FCB->PendingIrpList[FUNCTION_SEND]))
    {
        Entry = RemoveHeadList(&FCB->PendingIrpList[FUNCTION_SEND]);
        Irp = CONTAINING_RECORD(Entry, IRP, Tail.Overlay.ListEntry);
        IrpSp = IoGetCurrentIrpStackLocation(Irp);
        SendReq = GetLockedData(Irp, IrpSp);
        Irp->IoStatus.Status = Status;
        Irp->IoStatus.Information = 0;
        (void)IoSetCancelRoutine(Irp, NULL);
        UnlockBuffers(SendReq->BufferArray, SendReq->BufferCount, FALSE);
        if (Irp->MdlAddress) UnlockRequest(Irp, IrpSp);
        IoCompleteRequest(Irp, IO_NETWORK_INCREMENT);
    }

    PollReeval(FCB->DeviceExt, FCB->FileObject);
}

static NTSTATUS NTAPI SendComplete
( PDEVICE_OBJECT DeviceObject,
  PIRP Irp,
  PVOID Context ) {
    NTSTATUS Status = Irp->IoStatus.Status;
    PAFD_FCB FCB = (PAFD_FCB)Context;
    PLIST_ENTRY NextIrpEntry, Entry;
    PIRP NextIrp = NULL;
    PIO_STACK_LOCATION NextIrpSp;
    PAFD_SEND_INFO SendReq = NULL;
    PAFD_MAPBUF Map;
    SIZE_T TotalBytesCopied = 0, CompletionOffset, SpaceAvail, i;
    UINT SendLength, BytesCopied;

    UNREFERENCED_PARAMETER(DeviceObject);

    /*
     * The Irp parameter passed in is the IRP of the stream between AFD and
     * TDI driver. It's not very useful to us. We need the IRPs of the stream
     * between usermode and AFD. Those are chained from
     * FCB->PendingIrpList[FUNCTION_SEND] and you'll see them in the code
     * below as "NextIrp" ('cause they are the next usermode IRP to be
     * processed).
     */

    AFD_DbgPrint(MID_TRACE,("Called, status %x, %u bytes used\n",
                            Irp->IoStatus.Status,
                            Irp->IoStatus.Information));

    if( !SocketAcquireStateLock( FCB ) )
        return STATUS_FILE_CLOSED;

    ASSERT(FCB->SendIrp.InFlightRequest == Irp);
    FCB->SendIrp.InFlightRequest = NULL;
    /* Request is not in flight any longer */

    if( FCB->SharedData.State == SOCKET_STATE_CLOSED ) {
        /* Cleanup our IRP queue because the FCB is being destroyed */
        while( !IsListEmpty( &FCB->PendingIrpList[FUNCTION_SEND] ) ) {
            NextIrpEntry = RemoveHeadList(&FCB->PendingIrpList[FUNCTION_SEND]);
            NextIrp = CONTAINING_RECORD(NextIrpEntry, IRP, Tail.Overlay.ListEntry);
            NextIrpSp = IoGetCurrentIrpStackLocation( NextIrp );
            SendReq = GetLockedData(NextIrp, NextIrpSp);
            NextIrp->IoStatus.Status = STATUS_FILE_CLOSED;
            NextIrp->IoStatus.Information = 0;
            UnlockBuffers(SendReq->BufferArray, SendReq->BufferCount, FALSE);
            if( NextIrp->MdlAddress ) UnlockRequest( NextIrp, IoGetCurrentIrpStackLocation( NextIrp ) );
            (void)IoSetCancelRoutine(NextIrp, NULL);
            IoCompleteRequest( NextIrp, IO_NETWORK_INCREMENT );
        }

        RetryDisconnectCompletion(FCB);

        if (FCB->TdiRundownEvent) AfdSignalTdiRundown(FCB);
        SocketStateUnlock( FCB );
        return STATUS_FILE_CLOSED;
    }

    if( !NT_SUCCESS(Status) ) {
        AfdFailSendQueue(FCB, Status);
        RetryDisconnectCompletion(FCB);

        if (FCB->TdiRundownEvent) AfdSignalTdiRundown(FCB);
        SocketStateUnlock( FCB );

        return STATUS_SUCCESS;
    }

    ASSERT(Irp->IoStatus.Information <= FCB->Send.BytesUsed);
    SendLength = Irp->IoStatus.Information;
    FCB->Send.BytesUsed -= SendLength;
    RtlMoveMemory(FCB->Send.Window, FCB->Send.Window + SendLength, FCB->Send.BytesUsed);

    /* DriverContext[3] is the end of this IRP's data relative to the start
     * of the send window, or zero if it has not been buffered yet. Update
     * every buffered IRP by the same consumed byte count. Nonblocking sends
     * and cancelled IRPs may have data in the window without a queued IRP. */
    Entry = FCB->PendingIrpList[FUNCTION_SEND].Flink;
    while (Entry != &FCB->PendingIrpList[FUNCTION_SEND]) {
        NextIrpEntry = Entry;
        Entry = Entry->Flink;
        NextIrp = CONTAINING_RECORD(NextIrpEntry, IRP, Tail.Overlay.ListEntry);
        CompletionOffset = (ULONG_PTR)NextIrp->Tail.Overlay.DriverContext[3];
        if (!CompletionOffset)
            break;

        if (CompletionOffset > SendLength)
        {
            NextIrp->Tail.Overlay.DriverContext[3] = (PVOID)(CompletionOffset - SendLength);
            continue;
        }

        RemoveEntryList(NextIrpEntry);
        NextIrpSp = IoGetCurrentIrpStackLocation( NextIrp );
        SendReq = GetLockedData(NextIrp, NextIrpSp);

        ASSERT(NextIrp->IoStatus.Information != 0);

        NextIrp->IoStatus.Status = Irp->IoStatus.Status;

        (void)IoSetCancelRoutine(NextIrp, NULL);

        UnlockBuffers( SendReq->BufferArray,
                       SendReq->BufferCount,
                       FALSE );

        if (NextIrp->MdlAddress) UnlockRequest(NextIrp, NextIrpSp);

        IoCompleteRequest(NextIrp, IO_NETWORK_INCREMENT);
    }

   if (!IsListEmpty(&FCB->PendingIrpList[FUNCTION_SEND])) {
        NextIrpEntry = FCB->PendingIrpList[FUNCTION_SEND].Flink;
        NextIrp = CONTAINING_RECORD(NextIrpEntry, IRP, Tail.Overlay.ListEntry);
        NextIrpSp = IoGetCurrentIrpStackLocation( NextIrp );
        SendReq = GetLockedData(NextIrp, NextIrpSp);
        Map = (PAFD_MAPBUF)(SendReq->BufferArray + SendReq->BufferCount);

        /* A partially consumed IRP is already in the window. */
        if (NextIrp->Tail.Overlay.DriverContext[3])
            NextIrp = NULL;

        AFD_DbgPrint(MID_TRACE,("SendReq @ %p\n", SendReq));

        SpaceAvail = FCB->Send.Size - FCB->Send.BytesUsed;
        TotalBytesCopied = 0;

        /* Count the total transfer size */
        SendLength = 0;
        for (i = 0; i < SendReq->BufferCount; i++)
        {
            SendLength += SendReq->BufferArray[i].len;
        }

        /* Make sure we've got the space */
        if (SendLength > SpaceAvail)
        {
           /* Blocking sockets have to wait here */
           if (SendLength <= FCB->Send.Size && !((SendReq->AfdFlags & AFD_IMMEDIATE) || (FCB->NonBlocking)))
           {
               FCB->PollState &= ~AFD_EVENT_SEND;

               NextIrp = NULL;
           }

           /* Check if we can send anything */
           if (SpaceAvail == 0)
           {
               FCB->PollState &= ~AFD_EVENT_SEND;

               /* Blocking and overlapped requests wait for buffer space. */
               NextIrp = NULL;
           }
        }

        if (NextIrp != NULL)
        {
            for (i = 0; SpaceAvail > 0 && i < SendReq->BufferCount; i++) {
                BytesCopied = MIN(SendReq->BufferArray[i].len, SpaceAvail);
                if (!BytesCopied)
                    continue;

                Map[i].BufferAddress =
                   MmMapLockedPages( Map[i].Mdl, KernelMode );

                RtlCopyMemory( FCB->Send.Window + FCB->Send.BytesUsed,
                               Map[i].BufferAddress,
                               BytesCopied );

                MmUnmapLockedPages( Map[i].BufferAddress, Map[i].Mdl );

                TotalBytesCopied += BytesCopied;
                SpaceAvail -= BytesCopied;
                FCB->Send.BytesUsed += BytesCopied;
            }

            NextIrp->IoStatus.Information = TotalBytesCopied;
            NextIrp->Tail.Overlay.DriverContext[3] = (PVOID)(ULONG_PTR)FCB->Send.BytesUsed;
        }
    }

    if (FCB->Send.Size - FCB->Send.BytesUsed != 0 && !FCB->SendClosed &&
        IsListEmpty(&FCB->PendingIrpList[FUNCTION_SEND]))
    {
        FCB->PollState |= AFD_EVENT_SEND;
        FCB->PollStatus[FD_WRITE_BIT] = STATUS_SUCCESS;
        PollReeval( FCB->DeviceExt, FCB->FileObject );
    }
    else
    {
        FCB->PollState &= ~AFD_EVENT_SEND;
    }


    /* Some data is still waiting */
    if( FCB->Send.BytesUsed )
    {
        Status = AfdStartBufferedSend(FCB);
        if (!NT_SUCCESS(Status))
        {
            AfdFailSendQueue(FCB, Status);
            RetryDisconnectCompletion(FCB);
        }
    }
    else
    {
        /* Nothing is waiting so try to complete a pending disconnect */
        RetryDisconnectCompletion(FCB);
    }

    if (FCB->TdiRundownEvent) AfdSignalTdiRundown(FCB);
    SocketStateUnlock( FCB );

    return STATUS_SUCCESS;
}

static IO_COMPLETION_ROUTINE PacketSocketSendComplete;
static NTSTATUS NTAPI PacketSocketSendComplete
( PDEVICE_OBJECT DeviceObject,
  PIRP Irp,
  PVOID Context ) {
    PAFD_FCB FCB = (PAFD_FCB)Context;
    PLIST_ENTRY NextIrpEntry;
    PIRP NextIrp;
    PAFD_SEND_INFO SendReq;

    UNREFERENCED_PARAMETER(DeviceObject);

    AFD_DbgPrint(MID_TRACE,("Called, status %x, %u bytes used\n",
                            Irp->IoStatus.Status,
                            Irp->IoStatus.Information));

    if( !SocketAcquireStateLock( FCB ) )
        return STATUS_FILE_CLOSED;

    ASSERT(FCB->SendIrp.InFlightRequest == Irp);
    FCB->SendIrp.InFlightRequest = NULL;
    /* Request is not in flight any longer */

    if( FCB->SharedData.State == SOCKET_STATE_CLOSED ) {
        /* Cleanup our IRP queue because the FCB is being destroyed */
        while( !IsListEmpty( &FCB->PendingIrpList[FUNCTION_SEND] ) ) {
            NextIrpEntry = RemoveHeadList(&FCB->PendingIrpList[FUNCTION_SEND]);
            NextIrp = CONTAINING_RECORD(NextIrpEntry, IRP, Tail.Overlay.ListEntry);
            SendReq = GetLockedData(NextIrp, IoGetCurrentIrpStackLocation(NextIrp));
            NextIrp->IoStatus.Status = STATUS_FILE_CLOSED;
            NextIrp->IoStatus.Information = 0;
            (void)IoSetCancelRoutine(NextIrp, NULL);
            UnlockBuffers(SendReq->BufferArray, SendReq->BufferCount, FALSE);
            UnlockRequest( NextIrp, IoGetCurrentIrpStackLocation( NextIrp ) );
            IoCompleteRequest( NextIrp, IO_NETWORK_INCREMENT );
        }
        if (FCB->TdiRundownEvent) AfdSignalTdiRundown(FCB);
        SocketStateUnlock( FCB );
        return STATUS_FILE_CLOSED;
    }

    ASSERT(!IsListEmpty(&FCB->PendingIrpList[FUNCTION_SEND]));

    /* TDI spec guarantees FIFO ordering on IRPs */
    NextIrpEntry = RemoveHeadList(&FCB->PendingIrpList[FUNCTION_SEND]);
    NextIrp = CONTAINING_RECORD(NextIrpEntry, IRP, Tail.Overlay.ListEntry);

    SendReq = GetLockedData(NextIrp, IoGetCurrentIrpStackLocation(NextIrp));

    NextIrp->IoStatus.Status = Irp->IoStatus.Status;
    NextIrp->IoStatus.Information = Irp->IoStatus.Information;

    (void)IoSetCancelRoutine(NextIrp, NULL);

    UnlockBuffers(SendReq->BufferArray, SendReq->BufferCount, FALSE);

    UnlockRequest(NextIrp, IoGetCurrentIrpStackLocation(NextIrp));

    IoCompleteRequest(NextIrp, IO_NETWORK_INCREMENT);

    FCB->PollState |= AFD_EVENT_SEND;
    FCB->PollStatus[FD_WRITE_BIT] = STATUS_SUCCESS;
    PollReeval(FCB->DeviceExt, FCB->FileObject);

    if (FCB->TdiRundownEvent) AfdSignalTdiRundown(FCB);
    SocketStateUnlock(FCB);

    return STATUS_SUCCESS;
}

NTSTATUS NTAPI
AfdConnectedSocketWriteData(PDEVICE_OBJECT DeviceObject, PIRP Irp,
                            PIO_STACK_LOCATION IrpSp, BOOLEAN Short) {
    NTSTATUS Status = STATUS_SUCCESS;
    PFILE_OBJECT FileObject = IrpSp->FileObject;
    PAFD_FCB FCB = FileObject->FsContext;
    PAFD_SEND_INFO SendReq;
    UINT TotalBytesCopied = 0, i, SpaceAvail = 0, BytesCopied, SendLength;
    KPROCESSOR_MODE LockMode;
    BOOLEAN Immediate;
    NTSTATUS SendStatus;

    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Short);

    AFD_DbgPrint(MID_TRACE,("Called on %p\n", FCB));

    if( !SocketAcquireStateLock( FCB ) ) return LostSocket( Irp );

    FCB->EventSelectDisabled &= ~AFD_EVENT_SEND;

    if( FCB->Flags & AFD_ENDPOINT_CONNECTIONLESS )
    {
        PAFD_SEND_INFO_UDP SendReq;
        PTDI_CONNECTION_INFORMATION TargetAddress;

        /* Check that the socket is bound */
        if( FCB->SharedData.State != SOCKET_STATE_BOUND || !FCB->RemoteAddress )
        {
            AFD_DbgPrint(MIN_TRACE,("Invalid parameter\n"));
            return UnlockAndMaybeComplete( FCB, STATUS_INVALID_PARAMETER, Irp,
                                           0 );
        }

        if( !(SendReq = LockRequest( Irp, IrpSp, FALSE, &LockMode )) )
            return UnlockAndMaybeComplete( FCB, STATUS_NO_MEMORY, Irp, 0 );

        /* Must lock buffers before handing off user data */
        SendReq->BufferArray = LockBuffers(SendReq->BufferArray, SendReq->BufferCount, NULL, NULL, FALSE, FALSE, LockMode, AfdIs32bitIoctl(Irp));

        if( !SendReq->BufferArray ) {
            return UnlockAndMaybeComplete( FCB, STATUS_ACCESS_VIOLATION,
                                           Irp, 0 );
        }

        Status = TdiBuildConnectionInfo( &TargetAddress, FCB->RemoteAddress );

        if( NT_SUCCESS(Status) ) {
            FCB->PollState &= ~AFD_EVENT_SEND;

            Status = QueueUserModeIrp(FCB, Irp, FUNCTION_SEND);
            if (Status == STATUS_PENDING)
            {
                if (SendReq->BufferCount > 1)
                {
                    AFD_DbgPrint(MIN_TRACE,("WARN: More than one buffer %ld\n", SendReq->BufferCount));
                }
                Status = TdiSendDatagram(&FCB->SendIrp.InFlightRequest,
                                         FCB->AddressFile.Object,
                                         SendReq->BufferArray[0].buf,
                                         SendReq->BufferArray[0].len,
                                         TargetAddress,
                                         PacketSocketSendComplete,
                                         FCB);
                if (Status != STATUS_PENDING)
                {
                    NT_VERIFY(RemoveHeadList(&FCB->PendingIrpList[FUNCTION_SEND]) == &Irp->Tail.Overlay.ListEntry);
                    Irp->IoStatus.Status = Status;
                    Irp->IoStatus.Information = ((Status == STATUS_SUCCESS) ? SendReq->BufferArray[0].len : 0);
                    (void)IoSetCancelRoutine(Irp, NULL);
                    UnlockBuffers(SendReq->BufferArray, SendReq->BufferCount, FALSE);
                    UnlockRequest(Irp, IoGetCurrentIrpStackLocation(Irp));
                    IoCompleteRequest(Irp, IO_NETWORK_INCREMENT);
                }
            }

            ExFreePoolWithTag(TargetAddress, TAG_AFD_TDI_CONNECTION_INFORMATION);

            SocketStateUnlock(FCB);

            return STATUS_PENDING;
        }
        else
        {
            UnlockBuffers(SendReq->BufferArray, SendReq->BufferCount, FALSE);
            return UnlockAndMaybeComplete( FCB, Status, Irp, 0 );
        }
    }

    if (FCB->PollState & AFD_EVENT_CLOSE)
    {
        AFD_DbgPrint(MIN_TRACE,("Connection reset by remote peer\n"));

        /* This is an unexpected remote disconnect */
        return UnlockAndMaybeComplete(FCB, FCB->PollStatus[FD_CLOSE_BIT], Irp, 0);
    }

    if (FCB->PollState & AFD_EVENT_ABORT)
    {
        AFD_DbgPrint(MIN_TRACE,("Connection aborted\n"));

        /* This is an abortive socket closure on our side */
        return UnlockAndMaybeComplete(FCB, FCB->PollStatus[FD_CLOSE_BIT], Irp, 0);
    }

    if (FCB->SendClosed)
    {
        AFD_DbgPrint(MIN_TRACE,("No more sends\n"));

        /* This is a graceful send closure */
        return UnlockAndMaybeComplete(FCB, STATUS_FILE_CLOSED, Irp, 0);
    }

    if( !(SendReq = LockRequest( Irp, IrpSp, FALSE, &LockMode )) )
        return UnlockAndMaybeComplete
            ( FCB, STATUS_NO_MEMORY, Irp, 0 );

    SendReq->BufferArray = LockBuffers(SendReq->BufferArray, SendReq->BufferCount, NULL, NULL, FALSE, FALSE, LockMode, AfdIs32bitIoctl(Irp));

    if( !SendReq->BufferArray ) {
        return UnlockAndMaybeComplete( FCB, STATUS_ACCESS_VIOLATION,
                                       Irp, 0 );
    }

    AFD_DbgPrint(MID_TRACE,("Socket state %u\n", FCB->SharedData.State));

    if( FCB->SharedData.State != SOCKET_STATE_CONNECTED ) {
        AFD_DbgPrint(MID_TRACE,("Socket not connected\n"));
        UnlockBuffers( SendReq->BufferArray, SendReq->BufferCount, FALSE );
        return UnlockAndMaybeComplete( FCB, STATUS_INVALID_CONNECTION, Irp, 0 );
    }

    AFD_DbgPrint(MID_TRACE,("FCB->Send.BytesUsed = %u\n",
                            FCB->Send.BytesUsed));

    SpaceAvail = FCB->Send.Size - FCB->Send.BytesUsed;

    AFD_DbgPrint(MID_TRACE,("We can accept %u bytes\n",
                            SpaceAvail));

    /* Count the total transfer size */
    SendLength = 0;
    for (i = 0; i < SendReq->BufferCount; i++)
    {
        SendLength += SendReq->BufferArray[i].len;
    }

    Irp->Tail.Overlay.DriverContext[3] = NULL;
    Immediate = !(SendReq->AfdFlags & AFD_OVERLAPPED) && ((SendReq->AfdFlags & AFD_IMMEDIATE) || FCB->NonBlocking);

    if (!SendLength)
    {
        UnlockBuffers(SendReq->BufferArray, SendReq->BufferCount, FALSE);
        return UnlockAndMaybeComplete(FCB, STATUS_SUCCESS, Irp, 0);
    }

    /* Do not let a later write overtake an unbuffered pending request. */
    if (!IsListEmpty(&FCB->PendingIrpList[FUNCTION_SEND]))
    {
        FCB->PollState &= ~AFD_EVENT_SEND;
        if (Immediate)
        {
            UnlockBuffers(SendReq->BufferArray, SendReq->BufferCount, FALSE);
            return UnlockAndMaybeComplete(FCB, STATUS_CANT_WAIT, Irp, 0);
        }
        return LeaveIrpUntilLater(FCB, Irp, FUNCTION_SEND);
    }

    /* Make sure we've got the space */
    if (SendLength > SpaceAvail)
    {
        /* Blocking sockets have to wait here */
        if (SendLength <= FCB->Send.Size && !((SendReq->AfdFlags & AFD_IMMEDIATE) || (FCB->NonBlocking)))
        {
            FCB->PollState &= ~AFD_EVENT_SEND;
            return LeaveIrpUntilLater(FCB, Irp, FUNCTION_SEND);
        }

        /* Check if we can send anything */
        if (SpaceAvail == 0)
        {
            FCB->PollState &= ~AFD_EVENT_SEND;

            /* Non-overlapped sockets will fail if we can send nothing */
            if (!(SendReq->AfdFlags & AFD_OVERLAPPED))
            {
                UnlockBuffers( SendReq->BufferArray, SendReq->BufferCount, FALSE );
                return UnlockAndMaybeComplete( FCB, STATUS_CANT_WAIT, Irp, 0 );
            }
            else
            {
                /* Overlapped sockets just pend */
                return LeaveIrpUntilLater(FCB, Irp, FUNCTION_SEND);
            }
        }
    }

    for ( i = 0; SpaceAvail > 0 && i < SendReq->BufferCount; i++ )
    {
        BytesCopied = MIN(SendReq->BufferArray[i].len, SpaceAvail);

        AFD_DbgPrint(MID_TRACE,("Copying Buffer %u, %p:%u to %p\n",
                                i,
                                SendReq->BufferArray[i].buf,
                                BytesCopied,
                                FCB->Send.Window + FCB->Send.BytesUsed));

        RtlCopyMemory(FCB->Send.Window + FCB->Send.BytesUsed,
                      SendReq->BufferArray[i].buf,
                      BytesCopied);

        TotalBytesCopied += BytesCopied;
        SpaceAvail -= BytesCopied;
        FCB->Send.BytesUsed += BytesCopied;
    }

    Irp->IoStatus.Information = TotalBytesCopied;

    if( TotalBytesCopied == 0 ) {
        AFD_DbgPrint(MID_TRACE,("Empty send\n"));
        UnlockBuffers( SendReq->BufferArray, SendReq->BufferCount, FALSE );
        return UnlockAndMaybeComplete
        ( FCB, STATUS_SUCCESS, Irp, TotalBytesCopied );
    }

    if (SpaceAvail)
    {
        FCB->PollState |= AFD_EVENT_SEND;
        FCB->PollStatus[FD_WRITE_BIT] = STATUS_SUCCESS;
        PollReeval( FCB->DeviceExt, FCB->FileObject );
    }
    else
    {
        FCB->PollState &= ~AFD_EVENT_SEND;
    }

    if (Immediate)
    {
        /* The user buffer is no longer needed. Waiting for the TDI send
         * here can deadlock an event loop whose peer has not begun reading. */
        UnlockBuffers(SendReq->BufferArray, SendReq->BufferCount, FALSE);
        if (!FCB->SendIrp.InFlightRequest)
        {
            SendStatus = AfdStartBufferedSend(FCB);
            if (!NT_SUCCESS(SendStatus))
            {
                AfdFailSendQueue(FCB, SendStatus);
                return UnlockAndMaybeComplete(FCB, SendStatus, Irp, 0);
            }
        }
        return UnlockAndMaybeComplete(FCB, STATUS_SUCCESS, Irp, TotalBytesCopied);
    }

    /* Completion includes any previously buffered nonblocking data. */
    Irp->Tail.Overlay.DriverContext[3] = (PVOID)(ULONG_PTR)FCB->Send.BytesUsed;

    Status = QueueUserModeIrp(FCB, Irp, FUNCTION_SEND);
    if (!FCB->SendIrp.InFlightRequest)
    {
        SendStatus = AfdStartBufferedSend(FCB);
        if (!NT_SUCCESS(SendStatus))
            AfdFailSendQueue(FCB, SendStatus);
    }

    SocketStateUnlock(FCB);

    return Status;
}

NTSTATUS NTAPI
AfdPacketSocketWriteData(PDEVICE_OBJECT DeviceObject, PIRP Irp,
                         PIO_STACK_LOCATION IrpSp) {
    NTSTATUS Status = STATUS_SUCCESS;
    PTDI_CONNECTION_INFORMATION TargetAddress;
    PFILE_OBJECT FileObject = IrpSp->FileObject;
    PAFD_FCB FCB = FileObject->FsContext;
    PAFD_SEND_INFO_UDP SendReq;
    KPROCESSOR_MODE LockMode;

    UNREFERENCED_PARAMETER(DeviceObject);

    AFD_DbgPrint(MID_TRACE,("Called on %p\n", FCB));

    if( !SocketAcquireStateLock( FCB ) ) return LostSocket( Irp );

    FCB->EventSelectDisabled &= ~AFD_EVENT_SEND;

    /* Check that the socket is bound */
    if( FCB->SharedData.State != SOCKET_STATE_BOUND &&
        FCB->SharedData.State != SOCKET_STATE_CREATED)
    {
        AFD_DbgPrint(MIN_TRACE,("Invalid socket state\n"));
        return UnlockAndMaybeComplete(FCB, STATUS_INVALID_PARAMETER, Irp, 0);
    }

    if (FCB->SendClosed)
    {
        AFD_DbgPrint(MIN_TRACE,("No more sends\n"));
        return UnlockAndMaybeComplete(FCB, STATUS_FILE_CLOSED, Irp, 0);
    }

    if( !(SendReq = LockRequest( Irp, IrpSp, FALSE, &LockMode )) )
        return UnlockAndMaybeComplete(FCB, STATUS_NO_MEMORY, Irp, 0);

    if (FCB->SharedData.State == SOCKET_STATE_CREATED)
    {
        if (FCB->LocalAddress)
        {
            ExFreePoolWithTag(FCB->LocalAddress, TAG_AFD_TRANSPORT_ADDRESS);
        }

        FCB->LocalAddress =
        TaBuildNullTransportAddress( ((PTRANSPORT_ADDRESS)SendReq->TdiConnection.RemoteAddress)->
                                      Address[0].AddressType );

        if( FCB->LocalAddress ) {
            Status = WarmSocketForBind( FCB, AFD_SHARE_WILDCARD );

            if( NT_SUCCESS(Status) )
                FCB->SharedData.State = SOCKET_STATE_BOUND;
            else
                return UnlockAndMaybeComplete( FCB, Status, Irp, 0 );
        } else
            return UnlockAndMaybeComplete
            ( FCB, STATUS_NO_MEMORY, Irp, 0 );
    }

    SendReq->BufferArray = LockBuffers(SendReq->BufferArray, SendReq->BufferCount, NULL, NULL, FALSE, FALSE, LockMode, AfdIs32bitIoctl(Irp));

    if( !SendReq->BufferArray )
        return UnlockAndMaybeComplete( FCB, STATUS_ACCESS_VIOLATION,
                                       Irp, 0 );

    AFD_DbgPrint
        (MID_TRACE,("RemoteAddress #%d Type %u\n",
                    ((PTRANSPORT_ADDRESS)SendReq->TdiConnection.RemoteAddress)->
                    TAAddressCount,
                    ((PTRANSPORT_ADDRESS)SendReq->TdiConnection.RemoteAddress)->
                    Address[0].AddressType));

    Status = TdiBuildConnectionInfo( &TargetAddress,
                            ((PTRANSPORT_ADDRESS)SendReq->TdiConnection.RemoteAddress) );

    /* Check the size of the Address given ... */

    if( NT_SUCCESS(Status) ) {
        FCB->PollState &= ~AFD_EVENT_SEND;

        Status = QueueUserModeIrp(FCB, Irp, FUNCTION_SEND);
        if (Status == STATUS_PENDING)
        {
            if (SendReq->BufferCount > 1)
            {
                AFD_DbgPrint(MIN_TRACE,("WARN: More than one buffer %ld\n", SendReq->BufferCount));
            }
            Status = TdiSendDatagram(&FCB->SendIrp.InFlightRequest,
                                     FCB->AddressFile.Object,
                                     SendReq->BufferArray[0].buf,
                                     SendReq->BufferArray[0].len,
                                     TargetAddress,
                                     PacketSocketSendComplete,
                                     FCB);
            if (Status != STATUS_PENDING)
            {
                NT_VERIFY(RemoveHeadList(&FCB->PendingIrpList[FUNCTION_SEND]) == &Irp->Tail.Overlay.ListEntry);
                Irp->IoStatus.Status = Status;
                Irp->IoStatus.Information = ((Status == STATUS_SUCCESS) ? SendReq->BufferArray[0].len : 0);
                (void)IoSetCancelRoutine(Irp, NULL);
                UnlockBuffers(SendReq->BufferArray, SendReq->BufferCount, FALSE);
                UnlockRequest(Irp, IoGetCurrentIrpStackLocation(Irp));
                IoCompleteRequest(Irp, IO_NETWORK_INCREMENT);
            }
        }

        ExFreePoolWithTag(TargetAddress, TAG_AFD_TDI_CONNECTION_INFORMATION);

        SocketStateUnlock(FCB);

        return STATUS_PENDING;
    }
    else
    {
        UnlockBuffers(SendReq->BufferArray, SendReq->BufferCount, FALSE);
        return UnlockAndMaybeComplete( FCB, Status, Irp, 0 );
    }
}
