/*
 * COPYRIGHT:   See COPYING in the top level directory
 * PROJECT:     ReactOS TCP/IP protocol driver
 * FILE:        transport/udp/udp.c
 * PURPOSE:     User Datagram Protocol routines
 * PROGRAMMERS: Casper S. Hornstrup (chorns@users.sourceforge.net)
 * REVISIONS:
 *   CSH 01/08-2000 Created
 */

#include "precomp.h"

BOOLEAN UDPInitialized = FALSE;
PORT_SET UDPPorts;

NTSTATUS AddUDPHeaderIPv4(
    PADDRESS_FILE AddrFile,
    PIP_ADDRESS RemoteAddress,
    USHORT RemotePort,
    PIP_ADDRESS LocalAddress,
    USHORT LocalPort,
    PIP_PACKET IPPacket,
    PVOID Data,
    UINT DataLength)
/*
 * FUNCTION: Adds an IPv4 and UDP header to an IP packet
 * ARGUMENTS:
 *     SendRequest  = Pointer to send request
 *     LocalAddress = Pointer to our local address
 *     LocalPort    = The port we send this datagram from
 *     IPPacket     = Pointer to IP packet
 * RETURNS:
 *     Status of operation
 */
{
    PUDP_HEADER UDPHeader;
    NTSTATUS Status;

    TI_DbgPrint(MID_TRACE, ("Packet: %x NdisPacket %x\n",
			    IPPacket, IPPacket->NdisPacket));

    Status = AddGenericHeaderIPv4
        ( AddrFile, RemoteAddress, RemotePort,
          LocalAddress, LocalPort,
          IPPacket, DataLength, IPPROTO_UDP,
          sizeof(UDP_HEADER), (PVOID *)&UDPHeader );

    if (!NT_SUCCESS(Status))
        return Status;

    /* Port values are already big-endian values */
    UDPHeader->SourcePort = LocalPort;
    UDPHeader->DestPort   = RemotePort;
    UDPHeader->Checksum   = 0;
    /* Length of UDP header and data */
    UDPHeader->Length     = WH2N(DataLength + sizeof(UDP_HEADER));

    TI_DbgPrint(MID_TRACE, ("Copying data (hdr %x data %x (%d))\n",
			    IPPacket->Header, IPPacket->Data,
			    (PCHAR)IPPacket->Data - (PCHAR)IPPacket->Header));

    RtlCopyMemory(IPPacket->Data, Data, DataLength);

    UDPHeader->Checksum = UDPv4ChecksumCalculate((PIPv4_HEADER)IPPacket->Header,
                                                 (PUCHAR)UDPHeader,
                                                 DataLength + sizeof(UDP_HEADER));
    UDPHeader->Checksum = WH2N(UDPHeader->Checksum);

    TI_DbgPrint(MID_TRACE, ("Packet: %d ip %d udp %d payload\n",
			    (PCHAR)UDPHeader - (PCHAR)IPPacket->Header,
			    (PCHAR)IPPacket->Data - (PCHAR)UDPHeader,
			    DataLength));

    return STATUS_SUCCESS;
}


NTSTATUS BuildUDPPacket(
    PADDRESS_FILE AddrFile,
    PIP_PACKET Packet,
    PIP_ADDRESS RemoteAddress,
    USHORT RemotePort,
    PIP_ADDRESS LocalAddress,
    USHORT LocalPort,
    PCHAR DataBuffer,
    UINT DataLen )
