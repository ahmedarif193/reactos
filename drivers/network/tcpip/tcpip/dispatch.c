/*
 * COPYRIGHT:   See COPYING in the top level directory
 * PROJECT:     ReactOS TCP/IP protocol driver
 * FILE:        tcpip/dispatch.h
 * PURPOSE:     TDI dispatch routines
 * PROGRAMMERS: Casper S. Hornstrup (chorns@users.sourceforge.net)
 * REVISIONS:
 *   CSH 01/08-2000 Created
 * TODO:        Validate device object in all dispatch routines
 */

#include "precomp.h"

#include <datagram.h>
#include <pseh/pseh2.h>

typedef struct _QUERY_HW_WORK_ITEM {
    PIO_WORKITEM WorkItem;
    PIRP Irp;
    PIO_STACK_LOCATION IrpSp;
    PIP_INTERFACE Interface;
    LARGE_INTEGER StartTime;
    ULONG RemoteIP;
} QUERY_HW_WORK_ITEM, *PQUERY_HW_WORK_ITEM;

NTSTATUS IRPFinish( PIRP Irp, NTSTATUS Status ) {
    KIRQL OldIrql;

    if (Status != STATUS_PENDING) {
        Irp->IoStatus.Status = Status;
        IoAcquireCancelSpinLock(&OldIrql);
        (void)IoSetCancelRoutine( Irp, NULL );
        IoReleaseCancelSpinLock(OldIrql);

        IoCompleteRequest( Irp, IO_NETWORK_INCREMENT );
    }

    return Status;
}

NTSTATUS DispPrepareIrpForCancel(
    PTRANSPORT_CONTEXT Context,
    PIRP Irp,
    PDRIVER_CANCEL CancelRoutine)
/*
 * FUNCTION: Prepare an IRP for cancellation
 * ARGUMENTS:
 *     Context       = Pointer to context information
 *     Irp           = Pointer to an I/O request packet
 *     CancelRoutine = Routine to be called when I/O request is cancelled
 * RETURNS:
 *     Status of operation
 */
{
    KIRQL OldIrql;
    PIO_STACK_LOCATION IrpSp;
    PTRANSPORT_CONTEXT TransContext;

    TI_DbgPrint(DEBUG_IRP, ("Called.\n"));

    IrpSp       = IoGetCurrentIrpStackLocation(Irp);
    TransContext = (PTRANSPORT_CONTEXT)IrpSp->FileObject->FsContext;

    IoAcquireCancelSpinLock(&OldIrql);

    if (!Irp->Cancel && !TransContext->CancelIrps) {
        (void)IoSetCancelRoutine(Irp, CancelRoutine);
        IoReleaseCancelSpinLock(OldIrql);

        TI_DbgPrint(DEBUG_IRP, ("Leaving (IRP at 0x%X can now be cancelled).\n", Irp));

        return STATUS_SUCCESS;
    }

    /* IRP has already been cancelled */

    IoReleaseCancelSpinLock(OldIrql);

    Irp->IoStatus.Status      = STATUS_CANCELLED;
    Irp->IoStatus.Information = 0;

    TI_DbgPrint(DEBUG_IRP, ("Leaving (IRP was already cancelled).\n"));

    return Irp->IoStatus.Status;
}

VOID DispDataRequestComplete(
    PVOID Context,
    NTSTATUS Status,
    ULONG Count)
/*
 * FUNCTION: Completes a send/receive IRP
 * ARGUMENTS:
 *     Context = Pointer to context information (IRP)
 *     Status  = Status of the request
 *     Count   = Number of bytes sent or received
 */
{
    PIRP Irp = Context;

    TI_DbgPrint(DEBUG_IRP, ("Called for irp %x (%x, %d).\n",
			    Irp, Status, Count));

    Irp->IoStatus.Status      = Status;
    Irp->IoStatus.Information = Count;

    TI_DbgPrint(MID_TRACE, ("Irp->IoStatus.Status = %x\n",
			    Irp->IoStatus.Status));
    TI_DbgPrint(MID_TRACE, ("Irp->IoStatus.Information = %d\n",
			    Irp->IoStatus.Information));
    TI_DbgPrint(DEBUG_IRP, ("Completing IRP at (0x%X).\n", Irp));

    IRPFinish(Irp, Status);

    TI_DbgPrint(DEBUG_IRP, ("Done Completing IRP\n"));
}

VOID NTAPI DispCancelRequest(
    PDEVICE_OBJECT Device,
    PIRP Irp)
/*
 * FUNCTION: Cancels an IRP
 * ARGUMENTS:
 *     Device = Pointer to device object
 *     Irp    = Pointer to an I/O request packet
 */
{
    PIO_STACK_LOCATION IrpSp;
    PTRANSPORT_CONTEXT TranContext;
    PFILE_OBJECT FileObject;
    UCHAR MinorFunction;
    PVOID ContextType;
    PCONNECTION_ENDPOINT Connection = NULL;
    PADDRESS_FILE AddressFile = NULL;
    BOOLEAN DequeuedIrp = FALSE;

    TI_DbgPrint(DEBUG_IRP, ("Called.\n"));

    IrpSp         = IoGetCurrentIrpStackLocation(Irp);
    FileObject    = IrpSp->FileObject;
    TranContext   = (PTRANSPORT_CONTEXT)FileObject->FsContext;
    ContextType   = FileObject->FsContext2;
    MinorFunction = IrpSp->MinorFunction;

    if (TranContext)
    {
        switch (MinorFunction)
        {
            case TDI_SEND:
            case TDI_RECEIVE:
            case TDI_CONNECT:
            case TDI_DISCONNECT:
                Connection = (PCONNECTION_ENDPOINT)TranContext->Handle.ConnectionContext;
                if (Connection) ReferenceObject(Connection);
                break;

            case TDI_SEND_DATAGRAM:
            case TDI_RECEIVE_DATAGRAM:
                AddressFile = (PADDRESS_FILE)TranContext->Handle.AddressHandle;
                if (AddressFile) ReferenceObject(AddressFile);
                break;
        }
    }

#if DBG
    if (!Irp->Cancel)
        TI_DbgPrint(MIN_TRACE, ("Irp->Cancel is FALSE, should be TRUE.\n"));
#endif

    /* IRP stack locations are no longer stable once completion acquires this lock. */
    IoReleaseCancelSpinLock(Irp->CancelIrql);

    TI_DbgPrint(DEBUG_IRP, ("IRP at (0x%X)  MinorFunction (0x%X)  IrpSp (0x%X).\n", Irp, MinorFunction, IrpSp));

    /* Try canceling the request */
    switch(MinorFunction) {
    case TDI_SEND:
    case TDI_RECEIVE:
        if (Connection) DequeuedIrp = TCPRemoveIRP(Connection, Irp);
        break;

    case TDI_SEND_DATAGRAM:
        if (ContextType != (PVOID)TDI_TRANSPORT_ADDRESS_FILE) {
            TI_DbgPrint(MIN_TRACE, ("TDI_SEND_DATAGRAM, but no address file.\n"));
            break;
        }

        if (AddressFile) DequeuedIrp = DGRemoveIRP(AddressFile, Irp);
        break;

    case TDI_RECEIVE_DATAGRAM:
        if (ContextType != (PVOID)TDI_TRANSPORT_ADDRESS_FILE) {
            TI_DbgPrint(MIN_TRACE, ("TDI_RECEIVE_DATAGRAM, but no address file.\n"));
            break;
        }

        if (AddressFile) DequeuedIrp = DGRemoveIRP(AddressFile, Irp);
        break;

    case TDI_CONNECT:
        if (Connection) DequeuedIrp = TCPRemoveIRP(Connection, Irp);
        break;

    case TDI_DISCONNECT:
        if (Connection) DequeuedIrp = TCPRemoveIRP(Connection, Irp);
        if (DequeuedIrp)
        {
            if (KeCancelTimer(&Connection->DisconnectTimer))
            {
                DereferenceObject(Connection);
            }
        }
        break;

    default:
        TI_DbgPrint(MIN_TRACE, ("Unknown IRP. MinorFunction (0x%X).\n", MinorFunction));
        ASSERT(FALSE);
        break;
    }

    if (DequeuedIrp)
       IRPFinish(Irp, STATUS_CANCELLED);

    if (AddressFile) DereferenceObject(AddressFile);
    if (Connection) DereferenceObject(Connection);

    TI_DbgPrint(MAX_TRACE, ("Leaving.\n"));
}


VOID NTAPI DispCancelListenRequest(
    PDEVICE_OBJECT Device,
    PIRP Irp)
/*
 * FUNCTION: Cancels a listen IRP
 * ARGUMENTS:
 *     Device = Pointer to device object
 *     Irp    = Pointer to an I/O request packet
 */
{
    PIO_STACK_LOCATION IrpSp;
    PTRANSPORT_CONTEXT TranContext;
    PFILE_OBJECT FileObject;
    PCONNECTION_ENDPOINT Connection;
    PCONNECTION_ENDPOINT Listener = NULL;
    PADDRESS_FILE AddressFile = NULL;
    BOOLEAN DequeuedIrp = FALSE;

    TI_DbgPrint(DEBUG_IRP, ("Called.\n"));

    IrpSp         = IoGetCurrentIrpStackLocation(Irp);
    FileObject    = IrpSp->FileObject;
    TranContext   = (PTRANSPORT_CONTEXT)FileObject->FsContext;
    ASSERT(TDI_LISTEN == IrpSp->MinorFunction);
    Connection    = TranContext ? (PCONNECTION_ENDPOINT)TranContext->Handle.ConnectionContext : NULL;
    if (Connection) ReferenceObject(Connection);

#if DBG
    if (!Irp->Cancel)
        TI_DbgPrint(MIN_TRACE, ("Irp->Cancel is FALSE, should be TRUE.\n"));
#endif

    /* Capture the endpoint before completion can advance or release the IRP stack. */
    IoReleaseCancelSpinLock(Irp->CancelIrql);

    TI_DbgPrint(DEBUG_IRP, ("IRP at (0x%X).\n", Irp));

    if (Connection)
    {
        LockObject(Connection);
        AddressFile = Connection->AddressFile;
        if (AddressFile) ReferenceObject(AddressFile);
        UnlockObject(Connection);
    }

    if (AddressFile)
    {
        LockObject(AddressFile);
        Listener = AddressFile->Listener;
        if (Listener) ReferenceObject(Listener);
        UnlockObject(AddressFile);
        DereferenceObject(AddressFile);
    }

    if (Listener)
    {
        DequeuedIrp = TCPAbortListenForSocket(Listener, Connection);
        DereferenceObject(Listener);
    }

    if (DequeuedIrp)
    {
        Irp->IoStatus.Information = 0;
        IRPFinish(Irp, STATUS_CANCELLED);
    }

    if (Connection) DereferenceObject(Connection);

    TI_DbgPrint(MAX_TRACE, ("Leaving.\n"));
}


