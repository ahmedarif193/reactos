/*
 * COPYRIGHT:        See COPYING in the top level directory
 * PROJECT:          ReactOS kernel
 * FILE:             drivers/net/afd/afd/listen.c
 * PURPOSE:          Ancillary functions driver
 * PROGRAMMER:       Art Yerkes (ayerkes@speakeasy.net)
 * UPDATE HISTORY:
 * 20040708 Created
 */

#include "afd.h"

static NTSTATUS SatisfyAccept( PAFD_DEVICE_EXTENSION DeviceExt,
                               PIRP Irp,
                               PFILE_OBJECT NewFileObject,
                               PAFD_TDI_OBJECT_QELT Qelt ) {
    PAFD_FCB FCB = NewFileObject->FsContext;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(DeviceExt);

    if( !SocketAcquireStateLock( FCB ) )
        return LostSocket( Irp );

    /* Transfer the connection to the new socket, launch the opening read */
    AFD_DbgPrint(MID_TRACE,("Completing a real accept (FCB %p)\n", FCB));

    FCB->Connection = Qelt->Object;

    if (FCB->RemoteAddress)
    {
        ExFreePoolWithTag(FCB->RemoteAddress, TAG_AFD_TRANSPORT_ADDRESS);
    }

    FCB->RemoteAddress =
        TaCopyTransportAddress( Qelt->ConnInfo->RemoteAddress );

    if( !FCB->RemoteAddress )
        Status = STATUS_NO_MEMORY;
    else
        Status = MakeSocketIntoConnection( FCB );

    if (NT_SUCCESS(Status))
        Status = TdiBuildConnectionInfo(&FCB->ConnectCallInfo, FCB->RemoteAddress);

    if (NT_SUCCESS(Status))
        Status = TdiBuildConnectionInfo(&FCB->ConnectReturnInfo, FCB->RemoteAddress);

    return UnlockAndMaybeComplete( FCB, Status, Irp, 0 );
}

static NTSTATUS SatisfyPreAccept( PIRP Irp, PAFD_TDI_OBJECT_QELT Qelt ) {
    PAFD_RECEIVED_ACCEPT_DATA ListenReceive =
        (PAFD_RECEIVED_ACCEPT_DATA)Irp->AssociatedIrp.SystemBuffer;
    PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation( Irp );
    PTA_IP_ADDRESS IPAddr;
    ULONG Required;

    Required = FIELD_OFFSET(AFD_RECEIVED_ACCEPT_DATA, Address) +
               TaLengthOfTransportAddress( Qelt->ConnInfo->RemoteAddress );

    if( IrpSp->Parameters.DeviceIoControl.OutputBufferLength < Required ) {
        if( Irp->MdlAddress ) UnlockRequest( Irp, IrpSp );
        Irp->IoStatus.Information = 0;
        Irp->IoStatus.Status = STATUS_BUFFER_TOO_SMALL;
        (void)IoSetCancelRoutine(Irp, NULL);
        IoCompleteRequest( Irp, IO_NETWORK_INCREMENT );
        return STATUS_BUFFER_TOO_SMALL;
    }

    ListenReceive->SequenceNumber = Qelt->Seq;

    AFD_DbgPrint(MID_TRACE,("Giving SEQ %u to userland\n", Qelt->Seq));
    AFD_DbgPrint(MID_TRACE,("Socket Address (K) %p (U) %p\n",
                            &ListenReceive->Address,
                            Qelt->ConnInfo->RemoteAddress));

    TaCopyTransportAddressInPlace( &ListenReceive->Address,
                                   Qelt->ConnInfo->RemoteAddress );

    IPAddr = (PTA_IP_ADDRESS)&ListenReceive->Address;

    AFD_DbgPrint(MID_TRACE,("IPAddr->TAAddressCount %d\n",
                            IPAddr->TAAddressCount));
    AFD_DbgPrint(MID_TRACE,("IPAddr->Address[0].AddressType %u\n",
                            IPAddr->Address[0].AddressType));
    AFD_DbgPrint(MID_TRACE,("IPAddr->Address[0].AddressLength %u\n",
                            IPAddr->Address[0].AddressLength));
    AFD_DbgPrint(MID_TRACE,("IPAddr->Address[0].Address[0].sin_port %x\n",
                            IPAddr->Address[0].Address[0].sin_port));
    AFD_DbgPrint(MID_TRACE,("IPAddr->Address[0].Address[0].sin_addr %x\n",
                            IPAddr->Address[0].Address[0].in_addr));

    if( Irp->MdlAddress ) UnlockRequest( Irp, IoGetCurrentIrpStackLocation( Irp ) );

    Irp->IoStatus.Information = ((PCHAR)&IPAddr[1]) - ((PCHAR)ListenReceive);
    Irp->IoStatus.Status = STATUS_SUCCESS;
    (void)IoSetCancelRoutine(Irp, NULL);
    IoCompleteRequest( Irp, IO_NETWORK_INCREMENT );
    return STATUS_SUCCESS;
}

