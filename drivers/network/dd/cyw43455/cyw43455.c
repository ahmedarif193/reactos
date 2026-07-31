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
    GenAttr.MaxXmitLinkSpeed = CYW_MAX_LINK_SPEED_BPS;
    GenAttr.MaxRcvLinkSpeed = CYW_MAX_LINK_SPEED_BPS;
    GenAttr.XmitLinkSpeed = NDIS_LINK_SPEED_UNKNOWN;
    GenAttr.RcvLinkSpeed = NDIS_LINK_SPEED_UNKNOWN;
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
    GenAttr.SupportedStatistics =
        NDIS_STATISTICS_FLAGS_VALID_DIRECTED_FRAMES_RCV |
        NDIS_STATISTICS_FLAGS_VALID_DIRECTED_FRAMES_XMIT |
        NDIS_STATISTICS_FLAGS_VALID_XMIT_ERROR;
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

    if (InterlockedCompareExchange(&Adapter->Halting, 0, 0) != 0)
    {
        return;
    }
    InterlockedExchange(&Adapter->CardInterruptPending, 1);
    KeSetEvent(&Adapter->BusEvent, IO_NETWORK_INCREMENT, FALSE);
}

static
NTSTATUS
CywEnableCardInterrupt(
    _In_ PCYW_ADAPTER Adapter)
{
    SDBUS_INTERFACE_PARAMETERS Params;
    NTSTATUS Status;

    if (Adapter->SdioCoreBase == 0 || Adapter->SdBus.InitializeInterface == NULL)
    {
        return STATUS_INVALID_DEVICE_STATE;
    }

    RtlZeroMemory(&Params, sizeof(Params));
    Params.Size = sizeof(Params);
    Params.SdioFlags = SDIO_FLAG_DO_NOT_MANAGE_IO_ENABLE;
    Params.TargetObject = Adapter->Pdo;
    Params.DeviceGeneratesInterrupts = TRUE;
    Params.CallbackAtDpcLevel = TRUE;
    Params.CallbackRoutine = CywCardInterruptCallback;
    Params.CallbackRoutineContext = Adapter;

    Status = Adapter->SdBus.InitializeInterface(Adapter->SdBus.Context, &Params);
    if (NT_SUCCESS(Status))
    {
        InterlockedExchange(&Adapter->CardIntRegistered, 1);
    }
    return Status;
}

static
NTSTATUS
CywDisableCardInterrupt(
    _In_ PCYW_ADAPTER Adapter)
{
    SDBUS_INTERFACE_PARAMETERS Params;
    NTSTATUS Status;

    if (InterlockedCompareExchange(&Adapter->CardIntRegistered, 0, 0) == 0)
    {
        return STATUS_SUCCESS;
    }
    if (Adapter->SdBus.InitializeInterface == NULL)
    {
        return STATUS_INVALID_DEVICE_STATE;
    }

    RtlZeroMemory(&Params, sizeof(Params));
    Params.Size = sizeof(Params);
    Params.SdioFlags = SDIO_FLAG_DO_NOT_MANAGE_IO_ENABLE;
    Params.TargetObject = Adapter->Pdo;
    Params.DeviceGeneratesInterrupts = FALSE;
    Params.CallbackRoutine = NULL;
    Params.CallbackRoutineContext = NULL;

    Status = Adapter->SdBus.InitializeInterface(Adapter->SdBus.Context, &Params);
    if (NT_SUCCESS(Status))
    {
        InterlockedExchange(&Adapter->CardInterruptPending, 0);
        InterlockedExchange(&Adapter->CardIntRegistered, 0);
    }
    return Status;
}