NTSTATUS DispTdiAccept(
  PIRP Irp)
/*
 * FUNCTION: TDI_ACCEPT handler
 * ARGUMENTS:
 *     Irp = Pointer to an I/O request packet
 * RETURNS:
 *     Status of operation
 */
{
  TI_DbgPrint(DEBUG_IRP, ("Called.\n"));

  return STATUS_NOT_IMPLEMENTED;
}


NTSTATUS DispTdiAssociateAddress(
    PIRP Irp)
/*
 * FUNCTION: TDI_ASSOCIATE_ADDRESS handler
 * ARGUMENTS:
 *     Irp = Pointer to an I/O request packet
 * RETURNS:
 *     Status of operation
 */
{
  PTDI_REQUEST_KERNEL_ASSOCIATE Parameters;
  PTRANSPORT_CONTEXT TranContext;
  PIO_STACK_LOCATION IrpSp;
  PCONNECTION_ENDPOINT Connection, LastConnection;
  PFILE_OBJECT FileObject;
  PADDRESS_FILE AddrFile = NULL;
  NTSTATUS Status;

  TI_DbgPrint(DEBUG_IRP, ("Called.\n"));

  IrpSp = IoGetCurrentIrpStackLocation(Irp);

  /* Get associated connection endpoint file object. Quit if none exists */

  TranContext = IrpSp->FileObject->FsContext;
  if (!TranContext) {
    TI_DbgPrint(MID_TRACE, ("Bad transport context.\n"));
    return STATUS_INVALID_PARAMETER;
  }

  Connection = (PCONNECTION_ENDPOINT)TranContext->Handle.ConnectionContext;
  if (!Connection) {
    TI_DbgPrint(MID_TRACE, ("No connection endpoint file object.\n"));
    return STATUS_INVALID_PARAMETER;
  }

  Parameters = (PTDI_REQUEST_KERNEL_ASSOCIATE)&IrpSp->Parameters;

  Status = ObReferenceObjectByHandle(
    Parameters->AddressHandle,
    0,
    *IoFileObjectType,
    KernelMode,
    (PVOID*)&FileObject,
    NULL);
  if (!NT_SUCCESS(Status)) {
    TI_DbgPrint(MID_TRACE, ("Bad address file object handle (0x%X): %x.\n",
      Parameters->AddressHandle, Status));
    return STATUS_INVALID_PARAMETER;
  }

  LockObject(Connection);

  if (Connection->AddressFile) {
    ObDereferenceObject(FileObject);
    UnlockObject(Connection);
    TI_DbgPrint(MID_TRACE, ("An address file is already associated.\n"));
    return STATUS_INVALID_PARAMETER;
  }

  if (FileObject->FsContext2 != (PVOID)TDI_TRANSPORT_ADDRESS_FILE) {
    ObDereferenceObject(FileObject);
    UnlockObject(Connection);
    TI_DbgPrint(MID_TRACE, ("Bad address file object. Magic (0x%X).\n",
      FileObject->FsContext2));
    return STATUS_INVALID_PARAMETER;
  }

  /* Get associated address file object. Quit if none exists */

  TranContext = FileObject->FsContext;
  if (!TranContext) {
    ObDereferenceObject(FileObject);
    UnlockObject(Connection);
    TI_DbgPrint(MID_TRACE, ("Bad transport context.\n"));
    return STATUS_INVALID_PARAMETER;
  }

  AddrFile = (PADDRESS_FILE)TranContext->Handle.AddressHandle;
  if (!AddrFile) {
      UnlockObject(Connection);
      ObDereferenceObject(FileObject);
      TI_DbgPrint(MID_TRACE, ("No address file object.\n"));
      return STATUS_INVALID_PARAMETER;
  }

  LockObject(AddrFile);

  ReferenceObject(AddrFile);
  Connection->AddressFile = AddrFile;

  /* Add connection endpoint to the address file */
  ReferenceObject(Connection);
  if (AddrFile->Connection == NULL)
      AddrFile->Connection = Connection;
  else
  {
      LastConnection = AddrFile->Connection;
      while (LastConnection->Next != NULL)
         LastConnection = LastConnection->Next;
      LastConnection->Next = Connection;
  }

  ObDereferenceObject(FileObject);

  UnlockObject(AddrFile);
  UnlockObject(Connection);

  return STATUS_SUCCESS;
}


NTSTATUS DispTdiConnect(
  PIRP Irp)
/*
 * FUNCTION: TDI_CONNECT handler
 * ARGUMENTS:
 *     Irp = Pointer to an I/O request packet
 * RETURNS:
 *     Status of operation
 */
{
  PCONNECTION_ENDPOINT Connection;
  PTDI_REQUEST_KERNEL Parameters;
  PTRANSPORT_CONTEXT TranContext;
  PIO_STACK_LOCATION IrpSp;
  NTSTATUS Status;

  TI_DbgPrint(DEBUG_IRP, ("Called.\n"));

  IrpSp = IoGetCurrentIrpStackLocation(Irp);

  IoMarkIrpPending(Irp);

  /* Get associated connection endpoint file object. Quit if none exists */

  TranContext = IrpSp->FileObject->FsContext;
  if (!TranContext) {
    TI_DbgPrint(MID_TRACE, ("Bad transport context.\n"));
    Status = STATUS_INVALID_PARAMETER;
    goto done;
  }

  Connection = (PCONNECTION_ENDPOINT)TranContext->Handle.ConnectionContext;
  if (!Connection) {
    TI_DbgPrint(MID_TRACE, ("No connection endpoint file object.\n"));
    Status = STATUS_INVALID_PARAMETER;
    goto done;
  }

  Parameters = (PTDI_REQUEST_KERNEL)&IrpSp->Parameters;

  Status = DispPrepareIrpForCancel(TranContext->Handle.ConnectionContext,
                                   Irp,
                                   DispCancelRequest);

  if (NT_SUCCESS(Status)) {
      Status = TCPConnect(
          TranContext->Handle.ConnectionContext,
          Parameters->RequestConnectionInformation,
          Parameters->ReturnConnectionInformation,
          DispDataRequestComplete,
          Irp );
  }

done:
  if (Status != STATUS_PENDING) {
      DispDataRequestComplete(Irp, Status, 0);
  }

  TI_DbgPrint(MAX_TRACE, ("TCP Connect returned %08x\n", Status));

  return STATUS_PENDING;
}


NTSTATUS DispTdiDisassociateAddress(
  PIRP Irp)
/*
 * FUNCTION: TDI_DISASSOCIATE_ADDRESS handler
 * ARGUMENTS:
 *     Irp = Pointer to an I/O request packet
 * RETURNS:
 *     Status of operation
 */
{
  PCONNECTION_ENDPOINT Connection;
  PTRANSPORT_CONTEXT TranContext;
  PIO_STACK_LOCATION IrpSp;

  TI_DbgPrint(DEBUG_IRP, ("Called.\n"));

  IrpSp = IoGetCurrentIrpStackLocation(Irp);

  /* Get associated connection endpoint file object. Quit if none exists */

  TranContext = IrpSp->FileObject->FsContext;
  if (!TranContext) {
    TI_DbgPrint(MID_TRACE, ("Bad transport context.\n"));
    return STATUS_INVALID_PARAMETER;
  }

  Connection = (PCONNECTION_ENDPOINT)TranContext->Handle.ConnectionContext;
  if (!Connection) {
    TI_DbgPrint(MID_TRACE, ("No connection endpoint file object.\n"));
    return STATUS_INVALID_PARAMETER;
  }

  /* NO-OP because we need the address to deallocate the port when the connection closes */

  return STATUS_SUCCESS;
}


NTSTATUS DispTdiDisconnect(
  PIRP Irp)
