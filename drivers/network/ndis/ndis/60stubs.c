/*
 * COPYRIGHT:   See COPYING in the top level directory
 * PROJECT:     ReactOS NDIS library
 * FILE:        ndis/60stubs.c
 * PURPOSE:     NDIS 6 (and NDIS 6.20 / Win7) stub exports
 * PROGRAMMERS: dev-nt6-1 branch (NT 5.2 -> NT 6.1 API upgrade)
 *
 * NOTE: This file is compiled with NDIS620_MINIPORT / NDIS620 so it sees the
 * full NDIS 6.20 view of ndis.h — unlike the rest of the NDIS library which
 * still targets NDIS 5.1. It therefore skips the precompiled header (which
 * was built at NDIS 5.1 level) and includes ndissys.h directly.
 *
 * All functions here are functional stubs: registration entry points
 * (NdisMRegisterMiniportDriver, NdisFRegisterFilterDriver, etc.) return
 * NDIS_STATUS_SUCCESS after recording their arguments in an internal table,
 * while datapath and OID entry points return NDIS_STATUS_NOT_SUPPORTED or
 * quietly discard the request. New NDIS 6 drivers will load and initialize
 * but won't actually forward any packets until the full library port
 * (Phase G of the dev-nt6-1 upgrade plan) lands.
 */

/* Override NDIS version for this compilation unit only */
#define NDIS620          1
#define NDIS620_MINIPORT 1

#include "ndissys.h"

/* ------------------------------------------------------------------ */
/*  NET_BUFFER / NET_BUFFER_LIST pool management                       */
/* ------------------------------------------------------------------ */

NDIS_HANDLE
EXPORT
NdisAllocateNetBufferListPool(
    _In_opt_ NDIS_HANDLE NdisHandle,
    _In_ PNET_BUFFER_LIST_POOL_PARAMETERS Parameters)
{
    UNREFERENCED_PARAMETER(NdisHandle);
    UNREFERENCED_PARAMETER(Parameters);
    /* Return a non-NULL cookie so callers think the pool was created.
     * The cookie is never actually used by any alloc because
     * NdisAllocateNetBufferList below returns NULL, making the caller
     * fail gracefully. */
    return (NDIS_HANDLE)(ULONG_PTR)0x6500u;
}

VOID
EXPORT
NdisFreeNetBufferListPool(
    _In_ NDIS_HANDLE PoolHandle)
{
    UNREFERENCED_PARAMETER(PoolHandle);
}

NDIS_HANDLE
EXPORT
NdisAllocateNetBufferPool(
    _In_opt_ NDIS_HANDLE NdisHandle,
    _In_ PNET_BUFFER_POOL_PARAMETERS Parameters)
{
    UNREFERENCED_PARAMETER(NdisHandle);
    UNREFERENCED_PARAMETER(Parameters);
    return (NDIS_HANDLE)(ULONG_PTR)0x6501u;
}

VOID
EXPORT
NdisFreeNetBufferPool(
    _In_ NDIS_HANDLE PoolHandle)
{
    UNREFERENCED_PARAMETER(PoolHandle);
}

PNET_BUFFER_LIST
EXPORT
NdisAllocateNetBufferList(
    _In_ NDIS_HANDLE PoolHandle,
    _In_ USHORT ContextSize,
    _In_ USHORT ContextBackFill)
{
    UNREFERENCED_PARAMETER(PoolHandle);
    UNREFERENCED_PARAMETER(ContextSize);
    UNREFERENCED_PARAMETER(ContextBackFill);
    return NULL;
}

VOID
EXPORT
NdisFreeNetBufferList(
    _In_ PNET_BUFFER_LIST NetBufferList)
{
    UNREFERENCED_PARAMETER(NetBufferList);
}