static
VOID
CywFreeAdapter(
    _In_ PCYW_ADAPTER Adapter)
{
    CywSdioClose(Adapter);
    CywFreeDmaBufs(Adapter);
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

BOOLEAN
CywQueueWorkItem(
    _In_ PCYW_ADAPTER Adapter,
    _In_ NDIS_IO_WORKITEM_ROUTINE Routine,
    _In_opt_ PVOID Context)
{
    NDIS_HANDLE WorkItem;
    KIRQL OldIrql;
    BOOLEAN Queue = FALSE;

    if (Routine == NULL)
    {
        return FALSE;
    }

    WorkItem = NdisAllocateIoWorkItem(Adapter->MiniportAdapterHandle);
    if (WorkItem == NULL)
    {
        return FALSE;
    }

    KeAcquireSpinLock(&Adapter->WorkItemLock, &OldIrql);
    if (InterlockedCompareExchange(&Adapter->Halting, 0, 0) == 0 &&
        Adapter->WorkItemsPending < MAXLONG)
    {
        if (Adapter->WorkItemsPending++ == 0)
        {
            KeResetEvent(&Adapter->WorkItemsDrainedEvent);
        }
        Queue = TRUE;
    }
    KeReleaseSpinLock(&Adapter->WorkItemLock, OldIrql);

    if (Queue)
    {
        NdisQueueIoWorkItem(WorkItem, Routine, Context);
    }
    else
    {
        NdisFreeIoWorkItem(WorkItem);
    }
    return Queue;
}

VOID
CywCompleteWorkItem(
    _In_ PCYW_ADAPTER Adapter,
    _In_ NDIS_HANDLE WorkItem)
{
    KIRQL OldIrql;

    NdisFreeIoWorkItem(WorkItem);

    KeAcquireSpinLock(&Adapter->WorkItemLock, &OldIrql);
    ASSERT(Adapter->WorkItemsPending > 0);
    if (Adapter->WorkItemsPending > 0 && --Adapter->WorkItemsPending == 0)
    {
        KeSetEvent(&Adapter->WorkItemsDrainedEvent, IO_NO_INCREMENT, FALSE);
    }
    KeReleaseSpinLock(&Adapter->WorkItemLock, OldIrql);
}

PCYW_RX_BUF
CywAcquireRxBuffer(
    _In_ PCYW_ADAPTER Adapter)
{
    PCYW_RX_BUF RxBuffer;
    KIRQL OldIrql;

    KeAcquireSpinLock(&Adapter->RxBufLock, &OldIrql);
    RxBuffer = Adapter->RxBufFree;
    if (RxBuffer != NULL)
    {
        Adapter->RxBufFree = RxBuffer->Next;
        ASSERT(Adapter->OutstandingRxNbls >= 0);
        ASSERT(Adapter->OutstandingRxNbls < CYW_RX_POOL_COUNT);
        if (Adapter->OutstandingRxNbls++ == 0)
        {
            KeResetEvent(&Adapter->RxDrainEvent);
        }
    }
    KeReleaseSpinLock(&Adapter->RxBufLock, OldIrql);
    return RxBuffer;
}

VOID
CywReleaseRxBuffer(
    _In_ PCYW_ADAPTER Adapter,
    _In_ PCYW_RX_BUF RxBuffer)
{
    KIRQL OldIrql;

    KeAcquireSpinLock(&Adapter->RxBufLock, &OldIrql);
    RxBuffer->Next = Adapter->RxBufFree;
    Adapter->RxBufFree = RxBuffer;
    ASSERT(Adapter->OutstandingRxNbls > 0);
    if (Adapter->OutstandingRxNbls > 0 && --Adapter->OutstandingRxNbls == 0)
    {
        KeSetEvent(&Adapter->RxDrainEvent, IO_NO_INCREMENT, FALSE);
    }
    KeReleaseSpinLock(&Adapter->RxBufLock, OldIrql);
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
    NdisAllocateSpinLock(&Adapter->Lock);
    Adapter->CurrentPhyType = dot11_phy_type_ht;
    Adapter->CurrentOperationMode = DOT11_OPERATION_MODE_EXTENSIBLE_STATION;
    Adapter->AuthAlgorithm = DOT11_AUTH_ALGO_80211_OPEN;
    Adapter->RadioOn = TRUE;
    Adapter->AutoConfigEnabled = 0x00000003;
    Adapter->CurrentBackplaneWindow = 0xFFFFFFFF;
    Adapter->ExpectedBcdcRequestId = -1;
    Adapter->ExpectedBcdcCommand = -1;

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
    Adapter->RegScratch = CywAllocate(CYW_REG_SCRATCH_SIZE);
    if (Adapter->ControlBuffer == NULL || Adapter->RxBuffer == NULL ||
        Adapter->TxBuffer == NULL || Adapter->RegScratch == NULL)
    {
        Status = NDIS_STATUS_RESOURCES;
        goto Fail;
    }

    NtStatus = CywRegisterDmaBuf(Adapter, Adapter->ControlBuffer, CYW_CONTROL_BUFFER_SIZE);
    if (NT_SUCCESS(NtStatus))
        NtStatus = CywRegisterDmaBuf(Adapter, Adapter->TxBuffer, CYW_CONTROL_BUFFER_SIZE);
    if (NT_SUCCESS(NtStatus))
        NtStatus = CywRegisterDmaBuf(Adapter, Adapter->RxBuffer, CYW_RX_BUFFER_SIZE);
    if (NT_SUCCESS(NtStatus))
        NtStatus = CywRegisterDmaBuf(Adapter, Adapter->RegScratch, CYW_REG_SCRATCH_SIZE);
    if (!NT_SUCCESS(NtStatus))
    {
        Status = NDIS_STATUS_RESOURCES;
        goto Fail;
    }
    KeInitializeMutex(&Adapter->F2Lock, 0);
    KeInitializeMutex(&Adapter->CmdLock, 0);
    KeInitializeMutex(&Adapter->ConnectLock, 0);
    KeInitializeMutex(&Adapter->BackplaneLock, 0);
    KeInitializeEvent(&Adapter->CtrlEvent, NotificationEvent, FALSE);
    KeInitializeEvent(&Adapter->BusEvent, SynchronizationEvent, FALSE);
    InitializeListHead(&Adapter->TxQueue);
    KeInitializeSpinLock(&Adapter->TxLock);
    KeInitializeEvent(&Adapter->TxDrainEvent, NotificationEvent, TRUE);
    KeInitializeSpinLock(&Adapter->WorkItemLock);
    KeInitializeEvent(&Adapter->WorkItemsDrainedEvent, NotificationEvent, TRUE);
    InterlockedExchange(&Adapter->Paused, 1);

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
    KeInitializeEvent(&Adapter->RxDrainEvent, NotificationEvent, TRUE);
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
            Rb->Next = Adapter->RxBufFree;
            Adapter->RxBufFree = Rb;
        }
        if (RbIdx == 0)
        {
            Status = NDIS_STATUS_RESOURCES;
            goto Fail;
        }
    }

    NdisMGetDeviceProperty(NdisMiniportHandle,
                           &Adapter->Pdo,
                           NULL,
                           NULL,
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

    NtStatus = CywStartBusThread(Adapter);
    if (!NT_SUCCESS(NtStatus))
    {
        Status = NDIS_STATUS_FAILURE;
        goto Fail;
    }

    NtStatus = CywEnableCardInterrupt(Adapter);
    if (!NT_SUCCESS(NtStatus))
    {
        Status = NDIS_STATUS_HARD_ERRORS;
        goto Fail;
    }

    return NDIS_STATUS_SUCCESS;

Fail:
    CywStopBusThread(Adapter);
    CywFreeAdapter(Adapter);
    return Status;
}