/*
 * FUNCTION: TDI_DISCONNECT handler
 * ARGUMENTS:
 *     Irp = Pointer to an I/O request packet
 * RETURNS:
 *     Status of operation
 */
{
  NTSTATUS Status;
  PTDI_REQUEST_KERNEL_DISCONNECT DisReq;
  PCONNECTION_ENDPOINT Connection;
  PTRANSPORT_CONTEXT TranContext;
  PIO_STACK_LOCATION IrpSp;

  TI_DbgPrint(DEBUG_IRP, ("Called.\n"));

  IrpSp = IoGetCurrentIrpStackLocation(Irp);
  DisReq = (PTDI_REQUEST_KERNEL_DISCONNECT)&IrpSp->Parameters;

  IoMarkIrpPending(Irp);

  /* Get associated connection endpoint file object. Quit if none exists */

  TranContext = IrpSp->FileObject->FsContext;
  if (!TranContext) {
    TI_DbgPrint(MID_TRACE, ("Bad transport context.\n"));
    Status = STATUS_INVALID_PARAMETER;
    goto done;
  }

  Connection = (PCONNECTION_ENDPOINT)TranContext->Handle.ConnectionContext;
  if (!Connection) {
    TI_DbgPrint(MID_TRACE, ("No connection endpoint file object.\n"));
    Status = STATUS_INVALID_PARAMETER;
    goto done;
  }

  Status = DispPrepareIrpForCancel
    (TranContext->Handle.ConnectionContext,
     Irp,
     (PDRIVER_CANCEL)DispCancelRequest);

  if (NT_SUCCESS(Status))
  {
      Status = TCPDisconnect(TranContext->Handle.ConnectionContext,
                             DisReq->RequestFlags,
                             DisReq->RequestSpecific,
                             DisReq->RequestConnectionInformation,
                             DisReq->ReturnConnectionInformation,
                             DispDataRequestComplete,
                             Irp);
  }

done:
   if (Status != STATUS_PENDING) {
       DispDataRequestComplete(Irp, Status, 0);
   }

  TI_DbgPrint(MAX_TRACE, ("TCP Disconnect returned %08x\n", Status));

  return STATUS_PENDING;
}


NTSTATUS DispTdiListen(
  PIRP Irp)
/*
 * FUNCTION: TDI_LISTEN handler
 * ARGUMENTS:
 *     Irp = Pointer to an I/O request packet
 * RETURNS:
 *     Status of operation
 */
{
  PCONNECTION_ENDPOINT Connection;
  PTDI_REQUEST_KERNEL Parameters;
  PTRANSPORT_CONTEXT TranContext;
  PIO_STACK_LOCATION IrpSp;
  NTSTATUS Status = STATUS_SUCCESS;

  TI_DbgPrint(DEBUG_IRP, ("Called.\n"));

  IrpSp = IoGetCurrentIrpStackLocation(Irp);

  IoMarkIrpPending(Irp);

  /* Get associated connection endpoint file object. Quit if none exists */

  TranContext = IrpSp->FileObject->FsContext;
  if (TranContext == NULL)
    {
      TI_DbgPrint(MID_TRACE, ("Bad transport context.\n"));
      Status = STATUS_INVALID_PARAMETER;
      goto done;
    }

  Connection = (PCONNECTION_ENDPOINT)TranContext->Handle.ConnectionContext;
  if (Connection == NULL)
    {
      TI_DbgPrint(MID_TRACE, ("No connection endpoint file object.\n"));
      Status = STATUS_INVALID_PARAMETER;
      goto done;
    }

  Parameters = (PTDI_REQUEST_KERNEL)&IrpSp->Parameters;

  Status = DispPrepareIrpForCancel
      (TranContext->Handle.ConnectionContext,
       Irp,
       (PDRIVER_CANCEL)DispCancelListenRequest);

  LockObject(Connection);

  if (Connection->AddressFile == NULL)
  {
     TI_DbgPrint(MID_TRACE, ("No associated address file\n"));
     UnlockObject(Connection);
     Status = STATUS_INVALID_PARAMETER;
     goto done;
  }

  LockObject(Connection->AddressFile);

  /* Listening will require us to create a listening socket and store it in
   * the address file.  It will be signalled, and attempt to complete an irp
   * when a new connection arrives. */
  /* The important thing to note here is that the irp we'll complete belongs
   * to the socket to be accepted onto, not the listener */
  if( NT_SUCCESS(Status) && !Connection->AddressFile->Listener ) {
      Connection->AddressFile->Listener =
	  TCPAllocateConnectionEndpoint( NULL );

      if( !Connection->AddressFile->Listener )
	  Status = STATUS_NO_MEMORY;

      if( NT_SUCCESS(Status) ) {
          ReferenceObject(Connection->AddressFile);
	  Connection->AddressFile->Listener->AddressFile =
	      Connection->AddressFile;

	  Status = TCPSocket( Connection->AddressFile->Listener,
			      Connection->AddressFile->Family,
			      SOCK_STREAM,
			      Connection->AddressFile->Protocol );
      }

      if( NT_SUCCESS(Status) ) {
	  ReferenceObject(Connection->AddressFile->Listener);
	  Status = TCPListen( Connection->AddressFile->Listener, 1024 );
	  /* BACKLOG */
      }
  }

  if( NT_SUCCESS(Status) ) {
      Status = TCPAccept
	  ( (PTDI_REQUEST)Parameters,
	    Connection->AddressFile->Listener,
	    Connection,
	    DispDataRequestComplete,
	    Irp );
  }

  UnlockObject(Connection->AddressFile);
  UnlockObject(Connection);

done:
  if (Status != STATUS_PENDING) {
      DispDataRequestComplete(Irp, Status, 0);
  }

  TI_DbgPrint(MID_TRACE,("Leaving %x\n", Status));

  return STATUS_PENDING;
}


NTSTATUS DispTdiQueryInformation(
  PDEVICE_OBJECT DeviceObject,
  PIRP Irp)
/*
 * FUNCTION: TDI_QUERY_INFORMATION handler
 * ARGUMENTS:
 *     DeviceObject = Pointer to device object structure
 *     Irp          = Pointer to an I/O request packet
 * RETURNS:
 *     Status of operation
 */
{
  PTDI_REQUEST_KERNEL_QUERY_INFORMATION Parameters;
  PTRANSPORT_CONTEXT TranContext;
  PIO_STACK_LOCATION IrpSp;

  TI_DbgPrint(DEBUG_IRP, ("Called.\n"));

  IrpSp = IoGetCurrentIrpStackLocation(Irp);
  Parameters = (PTDI_REQUEST_KERNEL_QUERY_INFORMATION)&IrpSp->Parameters;

  TranContext = IrpSp->FileObject->FsContext;
  if (!TranContext) {
    TI_DbgPrint(MID_TRACE, ("Bad transport context.\n"));
    return STATUS_INVALID_PARAMETER;
  }

  switch (Parameters->QueryType)
  {
    case TDI_QUERY_PROVIDER_INFO:
      {
        PTDI_PROVIDER_INFO ProviderInfo;

        if (DeviceObject == IPDeviceObject)
          return STATUS_NOT_IMPLEMENTED;

        if (MmGetMdlByteCount(Irp->MdlAddress) < sizeof(*ProviderInfo)) {
          TI_DbgPrint(MID_TRACE, ("MDL buffer too small.\n"));
          return STATUS_BUFFER_TOO_SMALL;
        }

        ProviderInfo = (PTDI_PROVIDER_INFO)MmGetSystemAddressForMdl(Irp->MdlAddress);
        RtlZeroMemory(ProviderInfo, sizeof(*ProviderInfo));

        ProviderInfo->Version = 0x0100;
        ProviderInfo->MaxSendSize = 0xFFFFFFFF;
        ProviderInfo->MaxConnectionUserData = 0;
        ProviderInfo->MaxDatagramSize = 65507;
        ProviderInfo->ServiceFlags = TDI_SERVICE_CONNECTION_MODE |
                                     TDI_SERVICE_ORDERLY_RELEASE |
                                     TDI_SERVICE_CONNECTIONLESS_MODE |
                                     TDI_SERVICE_ERROR_FREE_DELIVERY |
                                     TDI_SERVICE_BROADCAST_SUPPORTED |
                                     TDI_SERVICE_DELAYED_ACCEPTANCE |
                                     TDI_SERVICE_EXPEDITED_DATA |
                                     TDI_SERVICE_NO_ZERO_LENGTH |
                                     TDI_SERVICE_DGRAM_CONNECTION |
                                     TDI_SERVICE_FORCE_ACCESS_CHECK |
                                     TDI_SERVICE_SEND_AND_DISCONNECT |
                                     TDI_SERVICE_ACCEPT_LOCAL_ADDR |
                                     TDI_SERVICE_ADDRESS_SECURITY |
                                     TDI_SERVICE_PREPOST_RECVS |
                                     TDI_SERVICE_NO_PUSH;
        ProviderInfo->MinimumLookaheadData = 1;
        ProviderInfo->MaximumLookaheadData = 65535;
        ProviderInfo->NumberOfResources = 0;

        return STATUS_SUCCESS;
      }

    case TDI_QUERY_ADDRESS_INFO:
      {
        PTDI_ADDRESS_INFO AddressInfo;
        PADDRESS_FILE AddrFile;
        PTA_IP_ADDRESS Address;
        PCONNECTION_ENDPOINT Endpoint = NULL;


        if (MmGetMdlByteCount(Irp->MdlAddress) <
            (FIELD_OFFSET(TDI_ADDRESS_INFO, Address.Address[0].Address) +
             sizeof(TDI_ADDRESS_IP))) {
          TI_DbgPrint(MID_TRACE, ("MDL buffer too small.\n"));
          return STATUS_BUFFER_TOO_SMALL;
        }

        AddressInfo = (PTDI_ADDRESS_INFO)MmGetSystemAddressForMdl(Irp->MdlAddress);
		Address = (PTA_IP_ADDRESS)&AddressInfo->Address;

        switch ((ULONG_PTR)IrpSp->FileObject->FsContext2) {
          case TDI_TRANSPORT_ADDRESS_FILE:
            AddrFile = (PADDRESS_FILE)TranContext->Handle.AddressHandle;
            if (AddrFile == NULL)
            {
                TI_DbgPrint(MIN_TRACE, ("FIXME: No address file object.\n"));
                ASSERT(AddrFile != NULL);
                return STATUS_INVALID_PARAMETER;
            }

			Address->TAAddressCount = 1;
			Address->Address[0].AddressLength = TDI_ADDRESS_LENGTH_IP;
			Address->Address[0].AddressType = TDI_ADDRESS_TYPE_IP;
			Address->Address[0].Address[0].sin_port = AddrFile->Port;
			Address->Address[0].Address[0].in_addr = AddrFile->Address.Address.IPv4Address;
			RtlZeroMemory(
				&Address->Address[0].Address[0].sin_zero,
				sizeof(Address->Address[0].Address[0].sin_zero));
			return STATUS_SUCCESS;

          case TDI_CONNECTION_FILE:
            Endpoint =
				(PCONNECTION_ENDPOINT)TranContext->Handle.ConnectionContext;
            if (Endpoint == NULL || Endpoint->AddressFile == NULL)
            {
                TI_DbgPrint(MIN_TRACE, ("FIXME: No connection endpoint file object.\n"));
                ASSERT(Endpoint != NULL && Endpoint->AddressFile != NULL);
                return STATUS_INVALID_PARAMETER;
            }

            Address->TAAddressCount = 1;
            Address->Address[0].AddressLength = TDI_ADDRESS_LENGTH_IP;
            Address->Address[0].AddressType = TDI_ADDRESS_TYPE_IP;
            Address->Address[0].Address[0].sin_port = Endpoint->AddressFile->Port;
            Address->Address[0].Address[0].in_addr = Endpoint->AddressFile->Address.Address.IPv4Address;
			RtlZeroMemory(
				&Address->Address[0].Address[0].sin_zero,
				sizeof(Address->Address[0].Address[0].sin_zero));
            return STATUS_SUCCESS;

          default:
            TI_DbgPrint(MIN_TRACE, ("Invalid transport context\n"));
            return STATUS_INVALID_PARAMETER;
        }
      }

    case TDI_QUERY_CONNECTION_INFO:
      {
        PTDI_CONNECTION_INFO ConnectionInfo;
        //PCONNECTION_ENDPOINT Endpoint;

        if (MmGetMdlByteCount(Irp->MdlAddress) < sizeof(*ConnectionInfo)) {
          TI_DbgPrint(MID_TRACE, ("MDL buffer too small.\n"));
          return STATUS_BUFFER_TOO_SMALL;
        }

        ConnectionInfo = (PTDI_CONNECTION_INFO)
          MmGetSystemAddressForMdl(Irp->MdlAddress);

        switch ((ULONG_PTR)IrpSp->FileObject->FsContext2) {
          case TDI_CONNECTION_FILE:
            //Endpoint = (PCONNECTION_ENDPOINT)TranContext->Handle.ConnectionContext;
            RtlZeroMemory(ConnectionInfo, sizeof(*ConnectionInfo));
            return STATUS_SUCCESS;

          default:
            TI_DbgPrint(MIN_TRACE, ("Invalid transport context\n"));
            return STATUS_INVALID_PARAMETER;
        }
      }

      case TDI_QUERY_MAX_DATAGRAM_INFO:
      {
          PTDI_MAX_DATAGRAM_INFO MaxDatagramInfo;

          if (MmGetMdlByteCount(Irp->MdlAddress) < sizeof(*MaxDatagramInfo)) {
              TI_DbgPrint(MID_TRACE, ("MDL buffer too small.\n"));
              return STATUS_BUFFER_TOO_SMALL;
          }

          MaxDatagramInfo = (PTDI_MAX_DATAGRAM_INFO)
            MmGetSystemAddressForMdl(Irp->MdlAddress);

          MaxDatagramInfo->MaxDatagramSize = 0xFFFF;

          return STATUS_SUCCESS;
     }
  }

  return STATUS_NOT_IMPLEMENTED;
}