/*
 * FUNCTION: Builds an UDP packet
 * ARGUMENTS:
 *     Context      = Pointer to context information (DATAGRAM_SEND_REQUEST)
 *     LocalAddress = Pointer to our local address
 *     LocalPort    = The port we send this datagram from
 *     IPPacket     = Address of pointer to IP packet
 * RETURNS:
 *     Status of operation
 */
{
    NTSTATUS Status;

    TI_DbgPrint(MAX_TRACE, ("Called.\n"));

    /* FIXME: Assumes IPv4 */
    IPInitializePacket(Packet, IP_ADDRESS_V4);

    Packet->TotalSize = sizeof(IPv4_HEADER) + sizeof(UDP_HEADER) + DataLen;

    /* Prepare packet */
    Status = AllocatePacketWithBuffer(&Packet->NdisPacket,
                                      NULL,
                                      Packet->TotalSize );

    if( !NT_SUCCESS(Status) )
    {
        Packet->Free(Packet);
        return Status;
    }

    TI_DbgPrint(MID_TRACE, ("Allocated packet: %x\n", Packet->NdisPacket));
    TI_DbgPrint(MID_TRACE, ("Local Addr : %s\n", A2S(LocalAddress)));
    TI_DbgPrint(MID_TRACE, ("Remote Addr: %s\n", A2S(RemoteAddress)));

    switch (RemoteAddress->Type) {
        case IP_ADDRESS_V4:
            Status = AddUDPHeaderIPv4(AddrFile, RemoteAddress, RemotePort,
                                      LocalAddress, LocalPort, Packet, DataBuffer, DataLen);
            break;
        case IP_ADDRESS_V6:
            /* FIXME: Support IPv6 */
            TI_DbgPrint(MIN_TRACE, ("IPv6 UDP datagrams are not supported.\n"));
        default:
            Status = STATUS_UNSUCCESSFUL;
            break;
    }
    if (!NT_SUCCESS(Status)) {
        TI_DbgPrint(MIN_TRACE, ("Cannot add UDP header. Status = (0x%X)\n",
                                Status));
        Packet->Free(Packet);
        return Status;
    }

    TI_DbgPrint(MID_TRACE, ("Displaying packet\n"));

    DISPLAY_IP_PACKET(Packet);

    TI_DbgPrint(MID_TRACE, ("Leaving\n"));

    return STATUS_SUCCESS;
}

static BOOLEAN UDPClassify(
    PADDRESS_FILE AddrFile,
    BOOLEAN Outbound,
    PIP_INTERFACE Interface,
    PIP_ADDRESS LocalAddress,
    USHORT LocalPort,
    PIP_ADDRESS RemoteAddress,
    USHORT RemotePort,
    PVOID IpHeader,
    ULONG IpHeaderSize,
    PVOID Data,
    ULONG DataSize,
    const WFP_SHIM_TAG *Tag)
{
    WFP_SHIM_DATAGRAM Datagram;
    UDP_HEADER UDPHeader;

    if (!WfpShimDatagramActive())
        return TRUE;

    UDPHeader.SourcePort = Outbound ? LocalPort : RemotePort;
    UDPHeader.DestPort = Outbound ? RemotePort : LocalPort;
    UDPHeader.Length = WH2N((USHORT)(DataSize + sizeof(UDP_HEADER)));
    UDPHeader.Checksum = 0;

    RtlZeroMemory(&Datagram, sizeof(Datagram));
    Datagram.EndpointId = AddrFile->WfpEndpointId;
    Datagram.Outbound = Outbound;
    Datagram.Loopback = (Interface == Loopback);
    Datagram.Protocol = IPPROTO_UDP;
    Datagram.LocalAddress = DN2H(LocalAddress->Address.IPv4Address);
    Datagram.RemoteAddress = DN2H(RemoteAddress->Address.IPv4Address);
    Datagram.LocalPort = WN2H(LocalPort);
    Datagram.RemotePort = WN2H(RemotePort);
    Datagram.InterfaceIndex = Interface->Index;
    Datagram.InterfaceType = (Interface == Loopback) ? IF_TYPE_SOFTWARE_LOOPBACK : IF_TYPE_ETHERNET_CSMACD;
    Datagram.ProcessId = AddrFile->ProcessId;
    Datagram.IpHeader = IpHeader;
    Datagram.IpHeaderSize = IpHeaderSize;
    Datagram.TransportHeader = &UDPHeader;
    Datagram.TransportHeaderSize = sizeof(UDPHeader);
    Datagram.Data = Data;
    Datagram.DataLength = DataSize;
    Datagram.Tag = *Tag;

    return WfpShimClassifyDatagram(&Datagram);
}