VOID
NTAPI
CywMiniportHaltEx(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ NDIS_HALT_ACTION HaltAction)
{
    PCYW_ADAPTER Adapter = (PCYW_ADAPTER)MiniportAdapterContext;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(HaltAction);

    {
        KIRQL OldIrql;

        KeAcquireSpinLock(&Adapter->TxLock, &OldIrql);
        InterlockedExchange(&Adapter->Paused, 1);
        InterlockedExchange(&Adapter->Halting, 1);
        KeReleaseSpinLock(&Adapter->TxLock, OldIrql);
    }

    CywAbortPendingOids(Adapter, NDIS_STATUS_NOT_ACCEPTED);
    CywStopBusThread(Adapter);
    CywDrainTxQueue(Adapter);

    Status = CywDisableCardInterrupt(Adapter);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("CYW: failed to unregister the SDIO interrupt callback (0x%08lx)\n",
                Status);
    }

    KeFlushQueuedDpcs();

    /* Halting blocks new references; wait for every queued callback before
     * freeing its work-item handle and adapter context. */
    KeWaitForSingleObject(&Adapter->WorkItemsDrainedEvent,
                          Executive,
                          KernelMode,
                          FALSE,
                          NULL);

    KeWaitForSingleObject(&Adapter->TxDrainEvent, Executive, KernelMode, FALSE, NULL);
    KeWaitForSingleObject(&Adapter->RxDrainEvent, Executive, KernelMode, FALSE, NULL);

    CywFreeAdapter(Adapter);
}