NTSTATUS DispTdiReceive(
  PIRP Irp)
/*
 * FUNCTION: TDI_RECEIVE handler
 * ARGUMENTS:
 *     Irp = Pointer to an I/O request packet
 * RETURNS:
 *     Status of operation
 */
{
  PIO_STACK_LOCATION IrpSp;
  PTDI_REQUEST_KERNEL_RECEIVE ReceiveInfo;
  PTRANSPORT_CONTEXT TranContext;
  NTSTATUS Status;
  ULONG BytesReceived = 0;

  TI_DbgPrint(DEBUG_IRP, ("Called.\n"));

  IrpSp = IoGetCurrentIrpStackLocation(Irp);
  ReceiveInfo = (PTDI_REQUEST_KERNEL_RECEIVE)&(IrpSp->Parameters);

  IoMarkIrpPending(Irp);

  TranContext = IrpSp->FileObject->FsContext;
  if (TranContext == NULL)
    {
      TI_DbgPrint(MID_TRACE, ("Bad transport context.\n"));
      Status = STATUS_INVALID_PARAMETER;
      goto done;
    }

  if (TranContext->Handle.ConnectionContext == NULL)
    {
      TI_DbgPrint(MID_TRACE, ("No connection endpoint file object.\n"));
      Status = STATUS_INVALID_PARAMETER;
      goto done;
    }

  /* Initialize a receive request */
  Status = DispPrepareIrpForCancel
      (TranContext->Handle.ConnectionContext,
       Irp,
       (PDRIVER_CANCEL)DispCancelRequest);

  TI_DbgPrint(MID_TRACE,("TCPIP<<< Got an MDL: %x\n", Irp->MdlAddress));
  if (NT_SUCCESS(Status))
    {
      Status = TCPReceiveData(
	  TranContext->Handle.ConnectionContext,
	  (PNDIS_BUFFER)Irp->MdlAddress,
	  ReceiveInfo->ReceiveLength,
	  &BytesReceived,
	  ReceiveInfo->ReceiveFlags,
	  DispDataRequestComplete,
	  Irp);
    }

done:
  if (Status != STATUS_PENDING) {
      DispDataRequestComplete(Irp, Status, BytesReceived);
  }

  TI_DbgPrint(DEBUG_IRP, ("Leaving. Status is (0x%X)\n", Status));

  return STATUS_PENDING;
}


NTSTATUS DispTdiReceiveDatagram(
    PIRP Irp)
/*
 * FUNCTION: TDI_RECEIVE_DATAGRAM handler
 * ARGUMENTS:
 *     Irp = Pointer to an I/O request packet
 * RETURNS:
 *     Status of operation
 */
{
  PIO_STACK_LOCATION IrpSp;
  PTDI_REQUEST_KERNEL_RECEIVEDG DgramInfo;
  PTRANSPORT_CONTEXT TranContext;
  TDI_REQUEST Request;
  NTSTATUS Status;
  ULONG BytesReceived = 0;

  TI_DbgPrint(DEBUG_IRP, ("Called.\n"));

  IrpSp     = IoGetCurrentIrpStackLocation(Irp);
  DgramInfo = (PTDI_REQUEST_KERNEL_RECEIVEDG)&(IrpSp->Parameters);

  IoMarkIrpPending(Irp);

  TranContext = IrpSp->FileObject->FsContext;
  if (TranContext == NULL)
    {
      TI_DbgPrint(MID_TRACE, ("Bad transport context.\n"));
      Status = STATUS_INVALID_PARAMETER;
      goto done;
    }

  /* Initialize a receive request */
  Request.Handle.AddressHandle = TranContext->Handle.AddressHandle;
  Request.RequestNotifyObject  = DispDataRequestComplete;
  Request.RequestContext       = Irp;

  Status = DispPrepareIrpForCancel(
    IrpSp->FileObject->FsContext,
    Irp,
    (PDRIVER_CANCEL)DispCancelRequest);

  if (NT_SUCCESS(Status))
    {
	PVOID DataBuffer;
	UINT BufferSize;

	NdisQueryBuffer( (PNDIS_BUFFER)Irp->MdlAddress,
			 &DataBuffer,
			 &BufferSize );

      Status = DGReceiveDatagram(
	  Request.Handle.AddressHandle,
	  DgramInfo->ReceiveDatagramInformation,
	  DataBuffer,
	  DgramInfo->ReceiveLength,
	  DgramInfo->ReceiveFlags,
	  DgramInfo->ReturnDatagramInformation,
	  &BytesReceived,
	  (PDATAGRAM_COMPLETION_ROUTINE)DispDataRequestComplete,
	  Irp,
          Irp);
    }

done:
   if (Status != STATUS_PENDING) {
       DispDataRequestComplete(Irp, Status, BytesReceived);
   }

  TI_DbgPrint(DEBUG_IRP, ("Leaving. Status is (0x%X)\n", Status));

  return STATUS_PENDING;
}


NTSTATUS DispTdiSend(
    PIRP Irp)
/*
 * FUNCTION: TDI_SEND handler
 * ARGUMENTS:
 *     Irp = Pointer to an I/O request packet
 * RETURNS:
 *     Status of operation
 */
{
  PIO_STACK_LOCATION IrpSp;
  PTDI_REQUEST_KERNEL_SEND SendInfo;
  PTRANSPORT_CONTEXT TranContext;
  NTSTATUS Status;
  ULONG BytesSent = 0;

  TI_DbgPrint(DEBUG_IRP, ("Called.\n"));

  IrpSp = IoGetCurrentIrpStackLocation(Irp);
  SendInfo = (PTDI_REQUEST_KERNEL_SEND)&(IrpSp->Parameters);

  IoMarkIrpPending(Irp);

  TranContext = IrpSp->FileObject->FsContext;
  if (TranContext == NULL)
    {
      TI_DbgPrint(MID_TRACE, ("Bad transport context.\n"));
      Status = STATUS_INVALID_PARAMETER;
      goto done;
    }

  if (TranContext->Handle.ConnectionContext == NULL)
    {
      TI_DbgPrint(MID_TRACE, ("No connection endpoint file object.\n"));
      Status = STATUS_INVALID_PARAMETER;
      goto done;
    }

  Status = DispPrepareIrpForCancel(
    IrpSp->FileObject->FsContext,
    Irp,
    (PDRIVER_CANCEL)DispCancelRequest);

  TI_DbgPrint(MID_TRACE,("TCPIP<<< Got an MDL: %x\n", Irp->MdlAddress));
  if (NT_SUCCESS(Status))
    {
	PVOID Data;
	UINT Len;

	NdisQueryBuffer( Irp->MdlAddress, &Data, &Len );

	TI_DbgPrint(MID_TRACE,("About to TCPSendData\n"));
	Status = TCPSendData(
	    TranContext->Handle.ConnectionContext,
	    Data,
	    SendInfo->SendLength,
	    &BytesSent,
	    SendInfo->SendFlags,
	    DispDataRequestComplete,
	    Irp);
    }

done:
   if (Status != STATUS_PENDING) {
       DispDataRequestComplete(Irp, Status, BytesSent);
   }

  TI_DbgPrint(DEBUG_IRP, ("Leaving. Status is (0x%X)\n", Status));

  return STATUS_PENDING;
}