static NTSTATUS UDPSendToAddress(
    PADDRESS_FILE AddrFile,
    IP_ADDRESS RemoteAddress,
    USHORT RemotePort,
    PCHAR BufferData,
    ULONG DataSize,
    const WFP_SHIM_TAG *Tag)
{
    IP_PACKET Packet;
    IP_ADDRESS LocalAddress;
    USHORT LocalPort;
    NTSTATUS Status;
    PNEIGHBOR_CACHE_ENTRY NCE;

    LockObject(AddrFile);

    LocalAddress = AddrFile->Address;
    if ((DN2H(RemoteAddress.Address.IPv4Address) & 0xf0000000) == 0xe0000000)
    {
        ULONG Selector = AddrFile->MulticastInterface;
        if (!Selector) Selector = LocalAddress.Address.IPv4Address;
        NCE = RouteGetMulticastRoute(&RemoteAddress, Selector);
        if (!NCE)
        {
            UnlockObject(AddrFile);
            return STATUS_NETWORK_UNREACHABLE;
        }
        if (AddrIsUnspecified(&LocalAddress)) LocalAddress = NCE->Interface->Unicast;
    }
    else if (AddrIsUnspecified(&LocalAddress))
    {
        /* If the local address is unspecified (0),
         * then use the unicast address of the
         * interface we're sending over
         */
        if(!(NCE = RouteGetRouteToDestination( &RemoteAddress ))) {
            UnlockObject(AddrFile);
            return STATUS_NETWORK_UNREACHABLE;
        }

        LocalAddress = NCE->Interface->Unicast;
    }
    else
    {
        if(!(NCE = NBLocateNeighbor( &LocalAddress, NULL ))) {
            UnlockObject(AddrFile);
            return STATUS_INVALID_PARAMETER;
        }
    }

    LocalPort = AddrFile->Port;
    UnlockObject(AddrFile);

    if (!UDPClassify(AddrFile,
                     TRUE,
                     NCE->Interface,
                     &LocalAddress,
                     LocalPort,
                     &RemoteAddress,
                     RemotePort,
                     NULL,
                     0,
                     BufferData,
                     DataSize,
                     Tag))
    {
        NBDereferenceNeighbor(NCE);
        return STATUS_SUCCESS;
    }

    LockObject(AddrFile);

    Status = BuildUDPPacket( AddrFile,
							 &Packet,
							 &RemoteAddress,
							 RemotePort,
							 &LocalAddress,
							 LocalPort,
							 BufferData,
							 DataSize );

    UnlockObject(AddrFile);

    if( !NT_SUCCESS(Status) ) {
        NBDereferenceNeighbor(NCE);
	return Status;
    }

    Packet.WfpTag = *Tag;
    Status = IPSendDatagram(&Packet, NCE);
    NBDereferenceNeighbor(NCE);
    return Status;
}

NTSTATUS UDPSendDatagram(
    PADDRESS_FILE AddrFile,
    PTDI_CONNECTION_INFORMATION ConnInfo,
    PCHAR BufferData,
    ULONG DataSize,
    PULONG DataUsed )
/*
 * FUNCTION: Sends an UDP datagram to a remote address
 * ARGUMENTS:
 *     Request   = Pointer to TDI request
 *     ConnInfo  = Pointer to connection information
 *     Buffer    = Pointer to NDIS buffer with data
 *     DataSize  = Size in bytes of data to be sent
 * RETURNS:
 *     Status of operation
 */
{
    static const WFP_SHIM_TAG NoTag;
    PTA_IP_ADDRESS RemoteAddressTa = (PTA_IP_ADDRESS)ConnInfo->RemoteAddress;
    IP_ADDRESS RemoteAddress;
    USHORT RemotePort;
    NTSTATUS Status;

    TI_DbgPrint(MID_TRACE,("Sending Datagram(%x %x %x %d)\n",
						   AddrFile, ConnInfo, BufferData, DataSize));
    TI_DbgPrint(MID_TRACE,("RemoteAddressTa: %x\n", RemoteAddressTa));

    switch( RemoteAddressTa->Address[0].AddressType ) {
    case TDI_ADDRESS_TYPE_IP:
		RemoteAddress.Type = IP_ADDRESS_V4;
		RemoteAddress.Address.IPv4Address =
			RemoteAddressTa->Address[0].Address[0].in_addr;
		RemotePort = RemoteAddressTa->Address[0].Address[0].sin_port;
		break;

    default:
		return STATUS_UNSUCCESSFUL;
    }

    Status = UDPSendToAddress(AddrFile, RemoteAddress, RemotePort, BufferData, DataSize, &NoTag);
    if (!NT_SUCCESS(Status))
        return Status;

    *DataUsed = DataSize;

    return STATUS_SUCCESS;
}