NDIS_STATUS
NTAPI
CywMiniportPauseEx(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNDIS_MINIPORT_PAUSE_PARAMETERS MiniportPauseParameters)
{
    PCYW_ADAPTER Adapter = (PCYW_ADAPTER)MiniportAdapterContext;
    KIRQL OldIrql;

    UNREFERENCED_PARAMETER(MiniportPauseParameters);

    KeAcquireSpinLock(&Adapter->TxLock, &OldIrql);
    InterlockedExchange(&Adapter->Paused, 1);
    KeReleaseSpinLock(&Adapter->TxLock, OldIrql);

    KeSetEvent(&Adapter->BusEvent, IO_NETWORK_INCREMENT, FALSE);
    CywDrainTxQueue(Adapter);
    KeWaitForSingleObject(&Adapter->TxDrainEvent, Executive, KernelMode, FALSE, NULL);
    KeWaitForSingleObject(&Adapter->RxDrainEvent, Executive, KernelMode, FALSE, NULL);
    return NDIS_STATUS_SUCCESS;
}

NDIS_STATUS
NTAPI
CywMiniportRestartEx(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNDIS_MINIPORT_RESTART_PARAMETERS MiniportRestartParameters)
{
    PCYW_ADAPTER Adapter = (PCYW_ADAPTER)MiniportAdapterContext;

    UNREFERENCED_PARAMETER(MiniportRestartParameters);

    if (InterlockedCompareExchange(&Adapter->Halting, 0, 0) != 0)
    {
        return NDIS_STATUS_FAILURE;
    }

    InterlockedExchange(&Adapter->Paused, 0);
    KeSetEvent(&Adapter->BusEvent, IO_NETWORK_INCREMENT, FALSE);
    return NDIS_STATUS_SUCCESS;
}

VOID
NTAPI
CywMiniportShutdownEx(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ NDIS_SHUTDOWN_ACTION ShutdownAction)
{
    PCYW_ADAPTER Adapter = (PCYW_ADAPTER)MiniportAdapterContext;

    if (ShutdownAction == NdisShutdownBugCheck)
    {
        return;
    }

    InterlockedExchange(&Adapter->Paused, 1);
    InterlockedExchange(&Adapter->Halting, 1);
    CywStopBusThread(Adapter);
    (VOID)CywDisableCardInterrupt(Adapter);
}

VOID
NTAPI
CywMiniportDevicePnpEventNotify(
    _In_ NDIS_HANDLE MiniportAdapterContext,
    _In_ PNET_DEVICE_PNP_EVENT NetDevicePnPEvent)
{
    PCYW_ADAPTER Adapter = (PCYW_ADAPTER)MiniportAdapterContext;

    if (NetDevicePnPEvent->DevicePnPEvent == NdisDevicePnPEventSurpriseRemoved ||
        NetDevicePnPEvent->DevicePnPEvent == NdisDevicePnPEventRemoved)
    {
        KIRQL OldIrql;

        KeAcquireSpinLock(&Adapter->TxLock, &OldIrql);
        InterlockedExchange(&Adapter->Paused, 1);
        InterlockedExchange(&Adapter->Halting, 1);
        KeReleaseSpinLock(&Adapter->TxLock, OldIrql);

        CywAbortPendingOids(Adapter, NDIS_STATUS_NOT_ACCEPTED);
        CywStopBusThread(Adapter);
        CywDrainTxQueue(Adapter);
        KeWaitForSingleObject(&Adapter->TxDrainEvent, Executive, KernelMode, FALSE, NULL);
    }
}