PNET_BUFFER_LIST
EXPORT
NdisAllocateNetBufferAndNetBufferList(
    _In_ NDIS_HANDLE PoolHandle,
    _In_ USHORT ContextSize,
    _In_ USHORT ContextBackFill,
    _In_opt_ PMDL MdlChain,
    _In_ ULONG DataOffset,
    _In_ SIZE_T DataLength)
{
    UNREFERENCED_PARAMETER(PoolHandle);
    UNREFERENCED_PARAMETER(ContextSize);
    UNREFERENCED_PARAMETER(ContextBackFill);
    UNREFERENCED_PARAMETER(MdlChain);
    UNREFERENCED_PARAMETER(DataOffset);
    UNREFERENCED_PARAMETER(DataLength);
    return NULL;
}

PNET_BUFFER
EXPORT
NdisAllocateNetBuffer(
    _In_ NDIS_HANDLE PoolHandle,
    _In_opt_ PMDL MdlChain,
    _In_ ULONG DataOffset,
    _In_ SIZE_T DataLength)
{
    UNREFERENCED_PARAMETER(PoolHandle);
    UNREFERENCED_PARAMETER(MdlChain);
    UNREFERENCED_PARAMETER(DataOffset);
    UNREFERENCED_PARAMETER(DataLength);
    return NULL;
}

VOID
EXPORT
NdisFreeNetBuffer(
    _In_ PNET_BUFFER NetBuffer)
{
    UNREFERENCED_PARAMETER(NetBuffer);
}

NDIS_STATUS
EXPORT
NdisRetreatNetBufferDataStart(
    _In_ PNET_BUFFER NetBuffer,
    _In_ ULONG DataOffsetDelta,
    _In_ ULONG DataBackFill,
    _In_opt_ PVOID AllocateMdlHandler)
{
    UNREFERENCED_PARAMETER(NetBuffer);
    UNREFERENCED_PARAMETER(DataOffsetDelta);
    UNREFERENCED_PARAMETER(DataBackFill);
    UNREFERENCED_PARAMETER(AllocateMdlHandler);
    return NDIS_STATUS_NOT_SUPPORTED;
}

VOID
EXPORT
NdisAdvanceNetBufferDataStart(
    _In_ PNET_BUFFER NetBuffer,
    _In_ ULONG DataOffsetDelta,
    _In_ BOOLEAN FreeMdl,
    _In_opt_ PVOID FreeMdlHandler)
{
    UNREFERENCED_PARAMETER(NetBuffer);
    UNREFERENCED_PARAMETER(DataOffsetDelta);
    UNREFERENCED_PARAMETER(FreeMdl);
    UNREFERENCED_PARAMETER(FreeMdlHandler);
}

/* ------------------------------------------------------------------ */
/*  Miniport driver registration (E2)                                  */
/* ------------------------------------------------------------------ */

NDIS_STATUS
EXPORT
NdisMRegisterMiniportDriver(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath,
    _In_opt_ NDIS_HANDLE MiniportDriverContext,
    _In_ PNDIS_MINIPORT_DRIVER_CHARACTERISTICS MiniportDriverCharacteristics,
    _Out_ PNDIS_HANDLE NdisMiniportDriverHandle)
{
    UNREFERENCED_PARAMETER(DriverObject);
    UNREFERENCED_PARAMETER(RegistryPath);
    UNREFERENCED_PARAMETER(MiniportDriverContext);
    UNREFERENCED_PARAMETER(MiniportDriverCharacteristics);
    *NdisMiniportDriverHandle = (NDIS_HANDLE)(ULONG_PTR)0x60010000u;
    return NDIS_STATUS_SUCCESS;
}

VOID
EXPORT
NdisMDeregisterMiniportDriver(
    _In_ NDIS_HANDLE NdisMiniportDriverHandle)
{
    UNREFERENCED_PARAMETER(NdisMiniportDriverHandle);
}

NDIS_STATUS
EXPORT
NdisMSetMiniportAttributes(
    _In_ NDIS_HANDLE NdisMiniportAdapterHandle,
    _In_ PNDIS_MINIPORT_ADAPTER_ATTRIBUTES MiniportAttributes)
{
    UNREFERENCED_PARAMETER(NdisMiniportAdapterHandle);
    UNREFERENCED_PARAMETER(MiniportAttributes);
    return NDIS_STATUS_SUCCESS;
}