VOID AfdSuperAcceptRelease( PIRP Irp ) {
    PFILE_OBJECT AcceptFileObject = Irp->Tail.Overlay.DriverContext[2];
    PAFD_SUPER_ACCEPT_INFO Info = Irp->Tail.Overlay.DriverContext[3];

    if( AcceptFileObject ) {
        ObDereferenceObject( AcceptFileObject );
        Irp->Tail.Overlay.DriverContext[2] = NULL;
    }
    if( Info ) {
        ExFreePoolWithTag( Info, TAG_AFD_ACCEPT_QUEUE );
        Irp->Tail.Overlay.DriverContext[3] = NULL;
    }
}

static BOOLEAN IsSuperAcceptIrp( PIRP Irp ) {
    PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation( Irp );
    return IrpSp->MajorFunction == IRP_MJ_DEVICE_CONTROL &&
           IrpSp->Parameters.DeviceIoControl.IoControlCode == IOCTL_AFD_SUPER_ACCEPT;
}

static NTSTATUS SuperAcceptWriteAddress( PCHAR Buffer, ULONG Length,
                                         PTRANSPORT_ADDRESS Address ) {
    ULONG AddressLength = Address->Address[0].AddressLength + sizeof(USHORT);

    if( Length < sizeof(INT) + AddressLength )
        return STATUS_BUFFER_TOO_SMALL;

    *(PINT)Buffer = AddressLength;
    RtlCopyMemory( Buffer + sizeof(INT), &Address->Address[0].AddressType, AddressLength );
    return STATUS_SUCCESS;
}

static NTSTATUS SatisfySuperAccept( PAFD_FCB FCB, PIRP Irp,
                                    PAFD_TDI_OBJECT_QELT Qelt ) {
    PIO_STACK_LOCATION IrpSp = IoGetCurrentIrpStackLocation( Irp );
    PFILE_OBJECT AcceptFileObject = Irp->Tail.Overlay.DriverContext[2];
    PAFD_SUPER_ACCEPT_INFO Info = Irp->Tail.Overlay.DriverContext[3];
    PCHAR Buffer = Irp->Tail.Overlay.DriverContext[0];
    PAFD_FCB AcceptFCB = AcceptFileObject->FsContext;
    NTSTATUS Status;

    RemoveEntryList( &Qelt->ListEntry );

    if( !SocketAcquireStateLock( AcceptFCB ) ) {
        Status = STATUS_FILE_CLOSED;
    } else {
        AcceptFCB->Connection = Qelt->Object;
        if( AcceptFCB->RemoteAddress )
            ExFreePoolWithTag( AcceptFCB->RemoteAddress, TAG_AFD_TRANSPORT_ADDRESS );
        AcceptFCB->RemoteAddress = TaCopyTransportAddress( Qelt->ConnInfo->RemoteAddress );
        if( !AcceptFCB->RemoteAddress )
            Status = STATUS_NO_MEMORY;
        else
            Status = MakeSocketIntoConnection( AcceptFCB );
        if( NT_SUCCESS(Status) )
            Status = TdiBuildConnectionInfo( &AcceptFCB->ConnectCallInfo, AcceptFCB->RemoteAddress );
        if( NT_SUCCESS(Status) )
            Status = TdiBuildConnectionInfo( &AcceptFCB->ConnectReturnInfo, AcceptFCB->RemoteAddress );
        if( NT_SUCCESS(Status) ) {
            AcceptFCB->SharedData.State = SOCKET_STATE_CONNECTED;
            AcceptFCB->PollState |= AFD_EVENT_SEND;
            AcceptFCB->PollStatus[FD_WRITE_BIT] = STATUS_SUCCESS;
            PollReeval( AcceptFCB->DeviceExt, AcceptFileObject );
        }
        SocketStateUnlock( AcceptFCB );
    }

    if( NT_SUCCESS(Status) )
        Status = SuperAcceptWriteAddress( Buffer + Info->ReceiveDataLength,
                                          Info->LocalAddressLength,
                                          FCB->LocalAddress );
    if( NT_SUCCESS(Status) )
        Status = SuperAcceptWriteAddress( Buffer + Info->ReceiveDataLength + Info->LocalAddressLength,
                                          Info->RemoteAddressLength,
                                          Qelt->ConnInfo->RemoteAddress );

    ExFreePoolWithTag( Qelt, TAG_AFD_ACCEPT_QUEUE );
    AfdSuperAcceptRelease( Irp );

    Irp->IoStatus.Information = 0;
    Irp->IoStatus.Status = Status;
    if( Irp->MdlAddress ) UnlockRequest( Irp, IrpSp );
    (void)IoSetCancelRoutine(Irp, NULL);
    IoCompleteRequest( Irp, IO_NETWORK_INCREMENT );
    return Status;
}