typedef struct _CYW_TX_WORK
{
    LIST_ENTRY Link;
    PNET_BUFFER_LIST Nbl;
    ULONG DataOffset;
    ULONG DataLength;
    UCHAR Data[1];
} CYW_TX_WORK, *PCYW_TX_WORK;

static
NDIS_STATUS
CywBlockedSendStatus(
    _In_ PCYW_ADAPTER Adapter)
{
    if (InterlockedCompareExchange(&Adapter->Halting, 0, 0) != 0)
    {
        return NDIS_STATUS_NOT_ACCEPTED;
    }
    if (InterlockedCompareExchange(&Adapter->Paused, 0, 0) != 0)
    {
        return NDIS_STATUS_PAUSED;
    }
    return NDIS_STATUS_SUCCESS;
}

static
ULONG
CywSendCompleteFlags(VOID)
{
    return (KeGetCurrentIrql() == DISPATCH_LEVEL) ?
           NDIS_SEND_COMPLETE_FLAGS_DISPATCH_LEVEL : 0;
}

static
VOID
CywCompleteTrackedSend(
    _In_ PCYW_ADAPTER Adapter,
    _In_ PNET_BUFFER_LIST Nbl,
    _In_ NDIS_STATUS Status,
    _In_ ULONG CompleteFlags)
{
    LONG Remaining;
    KIRQL OldIrql;

    NET_BUFFER_LIST_STATUS(Nbl) = Status;
    NET_BUFFER_LIST_NEXT_NBL(Nbl) = NULL;
    NdisMSendNetBufferListsComplete(Adapter->MiniportAdapterHandle, Nbl, CompleteFlags);

    KeAcquireSpinLock(&Adapter->TxLock, &OldIrql);
    Remaining = --Adapter->OutstandingTxNbls;
    ASSERT(Remaining >= 0);
    if (Remaining == 0)
    {
        KeSetEvent(&Adapter->TxDrainEvent, IO_NO_INCREMENT, FALSE);
    }
    KeReleaseSpinLock(&Adapter->TxLock, OldIrql);
}

static
BOOLEAN
CywReferenceTxNbl(
    _In_ PCYW_ADAPTER Adapter)
{
    KIRQL OldIrql;
    BOOLEAN Referenced = FALSE;

    KeAcquireSpinLock(&Adapter->TxLock, &OldIrql);
    if (Adapter->Paused == 0 && Adapter->Halting == 0 &&
        Adapter->OutstandingTxNbls < MAXLONG)
    {
        if (Adapter->OutstandingTxNbls++ == 0)
        {
            KeResetEvent(&Adapter->TxDrainEvent);
        }
        Referenced = TRUE;
    }
    KeReleaseSpinLock(&Adapter->TxLock, OldIrql);
    return Referenced;
}

/* Convert the 802.11+SNAP frame in the NET_BUFFER to an 802.3 frame built at
 * Dest. Contiguous frames are converted from the NBL data directly; fragmented
 * ones are gathered into Dest first and converted in place, so no separate
 * scratch buffer is needed. Dest must hold the full 802.11 frame length. */