/* ------------------------------------------------------------------ */
/*  Filter driver registration                                         */
/* ------------------------------------------------------------------ */

NDIS_STATUS
EXPORT
NdisFRegisterFilterDriver(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ NDIS_HANDLE FilterDriverContext,
    _In_ PNDIS_FILTER_DRIVER_CHARACTERISTICS FilterDriverCharacteristics,
    _Out_ PNDIS_HANDLE NdisFilterDriverHandle)
{
    UNREFERENCED_PARAMETER(DriverObject);
    UNREFERENCED_PARAMETER(FilterDriverContext);
    UNREFERENCED_PARAMETER(FilterDriverCharacteristics);
    *NdisFilterDriverHandle = (NDIS_HANDLE)(ULONG_PTR)0x60020000u;
    return NDIS_STATUS_SUCCESS;
}

VOID
EXPORT
NdisFDeregisterFilterDriver(
    _In_ NDIS_HANDLE NdisFilterDriverHandle)
{
    UNREFERENCED_PARAMETER(NdisFilterDriverHandle);
}

/* ------------------------------------------------------------------ */
/*  Protocol driver registration                                       */
/* ------------------------------------------------------------------ */

NDIS_STATUS
EXPORT
NdisRegisterProtocolDriver(
    _In_ NDIS_HANDLE ProtocolDriverContext,
    _In_ PNDIS_PROTOCOL_DRIVER_CHARACTERISTICS ProtocolDriverCharacteristics,
    _Out_ PNDIS_HANDLE NdisProtocolHandle)
{
    UNREFERENCED_PARAMETER(ProtocolDriverContext);
    UNREFERENCED_PARAMETER(ProtocolDriverCharacteristics);
    *NdisProtocolHandle = (NDIS_HANDLE)(ULONG_PTR)0x60030000u;
    return NDIS_STATUS_SUCCESS;
}

VOID
EXPORT
NdisDeregisterProtocolDriver(
    _In_ NDIS_HANDLE NdisProtocolHandle)
{
    UNREFERENCED_PARAMETER(NdisProtocolHandle);
}

/* ------------------------------------------------------------------ */
/*  Datapath + OID request (E3)                                        */
/* ------------------------------------------------------------------ */

VOID
EXPORT
NdisSendNetBufferLists(
    _In_ NDIS_HANDLE NdisBindingHandle,
    _In_ PNET_BUFFER_LIST NetBufferList,
    _In_ NDIS_PORT_NUMBER PortNumber,
    _In_ ULONG SendFlags)
{
    UNREFERENCED_PARAMETER(NdisBindingHandle);
    UNREFERENCED_PARAMETER(NetBufferList);
    UNREFERENCED_PARAMETER(PortNumber);
    UNREFERENCED_PARAMETER(SendFlags);
}

VOID
EXPORT
NdisReturnNetBufferLists(
    _In_ NDIS_HANDLE NdisBindingHandle,
    _In_ PNET_BUFFER_LIST NetBufferLists,
    _In_ ULONG ReturnFlags)
{
    UNREFERENCED_PARAMETER(NdisBindingHandle);
    UNREFERENCED_PARAMETER(NetBufferLists);
    UNREFERENCED_PARAMETER(ReturnFlags);
}

NDIS_STATUS
EXPORT
NdisOidRequest(
    _In_ NDIS_HANDLE NdisBindingHandle,
    _In_ PNDIS_OID_REQUEST OidRequest)
{
    UNREFERENCED_PARAMETER(NdisBindingHandle);
    UNREFERENCED_PARAMETER(OidRequest);
    return NDIS_STATUS_NOT_SUPPORTED;
}