static IO_COMPLETION_ROUTINE ListenComplete;
static NTSTATUS NTAPI ListenComplete( PDEVICE_OBJECT DeviceObject,
                                      PIRP Irp,
                                      PVOID Context ) {
    NTSTATUS Status = STATUS_SUCCESS;
    PAFD_FCB FCB = (PAFD_FCB)Context;
    PAFD_TDI_OBJECT_QELT Qelt;
    PLIST_ENTRY NextIrpEntry;
    PIRP NextIrp;

    UNREFERENCED_PARAMETER(DeviceObject);

    if( !SocketAcquireStateLock( FCB ) )
        return STATUS_FILE_CLOSED;

    ASSERT(FCB->ListenIrp.InFlightRequest == Irp);
    FCB->ListenIrp.InFlightRequest = NULL;

    if( FCB->SharedData.State == SOCKET_STATE_CLOSED ) {
        /* Cleanup our IRP queue because the FCB is being destroyed */
        while( !IsListEmpty( &FCB->PendingIrpList[FUNCTION_PREACCEPT] ) ) {
           NextIrpEntry = RemoveHeadList(&FCB->PendingIrpList[FUNCTION_PREACCEPT]);
           NextIrp = CONTAINING_RECORD(NextIrpEntry, IRP, Tail.Overlay.ListEntry);
           NextIrp->IoStatus.Status = STATUS_FILE_CLOSED;
           NextIrp->IoStatus.Information = 0;
           if( IsSuperAcceptIrp( NextIrp ) ) AfdSuperAcceptRelease( NextIrp );
           if( NextIrp->MdlAddress ) UnlockRequest( NextIrp, IoGetCurrentIrpStackLocation( NextIrp ) );
           (void)IoSetCancelRoutine(NextIrp, NULL);
           IoCompleteRequest( NextIrp, IO_NETWORK_INCREMENT );
        }

        /* Free ConnectionReturnInfo and ConnectionCallInfo */
        if (FCB->ListenIrp.ConnectionReturnInfo)
        {
            ExFreePoolWithTag(FCB->ListenIrp.ConnectionReturnInfo,
                              TAG_AFD_TDI_CONNECTION_INFORMATION);

            FCB->ListenIrp.ConnectionReturnInfo = NULL;
        }

        if (FCB->ListenIrp.ConnectionCallInfo)
        {
            ExFreePoolWithTag(FCB->ListenIrp.ConnectionCallInfo,
                              TAG_AFD_TDI_CONNECTION_INFORMATION);

            FCB->ListenIrp.ConnectionCallInfo = NULL;
        }

        if (FCB->TdiRundownEvent) AfdSignalTdiRundown(FCB);
        SocketStateUnlock( FCB );
        return STATUS_FILE_CLOSED;
    }

    AFD_DbgPrint(MID_TRACE,("Completing listen request.\n"));
    AFD_DbgPrint(MID_TRACE,("IoStatus was %x\n", Irp->IoStatus.Status));

    if (Irp->IoStatus.Status != STATUS_SUCCESS)
    {
        if (FCB->TdiRundownEvent) AfdSignalTdiRundown(FCB);
        SocketStateUnlock(FCB);
        return Irp->IoStatus.Status;
    }

    Qelt = ExAllocatePoolWithTag(NonPagedPool,
                                 sizeof(*Qelt),
                                 TAG_AFD_ACCEPT_QUEUE);

    if( !Qelt ) {
        Status = STATUS_NO_MEMORY;
    } else {
        UINT AddressType =
            FCB->LocalAddress->Address[0].AddressType;

        Qelt->Object = FCB->Connection;
        Qelt->Seq = FCB->ConnSeq++;
        AFD_DbgPrint(MID_TRACE,("Address Type: %u (RA %p)\n",
                                AddressType,
                                FCB->ListenIrp.
                                ConnectionReturnInfo->RemoteAddress));

        Status = TdiBuildNullConnectionInfo( &Qelt->ConnInfo, AddressType );
        if( NT_SUCCESS(Status) ) {
            TaCopyTransportAddressInPlace
               ( Qelt->ConnInfo->RemoteAddress,
                 FCB->ListenIrp.ConnectionReturnInfo->RemoteAddress );
            InsertTailList( &FCB->PendingConnections, &Qelt->ListEntry );
        }
    }

    /* Satisfy a pre-accept request if one is available */
    if( !IsListEmpty( &FCB->PendingIrpList[FUNCTION_PREACCEPT] ) &&
        !IsListEmpty( &FCB->PendingConnections ) ) {
        PLIST_ENTRY PendingIrp  =
            RemoveHeadList( &FCB->PendingIrpList[FUNCTION_PREACCEPT] );
        PLIST_ENTRY PendingConn = FCB->PendingConnections.Flink;
        PIRP AcceptIrp = CONTAINING_RECORD( PendingIrp, IRP, Tail.Overlay.ListEntry );
        PAFD_TDI_OBJECT_QELT AcceptQelt = CONTAINING_RECORD( PendingConn, AFD_TDI_OBJECT_QELT, ListEntry );
        if( IsSuperAcceptIrp( AcceptIrp ) )
            SatisfySuperAccept( FCB, AcceptIrp, AcceptQelt );
        else
            SatisfyPreAccept( AcceptIrp, AcceptQelt );
    }

    /* Launch new accept socket */
    Status = WarmSocketForConnection( FCB );

    if (NT_SUCCESS(Status))
    {
        Status = TdiBuildNullConnectionInfoInPlace(FCB->ListenIrp.ConnectionCallInfo,
                                                   FCB->LocalAddress->Address[0].AddressType);
        ASSERT(Status == STATUS_SUCCESS);

        Status = TdiBuildNullConnectionInfoInPlace(FCB->ListenIrp.ConnectionReturnInfo,
                                                   FCB->LocalAddress->Address[0].AddressType);
        ASSERT(Status == STATUS_SUCCESS);

        Status = TdiListen( &FCB->ListenIrp.InFlightRequest,
                            FCB->Connection.Object,
                            &FCB->ListenIrp.ConnectionCallInfo,
                            &FCB->ListenIrp.ConnectionReturnInfo,
                            ListenComplete,
                            FCB );

        if (Status == STATUS_PENDING)
            Status = STATUS_SUCCESS;
    }

    /* Trigger a select return if appropriate */
    if( !IsListEmpty( &FCB->PendingConnections ) ) {
        FCB->PollState |= AFD_EVENT_ACCEPT;
        FCB->PollStatus[FD_ACCEPT_BIT] = STATUS_SUCCESS;
        PollReeval( FCB->DeviceExt, FCB->FileObject );
    } else
        FCB->PollState &= ~AFD_EVENT_ACCEPT;

    if (FCB->TdiRundownEvent) AfdSignalTdiRundown(FCB);
    SocketStateUnlock( FCB );

    return Status;
}