ULONG
CywBuildEthFromNbl(
    _In_ PNET_BUFFER Nb,
    _Out_writes_(Capacity) PUCHAR Dest,
    _In_ ULONG Capacity)
{
    ULONG FrameLen = NET_BUFFER_DATA_LENGTH(Nb);
    PUCHAR Frame;
    PCYW_DOT11_HEADER Dot11;
    PCYW_SNAP_HEADER Snap;
    UCHAR EthHeader[sizeof(CYW_ETHER_HEADER)];
    ULONG PayloadLen;

    if (FrameLen < sizeof(CYW_DOT11_HEADER) + sizeof(CYW_SNAP_HEADER) ||
        FrameLen > Capacity)
    {
        return 0;
    }

    Frame = NdisGetDataBuffer(Nb, FrameLen, Dest, 1, 0);
    if (Frame == NULL)
    {
        return 0;
    }

    Dot11 = (PCYW_DOT11_HEADER)Frame;
    Snap = (PCYW_SNAP_HEADER)(Frame + sizeof(CYW_DOT11_HEADER));
    PayloadLen = FrameLen - sizeof(CYW_DOT11_HEADER) - sizeof(CYW_SNAP_HEADER);

    if (Dot11->FrameControl[0] != CYW_FC0_TYPE_DATA ||
        (Dot11->FrameControl[1] & CYW_FC1_DIR_MASK) != CYW_FC1_TODS ||
        (Dot11->FrameControl[1] & CYW_FC1_PROTECTED) != 0 ||
        Snap->Dsap != CYW_SNAP_DSAP || Snap->Ssap != CYW_SNAP_SSAP ||
        Snap->Control != CYW_SNAP_CONTROL || Snap->Oui[0] != 0 ||
        Snap->Oui[1] != 0 ||
        (Snap->Oui[2] != 0 && Snap->Oui[2] != CYW_SNAP_OUI_BRIDGE_TUNNEL) ||
        PayloadLen > CYW_MTU_SIZE)
    {
        return 0;
    }

    RtlCopyMemory(EthHeader, Dot11->Address3, CYW_ADDRESS_LENGTH);
    RtlCopyMemory(EthHeader + CYW_ADDRESS_LENGTH, Dot11->Address2, CYW_ADDRESS_LENGTH);
    EthHeader[12] = Snap->EtherType[0];
    EthHeader[13] = Snap->EtherType[1];

    RtlMoveMemory(Dest + sizeof(CYW_ETHER_HEADER),
                  Frame + sizeof(CYW_DOT11_HEADER) + sizeof(CYW_SNAP_HEADER),
                  PayloadLen);
    RtlCopyMemory(Dest, EthHeader, sizeof(EthHeader));
    return sizeof(CYW_ETHER_HEADER) + PayloadLen;
}

BOOLEAN
CywTxAllowed(
    _In_ PCYW_ADAPTER Adapter,
    _In_reads_(EthLen) PUCHAR Eth,
    _In_ ULONG EthLen)
{
    if (Adapter->Associated)
    {
        return TRUE;
    }
    /* Only EAPOL handshake frames may pass before association completes */
    return EthLen >= sizeof(CYW_ETHER_HEADER) &&
           (USHORT)((Eth[12] << 8) | Eth[13]) == ETH_P_EAPOL;
}