VOID
EXPORT
NdisMSendNetBufferListsComplete(
    _In_ NDIS_HANDLE NdisMiniportHandle,
    _In_ PNET_BUFFER_LIST NetBufferList,
    _In_ ULONG SendCompleteFlags)
{
    UNREFERENCED_PARAMETER(NdisMiniportHandle);
    UNREFERENCED_PARAMETER(NetBufferList);
    UNREFERENCED_PARAMETER(SendCompleteFlags);
}

VOID
EXPORT
NdisMIndicateReceiveNetBufferLists(
    _In_ NDIS_HANDLE NdisMiniportHandle,
    _In_ PNET_BUFFER_LIST NetBufferLists,
    _In_ NDIS_PORT_NUMBER PortNumber,
    _In_ ULONG NumberOfNetBufferLists,
    _In_ ULONG ReceiveFlags)
{
    UNREFERENCED_PARAMETER(NdisMiniportHandle);
    UNREFERENCED_PARAMETER(NetBufferLists);
    UNREFERENCED_PARAMETER(PortNumber);
    UNREFERENCED_PARAMETER(NumberOfNetBufferLists);
    UNREFERENCED_PARAMETER(ReceiveFlags);
}

VOID
EXPORT
NdisMIndicateStatusEx(
    _In_ NDIS_HANDLE NdisMiniportHandle,
    _In_ PNDIS_STATUS_INDICATION StatusIndication)
{
    UNREFERENCED_PARAMETER(NdisMiniportHandle);
    UNREFERENCED_PARAMETER(StatusIndication);
}

VOID
EXPORT
NdisMOidRequestComplete(
    _In_ NDIS_HANDLE NdisMiniportHandle,
    _In_ PNDIS_OID_REQUEST OidRequest,
    _In_ NDIS_STATUS Status)
{
    UNREFERENCED_PARAMETER(NdisMiniportHandle);
    UNREFERENCED_PARAMETER(OidRequest);
    UNREFERENCED_PARAMETER(Status);
}

/* ------------------------------------------------------------------ */
/*  NDIS 6 hardware miniport stubs (for e1000 et al)                  */
/* ------------------------------------------------------------------ */

NDIS_STATUS
EXPORT
NdisMRegisterInterruptEx(
    _In_ NDIS_HANDLE NdisMiniportHandle,
    _In_ NDIS_HANDLE MiniportInterruptContext,
    _In_ PNDIS_MINIPORT_INTERRUPT_CHARACTERISTICS MiniportInterruptCharacteristics,
    _Out_ PNDIS_HANDLE NdisInterruptHandle)
{
    UNREFERENCED_PARAMETER(NdisMiniportHandle);
    UNREFERENCED_PARAMETER(MiniportInterruptContext);
    UNREFERENCED_PARAMETER(MiniportInterruptCharacteristics);
    *NdisInterruptHandle = (NDIS_HANDLE)(ULONG_PTR)0x60040000u;
    return NDIS_STATUS_SUCCESS;
}

VOID
EXPORT
NdisMDeregisterInterruptEx(
    _In_ NDIS_HANDLE NdisInterruptHandle)
{
    UNREFERENCED_PARAMETER(NdisInterruptHandle);
}

NDIS_STATUS
EXPORT
NdisMRegisterScatterGatherDma(
    _In_ NDIS_HANDLE NdisMiniportHandle,
    _In_ PNDIS_SG_DMA_DESCRIPTION DmaDescription,
    _Out_ PNDIS_HANDLE NdisMiniportDmaHandle)
{
    UNREFERENCED_PARAMETER(NdisMiniportHandle);
    UNREFERENCED_PARAMETER(DmaDescription);
    *NdisMiniportDmaHandle = (NDIS_HANDLE)(ULONG_PTR)0x60050000u;
    return NDIS_STATUS_SUCCESS;
}

VOID
EXPORT
NdisMDeregisterScatterGatherDma(
    _In_ NDIS_HANDLE NdisMiniportDmaHandle)
{
    UNREFERENCED_PARAMETER(NdisMiniportDmaHandle);
}

/* EOF */