NTSTATUS AfdListenSocket( PDEVICE_OBJECT DeviceObject, PIRP Irp,
                          PIO_STACK_LOCATION IrpSp ) {
    NTSTATUS Status = STATUS_SUCCESS;
    PFILE_OBJECT FileObject = IrpSp->FileObject;
    PAFD_FCB FCB = FileObject->FsContext;
    PAFD_LISTEN_DATA ListenReq;

    UNREFERENCED_PARAMETER(DeviceObject);

    AFD_DbgPrint(MID_TRACE,("Called on %p\n", FCB));

    if( !SocketAcquireStateLock( FCB ) ) return LostSocket( Irp );

    if( !(ListenReq = LockRequest( Irp, IrpSp, FALSE, NULL )) )
        return UnlockAndMaybeComplete( FCB, STATUS_NO_MEMORY, Irp,
                                       0 );

    if( FCB->SharedData.State != SOCKET_STATE_BOUND ) {
        Status = STATUS_INVALID_PARAMETER;
        AFD_DbgPrint(MIN_TRACE,("Could not listen an unbound socket\n"));
        return UnlockAndMaybeComplete( FCB, Status, Irp, 0 );
    }

    FCB->DelayedAccept = ListenReq->UseDelayedAcceptance;

    AFD_DbgPrint(MID_TRACE,("ADDRESSFILE: %p\n", FCB->AddressFile.Handle));

    Status = WarmSocketForConnection( FCB );

    AFD_DbgPrint(MID_TRACE,("Status from warmsocket %x\n", Status));

    if( !NT_SUCCESS(Status) ) return UnlockAndMaybeComplete( FCB, Status, Irp, 0 );

    Status = TdiBuildNullConnectionInfo
        ( &FCB->ListenIrp.ConnectionCallInfo,
          FCB->LocalAddress->Address[0].AddressType );

    if (!NT_SUCCESS(Status)) return UnlockAndMaybeComplete(FCB, Status, Irp, 0);

    Status = TdiBuildNullConnectionInfo
        ( &FCB->ListenIrp.ConnectionReturnInfo,
          FCB->LocalAddress->Address[0].AddressType );

    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(FCB->ListenIrp.ConnectionCallInfo,
                          TAG_AFD_TDI_CONNECTION_INFORMATION);

        FCB->ListenIrp.ConnectionCallInfo = NULL;
        return UnlockAndMaybeComplete(FCB, Status, Irp, 0);
    }

    FCB->SharedData.State = SOCKET_STATE_LISTENING;

    Status = TdiListen( &FCB->ListenIrp.InFlightRequest,
                        FCB->Connection.Object,
                        &FCB->ListenIrp.ConnectionCallInfo,
                        &FCB->ListenIrp.ConnectionReturnInfo,
                        ListenComplete,
                        FCB );

    if( Status == STATUS_PENDING )
        Status = STATUS_SUCCESS;

    AFD_DbgPrint(MID_TRACE,("Returning %x\n", Status));
    return UnlockAndMaybeComplete( FCB, Status, Irp, 0 );
}

