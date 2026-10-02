/*
 * PROJECT:     LiberNT NetIO driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Windows Filtering Platform packet helpers and injection
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "wfp.h"

#include <pshpack1.h>
typedef struct _WFP_IPV4_HEADER
{
    UCHAR VersionAndLength;
    UCHAR TypeOfService;
    USHORT TotalLength;
    USHORT Identification;
    USHORT FlagsAndOffset;
    UCHAR TimeToLive;
    UCHAR Protocol;
    USHORT Checksum;
    ULONG SourceAddress;
    ULONG DestinationAddress;
} WFP_IPV4_HEADER, *PWFP_IPV4_HEADER;
#include <poppack.h>

#define WFP_DEFAULT_TTL 128

static LIST_ENTRY WfpPackets = { &WfpPackets, &WfpPackets };
static LIST_ENTRY WfpInjections = { &WfpInjections, &WfpInjections };
static WFP_SHIM_DISPATCH WfpShim;

VOID
NTAPI
WfpShimRegister(
    _In_opt_ const WFP_SHIM_DISPATCH *Dispatch)
{
    KIRQL OldIrql;

    KeAcquireSpinLock(&WfpLock, &OldIrql);
    if (Dispatch != NULL)
    {
        WfpShim = *Dispatch;
    }
    else
    {
        RtlZeroMemory(&WfpShim, sizeof(WfpShim));
    }
    KeReleaseSpinLock(&WfpLock, OldIrql);
}

static
PWFP_PACKET
WfpFindPacketLocked(
    _In_ const NET_BUFFER_LIST *NetBufferList)
{
    PWFP_PACKET Packet;
    PLIST_ENTRY Entry;

    for (Entry = WfpPackets.Flink; Entry != &WfpPackets; Entry = Entry->Flink)
    {
        Packet = CONTAINING_RECORD(Entry, WFP_PACKET, Link);
        if (Packet->NetBufferList == NetBufferList)
        {
            return Packet;
        }
    }
    return NULL;
}

PWFP_PACKET
WfpCreatePacket(
    _In_reads_bytes_opt_(HeaderLength) const VOID *Header,
    _In_ ULONG HeaderLength,
    _In_reads_bytes_opt_(DataLength) const VOID *Data,
    _In_ ULONG DataLength,
    _In_ ULONG DataOffset,
    _In_ const WFP_SHIM_TAG *Tag)
{
    ULONG Length = HeaderLength + DataLength;
    PWFP_PACKET Packet;
    KIRQL OldIrql;

    if (WfpNblPool == NULL || Length == 0)
    {
        return NULL;
    }

    Packet = ExAllocatePoolWithTag(NonPagedPoolNx, sizeof(*Packet) + Length, WFP_TAG);
    if (Packet == NULL)
    {
        return NULL;
    }

    RtlZeroMemory(Packet, sizeof(*Packet));
    Packet->Buffer = Packet + 1;
    RtlCopyMemory(Packet->Buffer, Header, HeaderLength);
    RtlCopyMemory((PUCHAR)Packet->Buffer + HeaderLength, Data, DataLength);

    Packet->Mdl = IoAllocateMdl(Packet->Buffer, Length, FALSE, FALSE, NULL);
    if (Packet->Mdl == NULL)
    {
        ExFreePoolWithTag(Packet, WFP_TAG);
        return NULL;
    }
    MmBuildMdlForNonPagedPool(Packet->Mdl);

    Packet->NetBufferList = NdisAllocateNetBufferAndNetBufferList(WfpNblPool,
                                                                  0,
                                                                  0,
                                                                  Packet->Mdl,
                                                                  DataOffset,
                                                                  Length - DataOffset);
    if (Packet->NetBufferList == NULL)
    {
        IoFreeMdl(Packet->Mdl);
        ExFreePoolWithTag(Packet, WFP_TAG);
        return NULL;
    }

    Packet->References = 1;
    Packet->Owned = TRUE;
    Packet->Tag = *Tag;

    KeAcquireSpinLock(&WfpLock, &OldIrql);
    InsertTailList(&WfpPackets, &Packet->Link);
    KeReleaseSpinLock(&WfpLock, OldIrql);
    return Packet;
}

VOID
WfpDereferencePacket(
    _In_ PWFP_PACKET Packet)
{
    BOOLEAN Free;
    KIRQL OldIrql;

    KeAcquireSpinLock(&WfpLock, &OldIrql);
    Free = (--Packet->References == 0);
    if (Free)
    {
        RemoveEntryList(&Packet->Link);
    }
    KeReleaseSpinLock(&WfpLock, OldIrql);
    if (!Free)
    {
        return;
    }

    if (Packet->Owned)
    {
        NdisFreeNetBufferList(Packet->NetBufferList);
        IoFreeMdl(Packet->Mdl);
    }
    ExFreePoolWithTag(Packet, WFP_TAG);
}

VOID
NTAPI
FwpsReferenceNetBufferList0(
    _Inout_ NET_BUFFER_LIST *netBufferList,
    _In_ BOOLEAN intendToModify)
{
    PWFP_PACKET Packet;
    KIRQL OldIrql;

    UNREFERENCED_PARAMETER(intendToModify);

    KeAcquireSpinLock(&WfpLock, &OldIrql);
    Packet = WfpFindPacketLocked(netBufferList);
    if (Packet != NULL)
    {
        Packet->References++;
    }
    KeReleaseSpinLock(&WfpLock, OldIrql);
}

VOID
NTAPI
FwpsDereferenceNetBufferList0(
    _Inout_ NET_BUFFER_LIST *netBufferList,
    _In_ BOOLEAN dispatchLevel)
{
    PWFP_PACKET Packet;
    KIRQL OldIrql;

    UNREFERENCED_PARAMETER(dispatchLevel);

    KeAcquireSpinLock(&WfpLock, &OldIrql);
    Packet = WfpFindPacketLocked(netBufferList);
    KeReleaseSpinLock(&WfpLock, OldIrql);
    if (Packet != NULL)
    {
        WfpDereferencePacket(Packet);
    }
}

NTSTATUS
NTAPI
FwpsAllocateCloneNetBufferList0(
    _Inout_ NET_BUFFER_LIST *originalNetBufferList,
    _In_opt_ NDIS_HANDLE netBufferListPoolHandle,
    _In_opt_ NDIS_HANDLE netBufferPoolHandle,
    _In_ ULONG allocateCloneFlags,
    _Outptr_ NET_BUFFER_LIST **netBufferList)
{
    if (originalNetBufferList == NULL || netBufferList == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    *netBufferList = NdisAllocateCloneNetBufferList(originalNetBufferList,
                                                    netBufferListPoolHandle,
                                                    netBufferPoolHandle,
                                                    allocateCloneFlags);
    return *netBufferList != NULL ? STATUS_SUCCESS : STATUS_INSUFFICIENT_RESOURCES;
}

VOID
NTAPI
FwpsFreeCloneNetBufferList0(
    _In_ NET_BUFFER_LIST *netBufferList,
    _In_ ULONG freeCloneFlags)
{
    NdisFreeCloneNetBufferList(netBufferList, freeCloneFlags);
}

NTSTATUS
NTAPI
FwpsAllocateNetBufferAndNetBufferList0(
    _In_ NDIS_HANDLE poolHandle,
    _In_ USHORT contextSize,
    _In_ USHORT contextBackFill,
    _In_opt_ MDL *mdlChain,
    _In_ ULONG dataOffset,
    _In_ SIZE_T dataLength,
    _Outptr_ NET_BUFFER_LIST **netBufferList)
{
    if (netBufferList == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    *netBufferList = NdisAllocateNetBufferAndNetBufferList(poolHandle,
                                                           contextSize,
                                                           contextBackFill,
                                                           mdlChain,
                                                           dataOffset,
                                                           dataLength);
    return *netBufferList != NULL ? STATUS_SUCCESS : STATUS_INSUFFICIENT_RESOURCES;
}

VOID
NTAPI
FwpsFreeNetBufferList0(
    _In_ NET_BUFFER_LIST *netBufferList)
{
    NdisFreeNetBufferList(netBufferList);
}

static
PWFP_INJECTION
WfpFindInjectionLocked(
    _In_ HANDLE Handle)
{
    PLIST_ENTRY Entry;

    for (Entry = WfpInjections.Flink; Entry != &WfpInjections; Entry = Entry->Flink)
    {
        if (Entry == (PLIST_ENTRY)Handle)
        {
            return CONTAINING_RECORD(Entry, WFP_INJECTION, Link);
        }
    }
    return NULL;
}

NTSTATUS
NTAPI
FwpsInjectionHandleCreate0(
    _In_opt_ ADDRESS_FAMILY addressFamily,
    _In_ UINT32 flags,
    _Out_ HANDLE *injectionHandle)
{
    PWFP_INJECTION Injection;
    KIRQL OldIrql;

    if (injectionHandle == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    *injectionHandle = NULL;

    WfpInitialize();

    Injection = ExAllocatePoolWithTag(NonPagedPoolNx, sizeof(*Injection), WFP_TAG);
    if (Injection == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(Injection, sizeof(*Injection));
    Injection->Family = addressFamily;
    Injection->Flags = flags != 0 ? flags :
                       (FWPS_INJECTION_TYPE_TRANSPORT | FWPS_INJECTION_TYPE_STREAM | FWPS_INJECTION_TYPE_FORWARD);

    KeAcquireSpinLock(&WfpLock, &OldIrql);
    InsertTailList(&WfpInjections, &Injection->Link);
    KeReleaseSpinLock(&WfpLock, OldIrql);

    *injectionHandle = Injection;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
FwpsInjectionHandleDestroy0(
    _In_ HANDLE injectionHandle)
{
    PWFP_INJECTION Injection;
    LARGE_INTEGER Interval;
    KIRQL OldIrql;

    KeAcquireSpinLock(&WfpLock, &OldIrql);
    Injection = WfpFindInjectionLocked(injectionHandle);
    if (Injection != NULL)
    {
        Injection->Closing = TRUE;
    }
    KeReleaseSpinLock(&WfpLock, OldIrql);
    if (Injection == NULL)
    {
        return STATUS_INVALID_HANDLE;
    }

    Interval.QuadPart = -10 * 1000;
    while (Injection->Pending != 0)
    {
        KeDelayExecutionThread(KernelMode, FALSE, &Interval);
    }

    KeAcquireSpinLock(&WfpLock, &OldIrql);
    RemoveEntryList(&Injection->Link);
    KeReleaseSpinLock(&WfpLock, OldIrql);
    ExFreePoolWithTag(Injection, WFP_TAG);
    return STATUS_SUCCESS;
}

FWPS_PACKET_INJECTION_STATE
NTAPI
FwpsQueryPacketInjectionState0(
    _In_ HANDLE injectionHandle,
    _In_ const NET_BUFFER_LIST *netBufferList,
    _Out_opt_ HANDLE *injectionContext)
{
    FWPS_PACKET_INJECTION_STATE State = FWPS_PACKET_NOT_INJECTED;
    PWFP_PACKET Packet;
    KIRQL OldIrql;

    if (injectionContext != NULL)
    {
        *injectionContext = NULL;
    }

    KeAcquireSpinLock(&WfpLock, &OldIrql);
    Packet = WfpFindPacketLocked(netBufferList);
    if (Packet != NULL && Packet->Tag.Handle != NULL)
    {
        if (Packet->Tag.Handle == injectionHandle)
        {
            State = FWPS_PACKET_INJECTED_BY_SELF;
            if (injectionContext != NULL)
            {
                *injectionContext = Packet->Tag.Context;
            }
        }
        else if (Packet->Tag.Previous == injectionHandle)
        {
            State = FWPS_PACKET_PREVIOUSLY_INJECTED_BY_SELF;
        }
        else
        {
            State = FWPS_PACKET_INJECTED_BY_OTHER;
        }
    }
    KeReleaseSpinLock(&WfpLock, OldIrql);
    return State;
}

static
VOID
NTAPI
WfpInjectComplete(
    _In_ PVOID Context,
    _In_ NTSTATUS Status)
{
    PWFP_PACKET Packet = Context;
    PWFP_INJECTION Injection = Packet->Injection;
    BOOLEAN Done;
    KIRQL OldIrql;

    KeAcquireSpinLock(&WfpLock, &OldIrql);
    if (!NT_SUCCESS(Status) && NT_SUCCESS(Packet->Status))
    {
        Packet->Status = Status;
    }
    Done = (--Packet->Pending == 0);
    KeReleaseSpinLock(&WfpLock, OldIrql);
    if (!Done)
    {
        return;
    }

    Packet->NetBufferList->Status = Packet->Status;
    Packet->CompletionFn(Packet->CompletionContext,
                         Packet->NetBufferList,
                         KeGetCurrentIrql() == DISPATCH_LEVEL);
    WfpDereferencePacket(Packet);
    InterlockedDecrement(&Injection->Pending);
}

static
NTSTATUS
WfpInject(
    _In_ HANDLE InjectionHandle,
    _In_opt_ HANDLE InjectionContext,
    _In_ BOOLEAN Send,
    _In_ ULONG64 EndpointId,
    _In_ ULONG RemoteAddress,
    _In_ ULONG InterfaceIndex,
    _Inout_ NET_BUFFER_LIST *NetBufferList,
    _In_ FWPS_INJECT_COMPLETE0 CompletionFn,
    _In_opt_ HANDLE CompletionContext)
{
    PWFP_PACKET Packet, Existing;
    PWFP_INJECTION Injection;
    WFP_SHIM_DISPATCH Shim;
    PNET_BUFFER NetBuffer;
    NTSTATUS Status = STATUS_SUCCESS;
    PVOID Buffer, Data;
    ULONG Length, Queued = 0;
    KIRQL OldIrql;

    if (NetBufferList == NULL || CompletionFn == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }

    Packet = ExAllocatePoolWithTag(NonPagedPoolNx, sizeof(*Packet), WFP_TAG);
    if (Packet == NULL)
    {
        return STATUS_INSUFFICIENT_RESOURCES;
    }
    RtlZeroMemory(Packet, sizeof(*Packet));
    Packet->NetBufferList = NetBufferList;
    Packet->References = 1;
    Packet->Pending = 1;
    Packet->CompletionFn = CompletionFn;
    Packet->CompletionContext = CompletionContext;
    Packet->Tag.Handle = InjectionHandle;
    Packet->Tag.Context = InjectionContext;

    KeAcquireSpinLock(&WfpLock, &OldIrql);
    Shim = WfpShim;
    Injection = WfpFindInjectionLocked(InjectionHandle);
    if (Injection == NULL)
    {
        Status = STATUS_INVALID_HANDLE;
    }
    else if (Injection->Closing)
    {
        Status = STATUS_FWP_INJECT_HANDLE_CLOSING;
    }
    else if (Shim.InjectSend == NULL || Shim.InjectReceive == NULL)
    {
        Status = STATUS_FWP_TCPIP_NOT_READY;
    }
    else if (WfpFindPacketLocked(NetBufferList) != NULL &&
             !WfpFindPacketLocked(NetBufferList)->Owned)
    {
        Status = STATUS_INVALID_PARAMETER;
    }
    else
    {
        Existing = WfpFindPacketLocked(NetBufferList);
        if (Existing == NULL && NetBufferList->ParentNetBufferList != NULL)
        {
            Existing = WfpFindPacketLocked(NetBufferList->ParentNetBufferList);
        }
        if (Existing != NULL && Existing->Tag.Handle != InjectionHandle)
        {
            Packet->Tag.Previous = Existing->Tag.Handle != NULL ? Existing->Tag.Handle : Existing->Tag.Previous;
        }
        Packet->Injection = Injection;
        InterlockedIncrement(&Injection->Pending);
        InsertHeadList(&WfpPackets, &Packet->Link);
    }
    KeReleaseSpinLock(&WfpLock, OldIrql);

    if (!NT_SUCCESS(Status))
    {
        ExFreePoolWithTag(Packet, WFP_TAG);
        return Status;
    }

    for (NetBuffer = NET_BUFFER_LIST_FIRST_NB(NetBufferList);
         NetBuffer != NULL && NT_SUCCESS(Status);
         NetBuffer = NET_BUFFER_NEXT_NB(NetBuffer))
    {
        Length = NET_BUFFER_DATA_LENGTH(NetBuffer);
        Buffer = Length != 0 ? ExAllocatePoolWithTag(NonPagedPoolNx, Length, WFP_TAG) : NULL;
        Data = Buffer != NULL ? NdisGetDataBuffer(NetBuffer, Length, Buffer, 1, 0) : NULL;
        if (Data == NULL)
        {
            Status = Length != 0 ? STATUS_INSUFFICIENT_RESOURCES : STATUS_INVALID_PARAMETER;
        }
        else
        {
            KeAcquireSpinLock(&WfpLock, &OldIrql);
            Packet->Pending++;
            KeReleaseSpinLock(&WfpLock, OldIrql);

            if (Send)
            {
                Status = Shim.InjectSend(EndpointId, RemoteAddress, Data, Length, &Packet->Tag, WfpInjectComplete, Packet);
            }
            else
            {
                Status = Shim.InjectReceive(InterfaceIndex, Data, Length, &Packet->Tag, WfpInjectComplete, Packet);
            }

            if (NT_SUCCESS(Status))
            {
                Queued++;
            }
            else
            {
                KeAcquireSpinLock(&WfpLock, &OldIrql);
                Packet->Pending--;
                KeReleaseSpinLock(&WfpLock, OldIrql);
            }
        }

        if (Buffer != NULL)
        {
            ExFreePoolWithTag(Buffer, WFP_TAG);
        }
    }

    if (Queued == 0)
    {
        if (NT_SUCCESS(Status))
        {
            Status = STATUS_INVALID_PARAMETER;
        }
        WfpDereferencePacket(Packet);
        InterlockedDecrement(&Injection->Pending);
        return Status;
    }

    WfpInjectComplete(Packet, Status);
    return STATUS_SUCCESS;
}

static
NTSTATUS
WfpInjectTransportSend(
    _In_ HANDLE InjectionHandle,
    _In_opt_ HANDLE InjectionContext,
    _In_ UINT64 EndpointHandle,
    _In_ UINT32 Flags,
    _In_opt_ const UCHAR *RemoteAddress,
    _In_ ADDRESS_FAMILY AddressFamily,
    _Inout_ NET_BUFFER_LIST *NetBufferList,
    _In_ FWPS_INJECT_COMPLETE0 CompletionFn,
    _In_opt_ HANDLE CompletionContext)
{
    ULONG Address;

    if (Flags != 0 || RemoteAddress == NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (AddressFamily != AF_INET)
    {
        return STATUS_NOT_SUPPORTED;
    }

    RtlCopyMemory(&Address, RemoteAddress, sizeof(Address));
    return WfpInject(InjectionHandle,
                     InjectionContext,
                     TRUE,
                     EndpointHandle,
                     RtlUlongByteSwap(Address),
                     0,
                     NetBufferList,
                     CompletionFn,
                     CompletionContext);
}

NTSTATUS
NTAPI
FwpsInjectTransportSendAsync0(
    _In_ HANDLE injectionHandle,
    _In_opt_ HANDLE injectionContext,
    _In_ UINT64 endpointHandle,
    _In_ UINT32 flags,
    _In_opt_ FWPS_TRANSPORT_SEND_PARAMS0 *sendArgs,
    _In_ ADDRESS_FAMILY addressFamily,
    _In_ COMPARTMENT_ID compartmentId,
    _Inout_ NET_BUFFER_LIST *netBufferList,
    _In_ FWPS_INJECT_COMPLETE0 completionFn,
    _In_opt_ HANDLE completionContext)
{
    UNREFERENCED_PARAMETER(compartmentId);

    return WfpInjectTransportSend(injectionHandle,
                                  injectionContext,
                                  endpointHandle,
                                  flags,
                                  sendArgs != NULL ? sendArgs->remoteAddress : NULL,
                                  addressFamily,
                                  netBufferList,
                                  completionFn,
                                  completionContext);
}

NTSTATUS
NTAPI
FwpsInjectTransportSendAsync1(
    _In_ HANDLE injectionHandle,
    _In_opt_ HANDLE injectionContext,
    _In_ UINT64 endpointHandle,
    _In_ UINT32 flags,
    _In_opt_ FWPS_TRANSPORT_SEND_PARAMS1 *sendArgs,
    _In_ ADDRESS_FAMILY addressFamily,
    _In_ COMPARTMENT_ID compartmentId,
    _Inout_ NET_BUFFER_LIST *netBufferList,
    _In_ FWPS_INJECT_COMPLETE0 completionFn,
    _In_opt_ HANDLE completionContext)
{
    UNREFERENCED_PARAMETER(compartmentId);

    return WfpInjectTransportSend(injectionHandle,
                                  injectionContext,
                                  endpointHandle,
                                  flags,
                                  sendArgs != NULL ? sendArgs->remoteAddress : NULL,
                                  addressFamily,
                                  netBufferList,
                                  completionFn,
                                  completionContext);
}

NTSTATUS
NTAPI
FwpsInjectTransportReceiveAsync0(
    _In_ HANDLE injectionHandle,
    _In_opt_ HANDLE injectionContext,
    _In_opt_ PVOID reserved,
    _In_ UINT32 flags,
    _In_ ADDRESS_FAMILY addressFamily,
    _In_ COMPARTMENT_ID compartmentId,
    _In_ IF_INDEX interfaceIndex,
    _In_ IF_INDEX subInterfaceIndex,
    _Inout_ NET_BUFFER_LIST *netBufferList,
    _In_ FWPS_INJECT_COMPLETE0 completionFn,
    _In_opt_ HANDLE completionContext)
{
    UNREFERENCED_PARAMETER(compartmentId);
    UNREFERENCED_PARAMETER(subInterfaceIndex);

    if (reserved != NULL || flags != 0)
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (addressFamily != AF_INET)
    {
        return STATUS_NOT_SUPPORTED;
    }

    return WfpInject(injectionHandle,
                     injectionContext,
                     FALSE,
                     0,
                     0,
                     interfaceIndex,
                     netBufferList,
                     completionFn,
                     completionContext);
}

NTSTATUS
NTAPI
FwpsInjectNetworkSendAsync0(
    _In_ HANDLE injectionHandle,
    _In_opt_ HANDLE injectionContext,
    _In_ UINT32 flags,
    _In_ COMPARTMENT_ID compartmentId,
    _Inout_ NET_BUFFER_LIST *netBufferList,
    _In_ FWPS_INJECT_COMPLETE0 completionFn,
    _In_opt_ HANDLE completionContext)
{
    UNREFERENCED_PARAMETER(injectionHandle);
    UNREFERENCED_PARAMETER(injectionContext);
    UNREFERENCED_PARAMETER(flags);
    UNREFERENCED_PARAMETER(compartmentId);
    UNREFERENCED_PARAMETER(netBufferList);
    UNREFERENCED_PARAMETER(completionFn);
    UNREFERENCED_PARAMETER(completionContext);

    return STATUS_NOT_SUPPORTED;
}

NTSTATUS
NTAPI
FwpsInjectNetworkReceiveAsync0(
    _In_ HANDLE injectionHandle,
    _In_opt_ HANDLE injectionContext,
    _In_ UINT32 flags,
    _In_ COMPARTMENT_ID compartmentId,
    _In_ IF_INDEX interfaceIndex,
    _In_ IF_INDEX subInterfaceIndex,
    _Inout_ NET_BUFFER_LIST *netBufferList,
    _In_ FWPS_INJECT_COMPLETE0 completionFn,
    _In_opt_ HANDLE completionContext)
{
    UNREFERENCED_PARAMETER(injectionHandle);
    UNREFERENCED_PARAMETER(injectionContext);
    UNREFERENCED_PARAMETER(flags);
    UNREFERENCED_PARAMETER(compartmentId);
    UNREFERENCED_PARAMETER(interfaceIndex);
    UNREFERENCED_PARAMETER(subInterfaceIndex);
    UNREFERENCED_PARAMETER(netBufferList);
    UNREFERENCED_PARAMETER(completionFn);
    UNREFERENCED_PARAMETER(completionContext);

    return STATUS_NOT_SUPPORTED;
}

static
USHORT
WfpChecksum(
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length,
    _In_ ULONG Sum)
{
    ULONG Index;

    for (Index = 0; Index + 1 < Length; Index += 2)
    {
        Sum += ((ULONG)Data[Index] << 8) | Data[Index + 1];
    }
    if (Index < Length)
    {
        Sum += (ULONG)Data[Index] << 8;
    }
    while (Sum >> 16)
    {
        Sum = (Sum & 0xFFFF) + (Sum >> 16);
    }
    return RtlUshortByteSwap((USHORT)~Sum);
}

static
VOID
WfpFixTransportChecksum(
    _In_ PNET_BUFFER NetBuffer,
    _In_ PWFP_IPV4_HEADER Header,
    _In_ ULONG HeaderLength)
{
    ULONG Length = NET_BUFFER_DATA_LENGTH(NetBuffer) - HeaderLength;
    ULONG Offset, Minimum, Sum = 0;
    PUCHAR Transport, Data, Storage;
    USHORT Checksum;

    switch (Header->Protocol)
    {
        case IPPROTO_UDP:
            Offset = 6;
            Minimum = 8;
            break;

        case IPPROTO_TCP:
            Offset = 16;
            Minimum = 20;
            break;

        case IPPROTO_ICMP:
            Offset = 2;
            Minimum = 4;
            break;

        default:
            return;
    }
    if (Length < Minimum)
    {
        return;
    }

    NdisAdvanceNetBufferDataStart(NetBuffer, HeaderLength, FALSE, NULL);
    Transport = NdisGetDataBuffer(NetBuffer, Minimum, NULL, 1, 0);
    Storage = Transport != NULL ? ExAllocatePoolWithTag(NonPagedPoolNx, Length, WFP_TAG) : NULL;
    if (Storage != NULL)
    {
        Transport[Offset] = 0;
        Transport[Offset + 1] = 0;
        Data = NdisGetDataBuffer(NetBuffer, Length, Storage, 1, 0);
        if (Data != NULL)
        {
            if (Header->Protocol != IPPROTO_ICMP)
            {
                Sum = RtlUshortByteSwap((USHORT)(Header->SourceAddress & 0xFFFF)) +
                      RtlUshortByteSwap((USHORT)(Header->SourceAddress >> 16)) +
                      RtlUshortByteSwap((USHORT)(Header->DestinationAddress & 0xFFFF)) +
                      RtlUshortByteSwap((USHORT)(Header->DestinationAddress >> 16)) +
                      Header->Protocol + Length;
            }
            Checksum = WfpChecksum(Data, Length, Sum);
            if (Checksum == 0 && Header->Protocol == IPPROTO_UDP)
            {
                Checksum = 0xFFFF;
            }
            RtlCopyMemory(&Transport[Offset], &Checksum, sizeof(Checksum));
        }
        ExFreePoolWithTag(Storage, WFP_TAG);
    }
    NdisRetreatNetBufferDataStart(NetBuffer, HeaderLength, 0, NULL);
}

NTSTATUS
NTAPI
FwpsConstructIpHeaderForTransportPacket0(
    _Inout_ NET_BUFFER_LIST *netBufferList,
    _In_ ULONG headerIncludeHeaderLength,
    _In_ ADDRESS_FAMILY addressFamily,
    _In_ const UCHAR *sourceAddress,
    _In_ const UCHAR *remoteAddress,
    _In_ IPPROTO nextProtocol,
    _In_opt_ UINT64 endpointHandle,
    _In_opt_ const WSACMSGHDR *controlData,
    _In_ ULONG controlDataLength,
    _In_ UINT32 flags,
    _Reserved_ PVOID reserved,
    _In_opt_ IF_INDEX interfaceIndex,
    _In_opt_ IF_INDEX subInterfaceIndex)
{
    PWFP_IPV4_HEADER Header;
    PNET_BUFFER NetBuffer;
    ULONG HeaderLength;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(endpointHandle);
    UNREFERENCED_PARAMETER(controlData);
    UNREFERENCED_PARAMETER(controlDataLength);
    UNREFERENCED_PARAMETER(flags);
    UNREFERENCED_PARAMETER(interfaceIndex);
    UNREFERENCED_PARAMETER(subInterfaceIndex);

    if (netBufferList == NULL || sourceAddress == NULL || remoteAddress == NULL || reserved != NULL)
    {
        return STATUS_INVALID_PARAMETER;
    }
    if (addressFamily != AF_INET)
    {
        return STATUS_NOT_SUPPORTED;
    }

    NetBuffer = NET_BUFFER_LIST_FIRST_NB(netBufferList);
    if (headerIncludeHeaderLength != 0 && (NetBuffer == NULL || NET_BUFFER_NEXT_NB(NetBuffer) != NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }

    for (; NetBuffer != NULL; NetBuffer = NET_BUFFER_NEXT_NB(NetBuffer))
    {
        HeaderLength = headerIncludeHeaderLength;
        if (HeaderLength == 0)
        {
            HeaderLength = sizeof(WFP_IPV4_HEADER);
            Status = NdisRetreatNetBufferDataStart(NetBuffer, HeaderLength, 0, NULL);
            if (!NT_SUCCESS(Status))
            {
                return Status;
            }
        }
        else if (HeaderLength < sizeof(WFP_IPV4_HEADER) || HeaderLength > NET_BUFFER_DATA_LENGTH(NetBuffer))
        {
            return STATUS_INVALID_PARAMETER;
        }

        Header = NdisGetDataBuffer(NetBuffer, HeaderLength, NULL, 1, 0);
        if (Header == NULL)
        {
            return STATUS_INVALID_PARAMETER;
        }

        if (headerIncludeHeaderLength == 0)
        {
            RtlZeroMemory(Header, sizeof(*Header));
            Header->VersionAndLength = 0x45;
            Header->TimeToLive = WFP_DEFAULT_TTL;
        }
        Header->TotalLength = RtlUshortByteSwap((USHORT)NET_BUFFER_DATA_LENGTH(NetBuffer));
        Header->Protocol = (UCHAR)nextProtocol;
        RtlCopyMemory(&Header->SourceAddress, sourceAddress, sizeof(Header->SourceAddress));
        RtlCopyMemory(&Header->DestinationAddress, remoteAddress, sizeof(Header->DestinationAddress));
        Header->Checksum = 0;
        Header->Checksum = WfpChecksum((const UCHAR *)Header, HeaderLength, 0);
        WfpFixTransportChecksum(NetBuffer, Header, HeaderLength);
    }
    return STATUS_SUCCESS;
}

VOID
NTAPI
FwpsCopyStreamDataToBuffer0(
    _In_ const FWPS_STREAM_DATA0 *calloutStreamData,
    _Inout_ PVOID buffer,
    _In_ SIZE_T bytesToCopy,
    _Out_ SIZE_T *bytesCopied)
{
    PNET_BUFFER_LIST NetBufferList;
    PNET_BUFFER NetBuffer;
    SIZE_T Copied = 0, Chunk, Left;
    ULONG MdlOffset, Available;
    BOOLEAN First;
    PUCHAR Source;
    PMDL Mdl;

    Left = min(bytesToCopy, calloutStreamData->dataLength);
    First = calloutStreamData->dataOffset.netBufferList != NULL;
    for (NetBufferList = First ? calloutStreamData->dataOffset.netBufferList : calloutStreamData->netBufferListChain;
         NetBufferList != NULL && Left != 0;
         NetBufferList = NET_BUFFER_LIST_NEXT_NBL(NetBufferList))
    {
        for (NetBuffer = First ? calloutStreamData->dataOffset.netBuffer : NET_BUFFER_LIST_FIRST_NB(NetBufferList);
             NetBuffer != NULL && Left != 0;
             NetBuffer = NET_BUFFER_NEXT_NB(NetBuffer))
        {
            Available = NET_BUFFER_DATA_LENGTH(NetBuffer);
            Mdl = NET_BUFFER_CURRENT_MDL(NetBuffer);
            MdlOffset = NET_BUFFER_CURRENT_MDL_OFFSET(NetBuffer);
            if (First)
            {
                Available -= min(Available, calloutStreamData->dataOffset.netBufferOffset);
                Mdl = calloutStreamData->dataOffset.mdl;
                MdlOffset = calloutStreamData->dataOffset.mdlOffset;
                First = FALSE;
            }
            while (Mdl != NULL && Available != 0 && Left != 0)
            {
                Chunk = min(MmGetMdlByteCount(Mdl) - MdlOffset, Available);
                Chunk = min(Chunk, Left);
                Source = MmGetSystemAddressForMdlSafe(Mdl, NormalPagePriority);
                if (Source == NULL)
                {
                    *bytesCopied = Copied;
                    return;
                }
                RtlCopyMemory((PUCHAR)buffer + Copied, Source + MdlOffset, Chunk);
                Copied += Chunk;
                Left -= Chunk;
                Available -= (ULONG)Chunk;
                MdlOffset = 0;
                Mdl = Mdl->Next;
            }
        }
    }
    *bytesCopied = Copied;
}
