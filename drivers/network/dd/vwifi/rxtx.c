/*
 * PROJECT:     ReactOS Virtual Native 802.11 (dot11) Miniport
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Data path: transmit NBLs are counted and completed immediately
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 *
 * There is no air interface: transmitted frames are dropped and completed with
 * NDIS_STATUS_SUCCESS, and no receives are indicated.  The ExtSTA send context
 * (DOT11_EXTSTA_SEND_CONTEXT in NetBufferListInfo) is left untouched.
 */

#include "vwifi.h"
#include <debug.h>

static
ULONG
VWifiNblDataLength(
    IN PNET_BUFFER_LIST Nbl)
{
    PNET_BUFFER NetBuffer = NET_BUFFER_LIST_FIRST_NB(Nbl);
    ULONG Total = 0;

    while (NetBuffer != NULL)
    {
        Total += NET_BUFFER_DATA_LENGTH(NetBuffer);
        NetBuffer = NET_BUFFER_NEXT_NB(NetBuffer);
    }

    return Total;
}

VOID
NTAPI
VWifiMiniportSendNetBufferLists(
    IN NDIS_HANDLE MiniportAdapterContext,
    IN PNET_BUFFER_LIST NetBufferLists,
    IN NDIS_PORT_NUMBER PortNumber,
    IN ULONG SendFlags)
{
    PVWIFI_ADAPTER Adapter = (PVWIFI_ADAPTER)MiniportAdapterContext;
    PNET_BUFFER_LIST Nbl = NetBufferLists;
    ULONG CompleteFlags = 0;

    UNREFERENCED_PARAMETER(PortNumber);

    if (NDIS_TEST_SEND_AT_DISPATCH_LEVEL(SendFlags))
    {
        CompleteFlags |= NDIS_SEND_COMPLETE_FLAGS_DISPATCH_LEVEL;
    }

    /* Frames are always completed, even when paused or not associated:
     * a software sink never pends. */
    while (Nbl != NULL)
    {
        ULONG Length = VWifiNblDataLength(Nbl);
        BOOLEAN Deliverable = Adapter->DataPathRunning && !Adapter->Halting &&
                              (Adapter->ConnState == VWifiConnected);

        if (Deliverable)
        {
            Adapter->TxOkCount++;
            Adapter->TxBytes += Length;
            NET_BUFFER_LIST_STATUS(Nbl) = NDIS_STATUS_SUCCESS;
        }
        else
        {
            /* Not associated (or paused): succeed the NBL so the protocol is
             * not stalled, but count it as a tx error. */
            Adapter->TxErrorCount++;
            NET_BUFFER_LIST_STATUS(Nbl) = NDIS_STATUS_SUCCESS;
        }

        Nbl = NET_BUFFER_LIST_NEXT_NBL(Nbl);
    }

    NdisMSendNetBufferListsComplete(Adapter->MiniportAdapterHandle,
                                    NetBufferLists, CompleteFlags);
}

VOID
NTAPI
VWifiMiniportReturnNetBufferLists(
    IN NDIS_HANDLE MiniportAdapterContext,
    IN PNET_BUFFER_LIST NetBufferLists,
    IN ULONG ReturnFlags)
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(NetBufferLists);
    UNREFERENCED_PARAMETER(ReturnFlags);

    /* No receive NBLs are indicated, so nothing is ever returned. */
}

VOID
NTAPI
VWifiMiniportCancelSend(
    IN NDIS_HANDLE MiniportAdapterContext,
    IN PVOID CancelId)
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(CancelId);

    /* Sends complete synchronously inside SendNetBufferLists; nothing pends. */
}