NTSTATUS AfdWaitForListen( PDEVICE_OBJECT DeviceObject, PIRP Irp,
                           PIO_STACK_LOCATION IrpSp ) {
    PFILE_OBJECT FileObject = IrpSp->FileObject;
    PAFD_FCB FCB = FileObject->FsContext;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(DeviceObject);

    AFD_DbgPrint(MID_TRACE,("Called\n"));

    if( !SocketAcquireStateLock( FCB ) ) return LostSocket( Irp );

    if( !IsListEmpty( &FCB->PendingConnections ) ) {
        PLIST_ENTRY PendingConn = FCB->PendingConnections.Flink;

        /* We have a pending connection ... complete this irp right away */
        Status = SatisfyPreAccept
            ( Irp,
              CONTAINING_RECORD
              ( PendingConn, AFD_TDI_OBJECT_QELT, ListEntry ) );

        AFD_DbgPrint(MID_TRACE,("Completed a wait for accept\n"));

        if ( !IsListEmpty( &FCB->PendingConnections ) )
        {
             FCB->PollState |= AFD_EVENT_ACCEPT;
             FCB->PollStatus[FD_ACCEPT_BIT] = STATUS_SUCCESS;
             PollReeval( FCB->DeviceExt, FCB->FileObject );
        } else
             FCB->PollState &= ~AFD_EVENT_ACCEPT;

        SocketStateUnlock( FCB );
        return Status;
    } else if (FCB->NonBlocking) {
        AFD_DbgPrint(MIN_TRACE,("No connection ready on a non-blocking socket\n"));

        return UnlockAndMaybeComplete(FCB, STATUS_CANT_WAIT, Irp, 0);
    } else {
        AFD_DbgPrint(MID_TRACE,("Holding\n"));

        return LeaveIrpUntilLater( FCB, Irp, FUNCTION_PREACCEPT );
    }
}