VOID
CywDrainTxQueue(
    _In_ PCYW_ADAPTER Adapter)
{
    for (;;)
    {
        PLIST_ENTRY Entry;
        PCYW_TX_WORK Work;
        NDIS_STATUS CompletionStatus;
        BOOLEAN Requeued = FALSE;
        KIRQL OldIrql;

        KeAcquireSpinLock(&Adapter->TxLock, &OldIrql);
        Entry = IsListEmpty(&Adapter->TxQueue) ? NULL : RemoveHeadList(&Adapter->TxQueue);
        KeReleaseSpinLock(&Adapter->TxLock, OldIrql);

        if (Entry == NULL)
        {
            break;
        }

        Work = CONTAINING_RECORD(Entry, CYW_TX_WORK, Link);
        CompletionStatus = CywBlockedSendStatus(Adapter);
        while (CompletionStatus == NDIS_STATUS_SUCCESS &&
               Work->DataOffset < Work->DataLength)
        {
            ULONG Remaining = Work->DataLength - Work->DataOffset;
            PUCHAR Record = Work->Data + Work->DataOffset;
            ULONG EthLen;
            ULONG RecordLength;
            NTSTATUS SendStatus;

            if (Remaining < sizeof(ULONG))
            {
                CompletionStatus = NDIS_STATUS_FAILURE;
                break;
            }
            RtlCopyMemory(&EthLen, Record, sizeof(EthLen));
            if (EthLen == 0 || EthLen > Remaining - sizeof(ULONG))
            {
                CompletionStatus = NDIS_STATUS_FAILURE;
                break;
            }
            RecordLength = ALIGN_UP(sizeof(ULONG) + EthLen, ULONG);
            if (RecordLength > Remaining)
            {
                CompletionStatus = NDIS_STATUS_FAILURE;
                break;
            }

            SendStatus = CywSdpcmSendData(Adapter, Record + sizeof(ULONG), EthLen);
            if (SendStatus == STATUS_DEVICE_BUSY)
            {
                KeAcquireSpinLock(&Adapter->TxLock, &OldIrql);
                CompletionStatus = CywBlockedSendStatus(Adapter);
                if (CompletionStatus == NDIS_STATUS_SUCCESS)
                {
                    InsertHeadList(&Adapter->TxQueue, &Work->Link);
                    Requeued = TRUE;
                }
                KeReleaseSpinLock(&Adapter->TxLock, OldIrql);
                break;
            }
            if (!NT_SUCCESS(SendStatus))
            {
                CompletionStatus = NDIS_STATUS_FAILURE;
                break;
            }

            Work->DataOffset += RecordLength;
            CompletionStatus = CywBlockedSendStatus(Adapter);
        }

        if (Requeued)
        {
            break;
        }
        if (Work->DataOffset == Work->DataLength)
        {
            CompletionStatus = NDIS_STATUS_SUCCESS;
        }
        CywCompleteTrackedSend(Adapter, Work->Nbl, CompletionStatus, CywSendCompleteFlags());
        CywFree(Work);
    }
}

/* Serialize every NET_BUFFER in an NBL before handing it to the single F2
 * owner. An NBL is completed only after all of its frames have been sent. */
