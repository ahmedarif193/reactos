/*
 * PROJECT:     ReactOS Broadcom/Cypress CYW43455 Native 802.11 Miniport
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     NDIS 6.20 miniport lifecycle and SDIO bring-up orchestration
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "cyw43455.h"

#define NDEBUG
#include <debug.h>

extern NDIS_OID CywSupportedOids[];
extern ULONG CywSupportedOidCount;

static NDIS_HANDLE gMiniportDriverHandle = NULL;

static
NDIS_STATUS
CywSetMiniportAttributes(
    _In_ PCYW_ADAPTER Adapter)
{
    NDIS_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES RegAttr;
    NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES GenAttr;
    NDIS_STATUS Status;

    RtlZeroMemory(&RegAttr, sizeof(RegAttr));
    RegAttr.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES;
    RegAttr.Header.Revision = NDIS_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES_REVISION_1;
    RegAttr.Header.Size = sizeof(NDIS_MINIPORT_ADAPTER_REGISTRATION_ATTRIBUTES);
    RegAttr.MiniportAdapterContext = (NDIS_HANDLE)Adapter;
    RegAttr.AttributeFlags = NDIS_MINIPORT_ATTRIBUTES_NO_HALT_ON_SUSPEND |
                             NDIS_MINIPORT_ATTRIBUTES_SURPRISE_REMOVE_OK;
    RegAttr.CheckForHangTimeInSeconds = 0;
    RegAttr.InterfaceType = NdisInterfaceInternal;

    Status = NdisMSetMiniportAttributes(Adapter->MiniportAdapterHandle,
        (PNDIS_MINIPORT_ADAPTER_ATTRIBUTES)&RegAttr);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        return Status;
    }

    RtlZeroMemory(&GenAttr, sizeof(GenAttr));
    GenAttr.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES;
    GenAttr.Header.Revision = NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES_REVISION_1;
    GenAttr.Header.Size = sizeof(NDIS_MINIPORT_ADAPTER_GENERAL_ATTRIBUTES);

    GenAttr.MediaType = NdisMediumNative802_11;
    GenAttr.PhysicalMediumType = NdisPhysicalMediumNative802_11;
    GenAttr.MtuSize = CYW_MTU_SIZE;
    GenAttr.MaxXmitLinkSpeed = CYW_LINK_SPEED_BPS;
    GenAttr.MaxRcvLinkSpeed = CYW_LINK_SPEED_BPS;
    GenAttr.XmitLinkSpeed = CYW_LINK_SPEED_BPS;
    GenAttr.RcvLinkSpeed = CYW_LINK_SPEED_BPS;
    GenAttr.MediaConnectState = MediaConnectStateDisconnected;
    GenAttr.MediaDuplexState = MediaDuplexStateFull;
    GenAttr.LookaheadSize = CYW_MAX_FRAME_SIZE;
    GenAttr.MacOptions = NDIS_MAC_OPTION_COPY_LOOKAHEAD_DATA |
                         NDIS_MAC_OPTION_TRANSFERS_NOT_PEND |
                         NDIS_MAC_OPTION_NO_LOOPBACK;
    GenAttr.SupportedPacketFilters = NDIS_PACKET_TYPE_DIRECTED |
                                     NDIS_PACKET_TYPE_MULTICAST |
                                     NDIS_PACKET_TYPE_ALL_MULTICAST |
                                     NDIS_PACKET_TYPE_BROADCAST;
    GenAttr.MaxMulticastListSize = 32;
    GenAttr.MacAddressLength = CYW_ADDRESS_LENGTH;
    RtlCopyMemory(GenAttr.PermanentMacAddress, Adapter->PermanentAddress, CYW_ADDRESS_LENGTH);
    RtlCopyMemory(GenAttr.CurrentMacAddress, Adapter->CurrentAddress, CYW_ADDRESS_LENGTH);
    GenAttr.RecvScaleCapabilities = NULL;
    GenAttr.AccessType = NET_IF_ACCESS_BROADCAST;
    GenAttr.DirectionType = NET_IF_DIRECTION_SENDRECEIVE;
    GenAttr.ConnectionType = NET_IF_CONNECTION_DEDICATED;
    GenAttr.IfType = IF_TYPE_IEEE80211;
    GenAttr.IfConnectorPresent = TRUE;
    GenAttr.SupportedStatistics = 0;
    GenAttr.SupportedOidList = CywSupportedOids;
    GenAttr.SupportedOidListLength = CywSupportedOidCount * sizeof(NDIS_OID);

    Status = NdisMSetMiniportAttributes(Adapter->MiniportAdapterHandle,
        (PNDIS_MINIPORT_ADAPTER_ATTRIBUTES)&GenAttr);
    return Status;
}

static
VOID
NTAPI
CywCardInterruptCallback(
    _In_ PVOID CallbackRoutineContext,
    _In_ ULONG InterruptType)
{
    PCYW_ADAPTER Adapter = (PCYW_ADAPTER)CallbackRoutineContext;

    UNREFERENCED_PARAMETER(InterruptType);

    if (Adapter->Halting)
    {
        return;
    }
    KeSetEvent(&Adapter->RxEvent, IO_NETWORK_INCREMENT, FALSE);
}

static
VOID
CywEnableCardInterrupt(
    _In_ PCYW_ADAPTER Adapter)
{
    SDBUS_INTERFACE_PARAMETERS Params;
    NTSTATUS Status;

    if (Adapter->SdioCoreBase == 0 || Adapter->SdBus.InitializeInterface == NULL)
    {
        return;
    }

    RtlZeroMemory(&Params, sizeof(Params));
    Params.Size = sizeof(Params);
    Params.TargetObject = Adapter->Pdo;
    Params.DeviceGeneratesInterrupts = TRUE;
    Params.CallbackAtDpcLevel = TRUE;
    Params.CallbackRoutine = CywCardInterruptCallback;
    Params.CallbackRoutineContext = Adapter;

    Status = Adapter->SdBus.InitializeInterface(Adapter->SdBus.Context, &Params);
    if (NT_SUCCESS(Status))
    {
        Adapter->CardIntRegistered = TRUE;
    }
}

static
VOID
CywDisableCardInterrupt(
    _In_ PCYW_ADAPTER Adapter)
{
    SDBUS_INTERFACE_PARAMETERS Params;

    if (!Adapter->CardIntRegistered || Adapter->SdBus.InitializeInterface == NULL)
    {
        return;
    }

    RtlZeroMemory(&Params, sizeof(Params));
    Params.Size = sizeof(Params);
    Params.TargetObject = Adapter->Pdo;
    Params.DeviceGeneratesInterrupts = FALSE;
    Params.CallbackRoutine = NULL;
    Params.CallbackRoutineContext = NULL;

    Adapter->SdBus.InitializeInterface(Adapter->SdBus.Context, &Params);
    Adapter->CardIntRegistered = FALSE;
}

NDIS_STATUS
NTAPI
CywMiniportInitializeEx(
    _In_ NDIS_HANDLE NdisMiniportHandle,
    _In_ NDIS_HANDLE MiniportDriverContext,
    _In_ PNDIS_MINIPORT_INIT_PARAMETERS MiniportInitParameters)
{
    PCYW_ADAPTER Adapter;
    NDIS_STATUS Status;
    NTSTATUS NtStatus;

    UNREFERENCED_PARAMETER(MiniportDriverContext);
    UNREFERENCED_PARAMETER(MiniportInitParameters);

    Adapter = CywAllocate(sizeof(CYW_ADAPTER));
    if (Adapter == NULL)
    {
        return NDIS_STATUS_RESOURCES;
    }

    Adapter->MiniportAdapterHandle = NdisMiniportHandle;
    Adapter->RxBufFree = NULL;
    NdisAllocateSpinLock(&Adapter->Lock);
    Adapter->CurrentPhyType = dot11_phy_type_ht;
    Adapter->CurrentOperationMode = DOT11_OPERATION_MODE_EXTENSIBLE_STATION;
    Adapter->RadioOn = TRUE;
    Adapter->CurrentBackplaneWindow = 0xFFFFFFFF;

    Adapter->PermanentAddress[0] = 0x00;
    Adapter->PermanentAddress[1] = 0x90;
    Adapter->PermanentAddress[2] = 0x4C;
    Adapter->PermanentAddress[3] = 0xC5;
    Adapter->PermanentAddress[4] = 0x12;
    Adapter->PermanentAddress[5] = 0x38;
    RtlCopyMemory(Adapter->CurrentAddress, Adapter->PermanentAddress, CYW_ADDRESS_LENGTH);

    Adapter->ControlBuffer = CywAllocate(CYW_CONTROL_BUFFER_SIZE);
    Adapter->RxBuffer = CywAllocate(CYW_RX_BUFFER_SIZE);
    Adapter->TxBuffer = CywAllocate(CYW_CONTROL_BUFFER_SIZE);
    Adapter->RegScratch = CywAllocate(64);
    if (Adapter->ControlBuffer == NULL || Adapter->RxBuffer == NULL ||
        Adapter->TxBuffer == NULL || Adapter->RegScratch == NULL)
    {
        Status = NDIS_STATUS_RESOURCES;
        goto Fail;
    }
    KeInitializeMutex(&Adapter->F2Lock, 0);
    KeInitializeMutex(&Adapter->CmdLock, 0);
    KeInitializeEvent(&Adapter->CtrlEvent, NotificationEvent, FALSE);
    KeInitializeEvent(&Adapter->RxEvent, SynchronizationEvent, FALSE);
    InitializeListHead(&Adapter->TxQueue);
    KeInitializeSpinLock(&Adapter->TxLock);

    {
        NET_BUFFER_LIST_POOL_PARAMETERS PoolParams;
        RtlZeroMemory(&PoolParams, sizeof(PoolParams));
        PoolParams.Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
        PoolParams.Header.Revision = NET_BUFFER_LIST_POOL_PARAMETERS_REVISION_1;
        PoolParams.Header.Size = sizeof(PoolParams);
        PoolParams.ProtocolId = NDIS_PROTOCOL_ID_DEFAULT;
        PoolParams.fAllocateNetBuffer = TRUE;
        PoolParams.PoolTag = CYW_TAG;
        PoolParams.DataSize = 0;
        Adapter->RxNblPool = NdisAllocateNetBufferListPool(NdisMiniportHandle, &PoolParams);
        if (Adapter->RxNblPool == NULL)
        {
            Status = NDIS_STATUS_RESOURCES;
            goto Fail;
        }
    }

    KeInitializeSpinLock(&Adapter->RxBufLock);
    Adapter->RxBufFree = NULL;
    {
        ULONG RbIdx;
        for (RbIdx = 0; RbIdx < CYW_RX_POOL_COUNT; RbIdx++)
        {
            PCYW_RX_BUF Rb = CywAllocate(sizeof(CYW_RX_BUF));
            if (Rb == NULL)
                break;
            Rb->Buffer = CywAllocate(CYW_MAX_FRAME_SIZE);
            if (Rb->Buffer == NULL)
            {
                CywFree(Rb);
                break;
            }
            Rb->Mdl = NdisAllocateMdl(NdisMiniportHandle, Rb->Buffer, CYW_MAX_FRAME_SIZE);
            if (Rb->Mdl == NULL)
            {
                CywFree(Rb->Buffer);
                CywFree(Rb);
                break;
            }
            MmBuildMdlForNonPagedPool(Rb->Mdl);
            Rb->Next = Adapter->RxBufFree;
            Adapter->RxBufFree = Rb;
        }
    }

    NdisMGetDeviceProperty(NdisMiniportHandle,
                           &Adapter->Pdo,
                           NULL,
                           &Adapter->NextDeviceObject,
                           NULL,
                           NULL);
    if (Adapter->Pdo == NULL)
    {
        Status = NDIS_STATUS_FAILURE;
        goto Fail;
    }

    NtStatus = CywSdioOpen(Adapter);
    if (!NT_SUCCESS(NtStatus))
    {
        Status = NDIS_STATUS_OPEN_FAILED;
        goto Fail;
    }

    NtStatus = CywChipBringUp(Adapter);
    if (!NT_SUCCESS(NtStatus))
    {
        Status = NDIS_STATUS_HARD_ERRORS;
        goto Fail;
    }

    Status = CywSetMiniportAttributes(Adapter);
    if (Status != NDIS_STATUS_SUCCESS)
    {
        goto Fail;
    }

    Adapter->InterruptWorkItem = NdisAllocateIoWorkItem(NdisMiniportHandle);
    if (Adapter->InterruptWorkItem == NULL)
    {
        Status = NDIS_STATUS_RESOURCES;
        goto Fail;
    }

    NtStatus = CywStartRxThread(Adapter);
    if (!NT_SUCCESS(NtStatus))
    {
        Status = NDIS_STATUS_FAILURE;
        goto Fail;
    }

    CywEnableCardInterrupt(Adapter);

    return NDIS_STATUS_SUCCESS;

Fail:
    CywStopRxThread(Adapter);
    if (Adapter->InterruptWorkItem != NULL)
    {
        NdisFreeIoWorkItem(Adapter->InterruptWorkItem);
    }
    CywSdioClose(Adapter);
    CywFree(Adapter->ControlBuffer);
    CywFree(Adapter->RxBuffer);
    CywFree(Adapter->TxBuffer);
    CywFree(Adapter->RegScratch);
    while (Adapter->RxBufFree != NULL)
    {
        PCYW_RX_BUF Rb = Adapter->RxBufFree;
        Adapter->RxBufFree = Rb->Next;
        if (Rb->Mdl != NULL)
            NdisFreeMdl(Rb->Mdl);
        CywFree(Rb->Buffer);
        CywFree(Rb);
    }
    if (Adapter->RxNblPool != NULL)
    {
        NdisFreeNetBufferListPool(Adapter->RxNblPool);
    }
    NdisFreeSpinLock(&Adapter->Lock);
    CywFree(Adapter);
    return Status;
}

VOID
NTAPI
CywMiniportHaltEx(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ NDIS_HALT_ACTION HaltAction)
{
    PCYW_ADAPTER Adapter = (PCYW_ADAPTER)MiniportAdapterContext;

    UNREFERENCED_PARAMETER(HaltAction);

    Adapter->Halting = TRUE;

    CywStopRxThread(Adapter);
    CywDrainTxQueue(Adapter);

    CywDisableCardInterrupt(Adapter);

    KeFlushQueuedDpcs();

    if (Adapter->InterruptWorkItem != NULL)
    {
        NdisFreeIoWorkItem(Adapter->InterruptWorkItem);
        Adapter->InterruptWorkItem = NULL;
    }

    CywSdioClose(Adapter);
    CywFree(Adapter->ControlBuffer);
    CywFree(Adapter->RxBuffer);
    CywFree(Adapter->TxBuffer);
    CywFree(Adapter->RegScratch);
    while (Adapter->RxBufFree != NULL)
    {
        PCYW_RX_BUF Rb = Adapter->RxBufFree;
        Adapter->RxBufFree = Rb->Next;
        if (Rb->Mdl != NULL)
            NdisFreeMdl(Rb->Mdl);
        CywFree(Rb->Buffer);
        CywFree(Rb);
    }
    if (Adapter->RxNblPool != NULL)
    {
        NdisFreeNetBufferListPool(Adapter->RxNblPool);
    }
    NdisFreeSpinLock(&Adapter->Lock);
    CywFree(Adapter);
}

NDIS_STATUS
NTAPI
CywMiniportPauseEx(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNDIS_MINIPORT_PAUSE_PARAMETERS MiniportPauseParameters)
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(MiniportPauseParameters);

    return NDIS_STATUS_SUCCESS;
}

NDIS_STATUS
NTAPI
CywMiniportRestartEx(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNDIS_MINIPORT_RESTART_PARAMETERS MiniportRestartParameters)
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(MiniportRestartParameters);

    return NDIS_STATUS_SUCCESS;
}

VOID
NTAPI
CywMiniportShutdownEx(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ NDIS_SHUTDOWN_ACTION ShutdownAction)
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(ShutdownAction);
}

VOID
NTAPI
CywMiniportDevicePnpEventNotify(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNET_DEVICE_PNP_EVENT NetDevicePnPEvent)
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(NetDevicePnPEvent);
}

typedef struct _CYW_TX_WORK
{
    LIST_ENTRY Link;
    PNET_BUFFER_LIST Nbl;
    ULONG EthLen;
    UCHAR Eth[CYW_MAX_FRAME_SIZE];
} CYW_TX_WORK, *PCYW_TX_WORK;

static
ULONG
CywBuildEthFromNbl(
    _In_ PNET_BUFFER Nb,
    _Out_writes_(CYW_MAX_FRAME_SIZE) PUCHAR Scratch,
    _Out_writes_(CYW_MAX_FRAME_SIZE) PUCHAR Eth)
{
    ULONG FrameLen = NET_BUFFER_DATA_LENGTH(Nb);
    PUCHAR Frame;
    PCYW_DOT11_HEADER Dot11;
    PCYW_SNAP_HEADER Snap;
    ULONG PayloadLen;

    if (FrameLen < sizeof(CYW_DOT11_HEADER) + sizeof(CYW_SNAP_HEADER) ||
        FrameLen > CYW_MAX_FRAME_SIZE)
    {
        return 0;
    }

    Frame = NdisGetDataBuffer(Nb, FrameLen, Scratch, 1, 0);
    if (Frame == NULL)
    {
        return 0;
    }

    Dot11 = (PCYW_DOT11_HEADER)Frame;
    Snap = (PCYW_SNAP_HEADER)(Frame + sizeof(CYW_DOT11_HEADER));
    PayloadLen = FrameLen - sizeof(CYW_DOT11_HEADER) - sizeof(CYW_SNAP_HEADER);

    RtlCopyMemory(Eth, Dot11->Address3, CYW_ADDRESS_LENGTH);
    RtlCopyMemory(Eth + CYW_ADDRESS_LENGTH, Dot11->Address2, CYW_ADDRESS_LENGTH);
    Eth[12] = Snap->EtherType[0];
    Eth[13] = Snap->EtherType[1];
    RtlCopyMemory(Eth + sizeof(CYW_ETHER_HEADER),
                  Frame + sizeof(CYW_DOT11_HEADER) + sizeof(CYW_SNAP_HEADER),
                  PayloadLen);
    return sizeof(CYW_ETHER_HEADER) + PayloadLen;
}

VOID
CywDrainTxQueue(
    _In_ PCYW_ADAPTER Adapter)
{
    for (;;)
    {
        PLIST_ENTRY Entry = ExInterlockedRemoveHeadList(&Adapter->TxQueue, &Adapter->TxLock);
        PCYW_TX_WORK Work;
        NTSTATUS SendStatus;

        if (Entry == NULL)
        {
            break;
        }

        Work = CONTAINING_RECORD(Entry, CYW_TX_WORK, Link);
        SendStatus = CywSdpcmSendData(Adapter, Work->Eth, Work->EthLen);
        if (SendStatus == STATUS_DEVICE_BUSY && !Adapter->Halting)
        {
            ExInterlockedInsertHeadList(&Adapter->TxQueue, &Work->Link, &Adapter->TxLock);
            break;
        }

        NET_BUFFER_LIST_STATUS(Work->Nbl) =
            NT_SUCCESS(SendStatus) ? NDIS_STATUS_SUCCESS : NDIS_STATUS_FAILURE;
        NET_BUFFER_LIST_NEXT_NBL(Work->Nbl) = NULL;
        NdisMSendNetBufferListsComplete(Adapter->MiniportAdapterHandle, Work->Nbl, 0);
        CywFree(Work);
    }
}

VOID
NTAPI
CywMiniportSendNetBufferLists(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNET_BUFFER_LIST NetBufferLists,
    _In_ NDIS_PORT_NUMBER PortNumber,
    _In_ ULONG SendFlags)
{
    PCYW_ADAPTER Adapter = (PCYW_ADAPTER)MiniportAdapterContext;
    PNET_BUFFER_LIST Nbl = NetBufferLists;
    ULONG CompleteFlags = 0;

    UNREFERENCED_PARAMETER(PortNumber);

    if (NDIS_TEST_SEND_AT_DISPATCH_LEVEL(SendFlags))
    {
        CompleteFlags |= NDIS_SEND_COMPLETE_FLAGS_DISPATCH_LEVEL;
    }

    while (Nbl != NULL)
    {
        PNET_BUFFER_LIST Next = NET_BUFFER_LIST_NEXT_NBL(Nbl);
        PNET_BUFFER Nb = NET_BUFFER_LIST_FIRST_NB(Nbl);
        NDIS_STATUS NblStatus = NDIS_STATUS_FAILURE;
        BOOLEAN Deferred = FALSE;

        NET_BUFFER_LIST_NEXT_NBL(Nbl) = NULL;

        if (Nb != NULL)
        {
            UCHAR Scratch[CYW_MAX_FRAME_SIZE];
            UCHAR Eth[CYW_MAX_FRAME_SIZE];
            ULONG EthLen = CywBuildEthFromNbl(Nb, Scratch, Eth);
            BOOLEAN IsEapol = (EthLen >= 14 && Eth[12] == 0x88 && Eth[13] == 0x8E);

            if (EthLen != 0 && (Adapter->Associated || IsEapol))
            {
                NTSTATUS SendStatus = STATUS_DEVICE_BUSY;

                if (KeGetCurrentIrql() == PASSIVE_LEVEL)
                {
                    SendStatus = CywSdpcmSendData(Adapter, Eth, EthLen);
                    if (NT_SUCCESS(SendStatus))
                    {
                        NblStatus = NDIS_STATUS_SUCCESS;
                    }
                }

                if (SendStatus == STATUS_DEVICE_BUSY)
                {
                    PCYW_TX_WORK Work = CywAllocate(sizeof(CYW_TX_WORK));

                    if (Work != NULL)
                    {
                        RtlCopyMemory(Work->Eth, Eth, EthLen);
                        Work->EthLen = EthLen;
                        Work->Nbl = Nbl;
                        ExInterlockedInsertTailList(&Adapter->TxQueue,
                                                    &Work->Link,
                                                    &Adapter->TxLock);
                        KeSetEvent(&Adapter->RxEvent, IO_NETWORK_INCREMENT, FALSE);
                        Deferred = TRUE;
                    }
                }
            }
        }

        if (!Deferred)
        {
            NET_BUFFER_LIST_STATUS(Nbl) = NblStatus;
            NdisMSendNetBufferListsComplete(Adapter->MiniportAdapterHandle, Nbl, CompleteFlags);
        }

        Nbl = Next;
    }
}

VOID
NTAPI
CywMiniportReturnNetBufferLists(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNET_BUFFER_LIST NetBufferLists,
    _In_ ULONG ReturnFlags)
{
    PCYW_ADAPTER Adapter = (PCYW_ADAPTER)MiniportAdapterContext;
    PNET_BUFFER_LIST Nbl = NetBufferLists;
    UNREFERENCED_PARAMETER(ReturnFlags);

    while (Nbl != NULL)
    {
        PNET_BUFFER_LIST Next = NET_BUFFER_LIST_NEXT_NBL(Nbl);
        PCYW_RX_BUF Rb = (PCYW_RX_BUF)Nbl->MiniportReserved[0];
        NdisFreeNetBufferList(Nbl);
        if (Rb != NULL)
        {
            KIRQL OldIrql;
            KeAcquireSpinLock(&Adapter->RxBufLock, &OldIrql);
            Rb->Next = Adapter->RxBufFree;
            Adapter->RxBufFree = Rb;
            KeReleaseSpinLock(&Adapter->RxBufLock, OldIrql);
        }
        Nbl = Next;
    }
}

VOID
NTAPI
CywMiniportCancelSend(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PVOID CancelId)
{
    UNREFERENCED_PARAMETER(MiniportAdapterContext);
    UNREFERENCED_PARAMETER(CancelId);
}

VOID
NTAPI
CywMiniportUnload(
    _In_ PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);

    if (gMiniportDriverHandle != NULL)
    {
        NdisMDeregisterMiniportDriver(gMiniportDriverHandle);
        gMiniportDriverHandle = NULL;
    }
}

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    NDIS_MINIPORT_DRIVER_CHARACTERISTICS Chars;
    NDIS_STATUS Status;

    RtlZeroMemory(&Chars, sizeof(Chars));
    Chars.Header.Type = NDIS_OBJECT_TYPE_MINIPORT_DRIVER_CHARACTERISTICS;
    Chars.Header.Revision = NDIS_MINIPORT_DRIVER_CHARACTERISTICS_REVISION_2;
    Chars.Header.Size = sizeof(NDIS_MINIPORT_DRIVER_CHARACTERISTICS);
    Chars.MajorNdisVersion = 6;
    Chars.MinorNdisVersion = 30;
    Chars.MajorDriverVersion = 1;
    Chars.MinorDriverVersion = 0;

    Chars.InitializeHandlerEx = CywMiniportInitializeEx;
    Chars.HaltHandlerEx = CywMiniportHaltEx;
    Chars.UnloadHandler = CywMiniportUnload;
    Chars.PauseHandler = CywMiniportPauseEx;
    Chars.RestartHandler = CywMiniportRestartEx;
    Chars.OidRequestHandler = CywMiniportOidRequest;
    Chars.SendNetBufferListsHandler = CywMiniportSendNetBufferLists;
    Chars.ReturnNetBufferListsHandler = CywMiniportReturnNetBufferLists;
    Chars.CancelSendHandler = CywMiniportCancelSend;
    Chars.CheckForHangHandlerEx = NULL;
    Chars.ResetHandlerEx = NULL;
    Chars.DevicePnPEventNotifyHandler = CywMiniportDevicePnpEventNotify;
    Chars.ShutdownHandlerEx = CywMiniportShutdownEx;
    Chars.CancelOidRequestHandler = CywMiniportCancelOidRequest;

    Status = NdisMRegisterMiniportDriver(DriverObject,
                                         RegistryPath,
                                         NULL,
                                         &Chars,
                                         &gMiniportDriverHandle);

    return (NTSTATUS)Status;
}