typedef struct _UDP_WFP_INJECTION
{
    BOOLEAN Send;
    ULONG64 EndpointId;
    ULONG RemoteAddress;
    ULONG InterfaceIndex;
    WFP_SHIM_TAG Tag;
    PWFP_SHIM_COMPLETE Complete;
    PVOID Context;
    ULONG Length;
    UCHAR Data[ANYSIZE_ARRAY];
} UDP_WFP_INJECTION, *PUDP_WFP_INJECTION;

static NTSTATUS UDPInjectReceive(
    PUDP_WFP_INJECTION Injection)
{
    PIPv4_HEADER IPv4Header = (PIPv4_HEADER)Injection->Data;
    PIP_INTERFACE Interface = NULL;
    PNDIS_PACKET NdisPacket;
    IP_PACKET IPPacket;
    KIRQL OldIrql;
    IF_LIST_ITER(CurrentIF);

    if (Injection->Length < sizeof(IPv4_HEADER))
        return STATUS_INVALID_PARAMETER;

    if ((DN2H(IPv4Header->SrcAddr) >> 24) == 127 || (DN2H(IPv4Header->DstAddr) >> 24) == 127)
        return STATUS_DATA_NOT_ACCEPTED;

    TcpipAcquireSpinLock(&InterfaceListLock, &OldIrql);
    ForEachInterface(CurrentIF) {
        if (CurrentIF->Index == Injection->InterfaceIndex) {
            if (IPReferenceInterface(CurrentIF)) Interface = CurrentIF;
            break;
        }
    } EndFor(CurrentIF);
    TcpipReleaseSpinLock(&InterfaceListLock, OldIrql);
    if (!Interface)
        return STATUS_INVALID_PARAMETER;

    if (!NT_SUCCESS(AllocatePacketWithBuffer(&NdisPacket, (PCHAR)Injection->Data, Injection->Length)))
    {
        IPDereferenceInterface(Interface);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    IPInitializePacket(&IPPacket, 0);
    IPPacket.NdisPacket = NdisPacket;
    GetDataPtr(NdisPacket, 0, (PCHAR *)&IPPacket.Header, &IPPacket.TotalSize);
    IPPacket.MappedHeader = TRUE;
    IPPacket.WfpTag = Injection->Tag;

    IPReceive(Interface, &IPPacket);
    IPDereferenceInterface(Interface);

    return IPPacket.WfpAccepted ? STATUS_SUCCESS : STATUS_DATA_NOT_ACCEPTED;
}

static VOID UDPInjectWorker(
    PVOID Context)
{
    PUDP_WFP_INJECTION Injection = Context;
    PUDP_HEADER UDPHeader = (PUDP_HEADER)Injection->Data;
    PADDRESS_FILE AddrFile;
    IP_ADDRESS RemoteAddress;
    NTSTATUS Status;

    if (!Injection->Send)
    {
        Status = UDPInjectReceive(Injection);
    }
    else if ((AddrFile = AddrFindByWfpEndpointId(Injection->EndpointId)) == NULL)
    {
        Status = STATUS_INVALID_HANDLE;
    }
    else
    {
        if (AddrFile->Protocol != IPPROTO_UDP || Injection->Length < sizeof(UDP_HEADER))
        {
            Status = STATUS_INVALID_PARAMETER;
        }
        else
        {
            AddrInitIPv4(&RemoteAddress, DH2N(Injection->RemoteAddress));
            Status = UDPSendToAddress(AddrFile,
                                      RemoteAddress,
                                      UDPHeader->DestPort,
                                      (PCHAR)(UDPHeader + 1),
                                      Injection->Length - sizeof(UDP_HEADER),
                                      &Injection->Tag);
        }
        DereferenceObject(AddrFile);
    }

    Injection->Complete(Injection->Context, Status);
    ExFreePoolWithTag(Injection, PACKET_BUFFER_TAG);
}

static NTSTATUS UDPQueueInjection(
    BOOLEAN Send,
    ULONG64 EndpointId,
    ULONG RemoteAddress,
    ULONG InterfaceIndex,
    const VOID *Data,
    ULONG Length,
    const WFP_SHIM_TAG *Tag,
    PWFP_SHIM_COMPLETE Complete,
    PVOID Context)
{
    PUDP_WFP_INJECTION Injection;

    Injection = ExAllocatePoolWithTag(NonPagedPool,
                                      FIELD_OFFSET(UDP_WFP_INJECTION, Data) + Length,
                                      PACKET_BUFFER_TAG);
    if (!Injection)
        return STATUS_INSUFFICIENT_RESOURCES;

    Injection->Send = Send;
    Injection->EndpointId = EndpointId;
    Injection->RemoteAddress = RemoteAddress;
    Injection->InterfaceIndex = InterfaceIndex;
    Injection->Tag = *Tag;
    Injection->Complete = Complete;
    Injection->Context = Context;
    Injection->Length = Length;
    RtlCopyMemory(Injection->Data, Data, Length);

    if (!ChewCreate(UDPInjectWorker, Injection))
    {
        ExFreePoolWithTag(Injection, PACKET_BUFFER_TAG);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    return STATUS_SUCCESS;
}

static NTSTATUS NTAPI UDPWfpInjectSend(
    ULONG64 EndpointId,
    ULONG RemoteAddress,
    const VOID *Datagram,
    ULONG Length,
    const WFP_SHIM_TAG *Tag,
    PWFP_SHIM_COMPLETE Complete,
    PVOID Context)
{
    return UDPQueueInjection(TRUE, EndpointId, RemoteAddress, 0, Datagram, Length, Tag, Complete, Context);
}

static NTSTATUS NTAPI UDPWfpInjectReceive(
    ULONG InterfaceIndex,
    const VOID *Packet,
    ULONG Length,
    const WFP_SHIM_TAG *Tag,
    PWFP_SHIM_COMPLETE Complete,
    PVOID Context)
{
    return UDPQueueInjection(FALSE, 0, 0, InterfaceIndex, Packet, Length, Tag, Complete, Context);
}


VOID UDPReceive(PIP_INTERFACE Interface, PIP_PACKET IPPacket)
/*
 * FUNCTION: Receives and queues a UDP datagram
 * ARGUMENTS:
 *     NTE      = Pointer to net table entry which the packet was received on
*     IPPacket = Pointer to an IP packet that was received
* NOTES:
*     This is the low level interface for receiving UDP datagrams. It strips
*     the UDP header from a packet and delivers the data to anyone that wants it
*/
{
  AF_SEARCH SearchContext;
  PIPv4_HEADER IPv4Header;
  PADDRESS_FILE AddrFile;
  PUDP_HEADER UDPHeader;
  PIP_ADDRESS DstAddress, SrcAddress;
  UINT DataSize, i;

  TI_DbgPrint(MAX_TRACE, ("Called.\n"));

  switch (IPPacket->Type) {
  /* IPv4 packet */
  case IP_ADDRESS_V4:
    IPv4Header = IPPacket->Header;
    DstAddress = &IPPacket->DstAddr;
    SrcAddress = &IPPacket->SrcAddr;
    break;

  /* IPv6 packet */
  case IP_ADDRESS_V6:
    TI_DbgPrint(MIN_TRACE, ("Discarded IPv6 UDP datagram (%i bytes).\n", IPPacket->TotalSize));

    /* FIXME: IPv6 is not supported */
    return;

  default:
    return;
  }

  UDPHeader = (PUDP_HEADER)IPPacket->Data;

  /* Calculate and validate UDP checksum */
  i = UDPv4ChecksumCalculate(IPv4Header,
                             (PUCHAR)UDPHeader,
                             WH2N(UDPHeader->Length));
  if (i != DH2N(0x0000FFFF) && UDPHeader->Checksum != 0)
  {
      TI_DbgPrint(MIN_TRACE, ("Bad checksum on packet received.\n"));
      return;
  }

  /* Sanity checks */
  i = WH2N(UDPHeader->Length);
  if ((i < sizeof(UDP_HEADER)) || (i > IPPacket->TotalSize - IPPacket->Position)) {
    /* Incorrect or damaged packet received, discard it */
    TI_DbgPrint(MIN_TRACE, ("Incorrect or damaged UDP packet received.\n"));
    return;
  }

  DataSize = i - sizeof(UDP_HEADER);

  /* Go to UDP data area */
  IPPacket->Data = (PVOID)((ULONG_PTR)IPPacket->Data + sizeof(UDP_HEADER));

  /* Locate a receive request on destination address file object
     and deliver the packet if one is found. If there is no receive
     request on the address file object, call the associated receive
     handler. If no receive handler is registered, drop the packet */

  AddrFile = AddrSearchFirst(DstAddress,
                             UDPHeader->DestPort,
                             IPPROTO_UDP,
                             &SearchContext);
  if (AddrFile) {
    do {
      if (UDPClassify(AddrFile,
                      FALSE,
                      Interface,
                      DstAddress,
                      UDPHeader->DestPort,
                      SrcAddress,
                      UDPHeader->SourcePort,
                      IPv4Header,
                      IPPacket->HeaderSize,
                      IPPacket->Data,
                      DataSize,
                      &IPPacket->WfpTag))
      {
          IPPacket->WfpAccepted = TRUE;
          DGDeliverData(AddrFile,
		    SrcAddress,
                    DstAddress,
		    UDPHeader->SourcePort,
		    UDPHeader->DestPort,
                    IPPacket,
                    DataSize);
      }
      DereferenceObject(AddrFile);
    } while ((AddrFile = AddrSearchNext(&SearchContext)) != NULL);
  } else {
    /* There are no open address files that will take this datagram */
  }
  TI_DbgPrint(MAX_TRACE, ("Leaving.\n"));
}


static const WFP_SHIM_DISPATCH UDPWfpDispatch =
{
    UDPWfpInjectSend,
    UDPWfpInjectReceive
};

NTSTATUS UDPStartup(
  VOID)
/*
 * FUNCTION: Initializes the UDP subsystem
 * RETURNS:
 *     Status of operation
 */
{
  NTSTATUS Status;

  RtlZeroMemory(&UDPStats, sizeof(UDP_STATISTICS));

  Status = PortsStartup( &UDPPorts, 1, UDP_STARTING_PORT + UDP_DYNAMIC_PORTS );

  if( !NT_SUCCESS(Status) ) return Status;

  /* Register this protocol with IP layer */
  IPRegisterProtocol(IPPROTO_UDP, UDPReceive);
  WfpShimRegister(&UDPWfpDispatch);

  UDPInitialized = TRUE;

  return STATUS_SUCCESS;
}


NTSTATUS UDPShutdown(
  VOID)
/*
 * FUNCTION: Shuts down the UDP subsystem
 * RETURNS:
 *     Status of operation
 */
{
  if (!UDPInitialized)
      return STATUS_SUCCESS;

  PortsShutdown( &UDPPorts );
  WfpShimRegister(NULL);

  /* Deregister this protocol with IP layer */
  IPRegisterProtocol(IPPROTO_UDP, NULL);

  UDPInitialized = FALSE;

  return STATUS_SUCCESS;
}

UINT UDPAllocatePort( UINT HintPort ) {
    if( HintPort ) {
        if( AllocatePort( &UDPPorts, HintPort ) ) return HintPort;
        else return (UINT)-1;
    } else return AllocatePortFromRange
               ( &UDPPorts, UDP_STARTING_PORT,
                 UDP_STARTING_PORT + UDP_DYNAMIC_PORTS );
}

VOID UDPFreePort( UINT Port ) {
    DeallocatePort( &UDPPorts, Port );
}

/* EOF */