static
NDIS_STATUS
CywQueueTxWork(
    _In_ PCYW_ADAPTER Adapter,
    _In_ PNET_BUFFER_LIST Nbl)
{
    PNET_BUFFER Nb;
    PCYW_TX_WORK Work;
    ULONG AllocationLength = 0;
    ULONG DataLength = 0;
    KIRQL OldIrql;

    for (Nb = NET_BUFFER_LIST_FIRST_NB(Nbl); Nb != NULL; Nb = NET_BUFFER_NEXT_NB(Nb))
    {
        ULONG FrameLen = NET_BUFFER_DATA_LENGTH(Nb);
        ULONG RecordLength;

        if (FrameLen > CYW_MAX_FRAME_SIZE)
        {
            return NDIS_STATUS_INVALID_DATA;
        }
        RecordLength = ALIGN_UP(sizeof(ULONG) + FrameLen, ULONG);
        if (AllocationLength > MAXULONG - RecordLength)
        {
            return NDIS_STATUS_INVALID_LENGTH;
        }
        AllocationLength += RecordLength;
    }
    if (AllocationLength == 0 ||
        AllocationLength > MAXULONG - FIELD_OFFSET(CYW_TX_WORK, Data))
    {
        return NDIS_STATUS_INVALID_DATA;
    }

    Work = CywAllocate(FIELD_OFFSET(CYW_TX_WORK, Data) + AllocationLength);
    if (Work == NULL)
    {
        return NDIS_STATUS_RESOURCES;
    }

    for (Nb = NET_BUFFER_LIST_FIRST_NB(Nbl); Nb != NULL; Nb = NET_BUFFER_NEXT_NB(Nb))
    {
        ULONG FrameLen = NET_BUFFER_DATA_LENGTH(Nb);
        PUCHAR Record = Work->Data + DataLength;
        PUCHAR Eth = Record + sizeof(ULONG);
        ULONG EthLen = CywBuildEthFromNbl(Nb, Eth, FrameLen);

        if (EthLen == 0 || !CywTxAllowed(Adapter, Eth, EthLen))
        {
            CywFree(Work);
            return NDIS_STATUS_INVALID_DATA;
        }
        RtlCopyMemory(Record, &EthLen, sizeof(EthLen));
        DataLength += ALIGN_UP(sizeof(ULONG) + EthLen, ULONG);
    }

    Work->Nbl = Nbl;
    Work->DataOffset = 0;
    Work->DataLength = DataLength;

    KeAcquireSpinLock(&Adapter->TxLock, &OldIrql);
    if (Adapter->Paused != 0 || Adapter->Halting != 0)
    {
        NDIS_STATUS Status = CywBlockedSendStatus(Adapter);
        KeReleaseSpinLock(&Adapter->TxLock, OldIrql);
        CywFree(Work);
        return Status;
    }
    InsertTailList(&Adapter->TxQueue, &Work->Link);
    KeReleaseSpinLock(&Adapter->TxLock, OldIrql);
    KeSetEvent(&Adapter->BusEvent, IO_NETWORK_INCREMENT, FALSE);
    return NDIS_STATUS_PENDING;
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
        NDIS_STATUS NblStatus;
        BOOLEAN Deferred = FALSE;
        BOOLEAN Referenced;

        NET_BUFFER_LIST_NEXT_NBL(Nbl) = NULL;

        Referenced = CywReferenceTxNbl(Adapter);
        if (!Referenced)
        {
            NblStatus = CywBlockedSendStatus(Adapter);
        }
        else if (Nb != NULL)
        {
            NTSTATUS SendStatus = STATUS_DEVICE_BUSY;

            if (NET_BUFFER_NEXT_NB(Nb) == NULL && KeGetCurrentIrql() == PASSIVE_LEVEL)
            {
                SendStatus = CywSdpcmSendNb(Adapter, Nb);
            }

            if (NT_SUCCESS(SendStatus))
            {
                NblStatus = NDIS_STATUS_SUCCESS;
            }
            else if (SendStatus == STATUS_DEVICE_BUSY)
            {
                NblStatus = CywQueueTxWork(Adapter, Nbl);
                Deferred = (NblStatus == NDIS_STATUS_PENDING);
            }
            else
            {
                NblStatus = NDIS_STATUS_FAILURE;
            }
        }
        else
        {
            NblStatus = NDIS_STATUS_INVALID_DATA;
        }

        if (!Deferred)
        {
            if (Referenced)
            {
                CywCompleteTrackedSend(Adapter, Nbl, NblStatus, CompleteFlags);
            }
            else
            {
                NET_BUFFER_LIST_STATUS(Nbl) = NblStatus;
                NdisMSendNetBufferListsComplete(Adapter->MiniportAdapterHandle, Nbl, CompleteFlags);
            }
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
            CywReleaseRxBuffer(Adapter, Rb);
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
    PCYW_ADAPTER Adapter = (PCYW_ADAPTER)MiniportAdapterContext;
    LIST_ENTRY Cancelled;
    PLIST_ENTRY Entry;
    KIRQL OldIrql;

    InitializeListHead(&Cancelled);
    KeAcquireSpinLock(&Adapter->TxLock, &OldIrql);
    Entry = Adapter->TxQueue.Flink;
    while (Entry != &Adapter->TxQueue)
    {
        PLIST_ENTRY Next = Entry->Flink;
        PCYW_TX_WORK Work = CONTAINING_RECORD(Entry, CYW_TX_WORK, Link);

        if (NDIS_GET_NET_BUFFER_LIST_CANCEL_ID(Work->Nbl) == CancelId)
        {
            RemoveEntryList(Entry);
            InsertTailList(&Cancelled, Entry);
        }
        Entry = Next;
    }
    KeReleaseSpinLock(&Adapter->TxLock, OldIrql);

    while (!IsListEmpty(&Cancelled))
    {
        PCYW_TX_WORK Work = CONTAINING_RECORD(RemoveHeadList(&Cancelled), CYW_TX_WORK, Link);
        CywCompleteTrackedSend(Adapter, Work->Nbl, NDIS_STATUS_SEND_ABORTED, CywSendCompleteFlags());
        CywFree(Work);
    }
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