NTSTATUS AfdSuperAccept( PDEVICE_OBJECT DeviceObject, PIRP Irp,
                         PIO_STACK_LOCATION IrpSp ) {
    PFILE_OBJECT FileObject = IrpSp->FileObject;
    PAFD_FCB FCB = FileObject->FsContext;
    PAFD_SUPER_ACCEPT_INFO Info;
    PFILE_OBJECT AcceptFileObject = NULL;
    NTSTATUS Status;

    if( IrpSp->Parameters.DeviceIoControl.OutputBufferLength < sizeof(AFD_SUPER_ACCEPT_INFO) ||
        !Irp->UserBuffer ) {
        Irp->IoStatus.Status = STATUS_INVALID_PARAMETER;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest( Irp, IO_NO_INCREMENT );
        return STATUS_INVALID_PARAMETER;
    }

    Info = ExAllocatePoolWithTag( NonPagedPool, sizeof(*Info), TAG_AFD_ACCEPT_QUEUE );
    if( !Info ) {
        Irp->IoStatus.Status = STATUS_NO_MEMORY;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest( Irp, IO_NO_INCREMENT );
        return STATUS_NO_MEMORY;
    }

    Status = STATUS_SUCCESS;
    _SEH2_TRY {
        if( Irp->RequestorMode != KernelMode )
            ProbeForRead( Irp->UserBuffer, sizeof(AFD_SUPER_ACCEPT_INFO), sizeof(ULONG) );
        RtlCopyMemory( Info, Irp->UserBuffer, sizeof(*Info) );
    } _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) {
        Status = _SEH2_GetExceptionCode();
    } _SEH2_END;

    if( NT_SUCCESS(Status) &&
        IrpSp->Parameters.DeviceIoControl.InputBufferLength <
        Info->ReceiveDataLength + Info->LocalAddressLength + Info->RemoteAddressLength ) {
        Status = STATUS_INVALID_PARAMETER;
    }

    if( NT_SUCCESS(Status) ) {
        Status = ObReferenceObjectByHandle( Info->AcceptHandle,
                                            FILE_READ_DATA | FILE_WRITE_DATA,
                                            *IoFileObjectType,
                                            Irp->RequestorMode,
                                            (PVOID *)&AcceptFileObject,
                                            NULL );
    }

    if( NT_SUCCESS(Status) &&
        ( AcceptFileObject->DeviceObject != DeviceObject ||
          AcceptFileObject == FileObject ||
          !AcceptFileObject->FsContext ) ) {
        Status = STATUS_INVALID_PARAMETER;
    }

    if( NT_SUCCESS(Status) && !LockRequest( Irp, IrpSp, TRUE, NULL ) )
        Status = STATUS_ACCESS_VIOLATION;

    if( !NT_SUCCESS(Status) ) {
        if( AcceptFileObject ) ObDereferenceObject( AcceptFileObject );
        ExFreePoolWithTag( Info, TAG_AFD_ACCEPT_QUEUE );
        Irp->IoStatus.Status = Status;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest( Irp, IO_NO_INCREMENT );
        return Status;
    }

    Irp->Tail.Overlay.DriverContext[2] = AcceptFileObject;
    Irp->Tail.Overlay.DriverContext[3] = Info;

    if( !SocketAcquireStateLock( FCB ) ) {
        AfdSuperAcceptRelease( Irp );
        return LostSocket( Irp );
    }

    if( FCB->SharedData.State != SOCKET_STATE_LISTENING ) {
        AfdSuperAcceptRelease( Irp );
        return UnlockAndMaybeComplete( FCB, STATUS_INVALID_PARAMETER, Irp, 0 );
    }

    if( !IsListEmpty( &FCB->PendingConnections ) ) {
        Status = SatisfySuperAccept
            ( FCB, Irp,
              CONTAINING_RECORD( FCB->PendingConnections.Flink, AFD_TDI_OBJECT_QELT, ListEntry ) );
        if( !IsListEmpty( &FCB->PendingConnections ) ) {
            FCB->PollState |= AFD_EVENT_ACCEPT;
            FCB->PollStatus[FD_ACCEPT_BIT] = STATUS_SUCCESS;
            PollReeval( FCB->DeviceExt, FCB->FileObject );
        } else
            FCB->PollState &= ~AFD_EVENT_ACCEPT;
        SocketStateUnlock( FCB );
        return Status;
    }

    return LeaveIrpUntilLater( FCB, Irp, FUNCTION_PREACCEPT );
}