NTSTATUS DispTdiSendDatagram(
    PIRP Irp)
/*
 * FUNCTION: TDI_SEND_DATAGRAM handler
 * ARGUMENTS:
 *     Irp = Pointer to an I/O request packet
 * RETURNS:
 *     Status of operation
 */
{
    PIO_STACK_LOCATION IrpSp;
    TDI_REQUEST Request;
    PTDI_REQUEST_KERNEL_SENDDG DgramInfo;
    PTRANSPORT_CONTEXT TranContext;
    NTSTATUS Status;

    TI_DbgPrint(DEBUG_IRP, ("Called.\n"));

    IrpSp       = IoGetCurrentIrpStackLocation(Irp);
    DgramInfo   = (PTDI_REQUEST_KERNEL_SENDDG)&(IrpSp->Parameters);

    IoMarkIrpPending(Irp);

    TranContext = IrpSp->FileObject->FsContext;
    if (TranContext == NULL)
    {
      TI_DbgPrint(MID_TRACE, ("Bad transport context.\n"));
      Status = STATUS_INVALID_PARAMETER;
      goto done;
    }

    /* Initialize a send request */
    Request.Handle.AddressHandle = TranContext->Handle.AddressHandle;
    Request.RequestNotifyObject  = DispDataRequestComplete;
    Request.RequestContext       = Irp;

    Status = DispPrepareIrpForCancel(
        IrpSp->FileObject->FsContext,
        Irp,
        (PDRIVER_CANCEL)DispCancelRequest);

    if (NT_SUCCESS(Status)) {
	PVOID DataBuffer;
	UINT BufferSize;

	TI_DbgPrint(MID_TRACE,("About to query buffer %x\n", Irp->MdlAddress));

	NdisQueryBuffer( (PNDIS_BUFFER)Irp->MdlAddress,
			 &DataBuffer,
			 &BufferSize );

        /* FIXME: DgramInfo->SendDatagramInformation->RemoteAddress
           must be of type PTDI_ADDRESS_IP */
	TI_DbgPrint(MID_TRACE,
		    ("About to call send routine %x\n",
		     (*((PADDRESS_FILE)Request.Handle.AddressHandle)->Send)));

        if( (*((PADDRESS_FILE)Request.Handle.AddressHandle)->Send != NULL) )
        {
	        ULONG DataUsed = 0;
            Status = (*((PADDRESS_FILE)Request.Handle.AddressHandle)->Send)(
                Request.Handle.AddressHandle,
                DgramInfo->SendDatagramInformation,
                DataBuffer,
                BufferSize,
                &DataUsed);
            Irp->IoStatus.Information = DataUsed;
        }
        else {
            Status = STATUS_UNSUCCESSFUL;
            ASSERT(FALSE);
        }
    }

done:
    if (Status != STATUS_PENDING) {
        DispDataRequestComplete(Irp, Status, Irp->IoStatus.Information);
    }

    TI_DbgPrint(DEBUG_IRP, ("Leaving.\n"));

    return STATUS_PENDING;
}


NTSTATUS DispTdiSetEventHandler(PIRP Irp)
/*
 * FUNCTION: TDI_SET_EVENT_HANDER handler
 * ARGUMENTS:
 *     Irp = Pointer to a I/O request packet
 * RETURNS:
 *     Status of operation
 */
{
  PTDI_REQUEST_KERNEL_SET_EVENT Parameters;
  PTRANSPORT_CONTEXT TranContext;
  PIO_STACK_LOCATION IrpSp;
  PADDRESS_FILE AddrFile;
  NTSTATUS Status;

  TI_DbgPrint(DEBUG_IRP, ("Called.\n"));

  IrpSp = IoGetCurrentIrpStackLocation(Irp);

  /* Get associated address file object. Quit if none exists */

  TranContext = IrpSp->FileObject->FsContext;
  if (!TranContext) {
    TI_DbgPrint(MIN_TRACE, ("Bad transport context.\n"));
    return STATUS_INVALID_PARAMETER;
  }

  AddrFile = (PADDRESS_FILE)TranContext->Handle.AddressHandle;
  if (!AddrFile) {
    TI_DbgPrint(MIN_TRACE, ("No address file object.\n"));
    return STATUS_INVALID_PARAMETER;
  }

  Parameters = (PTDI_REQUEST_KERNEL_SET_EVENT)&IrpSp->Parameters;
  Status     = STATUS_SUCCESS;

  LockObject(AddrFile);

  /* Set the event handler. if an event handler is associated with
     a specific event, it's flag (RegisteredXxxHandler) is TRUE.
     If an event handler is not used it's flag is FALSE */
  switch (Parameters->EventType) {
  case TDI_EVENT_CONNECT:
    if (!Parameters->EventHandler) {
      AddrFile->ConnectHandlerContext    = NULL;
      AddrFile->RegisteredConnectHandler = FALSE;
    } else {
      AddrFile->ConnectHandler =
        (PTDI_IND_CONNECT)Parameters->EventHandler;
      AddrFile->ConnectHandlerContext    = Parameters->EventContext;
      AddrFile->RegisteredConnectHandler = TRUE;
    }
    break;

  case TDI_EVENT_DISCONNECT:
    if (!Parameters->EventHandler) {
      AddrFile->DisconnectHandlerContext    = NULL;
      AddrFile->RegisteredDisconnectHandler = FALSE;
    } else {
      AddrFile->DisconnectHandler =
        (PTDI_IND_DISCONNECT)Parameters->EventHandler;
      AddrFile->DisconnectHandlerContext    = Parameters->EventContext;
      AddrFile->RegisteredDisconnectHandler = TRUE;
    }
    break;

    case TDI_EVENT_ERROR:
    if (Parameters->EventHandler == NULL) {
      AddrFile->ErrorHandlerContext    = NULL;
      AddrFile->RegisteredErrorHandler = FALSE;
    } else {
      AddrFile->ErrorHandler =
        (PTDI_IND_ERROR)Parameters->EventHandler;
      AddrFile->ErrorHandlerContext    = Parameters->EventContext;
      AddrFile->RegisteredErrorHandler = TRUE;
    }
    break;

  case TDI_EVENT_RECEIVE:
    if (Parameters->EventHandler == NULL) {
      AddrFile->ReceiveHandlerContext    = NULL;
      AddrFile->RegisteredReceiveHandler = FALSE;
    } else {
      AddrFile->ReceiveHandler =
        (PTDI_IND_RECEIVE)Parameters->EventHandler;
      AddrFile->ReceiveHandlerContext    = Parameters->EventContext;
      AddrFile->RegisteredReceiveHandler = TRUE;
    }
    break;

  case TDI_EVENT_RECEIVE_DATAGRAM:
    if (Parameters->EventHandler == NULL) {
      AddrFile->ReceiveDatagramHandlerContext    = NULL;
      AddrFile->RegisteredReceiveDatagramHandler = FALSE;
    } else {
      AddrFile->ReceiveDatagramHandler =
        (PTDI_IND_RECEIVE_DATAGRAM)Parameters->EventHandler;
      AddrFile->ReceiveDatagramHandlerContext    = Parameters->EventContext;
      AddrFile->RegisteredReceiveDatagramHandler = TRUE;
    }
    break;

  case TDI_EVENT_RECEIVE_EXPEDITED:
    if (Parameters->EventHandler == NULL) {
      AddrFile->ExpeditedReceiveHandlerContext    = NULL;
      AddrFile->RegisteredExpeditedReceiveHandler = FALSE;
    } else {
      AddrFile->ExpeditedReceiveHandler =
        (PTDI_IND_RECEIVE_EXPEDITED)Parameters->EventHandler;
      AddrFile->ExpeditedReceiveHandlerContext    = Parameters->EventContext;
      AddrFile->RegisteredExpeditedReceiveHandler = TRUE;
    }
    break;

  case TDI_EVENT_CHAINED_RECEIVE:
    if (Parameters->EventHandler == NULL) {
      AddrFile->ChainedReceiveHandlerContext    = NULL;
      AddrFile->RegisteredChainedReceiveHandler = FALSE;
    } else {
      AddrFile->ChainedReceiveHandler =
        (PTDI_IND_CHAINED_RECEIVE)Parameters->EventHandler;
      AddrFile->ChainedReceiveHandlerContext    = Parameters->EventContext;
      AddrFile->RegisteredChainedReceiveHandler = TRUE;
    }
    break;

  case TDI_EVENT_CHAINED_RECEIVE_DATAGRAM:
    if (Parameters->EventHandler == NULL) {
      AddrFile->ChainedReceiveDatagramHandlerContext    = NULL;
      AddrFile->RegisteredChainedReceiveDatagramHandler = FALSE;
    } else {
      AddrFile->ChainedReceiveDatagramHandler =
        (PTDI_IND_CHAINED_RECEIVE_DATAGRAM)Parameters->EventHandler;
      AddrFile->ChainedReceiveDatagramHandlerContext    = Parameters->EventContext;
      AddrFile->RegisteredChainedReceiveDatagramHandler = TRUE;
    }
    break;

  case TDI_EVENT_CHAINED_RECEIVE_EXPEDITED:
    if (Parameters->EventHandler == NULL) {
      AddrFile->ChainedReceiveExpeditedHandlerContext    = NULL;
      AddrFile->RegisteredChainedReceiveExpeditedHandler = FALSE;
    } else {
      AddrFile->ChainedReceiveExpeditedHandler =
        (PTDI_IND_CHAINED_RECEIVE_EXPEDITED)Parameters->EventHandler;
      AddrFile->ChainedReceiveExpeditedHandlerContext    = Parameters->EventContext;
      AddrFile->RegisteredChainedReceiveExpeditedHandler = TRUE;
    }
    break;

  default:
    TI_DbgPrint(MIN_TRACE, ("Unknown event type (0x%X).\n",
      Parameters->EventType));

    Status = STATUS_INVALID_PARAMETER;
  }

  UnlockObject(AddrFile);

  return Status;
}


NTSTATUS DispTdiSetInformation(
    PIRP Irp)
/*
 * FUNCTION: TDI_SET_INFORMATION handler
 * ARGUMENTS:
 *     Irp = Pointer to an I/O request packet
 * RETURNS:
 *     Status of operation
 */
{
    TI_DbgPrint(DEBUG_IRP, ("Called.\n"));

	return STATUS_NOT_IMPLEMENTED;
}


VOID DispTdiQueryInformationExComplete(
    PVOID Context,
    ULONG Status,
    UINT ByteCount)
/*
 * FUNCTION: Completes a TDI QueryInformationEx request
 * ARGUMENTS:
 *     Context   = Pointer to the IRP for the request
 *     Status    = TDI status of the request
 *     ByteCount = Number of bytes returned in output buffer
 *
 * dev-nt6-1: this dispatch path uses kernel-pool buffers and copies
 * to/from user space via SEH at completion time, instead of relying on
 * MmProbeAndLockPages which is broken in ReactOS for valid user
 * buffers. The dispatch saves user pointers in QueryContext->User*Buffer
 * and we copy back here.
 */
{
    PTI_QUERY_CONTEXT QueryContext;

    QueryContext = (PTI_QUERY_CONTEXT)Context;

    if (NT_SUCCESS(Status))
    {
        /* Write the resolved Context block back into the user input
         * buffer at offset FIELD_OFFSET(TCP_REQUEST_QUERY_INFORMATION_EX, Context). */
        if (QueryContext->UserInputBuffer)
        {
            _SEH2_TRY {
                RtlCopyMemory(
                    (PUCHAR)QueryContext->UserInputBuffer +
                        FIELD_OFFSET(TCP_REQUEST_QUERY_INFORMATION_EX, Context),
                    &QueryContext->QueryInfo.Context,
                    CONTEXT_SIZE);
            } _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) {
                /* User buffer went away — keep going. */
            } _SEH2_END;
        }
        else if (QueryContext->InputMdl)
        {
            /* Legacy path: writeback via MDL (only used if the dispatch
             * didn't take the kernel-buffer path). */
            CopyBufferToBufferChain(
                QueryContext->InputMdl,
                FIELD_OFFSET(TCP_REQUEST_QUERY_INFORMATION_EX, Context),
                (PCHAR)&QueryContext->QueryInfo.Context,
                CONTEXT_SIZE);
        }

        /* Copy the kernel output buffer back to the user output buffer. */
        if (QueryContext->KernelOutputBuffer && QueryContext->UserOutputBuffer)
        {
            ULONG CopyLen = (ByteCount < QueryContext->UserOutputLength)
                ? ByteCount
                : QueryContext->UserOutputLength;
            _SEH2_TRY {
                RtlCopyMemory(QueryContext->UserOutputBuffer,
                              QueryContext->KernelOutputBuffer,
                              CopyLen);
            } _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) {
                /* User buffer went away — keep going. */
            } _SEH2_END;
        }
    }

    /* Free MDLs (built via MmBuildMdlForNonPagedPool, no MmUnlockPages
     * needed because we never locked). */
    if (QueryContext->InputMdl)
        IoFreeMdl(QueryContext->InputMdl);
    if (QueryContext->OutputMdl)
        IoFreeMdl(QueryContext->OutputMdl);

    /* Free kernel-pool copy buffers. */
    if (QueryContext->KernelInputBuffer)
        ExFreePoolWithTag(QueryContext->KernelInputBuffer, QUERY_CONTEXT_TAG);
    if (QueryContext->KernelOutputBuffer)
        ExFreePoolWithTag(QueryContext->KernelOutputBuffer, QUERY_CONTEXT_TAG);

    QueryContext->Irp->IoStatus.Information = ByteCount;
    QueryContext->Irp->IoStatus.Status      = Status;

    ExFreePoolWithTag(QueryContext, QUERY_CONTEXT_TAG);
}


NTSTATUS DispTdiQueryInformationEx(
    PIRP Irp,
    PIO_STACK_LOCATION IrpSp)