NTSTATUS AfdAccept( PDEVICE_OBJECT DeviceObject, PIRP Irp,
                    PIO_STACK_LOCATION IrpSp ) {
    NTSTATUS Status = STATUS_SUCCESS;
    PFILE_OBJECT FileObject = IrpSp->FileObject;
    PAFD_DEVICE_EXTENSION DeviceExt =
        (PAFD_DEVICE_EXTENSION)DeviceObject->DeviceExtension;
    PAFD_FCB FCB = FileObject->FsContext;
    PAFD_ACCEPT_DATA AcceptData = Irp->AssociatedIrp.SystemBuffer;
    PLIST_ENTRY PendingConn;

    AFD_DbgPrint(MID_TRACE,("Called\n"));

    if( !SocketAcquireStateLock( FCB ) ) return LostSocket( Irp );

    FCB->EventSelectDisabled &= ~AFD_EVENT_ACCEPT;

    for( PendingConn = FCB->PendingConnections.Flink;
         PendingConn != &FCB->PendingConnections;
         PendingConn = PendingConn->Flink ) {
        PAFD_TDI_OBJECT_QELT PendingConnObj =
            CONTAINING_RECORD( PendingConn, AFD_TDI_OBJECT_QELT, ListEntry );

        AFD_DbgPrint(MID_TRACE,("Comparing Seq %u to Q %u\n",
                                AcceptData->SequenceNumber,
                                PendingConnObj->Seq));

        if( PendingConnObj->Seq == AcceptData->SequenceNumber ) {
            PFILE_OBJECT NewFileObject = NULL;

            RemoveEntryList( PendingConn );

            Status = ObReferenceObjectByHandle
                ( AcceptData->ListenHandle,
                  FILE_ALL_ACCESS,
                  NULL,
                  KernelMode,
                  (PVOID *)&NewFileObject,
                  NULL );

            if( !NT_SUCCESS(Status) ) return UnlockAndMaybeComplete( FCB, Status, Irp, 0 );

            ASSERT(NewFileObject != FileObject);
            ASSERT(NewFileObject->FsContext != FCB);

            /* We have a pending connection ... complete this irp right away */
            Status = SatisfyAccept( DeviceExt, Irp, NewFileObject, PendingConnObj );

            ObDereferenceObject( NewFileObject );

            AFD_DbgPrint(MID_TRACE,("Completed a wait for accept\n"));

            ExFreePoolWithTag(PendingConnObj, TAG_AFD_ACCEPT_QUEUE);

            if( !IsListEmpty( &FCB->PendingConnections ) )
            {
                FCB->PollState |= AFD_EVENT_ACCEPT;
                FCB->PollStatus[FD_ACCEPT_BIT] = STATUS_SUCCESS;
                PollReeval( FCB->DeviceExt, FCB->FileObject );
            } else
                FCB->PollState &= ~AFD_EVENT_ACCEPT;

            SocketStateUnlock( FCB );
            return Status;
        }
    }

    AFD_DbgPrint(MIN_TRACE,("No connection waiting\n"));

    return UnlockAndMaybeComplete( FCB, STATUS_UNSUCCESSFUL, Irp, 0 );
}