/*
 * FUNCTION: TDI QueryInformationEx handler
 * ARGUMENTS:
 *     Irp   = Pointer to I/O request packet
 *     IrpSp = Pointer to current stack location of Irp
 * RETURNS:
 *     Status of operation
 */
{
    PTCP_REQUEST_QUERY_INFORMATION_EX InputBuffer;
    PTRANSPORT_CONTEXT TranContext;
    PTI_QUERY_CONTEXT QueryContext;
    PVOID OutputBuffer;
    TDI_REQUEST Request;
    UINT Size;
    UINT InputBufferLength;
    UINT OutputBufferLength;
    BOOLEAN InputMdlLocked  = FALSE;
    BOOLEAN OutputMdlLocked = FALSE;
    PMDL InputMdl           = NULL;
    PMDL OutputMdl          = NULL;
    NTSTATUS Status         = STATUS_SUCCESS;

    TranContext = (PTRANSPORT_CONTEXT)IrpSp->FileObject->FsContext;

    switch ((ULONG_PTR)IrpSp->FileObject->FsContext2) {
    case TDI_TRANSPORT_ADDRESS_FILE:
        Request.Handle.AddressHandle = TranContext->Handle.AddressHandle;
        break;

    case TDI_CONNECTION_FILE:
        Request.Handle.ConnectionContext = TranContext->Handle.ConnectionContext;
        break;

    case TDI_CONTROL_CHANNEL_FILE:
        Request.Handle.ControlChannel = TranContext->Handle.ControlChannel;
        break;

    default:
        return STATUS_INVALID_PARAMETER;
    }

    InputBufferLength  = IrpSp->Parameters.DeviceIoControl.InputBufferLength;
    OutputBufferLength = IrpSp->Parameters.DeviceIoControl.OutputBufferLength;

    /* Validate parameters */
    if ((InputBufferLength == sizeof(TCP_REQUEST_QUERY_INFORMATION_EX)) &&
        (OutputBufferLength != 0)) {

        InputBuffer = (PTCP_REQUEST_QUERY_INFORMATION_EX)
            IrpSp->Parameters.DeviceIoControl.Type3InputBuffer;
        OutputBuffer = Irp->UserBuffer;

        QueryContext = ExAllocatePoolWithTag(NonPagedPool, sizeof(TI_QUERY_CONTEXT), QUERY_CONTEXT_TAG);
        if (QueryContext) {
            RtlZeroMemory(QueryContext, sizeof(*QueryContext));

            /* Allocate kernel-pool buffers and SEH-copy the input from
             * user space. Bypass MmProbeAndLockPages entirely since
             * ReactOS's implementation faults on valid user pointers. */
            QueryContext->KernelInputBuffer = ExAllocatePoolWithTag(
                NonPagedPool,
                sizeof(TCP_REQUEST_QUERY_INFORMATION_EX),
                QUERY_CONTEXT_TAG);
            QueryContext->KernelOutputBuffer = ExAllocatePoolWithTag(
                NonPagedPool, OutputBufferLength, QUERY_CONTEXT_TAG);

            if (!QueryContext->KernelInputBuffer || !QueryContext->KernelOutputBuffer)
            {
                Status = STATUS_INSUFFICIENT_RESOURCES;
                goto fetch_cleanup;
            }

            _SEH2_TRY {
                RtlCopyMemory(QueryContext->KernelInputBuffer,
                              InputBuffer,
                              sizeof(TCP_REQUEST_QUERY_INFORMATION_EX));
                RtlCopyMemory(&QueryContext->QueryInfo,
                              QueryContext->KernelInputBuffer,
                              sizeof(TCP_REQUEST_QUERY_INFORMATION_EX));
                Status = STATUS_SUCCESS;
            } _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) {
                Status = _SEH2_GetExceptionCode();
                goto fetch_cleanup;
            } _SEH2_END;

            /* Build MDLs that wrap the KERNEL-pool buffers. These are
             * always valid in any process context, so the legacy
             * CopyBufferToBufferChain calls in InfoTdi* work without
             * touching user memory. */
            InputMdl = IoAllocateMdl(QueryContext->KernelInputBuffer,
                sizeof(TCP_REQUEST_QUERY_INFORMATION_EX), FALSE, FALSE, NULL);
            OutputMdl = IoAllocateMdl(QueryContext->KernelOutputBuffer,
                OutputBufferLength, FALSE, FALSE, NULL);

            if (!InputMdl || !OutputMdl)
            {
                Status = STATUS_INSUFFICIENT_RESOURCES;
                goto fetch_cleanup;
            }

            MmBuildMdlForNonPagedPool(InputMdl);
            MmBuildMdlForNonPagedPool(OutputMdl);
            InputMdlLocked = TRUE;    /* not locked, but treated as such for cleanup */
            OutputMdlLocked = TRUE;

            /* Save the original user pointers so completion can copy back. */
            QueryContext->UserInputBuffer  = InputBuffer;
            QueryContext->UserOutputBuffer = OutputBuffer;
            QueryContext->UserOutputLength = OutputBufferLength;

        fetch_cleanup:
            ;

            if (NT_SUCCESS(Status)) {
                Size = MmGetMdlByteCount(OutputMdl);

                QueryContext->Irp       = Irp;
                QueryContext->InputMdl  = InputMdl;
                QueryContext->OutputMdl = OutputMdl;

                Request.RequestNotifyObject = DispTdiQueryInformationExComplete;
                Request.RequestContext      = QueryContext;
                Status = InfoTdiQueryInformationEx(&Request,
                    &QueryContext->QueryInfo.ID, OutputMdl,
                    &Size, &QueryContext->QueryInfo.Context);
                DispTdiQueryInformationExComplete(QueryContext, Status, Size);

                TI_DbgPrint(MAX_TRACE, ("Leaving. Status = (0x%X)\n", Status));

                return Status;
            }

            /* An error occurred if we get here */

            if (InputMdl) {
                if (InputMdlLocked)
                    MmUnlockPages(InputMdl);
                IoFreeMdl(InputMdl);
            }

            if (OutputMdl) {
                if (OutputMdlLocked)
                    MmUnlockPages(OutputMdl);
                IoFreeMdl(OutputMdl);
            }

            ExFreePoolWithTag(QueryContext, QUERY_CONTEXT_TAG);
        } else
            Status = STATUS_INSUFFICIENT_RESOURCES;
    } else if( InputBufferLength ==
	       sizeof(TCP_REQUEST_QUERY_INFORMATION_EX) ) {
	/* Handle the case where the user is probing the buffer for length */
        InputBuffer = (PTCP_REQUEST_QUERY_INFORMATION_EX)
            IrpSp->Parameters.DeviceIoControl.Type3InputBuffer;

	Size = 0;

        QueryContext = ExAllocatePoolWithTag(NonPagedPool, sizeof(TI_QUERY_CONTEXT), QUERY_CONTEXT_TAG);
        if (!QueryContext) return STATUS_INSUFFICIENT_RESOURCES;
        RtlZeroMemory(QueryContext, sizeof(*QueryContext));

	/* Same kernel-buffer trick as the data-fetch path: SEH-copy
	 * the input from user space, build a kernel-pool MDL, then
	 * dispatch normally. Avoids ReactOS's broken
	 * MmProbeAndLockPages behaviour. */
	QueryContext->KernelInputBuffer = ExAllocatePoolWithTag(
	    NonPagedPool,
	    sizeof(TCP_REQUEST_QUERY_INFORMATION_EX),
	    QUERY_CONTEXT_TAG);
	if (!QueryContext->KernelInputBuffer)
	{
	    ExFreePoolWithTag(QueryContext, QUERY_CONTEXT_TAG);
	    return STATUS_INSUFFICIENT_RESOURCES;
	}

	_SEH2_TRY {
	    RtlCopyMemory(QueryContext->KernelInputBuffer,
	                  InputBuffer,
	                  sizeof(TCP_REQUEST_QUERY_INFORMATION_EX));
	    RtlCopyMemory(&QueryContext->QueryInfo,
	                  QueryContext->KernelInputBuffer,
	                  sizeof(TCP_REQUEST_QUERY_INFORMATION_EX));
	    Status = STATUS_SUCCESS;
	} _SEH2_EXCEPT(EXCEPTION_EXECUTE_HANDLER) {
	    Status = _SEH2_GetExceptionCode();
	} _SEH2_END;

	if (NT_SUCCESS(Status))
	{
	    InputMdl = IoAllocateMdl(QueryContext->KernelInputBuffer,
	                             sizeof(TCP_REQUEST_QUERY_INFORMATION_EX),
	                             FALSE, FALSE, NULL);
	    if (InputMdl == NULL)
	    {
	        Status = STATUS_INSUFFICIENT_RESOURCES;
	    }
	    else
	    {
	        MmBuildMdlForNonPagedPool(InputMdl);
	        InputMdlLocked = TRUE;
	        QueryContext->UserInputBuffer = InputBuffer;
	    }
	}

	if( !NT_SUCCESS(Status) || !InputMdl ) {
	    if( InputMdl ) IoFreeMdl( InputMdl );
	    ExFreePoolWithTag(QueryContext, QUERY_CONTEXT_TAG);
	    return Status;
	}

	RtlCopyMemory(&QueryContext->QueryInfo,
		      InputBuffer, sizeof(TCP_REQUEST_QUERY_INFORMATION_EX));

	QueryContext->Irp       = Irp;
	QueryContext->InputMdl  = InputMdl;
	QueryContext->OutputMdl = NULL;

	Request.RequestNotifyObject = DispTdiQueryInformationExComplete;
	Request.RequestContext      = QueryContext;
	Status = InfoTdiQueryInformationEx(&Request,
					   &QueryContext->QueryInfo.ID,
					   NULL,
					   &Size,
					   &QueryContext->QueryInfo.Context);
	DispTdiQueryInformationExComplete(QueryContext, Status, Size);
	TI_DbgPrint(MAX_TRACE, ("Leaving. Status = (0x%X)\n", Status));
    } else Status = STATUS_INVALID_PARAMETER;

    TI_DbgPrint(MIN_TRACE, ("Leaving. Status = (0x%X)\n", Status));

    return Status;
}


NTSTATUS DispTdiSetInformationEx(
    PIRP Irp,
    PIO_STACK_LOCATION IrpSp)
/*
 * FUNCTION: TDI SetInformationEx handler
 * ARGUMENTS:
 *     Irp   = Pointer to I/O request packet
 *     IrpSp = Pointer to current stack location of Irp
 * RETURNS:
 *     Status of operation
 */
{
    PTRANSPORT_CONTEXT TranContext;
    PTCP_REQUEST_SET_INFORMATION_EX Info;
    TDI_REQUEST Request;
    TDI_STATUS Status;

    TI_DbgPrint(DEBUG_IRP, ("Called.\n"));

    TranContext = (PTRANSPORT_CONTEXT)IrpSp->FileObject->FsContext;
    Info        = (PTCP_REQUEST_SET_INFORMATION_EX)Irp->AssociatedIrp.SystemBuffer;

    switch ((ULONG_PTR)IrpSp->FileObject->FsContext2) {
    case TDI_TRANSPORT_ADDRESS_FILE:
        Request.Handle.AddressHandle = TranContext->Handle.AddressHandle;
        break;

    case TDI_CONNECTION_FILE:
        Request.Handle.ConnectionContext = TranContext->Handle.ConnectionContext;
        break;

    case TDI_CONTROL_CHANNEL_FILE:
        Request.Handle.ControlChannel = TranContext->Handle.ControlChannel;
        break;

    default:
        Irp->IoStatus.Status      = STATUS_INVALID_PARAMETER;
        Irp->IoStatus.Information = 0;

        TI_DbgPrint(DEBUG_IRP, ("Completing IRP at (0x%X).\n", Irp));

        return Irp->IoStatus.Status;
    }

    Request.RequestNotifyObject = NULL;
    Request.RequestContext      = NULL;

    Status = InfoTdiSetInformationEx(&Request, &Info->ID,
            &Info->Buffer, Info->BufferSize);

    return Status;
}

/* TODO: Support multiple addresses per interface.
 * For now just set the nte context to the interface index.
 *
 * Later on, create an NTE context and NTE instance
 */

NTSTATUS DispTdiSetIPAddress( PIRP Irp, PIO_STACK_LOCATION IrpSp ) {
    NTSTATUS Status = STATUS_DEVICE_DOES_NOT_EXIST;
    KIRQL OldIrql;
    PIP_SET_ADDRESS IpAddrChange =
        (PIP_SET_ADDRESS)Irp->AssociatedIrp.SystemBuffer;
    PIP_INTERFACE TargetIF = NULL;
    IF_LIST_ITER(IF);

    TI_DbgPrint(MID_TRACE,("Setting IP Address for adapter %d\n",
			   IpAddrChange->NteIndex));
    TcpipAcquireSpinLock(&InterfaceListLock, &OldIrql);
    ForEachInterface(IF) {
	TI_DbgPrint(MID_TRACE,("Looking at adapter %d\n", IF->Index));

        if( IF->Unicast.Address.IPv4Address == IpAddrChange->Address ) {
            Status = STATUS_DUPLICATE_OBJECTID;
            break;
        }
        if( IF->Index == IpAddrChange->NteIndex ) {
            if (IPReferenceInterface(IF))
                TargetIF = IF;
            break;
        }
    } EndFor(IF);
    TcpipReleaseSpinLock(&InterfaceListLock, OldIrql);

    if (TargetIF) {
        IPRemoveInterfaceRoute(TargetIF);

        TargetIF->Unicast.Type = IP_ADDRESS_V4;
        TargetIF->Unicast.Address.IPv4Address = IpAddrChange->Address;

        TargetIF->Netmask.Type = IP_ADDRESS_V4;
        TargetIF->Netmask.Address.IPv4Address = IpAddrChange->Netmask;

        TargetIF->Broadcast.Type = IP_ADDRESS_V4;
	TargetIF->Broadcast.Address.IPv4Address =
	    TargetIF->Unicast.Address.IPv4Address |
	    ~TargetIF->Netmask.Address.IPv4Address;

        TI_DbgPrint(MID_TRACE,("New Unicast Address: %x\n",
                               TargetIF->Unicast.Address.IPv4Address));
        TI_DbgPrint(MID_TRACE,("New Netmask        : %x\n",
                               TargetIF->Netmask.Address.IPv4Address));

        IPAddInterfaceRoute(TargetIF);

        IpAddrChange->Address = TargetIF->Index;
        Status = STATUS_SUCCESS;
        Irp->IoStatus.Information = TargetIF->Index;
        IPDereferenceInterface(TargetIF);
    }

    Irp->IoStatus.Status = Status;
    return Status;
}

NTSTATUS DispTdiDeleteIPAddress( PIRP Irp, PIO_STACK_LOCATION IrpSp ) {
    NTSTATUS Status = STATUS_UNSUCCESSFUL;
    KIRQL OldIrql;
    PUSHORT NteIndex = Irp->AssociatedIrp.SystemBuffer;
    PIP_INTERFACE TargetIF = NULL;
    IF_LIST_ITER(IF);

    TcpipAcquireSpinLock(&InterfaceListLock, &OldIrql);
    ForEachInterface(IF) {
        if( IF->Index == *NteIndex ) {
            if (IPReferenceInterface(IF))
                TargetIF = IF;
            break;
        }
    } EndFor(IF);
    TcpipReleaseSpinLock(&InterfaceListLock, OldIrql);

    if (TargetIF) {
        IPRemoveInterfaceRoute(TargetIF);
        TargetIF->Unicast.Type = IP_ADDRESS_V4;
        TargetIF->Unicast.Address.IPv4Address = 0;

        TargetIF->Netmask.Type = IP_ADDRESS_V4;
        TargetIF->Netmask.Address.IPv4Address = 0;

        TargetIF->Broadcast.Type = IP_ADDRESS_V4;
        TargetIF->Broadcast.Address.IPv4Address = 0;

        Status = STATUS_SUCCESS;
        IPDereferenceInterface(TargetIF);
    }

    Irp->IoStatus.Status = Status;
    return Status;
}

VOID NTAPI
WaitForHwAddress ( PDEVICE_OBJECT DeviceObject, PVOID Context) {
    PQUERY_HW_WORK_ITEM WorkItem = (PQUERY_HW_WORK_ITEM)Context;
    LARGE_INTEGER Now;
    LARGE_INTEGER Wait;
    IP_ADDRESS Remote;
    PIRP Irp;
    PNEIGHBOR_CACHE_ENTRY NCE = NULL;
    NTSTATUS Status = STATUS_UNSUCCESSFUL;

    IoFreeWorkItem(WorkItem->WorkItem);
    Irp = WorkItem->Irp;
    AddrInitIPv4(&Remote, WorkItem->RemoteIP);
    KeQuerySystemTime(&Now);
    while (Now.QuadPart - WorkItem->StartTime.QuadPart < 10000 * 1000 && !Irp->Cancel) {
        NCE = NBLocateNeighbor(&Remote, WorkItem->Interface);
        if (NCE && !(NCE->State & NUD_INCOMPLETE)) {
            break;
        }

        if (NCE)
            NBDereferenceNeighbor(NCE);
        NCE = NULL;
        Wait.QuadPart = -10000;
        KeDelayExecutionThread(KernelMode, FALSE, &Wait);
        KeQuerySystemTime(&Now);
    }

    if (NCE) {
        PVOID OutputBuffer;

        if (NCE->LinkAddressLength > WorkItem->IrpSp->Parameters.DeviceIoControl.OutputBufferLength) {
            Status = STATUS_INSUFFICIENT_RESOURCES;
        } else {
            OutputBuffer = Irp->AssociatedIrp.SystemBuffer;
            RtlCopyMemory(OutputBuffer, NCE->LinkAddress, NCE->LinkAddressLength);
            Irp->IoStatus.Information = NCE->LinkAddressLength;
            Status = STATUS_SUCCESS;
        }
        NBDereferenceNeighbor(NCE);
    }

    IPDereferenceInterface(WorkItem->Interface);
    ExFreePoolWithTag(WorkItem, QUERY_CONTEXT_TAG);
    if (Irp->Flags & IRP_SYNCHRONOUS_API) {
        Irp->IoStatus.Status = Status;
    } else {
        IRPFinish(Irp, Status);
    }
}

NTSTATUS DispTdiQueryIpHwAddress( PDEVICE_OBJECT DeviceObject, PIRP Irp, PIO_STACK_LOCATION IrpSp ) {
    NTSTATUS Status;
    PULONG IPs;
    IP_ADDRESS Remote, Local;
    PNEIGHBOR_CACHE_ENTRY NCE;
    PIP_INTERFACE Interface;
    PQUERY_HW_WORK_ITEM WorkItem;

    NCE = NULL;
    Interface = NULL;
    WorkItem = NULL;
    Irp->IoStatus.Information = 0;

    if (IrpSp->Parameters.DeviceIoControl.InputBufferLength < 2 * sizeof(ULONG) ||
        IrpSp->Parameters.DeviceIoControl.OutputBufferLength == 0) {
        Status = STATUS_INVALID_BUFFER_SIZE;
        goto Exit;
    }

    IPs = (PULONG)Irp->AssociatedIrp.SystemBuffer;
    AddrInitIPv4(&Remote, IPs[0]);
    AddrInitIPv4(&Local, IPs[1]);

    if (AddrIsUnspecified(&Remote)) {
        Status = STATUS_UNSUCCESSFUL;
        goto Exit;
    }

    Interface = AddrLocateInterface(&Remote);
    if (Interface) {
        PVOID OutputBuffer;

        if (Interface->AddressLength > IrpSp->Parameters.DeviceIoControl.OutputBufferLength) {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto Exit;
        }

        OutputBuffer = Irp->AssociatedIrp.SystemBuffer;
        RtlCopyMemory(OutputBuffer, Interface->Address, Interface->AddressLength);
        Irp->IoStatus.Information = Interface->AddressLength;
        Status = STATUS_SUCCESS;
        goto Exit;
    }

    if (AddrIsUnspecified(&Local)) {
        NCE = RouteGetRouteToDestination(&Remote);
        if (NCE == NULL) {
            Status = STATUS_NETWORK_UNREACHABLE;
            goto Exit;
        }

        Interface = NCE->Interface;
        if (!IPReferenceInterface(Interface)) {
            NBDereferenceNeighbor(NCE);
            NCE = NULL;
            Status = STATUS_NETWORK_UNREACHABLE;
            goto Exit;
        }
        NBDereferenceNeighbor(NCE);
        NCE = NULL;
    }
    else {
        Interface = AddrLocateInterface(&Local);
        if (Interface == NULL) {
            Interface = GetDefaultInterface();
            if (Interface == NULL) {
                Status = STATUS_NETWORK_UNREACHABLE;
                goto Exit;
            }
        }
    }

    WorkItem = ExAllocatePoolWithTag(PagedPool, sizeof(QUERY_HW_WORK_ITEM), QUERY_CONTEXT_TAG);
    if (WorkItem == NULL) {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }

    WorkItem->WorkItem = IoAllocateWorkItem(DeviceObject);
    if (WorkItem->WorkItem == NULL) {
        ExFreePoolWithTag(WorkItem, QUERY_CONTEXT_TAG);
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto Exit;
    }

    WorkItem->Irp = Irp;
    WorkItem->IrpSp = IrpSp;
    WorkItem->Interface = Interface;
    WorkItem->RemoteIP = IPs[0];
    KeQuerySystemTime(&WorkItem->StartTime);

    NCE = NBLocateNeighbor(&Remote, Interface);
    if (NCE != NULL) {
        if (NCE->LinkAddressLength > IrpSp->Parameters.DeviceIoControl.OutputBufferLength) {
            NBDereferenceNeighbor(NCE);
            NCE = NULL;
            IoFreeWorkItem(WorkItem->WorkItem);
            ExFreePoolWithTag(WorkItem, QUERY_CONTEXT_TAG);
            Status = STATUS_INVALID_BUFFER_SIZE;
            goto Exit;
        }

        if (!(NCE->State & NUD_INCOMPLETE)) {
            PVOID LinkAddress = ExAllocatePoolWithTag(PagedPool, NCE->LinkAddressLength, QUERY_CONTEXT_TAG);
            if (LinkAddress == NULL) {
                NBDereferenceNeighbor(NCE);
                NCE = NULL;
                IoFreeWorkItem(WorkItem->WorkItem);
                ExFreePoolWithTag(WorkItem, QUERY_CONTEXT_TAG);
                Status = STATUS_INSUFFICIENT_RESOURCES;
                goto Exit;
            }
            memset(LinkAddress, 0xff, NCE->LinkAddressLength);
            NBUpdateNeighbor(NCE, LinkAddress, NUD_INCOMPLETE);
            ExFreePoolWithTag(LinkAddress, QUERY_CONTEXT_TAG);
        }
        NBDereferenceNeighbor(NCE);
        NCE = NULL;
    }

    if (!ARPTransmit(&Remote, NULL, Interface)) {
        IoFreeWorkItem(WorkItem->WorkItem);
        ExFreePoolWithTag(WorkItem, QUERY_CONTEXT_TAG);
        Status = STATUS_UNSUCCESSFUL;
        goto Exit;
    }

    Interface = NULL;

    if (Irp->Flags & IRP_SYNCHRONOUS_API) {
        WaitForHwAddress(DeviceObject, WorkItem);
        Status = Irp->IoStatus.Status;
    } else {
        IoMarkIrpPending(Irp);
        IoQueueWorkItem(WorkItem->WorkItem, WaitForHwAddress, DelayedWorkQueue, WorkItem);
        Status = STATUS_PENDING;
    }

Exit:
    if (NCE)
        NBDereferenceNeighbor(NCE);
    if (Interface)
        IPDereferenceInterface(Interface);
    return Status;
}

/* EOF */
