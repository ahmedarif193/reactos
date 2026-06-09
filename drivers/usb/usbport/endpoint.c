/*
 * PROJECT:     ReactOS USB Port Driver
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     USBPort endpoint functions
 * COPYRIGHT:   Copyright 2017 Vadim Galyant <vgal@rambler.ru>
 */

#include "usbport.h"

#define NDEBUG
#include <debug.h>

#define NDEBUG_USBPORT_CORE
#include "usbdebug.h"

#define USBPORT_BULK_MAX_TRANSFER_SIZE (4 * 1024 * 1024)

static USBD_STATUS
USBPORT_MpStatusToUsbdStatus(
    _In_ MPSTATUS MpStatus)
{
    switch (MpStatus)
    {
        case MP_STATUS_SUCCESS:
            return USBD_STATUS_SUCCESS;

        case MP_STATUS_NO_RESOURCES:
            return USBD_STATUS_INSUFFICIENT_RESOURCES;

        case MP_STATUS_NO_BANDWIDTH:
            return USBD_STATUS_NO_BANDWIDTH;

        case MP_STATUS_NOT_SUPPORTED:
            return USBD_STATUS_NOT_SUPPORTED;

        case MP_STATUS_HW_ERROR:
        case MP_STATUS_ERROR:
        case MP_STATUS_FAILURE:
        case MP_STATUS_RESERVED1:
        case MP_STATUS_UNSUCCESSFUL:
        default:
            return USBD_STATUS_REQUEST_FAILED;
    }
}

static
ULONG
USBPORT_EncodeEndpointLpmPolicy(
    _In_opt_ PUSBPORT_DEVICE_HANDLE DeviceHandle)
{
    ULONG Policy = 0;

    if (!DeviceHandle)
        return 0;

    if (DeviceHandle->DeviceSpeed != UsbSuperSpeed ||
        !DeviceHandle->LpmPolicyComputed)
    {
        return 0;
    }

    Policy |= USBPORT_EP_LPM_VALID;
    if (DeviceHandle->LpmAllowU1)
        Policy |= USBPORT_EP_LPM_ALLOW_U1;
    if (DeviceHandle->LpmAllowU2)
        Policy |= USBPORT_EP_LPM_ALLOW_U2;

    Policy |= ((ULONG)DeviceHandle->SsU1ExitLatency & 0xFFu) << USBPORT_EP_LPM_U1_SHIFT;
    Policy |= ((ULONG)DeviceHandle->SsU2ExitLatency & 0xFFFFu) << USBPORT_EP_LPM_U2_SHIFT;

    return Policy;
}

static
VOID
USBPORT_ApplyEndpointLpmPolicy(
    _Inout_ PUSBPORT_ENDPOINT_PROPERTIES EndpointProperties,
    _In_opt_ PUSBPORT_DEVICE_HANDLE DeviceHandle)
{
    if (!EndpointProperties)
        return;

    EndpointProperties->Reserved3 = USBPORT_EncodeEndpointLpmPolicy(DeviceHandle);
}

ULONG
NTAPI
USBPORT_CalculateUsbBandwidth(IN PDEVICE_OBJECT FdoDevice,
                              IN PUSBPORT_ENDPOINT Endpoint)
{
    PUSBPORT_ENDPOINT_PROPERTIES EndpointProperties;
    ULONG Bandwidth;
    ULONG Overhead;

    DPRINT("USBPORT_CalculateUsbBandwidth ... \n");

    EndpointProperties = &Endpoint->EndpointProperties;

    switch (EndpointProperties->TransferType)
    {
        case USBPORT_TRANSFER_TYPE_ISOCHRONOUS:
            if (EndpointProperties->DeviceSpeed == UsbHighSpeed)
            {
                Overhead = (EndpointProperties->Direction == USBPORT_TRANSFER_DIRECTION_OUT) ?
                           USB2_HS_ISOCHRONOUS_OUT_OVERHEAD :
                           USB2_HS_ISOCHRONOUS_IN_OVERHEAD;
            }
            else
            {
                Overhead = USB2_FS_ISOCHRONOUS_OVERHEAD;
            }
            break;

        case USBPORT_TRANSFER_TYPE_INTERRUPT:
            if (EndpointProperties->DeviceSpeed == UsbHighSpeed)
            {
                Overhead = (EndpointProperties->Direction == USBPORT_TRANSFER_DIRECTION_OUT) ?
                           USB2_HS_INTERRUPT_OUT_OVERHEAD :
                           USB2_HS_INTERRUPT_IN_OVERHEAD;
            }
            else
            {
                Overhead = USB2_FS_INTERRUPT_OVERHEAD;
            }
            break;

        default: //USBPORT_TRANSFER_TYPE_CONTROL or USBPORT_TRANSFER_TYPE_BULK
            Overhead = 0;
            break;
    }

    if (Overhead == 0)
    {
        Bandwidth = 0;
    }
    else
    {
        Bandwidth = (EndpointProperties->TotalMaxPacketSize + Overhead) * USB2_BIT_STUFFING_OVERHEAD;
    }

    if (EndpointProperties->DeviceSpeed == UsbLowSpeed)
    {
        Bandwidth *= 8;
    }

    return Bandwidth;
}

BOOLEAN
NTAPI
USBPORT_AllocateBandwidth(IN PDEVICE_OBJECT FdoDevice,
                          IN PUSBPORT_ENDPOINT Endpoint)
{
    PUSBPORT_DEVICE_EXTENSION FdoExtension;
    PUSBPORT_ENDPOINT_PROPERTIES EndpointProperties;
    ULONG TransferType;
    ULONG TotalBusBandwidth;
    ULONG EndpointBandwidth;
    ULONG MinBandwidth;
    ULONG MaxBandwidth = 0;
    ULONG ix;
    ULONG Offset;
    LONG ScheduleOffset = -1;
    ULONG Period;
    ULONG SlotsPerPeriod;
    UCHAR Bit;

    DPRINT("USBPORT_AllocateBandwidth: FdoDevice - %p, Endpoint - %p\n",
           FdoDevice,
           Endpoint);

    FdoExtension = FdoDevice->DeviceExtension;
    EndpointProperties = &Endpoint->EndpointProperties;
    TransferType = EndpointProperties->TransferType;

    /*
     * For SuperSpeed devices the legacy USB 1.1/2.0 frame-based
     * bandwidth accounting model is not applicable. Treat SS
     * endpoints as "unlimited" from the USBPORT scheduler point
     * of view and do not consume from the periodic bandwidth
     * pool. The xHCI miniport is responsible for enforcing any
     * hardware limits.
     */
    if (EndpointProperties->DeviceSpeed == UsbSuperSpeed)
    {
        EndpointProperties->ScheduleOffset = 0;
        return TRUE;
    }

    if (TransferType == USBPORT_TRANSFER_TYPE_BULK ||
        TransferType == USBPORT_TRANSFER_TYPE_CONTROL ||
        Endpoint->Flags & ENDPOINT_FLAG_ROOTHUB_EP0)
    {
        EndpointProperties->ScheduleOffset = 0;
        return TRUE;
    }

    TotalBusBandwidth = FdoExtension->TotalBusBandwidth;
    EndpointBandwidth = EndpointProperties->UsbBandwidth;

    Period = EndpointProperties->Period;
    ASSERT(Period != 0);

    SlotsPerPeriod = USB2_FRAMES / Period;

    for (Offset = 0; Offset < Period; Offset++)
    {
        MinBandwidth = TotalBusBandwidth;

        for (ix = 0; ix < SlotsPerPeriod; ix++)
        {
            ULONG Index = Offset + (ix * Period);
            ULONG Available = FdoExtension->Bandwidth[Index];

            if (Available < EndpointBandwidth)
                break;

            MinBandwidth = min(MinBandwidth, Available);
        }

        if (ix == SlotsPerPeriod && MinBandwidth > MaxBandwidth)
        {
            MaxBandwidth = MinBandwidth;
            ScheduleOffset = Offset;

            DPRINT("USBPORT_AllocateBandwidth: ScheduleOffset - %X\n",
                   ScheduleOffset);
        }
    }

    DPRINT("USBPORT_AllocateBandwidth: ScheduleOffset - %X\n", ScheduleOffset);

    if (ScheduleOffset != -1)
    {
        EndpointProperties->ScheduleOffset = ScheduleOffset;

        for (ix = 0; ix < SlotsPerPeriod; ix++)
        {
            ULONG Index = ScheduleOffset + (ix * Period);
            ASSERT(FdoExtension->Bandwidth[Index] >= EndpointBandwidth);
            FdoExtension->Bandwidth[Index] -= EndpointBandwidth;
        }

        if (TransferType == USBPORT_TRANSFER_TYPE_INTERRUPT)
        {
            for (Bit = 0x80; Bit != 0; Bit >>= 1)
            {
                if ((Period & Bit) != 0)
                {
                    Period = Bit;
                    break;
                }
            }

        }
    }

    DPRINT("USBPORT_AllocateBandwidth: ScheduleOffset - %X\n", ScheduleOffset);
    return ScheduleOffset != -1;
}

VOID
NTAPI
USBPORT_FreeBandwidth(IN PDEVICE_OBJECT FdoDevice,
                      IN PUSBPORT_ENDPOINT Endpoint)
{
    PUSBPORT_DEVICE_EXTENSION FdoExtension;
    PUSBPORT_ENDPOINT_PROPERTIES EndpointProperties;
    ULONG TransferType;
    ULONG Offset;
    ULONG EndpointBandwidth;
    ULONG Period;
    ULONG Factor;
    ULONG ix;
    UCHAR Bit;

    DPRINT("USBPORT_FreeBandwidth: FdoDevice - %p, Endpoint - %p\n",
           FdoDevice,
           Endpoint);

    FdoExtension = FdoDevice->DeviceExtension;

    EndpointProperties = &Endpoint->EndpointProperties;
    TransferType = EndpointProperties->TransferType;

    if (TransferType == USBPORT_TRANSFER_TYPE_BULK ||
        TransferType == USBPORT_TRANSFER_TYPE_CONTROL ||
        EndpointProperties->DeviceSpeed == UsbSuperSpeed ||
        (Endpoint->Flags & ENDPOINT_FLAG_ROOTHUB_EP0))
    {
        return;
    }

    Offset = Endpoint->EndpointProperties.ScheduleOffset;
    EndpointBandwidth = Endpoint->EndpointProperties.UsbBandwidth;

    Period = Endpoint->EndpointProperties.Period;
    ASSERT(Period != 0);

    Factor = USB2_FRAMES / Period;

    for (ix = 0; ix < Factor; ix++)
    {
        ULONG Index = Offset + (ix * Period);

        ASSERT(Index < RTL_NUMBER_OF(FdoExtension->Bandwidth));
        FdoExtension->Bandwidth[Index] += EndpointBandwidth;
    }

    if (TransferType == USBPORT_TRANSFER_TYPE_INTERRUPT)
    {
        for (Bit = 0x80; Bit != 0; Bit >>= 1)
        {
            if ((Period & Bit) != 0)
            {
                Period = Bit;
                break;
            }
        }

        ASSERT(Period != 0);

    }
}

UCHAR
NTAPI
USBPORT_NormalizeHsInterval(UCHAR Interval)
{
    UCHAR interval;

    DPRINT("USBPORT_NormalizeHsInterval: Interval - %x\n", Interval);

    interval = Interval;

    if (Interval)
       interval = Interval - 1;

    if (interval > 5)
       interval = 5;

    return 1 << interval;
}

BOOLEAN
NTAPI
USBPORT_EndpointHasQueuedTransfers(IN PDEVICE_OBJECT FdoDevice,
                                   IN PUSBPORT_ENDPOINT Endpoint,
                                   IN PULONG TransferCount)
{
    PLIST_ENTRY Entry;
    PUSBPORT_TRANSFER Transfer;
    BOOLEAN Result = FALSE;

    DPRINT_CORE("USBPORT_EndpointHasQueuedTransfers: ... \n");

    KeAcquireSpinLock(&Endpoint->EndpointSpinLock, &Endpoint->EndpointOldIrql);

    if (!IsListEmpty(&Endpoint->PendingTransferList))
        Result = TRUE;

    if (!IsListEmpty(&Endpoint->TransferList))
    {
        /*
         * Iterate TransferList to count active (non-completed) transfers.
         * Completed transfers (TRANSFER_FLAG_COMPLETED) are still on the
         * TransferList waiting for FlushDoneTransfers to remove them, so
         * they must not count as "queued" -- otherwise AbortTransfers
         * would spin waiting for them even though they are already done.
         */
        if (TransferCount)
            *TransferCount = 0;

        for (Entry = Endpoint->TransferList.Flink;
             Entry && Entry != &Endpoint->TransferList;
             Entry = Transfer->TransferLink.Flink)
        {
            Transfer = CONTAINING_RECORD(Entry,
                                         USBPORT_TRANSFER,
                                         TransferLink);

            if (Transfer->Flags & TRANSFER_FLAG_COMPLETED)
                continue;

            /* At least one non-completed transfer exists */
            Result = TRUE;

            if (TransferCount && (Transfer->Flags & TRANSFER_FLAG_SUBMITED))
            {
                ++*TransferCount;
            }
        }
    }

    KeReleaseSpinLock(&Endpoint->EndpointSpinLock, Endpoint->EndpointOldIrql);

    return Result;
}

VOID
NTAPI
USBPORT_NukeAllEndpoints(IN PDEVICE_OBJECT FdoDevice)
{
    PUSBPORT_DEVICE_EXTENSION  FdoExtension;
    PLIST_ENTRY EndpointList;
    PUSBPORT_ENDPOINT Endpoint;
    KIRQL OldIrql;

    DPRINT("USBPORT_NukeAllEndpoints \n");

    FdoExtension = FdoDevice->DeviceExtension;

    KeAcquireSpinLock(&FdoExtension->EndpointListSpinLock, &OldIrql);

    EndpointList = FdoExtension->EndpointList.Flink;

    while (EndpointList && (EndpointList != &FdoExtension->EndpointList))
    {
        Endpoint = CONTAINING_RECORD(EndpointList,
                                     USBPORT_ENDPOINT,
                                     EndpointLink);

        if (!(Endpoint->Flags & ENDPOINT_FLAG_ROOTHUB_EP0))
            Endpoint->Flags |= ENDPOINT_FLAG_NUKE;

        EndpointList = Endpoint->EndpointLink.Flink;
    }

    KeReleaseSpinLock(&FdoExtension->EndpointListSpinLock, OldIrql);
}

ULONG
NTAPI
USBPORT_GetEndpointState(IN PUSBPORT_ENDPOINT Endpoint)
{
    ULONG State;

    //DPRINT("USBPORT_GetEndpointState \n");

    KeAcquireSpinLockAtDpcLevel(&Endpoint->StateChangeSpinLock);

    if (Endpoint->StateLast != Endpoint->StateNext)
    {
        State = USBPORT_ENDPOINT_UNKNOWN;
    }
    else
    {
        State = Endpoint->StateLast;
    }

    KeReleaseSpinLockFromDpcLevel(&Endpoint->StateChangeSpinLock);

    if (State != USBPORT_ENDPOINT_ACTIVE)
    {
        DPRINT("USBPORT_GetEndpointState: Endpoint - %p, State - %x\n",
               Endpoint,
               State);
    }

    return State;
}

/* Drop a queued StateChangeLink so it cannot dangle once the endpoint is freed */
static
VOID
USBPORT_PurgeEndpointStateChange(IN PUSBPORT_ENDPOINT Endpoint)
{
    PUSBPORT_DEVICE_EXTENSION FdoExtension;
    PLIST_ENTRY Entry;
    KIRQL OldIrql;

    FdoExtension = Endpoint->FdoDevice->DeviceExtension;

    KeAcquireSpinLock(&FdoExtension->EpStateChangeSpinLock, &OldIrql);

    for (Entry = FdoExtension->EpStateChangeList.Flink; Entry != &FdoExtension->EpStateChangeList; Entry = Entry->Flink)
    {
        if (Entry == &Endpoint->StateChangeLink)
        {
            RemoveEntryList(Entry);
            break;
        }
    }

    KeReleaseSpinLock(&FdoExtension->EpStateChangeSpinLock, OldIrql);
}

VOID
NTAPI
USBPORT_SetEndpointState(IN PUSBPORT_ENDPOINT Endpoint,
                         IN ULONG State)
{
    PDEVICE_OBJECT FdoDevice;
    PUSBPORT_DEVICE_EXTENSION FdoExtension;
    PUSBPORT_REGISTRATION_PACKET Packet;
    KIRQL OldIrql;

    DPRINT("USBPORT_SetEndpointState: Endpoint - %p, State - %x\n",
           Endpoint,
           State);

    FdoDevice = Endpoint->FdoDevice;
    FdoExtension = FdoDevice->DeviceExtension;
    Packet = &FdoExtension->MiniPortInterface->Packet;

    KeAcquireSpinLock(&Endpoint->StateChangeSpinLock,
                      &Endpoint->EndpointStateOldIrql);

    if (!(Endpoint->Flags & ENDPOINT_FLAG_ROOTHUB_EP0))
    {
        if (Endpoint->Flags & ENDPOINT_FLAG_NUKE)
        {
            Endpoint->StateLast = State;
            Endpoint->StateNext = State;

            KeReleaseSpinLock(&Endpoint->StateChangeSpinLock,
                              Endpoint->EndpointStateOldIrql);

            USBPORT_PurgeEndpointStateChange(Endpoint);

            USBPORT_InvalidateEndpointHandler(FdoDevice,
                                              Endpoint,
                                              INVALIDATE_ENDPOINT_WORKER_THREAD);
            return;
        }

        /*
         * When ABORTING is set and we are transitioning to REMOVE,
         * use the same fast path as NUKE: set both StateLast and
         * StateNext atomically under the StateChangeSpinLock and
         * signal the worker.
         *
         * The normal path (below) releases StateChangeSpinLock, calls
         * the miniport SetEndpointState, then sets StateNext without
         * holding any endpoint lock. During disconnect cleanup, the
         * endpoint worker DPC may be concurrently forcing state
         * transitions (NUKE/ABORTING path in USBPORT_EndpointWorker).
         * This creates a race window where:
         *
         * 1. The endpoint worker sees StateLast != StateNext and
         *    forces StateLast = StateNext (e.g., PAUSED)
         * 2. DmaEndpointWorker runs and calls SetEndpointState(ACTIVE)
         *    which goes through the non-NUKE path, setting
         *    StateNext = ACTIVE without holding StateChangeSpinLock
         * 3. ClosePipe calls SetEndpointState(REMOVE) which also
         *    goes through the non-NUKE path
         * 4. The miniport SetEndpointState(REMOVE) calls
         *    XHCI_DropSlotEndpoint on an already-disabled slot,
         *    getting an error
         * 5. The endpoint state machine becomes corrupted, leading
         *    to a spinlock being released that was never acquired
         *    (BSOD 0x10 SPIN_LOCK_NOT_OWNED)
         *
         * By forcing the fast path here, we atomically transition
         * to REMOVE under the lock, avoiding the race entirely.
         * The miniport will handle endpoint cleanup when the
         * worker processes the REMOVE state.
         */
        if ((Endpoint->Flags & ENDPOINT_FLAG_ABORTING) &&
            State == USBPORT_ENDPOINT_REMOVE)
        {
            DPRINT1("USBPORT_SetEndpointState: ABORTING+REMOVE fast path Endpoint=%p\n",
                    Endpoint);

            Endpoint->StateLast = State;
            Endpoint->StateNext = State;

            KeReleaseSpinLock(&Endpoint->StateChangeSpinLock,
                              Endpoint->EndpointStateOldIrql);

            USBPORT_PurgeEndpointStateChange(Endpoint);

            USBPORT_InvalidateEndpointHandler(FdoDevice,
                                              Endpoint,
                                              INVALIDATE_ENDPOINT_WORKER_THREAD);
            return;
        }

        KeReleaseSpinLock(&Endpoint->StateChangeSpinLock,
                          Endpoint->EndpointStateOldIrql);

        KeAcquireSpinLock(&FdoExtension->MiniportSpinLock, &OldIrql);
        Packet->SetEndpointState(FdoExtension->MiniPortExt,
                                 Endpoint + 1,
                                 State);
        KeReleaseSpinLock(&FdoExtension->MiniportSpinLock, OldIrql);

        Endpoint->StateNext = State;

        KeAcquireSpinLock(&FdoExtension->MiniportSpinLock, &OldIrql);
        Endpoint->FrameNumber = Packet->Get32BitFrameNumber(FdoExtension->MiniPortExt);
        KeReleaseSpinLock(&FdoExtension->MiniportSpinLock, OldIrql);

        ExInterlockedInsertTailList(&FdoExtension->EpStateChangeList,
                                    &Endpoint->StateChangeLink,
                                    &FdoExtension->EpStateChangeSpinLock);

        KeAcquireSpinLock(&FdoExtension->MiniportSpinLock, &OldIrql);
        Packet->InterruptNextSOF(FdoExtension->MiniPortExt);
        KeReleaseSpinLock(&FdoExtension->MiniportSpinLock, OldIrql);
    }
    else
    {
        Endpoint->StateLast = State;
        Endpoint->StateNext = State;

        if (State == USBPORT_ENDPOINT_REMOVE)
        {
            KeReleaseSpinLock(&Endpoint->StateChangeSpinLock,
                              Endpoint->EndpointStateOldIrql);

            USBPORT_InvalidateEndpointHandler(FdoDevice,
                                              Endpoint,
                                              INVALIDATE_ENDPOINT_WORKER_THREAD);
            return;
        }

        KeReleaseSpinLock(&Endpoint->StateChangeSpinLock,
                          Endpoint->EndpointStateOldIrql);
    }
}

VOID
NTAPI
USBPORT_AddPipeHandle(IN PUSBPORT_DEVICE_HANDLE DeviceHandle,
                      IN PUSBPORT_PIPE_HANDLE PipeHandle)
{
    DPRINT("USBPORT_AddPipeHandle: DeviceHandle - %p, PipeHandle - %p\n",
           DeviceHandle,
           PipeHandle);

    InsertTailList(&DeviceHandle->PipeHandleList, &PipeHandle->PipeLink);
}

VOID
NTAPI
USBPORT_RemovePipeHandle(IN PUSBPORT_DEVICE_HANDLE DeviceHandle,
                         IN PUSBPORT_PIPE_HANDLE PipeHandle)
{
    DPRINT("USBPORT_RemovePipeHandle: PipeHandle - %p\n", PipeHandle);

    RemoveEntryList(&PipeHandle->PipeLink);

    PipeHandle->PipeLink.Flink = NULL;
    PipeHandle->PipeLink.Blink = NULL;
}

VOID
NTAPI
USBPORT_CloseStaticStreamsInternal(IN PDEVICE_OBJECT FdoDevice,
                                   IN PUSBPORT_DEVICE_HANDLE DeviceHandle,
                                   IN PUSBPORT_PIPE_HANDLE PipeHandle,
                                   IN BOOLEAN ReconfigureMiniport)
{
    PUSBPORT_DEVICE_EXTENSION FdoExtension;
    PUSBPORT_REGISTRATION_PACKET Packet;
    PUSBPORT_ENDPOINT Endpoint;
    PLIST_ENTRY Entry;
    PLIST_ENTRY Next;

    if (!PipeHandle || PipeHandle->StreamCount == 0)
        return;

    Endpoint = PipeHandle->Endpoint;
    if (!Endpoint)
        return;

    if (ReconfigureMiniport && FdoDevice)
    {
        FdoExtension = FdoDevice->DeviceExtension;
        Packet = &FdoExtension->MiniPortInterface->Packet;

        Endpoint->EndpointProperties.Reserved6 = 0;

        if (Packet->ReopenEndpoint)
        {
            (VOID)Packet->ReopenEndpoint(FdoExtension->MiniPortExt,
                                         &Endpoint->EndpointProperties,
                                         Endpoint + 1);
        }
    }

    for (Entry = PipeHandle->StreamList.Flink;
         Entry != &PipeHandle->StreamList;
         Entry = Next)
    {
        PUSBPORT_PIPE_HANDLE StreamHandle;

        Next = Entry->Flink;
        StreamHandle = CONTAINING_RECORD(Entry,
                                         USBPORT_PIPE_HANDLE,
                                         StreamLink);

        USBPORT_RemovePipeHandle(DeviceHandle, StreamHandle);
        RemoveEntryList(&StreamHandle->StreamLink);
        StreamHandle->StreamLink.Flink = NULL;
        StreamHandle->StreamLink.Blink = NULL;
        ExFreePoolWithTag(StreamHandle, USB_PORT_TAG);
    }

    InitializeListHead(&PipeHandle->StreamList);
    PipeHandle->StreamCount = 0;
}

BOOLEAN
NTAPI
USBPORT_ValidatePipeHandle(IN PUSBPORT_DEVICE_HANDLE DeviceHandle,
                           IN PUSBPORT_PIPE_HANDLE PipeHandle)
{
    PLIST_ENTRY HandleList;
    PUSBPORT_PIPE_HANDLE CurrentHandle;

    //DPRINT("USBPORT_ValidatePipeHandle: DeviceHandle - %p, PipeHandle - %p\n",
    //       DeviceHandle,
    //       PipeHandle);

    HandleList = DeviceHandle->PipeHandleList.Flink;

    while (HandleList != &DeviceHandle->PipeHandleList)
    {
        CurrentHandle = CONTAINING_RECORD(HandleList,
                                          USBPORT_PIPE_HANDLE,
                                          PipeLink);

        HandleList = HandleList->Flink;

        if (CurrentHandle == PipeHandle)
            return TRUE;
    }

    return FALSE;
}

BOOLEAN
NTAPI
USBPORT_DeleteEndpoint(IN PDEVICE_OBJECT FdoDevice,
                       IN PUSBPORT_ENDPOINT Endpoint)
{
    PUSBPORT_DEVICE_EXTENSION  FdoExtension;
    BOOLEAN Result;
    KIRQL OldIrql;

    DPRINT1("USBPORT_DeleteEndpoint: Endpoint - %p\n", Endpoint);

    FdoExtension = FdoDevice->DeviceExtension;

    if ((Endpoint->WorkerLink.Flink && Endpoint->WorkerLink.Blink) ||
        Endpoint->LockCounter != -1)
    {
        KeAcquireSpinLock(&FdoExtension->EndpointListSpinLock, &OldIrql);

        ExInterlockedInsertTailList(&FdoExtension->EndpointClosedList,
                                    &Endpoint->CloseLink,
                                    &FdoExtension->EndpointClosedSpinLock);

        KeReleaseSpinLock(&FdoExtension->EndpointListSpinLock, OldIrql);

        Result = FALSE;
    }
    else
    {
        KeAcquireSpinLock(&FdoExtension->EndpointListSpinLock, &OldIrql);

        RemoveEntryList(&Endpoint->EndpointLink);
        Endpoint->EndpointLink.Flink = NULL;
        Endpoint->EndpointLink.Blink = NULL;

        KeReleaseSpinLock(&FdoExtension->EndpointListSpinLock, OldIrql);

        USBPORT_PurgeEndpointStateChange(Endpoint);

        /* Free any cached reusable transfer before closing endpoint */
        USBPORT_FreeReusableTransfer(Endpoint);

        MiniportCloseEndpoint(FdoDevice, Endpoint);

        if (Endpoint->HeaderBuffer)
        {
            USBPORT_FreeCommonBuffer(FdoDevice, Endpoint->HeaderBuffer);
        }

        ExFreePoolWithTag(Endpoint, USB_PORT_TAG);

        Result = TRUE;
    }

    return Result;
}

VOID
NTAPI
MiniportCloseEndpoint(IN PDEVICE_OBJECT FdoDevice,
                      IN PUSBPORT_ENDPOINT Endpoint)
{
    PUSBPORT_DEVICE_EXTENSION  FdoExtension;
    PUSBPORT_REGISTRATION_PACKET Packet;
    BOOLEAN IsDoDisablePeriodic;
    BOOLEAN IsOpened;
    ULONG TransferType;
    KIRQL OldIrql;

    DPRINT("MiniportCloseEndpoint: Endpoint - %p\n", Endpoint);

    FdoExtension = FdoDevice->DeviceExtension;
    Packet = &FdoExtension->MiniPortInterface->Packet;

    KeAcquireSpinLock(&FdoExtension->MiniportSpinLock, &OldIrql);
    IsOpened = (Endpoint->Flags & ENDPOINT_FLAG_OPENED) != 0;

    if (IsOpened)
    {
        TransferType = Endpoint->EndpointProperties.TransferType;

        if (TransferType == USBPORT_TRANSFER_TYPE_INTERRUPT ||
            TransferType == USBPORT_TRANSFER_TYPE_ISOCHRONOUS)
        {
            --FdoExtension->PeriodicEndpoints;
        }

        IsDoDisablePeriodic = FdoExtension->PeriodicEndpoints == 0;
        Endpoint->Flags &= ~ENDPOINT_FLAG_OPENED;
        Endpoint->Flags |= ENDPOINT_FLAG_CLOSED;
    }

    KeReleaseSpinLock(&FdoExtension->MiniportSpinLock, OldIrql);

    if (!IsOpened)
        return;

    /*
     * Miniports that advertise USB_MINIPORT_FLAGS_CLOSE_AT_PASSIVE require
     * their CloseEndpoint callback to run at PASSIVE_LEVEL without the
     * MiniportSpinLock held — xHCI frees ring/common-buffer DMA via
     * MmFreeContiguousMemory, which is passive-only. Legacy miniports
     * (UHCI/OHCI/EHCI) keep the historical locked close path because
     * their callbacks only touch controller state at DISPATCH_LEVEL.
     */
    if (Packet->MiniPortFlags & USB_MINIPORT_FLAGS_CLOSE_AT_PASSIVE)
    {
        Packet->CloseEndpoint(FdoExtension->MiniPortExt,
                              Endpoint + 1,
                              IsDoDisablePeriodic);
        return;
    }

    KeAcquireSpinLock(&FdoExtension->MiniportSpinLock, &OldIrql);
    Packet->CloseEndpoint(FdoExtension->MiniPortExt,
                          Endpoint + 1,
                          IsDoDisablePeriodic);
    KeReleaseSpinLock(&FdoExtension->MiniportSpinLock, OldIrql);
}

VOID
NTAPI
USBPORT_ClosePipe(IN PUSBPORT_DEVICE_HANDLE DeviceHandle,
                  IN PDEVICE_OBJECT FdoDevice,
                  IN PUSBPORT_PIPE_HANDLE PipeHandle)
{
    PUSBPORT_DEVICE_EXTENSION FdoExtension;
    PUSBPORT_RHDEVICE_EXTENSION PdoExtension;
    PUSBPORT_ENDPOINT Endpoint;
    PUSBPORT_REGISTRATION_PACKET Packet;
    PUSB2_TT_EXTENSION TtExtension;
    ULONG ix;
    BOOLEAN IsReady;
    KIRQL OldIrql;

    DPRINT1("USBPORT_ClosePipe \n");

    FdoExtension = FdoDevice->DeviceExtension;

    if (PipeHandle->Flags & PIPE_HANDLE_FLAG_CLOSED)
        return;

    USBPORT_RemovePipeHandle(DeviceHandle, PipeHandle);

    PipeHandle->Flags |= PIPE_HANDLE_FLAG_CLOSED;

    if (PipeHandle->Flags & PIPE_HANDLE_FLAG_NULL_PACKET_SIZE)
    {
        PipeHandle->Flags &= ~PIPE_HANDLE_FLAG_NULL_PACKET_SIZE;
        return;
    }

    if (PipeHandle->BasePipe)
    {
        if (PipeHandle->BasePipe->StreamCount > 0)
            PipeHandle->BasePipe->StreamCount--;
        if (PipeHandle->BasePipe->StreamCount == 0)
            InitializeListHead(&PipeHandle->BasePipe->StreamList);
        USBPORT_RemovePipeHandle(DeviceHandle, PipeHandle);
        RemoveEntryList(&PipeHandle->StreamLink);
        PipeHandle->StreamLink.Flink = NULL;
        PipeHandle->StreamLink.Blink = NULL;
        ExFreePoolWithTag(PipeHandle, USB_PORT_TAG);
        return;
    }

    Endpoint = PipeHandle->Endpoint;

    /*
     * Guard against NULL or invalid Endpoint pointer.
     * This can happen if USBPORT_OpenPipe failed before allocating the
     * endpoint, or if the endpoint was never properly initialized due to
     * an ADDRESS_DEVICE failure in the miniport (xHCI).
     */
    if (!Endpoint || Endpoint == (PUSBPORT_ENDPOINT)-1)
    {
        DPRINT1("USBPORT_ClosePipe: Endpoint is NULL or invalid, nothing to close\n");
        return;
    }

    KeAcquireSpinLock(&FdoExtension->EndpointListSpinLock, &OldIrql);

    if (Endpoint->Flags & ENDPOINT_FLAG_ROOTHUB_EP0)
    {
        PdoExtension = USBPORT_GetRootHubExtension(FdoExtension);
        if (PdoExtension && PdoExtension->Endpoint == Endpoint)
        {
            PdoExtension->Endpoint = NULL;
        }
    }

    KeReleaseSpinLock(&FdoExtension->EndpointListSpinLock, OldIrql);

    while (TRUE)
    {
        IsReady = TRUE;

        KeAcquireSpinLock(&Endpoint->EndpointSpinLock,
                          &Endpoint->EndpointOldIrql);

        if (!IsListEmpty(&Endpoint->PendingTransferList))
            IsReady = FALSE;

        if (!IsListEmpty(&Endpoint->TransferList))
            IsReady = FALSE;

        if (!IsListEmpty(&Endpoint->CancelList))
            IsReady = FALSE;

        if (!IsListEmpty(&Endpoint->AbortList))
            IsReady = FALSE;

        KeAcquireSpinLockAtDpcLevel(&Endpoint->StateChangeSpinLock);
        if (Endpoint->StateLast != Endpoint->StateNext)
            IsReady = FALSE;
        KeReleaseSpinLockFromDpcLevel(&Endpoint->StateChangeSpinLock);

        KeReleaseSpinLock(&Endpoint->EndpointSpinLock,
                          Endpoint->EndpointOldIrql);

        if (InterlockedIncrement(&Endpoint->LockCounter))
            IsReady = FALSE;
        InterlockedDecrement(&Endpoint->LockCounter);

        if (IsReady == TRUE)
            break;

        USBPORT_Wait(FdoDevice, 1);
    }

    Endpoint->DeviceHandle = NULL;
    Packet = &FdoExtension->MiniPortInterface->Packet;

    if (Packet->MiniPortFlags & USB_MINIPORT_FLAGS_USB2)
    {
        USBPORT_FreeBandwidthUSB2(FdoDevice, Endpoint);

        KeAcquireSpinLock(&FdoExtension->TtSpinLock, &OldIrql);

        TtExtension = Endpoint->TtExtension;
        DPRINT1("USBPORT_ClosePipe: TtExtension - %p\n", TtExtension);

        if (TtExtension)
        {
            RemoveEntryList(&Endpoint->TtLink);

            Endpoint->TtLink.Flink = NULL;
            Endpoint->TtLink.Blink = NULL;

            if (TtExtension->Flags & USB2_TT_EXTENSION_FLAG_DELETED)
            {
                if (IsListEmpty(&TtExtension->EndpointList))
                {
                    USBPORT_UpdateAllocatedBwTt(TtExtension);

                    for (ix = 0; ix < USB2_FRAMES; ix++)
                    {
                        FdoExtension->Bandwidth[ix] += TtExtension->MaxBandwidth;
                    }

                    DPRINT1("USBPORT_ClosePipe: ExFreePoolWithTag TtExtension - %p\n", TtExtension);
                    ExFreePoolWithTag(TtExtension, USB_PORT_TAG);
                }
            }
        }

        KeReleaseSpinLock(&FdoExtension->TtSpinLock, OldIrql);
    }
    else
    {
        USBPORT_FreeBandwidth(FdoDevice, Endpoint);
    }

    KeAcquireSpinLock(&Endpoint->EndpointSpinLock, &Endpoint->EndpointOldIrql);
    USBPORT_SetEndpointState(Endpoint, USBPORT_ENDPOINT_REMOVE);
    KeReleaseSpinLock(&Endpoint->EndpointSpinLock, Endpoint->EndpointOldIrql);

    USBPORT_SignalWorkerThread(FdoDevice);
}

BOOLEAN
NTAPI
USBPORT_IsEndpointOnList(IN PUSBPORT_DEVICE_EXTENSION FdoExtension,
                         IN PUSBPORT_ENDPOINT Endpoint)
{
    PLIST_ENTRY Entry;
    KIRQL OldIrql;
    BOOLEAN Found = FALSE;

    if (!FdoExtension || !Endpoint)
        return FALSE;

    KeAcquireSpinLock(&FdoExtension->EndpointListSpinLock, &OldIrql);

    for (Entry = FdoExtension->EndpointList.Flink;
         Entry != &FdoExtension->EndpointList;
         Entry = Entry->Flink)
    {
        if (CONTAINING_RECORD(Entry, USBPORT_ENDPOINT, EndpointLink) == Endpoint)
        {
            Found = TRUE;
            break;
        }
    }

    KeReleaseSpinLock(&FdoExtension->EndpointListSpinLock, OldIrql);

    return Found;
}

MPSTATUS
NTAPI
MiniportOpenEndpoint(IN PDEVICE_OBJECT FdoDevice,
                     IN PUSBPORT_ENDPOINT Endpoint)
{
    PUSBPORT_DEVICE_EXTENSION  FdoExtension;
    PUSBPORT_REGISTRATION_PACKET Packet;
    KIRQL OldIrql;
    ULONG TransferType;
    MPSTATUS MpStatus;

    FdoExtension = FdoDevice->DeviceExtension;
    Packet = &FdoExtension->MiniPortInterface->Packet;

    KeAcquireSpinLock(&FdoExtension->MiniportSpinLock, &OldIrql);
    Endpoint->Flags &= ~ENDPOINT_FLAG_CLOSED;
    KeReleaseSpinLock(&FdoExtension->MiniportSpinLock, OldIrql);

    USBPORT_ASSERT_PASSIVE("MiniportOpenEndpoint before Packet->OpenEndpoint");
    MpStatus = Packet->OpenEndpoint(FdoExtension->MiniPortExt,
                                    &Endpoint->EndpointProperties,
                                    Endpoint + 1);

    KeAcquireSpinLock(&FdoExtension->MiniportSpinLock, &OldIrql);

    if (!MpStatus)
    {
        TransferType = Endpoint->EndpointProperties.TransferType;

        if (TransferType == USBPORT_TRANSFER_TYPE_INTERRUPT ||
            TransferType == USBPORT_TRANSFER_TYPE_ISOCHRONOUS)
        {
            ++FdoExtension->PeriodicEndpoints;
        }

        Endpoint->Flags |= ENDPOINT_FLAG_OPENED;
    }

    KeReleaseSpinLock(&FdoExtension->MiniportSpinLock, OldIrql);

    return MpStatus;
}

NTSTATUS
NTAPI
USBPORT_OpenPipe(IN PDEVICE_OBJECT FdoDevice,
                 IN PUSBPORT_DEVICE_HANDLE DeviceHandle,
                 IN PUSBPORT_PIPE_HANDLE PipeHandle,
                 IN OUT PUSBD_STATUS UsbdStatus)
{
    PUSBPORT_DEVICE_EXTENSION FdoExtension;
    PUSBPORT_RHDEVICE_EXTENSION PdoExtension;
    PUSBPORT_REGISTRATION_PACKET Packet;
    SIZE_T EndpointSize;
    PUSBPORT_ENDPOINT Endpoint;
    PUSBPORT_ENDPOINT_PROPERTIES EndpointProperties;
    PUSB_ENDPOINT_DESCRIPTOR EndpointDescriptor;
    UCHAR Direction;
    UCHAR Interval;
    UCHAR Period;
    USBPORT_ENDPOINT_REQUIREMENTS EndpointRequirements = {0};
    PUSBPORT_COMMON_BUFFER_HEADER HeaderBuffer;
    MPSTATUS MpStatus;
    USBD_STATUS USBDStatus = USBD_STATUS_SUCCESS;
    NTSTATUS Status;
    KIRQL OldIrql;
    USHORT MaxPacketSize;
    USHORT AdditionalTransaction;
    BOOLEAN IsAllocatedBandwidth = FALSE;
    ULONG RetryCount;

    USBPORT_LOG_IRQL("OpenPipe entry");
    USBPORT_ASSERT_PASSIVE("USBPORT_OpenPipe entry");

    FdoExtension = FdoDevice->DeviceExtension;
    Packet = &FdoExtension->MiniPortInterface->Packet;

    /* Never attempt to open pipes against a controller that was never
     * started successfully. This protects miniports from being called
     * with uninitialized hardware/MMIO state when StartController
     * failed earlier. */
    if (!(FdoExtension->Flags & USBPORT_FLAG_HC_STARTED))
    {
        DPRINT1("USBPORT_OpenPipe: HC not started, refusing to open pipe\n");
        if (UsbdStatus)
            *UsbdStatus = USBD_STATUS_DEVICE_GONE;
        return USBPORT_USBDStatusToNtStatus(NULL, USBD_STATUS_DEVICE_GONE);
    }

    EndpointSize = sizeof(USBPORT_ENDPOINT) + Packet->MiniPortEndpointSize;

    if (Packet->MiniPortFlags & USB_MINIPORT_FLAGS_USB2)
    {
        EndpointSize += sizeof(USB2_TT_ENDPOINT);
    }


    if (PipeHandle->EndpointDescriptor.wMaxPacketSize == 0)
    {
        DPRINT1("USBPORT_OpenPipe: wMaxPacketSize==0 (null pipe) IRQL=%lu\n",
                KeGetCurrentIrql());
        USBPORT_AddPipeHandle(DeviceHandle, PipeHandle);

        PipeHandle->Flags = (PipeHandle->Flags & ~PIPE_HANDLE_FLAG_CLOSED) |
                             PIPE_HANDLE_FLAG_NULL_PACKET_SIZE;

        PipeHandle->Endpoint = (PUSBPORT_ENDPOINT)-1;

        return STATUS_SUCCESS;
    }

    Endpoint = ExAllocatePoolWithTag(NonPagedPool, EndpointSize, USB_PORT_TAG);

    if (!Endpoint)
    {
        DPRINT1("USBPORT_OpenPipe: Not allocated Endpoint!\n");
        Status = STATUS_INSUFFICIENT_RESOURCES;
        return Status;
    }

    RtlZeroMemory(Endpoint, EndpointSize);
    DPRINT("USBPORT_OpenPipe: endpoint allocated=%p IRQL=%lu\n",
           Endpoint,
           KeGetCurrentIrql());

    Endpoint->FdoDevice = FdoDevice;
    Endpoint->DeviceHandle = DeviceHandle;
    Endpoint->LockCounter = -1;

    Endpoint->TtExtension = DeviceHandle->TtExtension;

    if (DeviceHandle->TtExtension)
    {
        ExInterlockedInsertTailList(&DeviceHandle->TtExtension->EndpointList,
                                    &Endpoint->TtLink,
                                    &FdoExtension->TtSpinLock);
    }

    if (Packet->MiniPortFlags & USB_MINIPORT_FLAGS_USB2)
    {
        Endpoint->TtEndpoint = (PUSB2_TT_ENDPOINT)((ULONG_PTR)Endpoint +
                                                   sizeof(USBPORT_ENDPOINT) +
                                                   Packet->MiniPortEndpointSize);
    }
    else
    {
        Endpoint->TtEndpoint = NULL;
    }

    KeInitializeSpinLock(&Endpoint->EndpointSpinLock);
    KeInitializeSpinLock(&Endpoint->StateChangeSpinLock);

    InitializeListHead(&Endpoint->PendingTransferList);
    InitializeListHead(&Endpoint->TransferList);
    InitializeListHead(&Endpoint->CancelList);
    InitializeListHead(&Endpoint->AbortList);

    EndpointProperties = &Endpoint->EndpointProperties;
    EndpointDescriptor = &PipeHandle->EndpointDescriptor;

    MaxPacketSize = EndpointDescriptor->wMaxPacketSize & 0x7FF;
    AdditionalTransaction = (EndpointDescriptor->wMaxPacketSize >> 11) & 3;

    EndpointProperties->DeviceAddress = DeviceHandle->DeviceAddress;
    EndpointProperties->DeviceSpeed = DeviceHandle->DeviceSpeed;
    EndpointProperties->Period = 0;
    EndpointProperties->EndpointAddress = EndpointDescriptor->bEndpointAddress;
    EndpointProperties->TransactionPerMicroframe = AdditionalTransaction + 1;
    EndpointProperties->MaxPacketSize = MaxPacketSize;
    EndpointProperties->TotalMaxPacketSize = MaxPacketSize *
                                             (AdditionalTransaction + 1);
    EndpointProperties->Reserved4 = EndpointDescriptor->bInterval;

    if (EndpointProperties->DeviceSpeed == UsbSuperSpeed &&
        PipeHandle->SsCompanionValid)
    {
        EndpointProperties->TransactionPerMicroframe =
            PipeHandle->SsCompanionMaxBurst + 1;
        EndpointProperties->TotalMaxPacketSize =
            MaxPacketSize * EndpointProperties->TransactionPerMicroframe;

        if (PipeHandle->SsCompanionBytesPerInterval)
        {
            EndpointProperties->TotalMaxPacketSize =
                PipeHandle->SsCompanionBytesPerInterval;
        }
    }

    if (Endpoint->TtExtension)
    {
        EndpointProperties->HubAddr = Endpoint->TtExtension->DeviceAddress;
    }
    else
    {
        EndpointProperties->HubAddr = -1;
    }

    EndpointProperties->PortNumber = DeviceHandle->PortNumber;

    PipeHandle->StreamId = 0;
    PipeHandle->StreamCount = 0;
    PipeHandle->BasePipe = NULL;
    InitializeListHead(&PipeHandle->StreamList);
    PipeHandle->StreamLink.Flink = NULL;
    PipeHandle->StreamLink.Blink = NULL;

    switch (EndpointDescriptor->bmAttributes & USB_ENDPOINT_TYPE_MASK)
    {
        case USB_ENDPOINT_TYPE_CONTROL:
            EndpointProperties->TransferType = USBPORT_TRANSFER_TYPE_CONTROL;

            if (EndpointProperties->EndpointAddress == 0)
            {
                EndpointProperties->MaxTransferSize = 0x1000; // OUT Ep0
            }
            else
            {
                EndpointProperties->MaxTransferSize = 0x10000;
            }

            break;

        case USB_ENDPOINT_TYPE_ISOCHRONOUS:
            EndpointProperties->TransferType = USBPORT_TRANSFER_TYPE_ISOCHRONOUS;
            EndpointProperties->MaxTransferSize = 0x1000000;
            break;

        case USB_ENDPOINT_TYPE_BULK:
            EndpointProperties->TransferType = USBPORT_TRANSFER_TYPE_BULK;
            EndpointProperties->MaxTransferSize = USBPORT_BULK_MAX_TRANSFER_SIZE;
            break;

        case USB_ENDPOINT_TYPE_INTERRUPT:
            EndpointProperties->TransferType = USBPORT_TRANSFER_TYPE_INTERRUPT;
            EndpointProperties->MaxTransferSize = 0x400;
            break;
    }

    if (EndpointProperties->TransferType == USBPORT_TRANSFER_TYPE_INTERRUPT)
    {
        if (EndpointProperties->DeviceSpeed == UsbHighSpeed)
        {
            Interval = USBPORT_NormalizeHsInterval(EndpointDescriptor->bInterval);
        }
        else
        {
            Interval = EndpointDescriptor->bInterval;
        }

        EndpointProperties->Period = ENDPOINT_INTERRUPT_32ms;

        if (Interval && (Interval < USB2_FRAMES))
        {
            if ((EndpointProperties->DeviceSpeed != UsbLowSpeed) ||
                (Interval >= ENDPOINT_INTERRUPT_8ms))
            {
                if (!(Interval & ENDPOINT_INTERRUPT_32ms))
                {
                    Period = EndpointProperties->Period;

                    do
                    {
                        Period >>= 1;
                    }
                    while (!(Period & Interval));

                    EndpointProperties->Period = Period;
                }
            }
            else
            {
                EndpointProperties->Period = ENDPOINT_INTERRUPT_8ms;
            }
        }
    }

    if (EndpointProperties->TransferType == USBPORT_TRANSFER_TYPE_ISOCHRONOUS)
    {
        if (EndpointProperties->DeviceSpeed == UsbHighSpeed)
        {
            EndpointProperties->Period =
                USBPORT_NormalizeHsInterval(EndpointDescriptor->bInterval);
        }
        else
        {
            EndpointProperties->Period = ENDPOINT_INTERRUPT_1ms;
        }
    }

    Direction = USB_ENDPOINT_DIRECTION_OUT(EndpointDescriptor->bEndpointAddress);
    EndpointProperties->Direction = Direction;

    if (EndpointProperties->TransferType == USBPORT_TRANSFER_TYPE_ISOCHRONOUS &&
        Packet->SubmitIsoTransfer == NULL)
    {
        USBDStatus = USBD_STATUS_NOT_SUPPORTED;

        if (UsbdStatus)
        {
            *UsbdStatus = USBDStatus;
        }

        Status = USBPORT_USBDStatusToNtStatus(NULL, USBDStatus);

        goto ExitWithError;
    }

    if ((DeviceHandle->Flags & DEVICE_HANDLE_FLAG_ROOTHUB) != 0)
    {
        Endpoint->Flags |= ENDPOINT_FLAG_ROOTHUB_EP0;
    }

    if (Packet->MiniPortFlags & USB_MINIPORT_FLAGS_USB2)
    {
        DPRINT("USBPORT_OpenPipe: calling AllocateBandwidthUSB2 IRQL=%lu\n", KeGetCurrentIrql());
        IsAllocatedBandwidth = USBPORT_AllocateBandwidthUSB2(FdoDevice, Endpoint);
        DPRINT("USBPORT_OpenPipe: AllocateBandwidthUSB2 done=%u IRQL=%lu\n",
               IsAllocatedBandwidth,
               KeGetCurrentIrql());
    }
    else
    {
        EndpointProperties->UsbBandwidth = USBPORT_CalculateUsbBandwidth(FdoDevice,
                                                                         Endpoint);

        IsAllocatedBandwidth = USBPORT_AllocateBandwidth(FdoDevice, Endpoint);
        DPRINT("USBPORT_OpenPipe: AllocateBandwidth done=%u IRQL=%lu\n",
               IsAllocatedBandwidth,
               KeGetCurrentIrql());
    }

    if (!IsAllocatedBandwidth)
    {
        Status = USBPORT_USBDStatusToNtStatus(NULL, USBD_STATUS_NO_BANDWIDTH);

        if (UsbdStatus)
        {
            *UsbdStatus = USBD_STATUS_NO_BANDWIDTH;
        }

        goto ExitWithError;
    }

    if (DeviceHandle->IsRootHub)
    {
        Endpoint->EndpointWorker = 0; // USBPORT_RootHubEndpointWorker;

        Endpoint->Flags |= ENDPOINT_FLAG_ROOTHUB_EP0;

        Endpoint->StateLast = USBPORT_ENDPOINT_ACTIVE;
        Endpoint->StateNext = USBPORT_ENDPOINT_ACTIVE;

        PdoExtension = USBPORT_GetRootHubExtension(FdoExtension);

        if (EndpointProperties->TransferType == USBPORT_TRANSFER_TYPE_INTERRUPT)
        {
            PdoExtension->Endpoint = Endpoint;
        }

        USBDStatus = USBD_STATUS_SUCCESS;
    }
    else
    {
        Endpoint->EndpointWorker = 1; // USBPORT_DmaEndpointWorker;

        KeAcquireSpinLock(&FdoExtension->MiniportSpinLock, &OldIrql);

        Packet->QueryEndpointRequirements(FdoExtension->MiniPortExt,
                                          &Endpoint->EndpointProperties,
                                          &EndpointRequirements);
        KeReleaseSpinLock(&FdoExtension->MiniportSpinLock, OldIrql);

        if ((EndpointProperties->TransferType == USBPORT_TRANSFER_TYPE_BULK) ||
            (EndpointProperties->TransferType == USBPORT_TRANSFER_TYPE_INTERRUPT))
        {
            EndpointProperties->MaxTransferSize = EndpointRequirements.MaxTransferSize;
        }

        if (EndpointRequirements.HeaderBufferSize)
        {
            HeaderBuffer = USBPORT_AllocateCommonBuffer(FdoDevice,
                                                        EndpointRequirements.HeaderBufferSize);
            DPRINT1("USBPORT_OpenPipe: AllocateCommonBuffer done=%p IRQL=%lu\n",
                    HeaderBuffer, KeGetCurrentIrql());
        }
        else
        {
            HeaderBuffer = NULL;
        }

        if (HeaderBuffer || (EndpointRequirements.HeaderBufferSize == 0))
        {
            Endpoint->HeaderBuffer = HeaderBuffer;

            if (HeaderBuffer)
            {
                EndpointProperties->BufferVA = HeaderBuffer->VirtualAddress;
                EndpointProperties->BufferPA = HeaderBuffer->PhysicalAddress;
                EndpointProperties->BufferLength = HeaderBuffer->BufferLength; // BufferLength + LengthPadded;
            }

            USBPORT_ApplyEndpointLpmPolicy(EndpointProperties, DeviceHandle);

            MpStatus = MiniportOpenEndpoint(FdoDevice, Endpoint);

            Endpoint->Flags |= ENDPOINT_FLAG_DMA_TYPE;
            Endpoint->Flags |= ENDPOINT_FLAG_QUEUENE_EMPTY;

            if (MpStatus == 0)
            {
                ULONG State;

                KeAcquireSpinLock(&Endpoint->EndpointSpinLock,
                                  &Endpoint->EndpointOldIrql);
                Endpoint->StateLast = USBPORT_ENDPOINT_PAUSED;
                Endpoint->StateNext = USBPORT_ENDPOINT_PAUSED;

                USBPORT_SetEndpointState(Endpoint, USBPORT_ENDPOINT_ACTIVE);

                KeReleaseSpinLock(&Endpoint->EndpointSpinLock,
                                  Endpoint->EndpointOldIrql);

                /* Wait maximum 1 second for the endpoint to be active */
                for (RetryCount = 0; RetryCount < 1000; RetryCount++)
                {
                    KeAcquireSpinLock(&Endpoint->EndpointSpinLock,
                                      &Endpoint->EndpointOldIrql);

                    State = USBPORT_GetEndpointState(Endpoint);

                    KeReleaseSpinLock(&Endpoint->EndpointSpinLock,
                                      Endpoint->EndpointOldIrql);

                    if (State == USBPORT_ENDPOINT_ACTIVE)
                    {
                        break;
                    }

                    USBPORT_ASSERT_PASSIVE("OpenPipe wait loop before USBPORT_Wait");
                    USBPORT_Wait(FdoDevice, 1); // 1 msec.
                }
                if (State != USBPORT_ENDPOINT_ACTIVE)
                {
                    DPRINT1("Timeout State %x\n", State);
                    USBDStatus = USBD_STATUS_TIMEOUT;
                }
            }
            else
            {
                DPRINT1("USBPORT_OpenPipe: MiniportOpenEndpoint FAILED MpStatus=%lx "
                        "Type=%lu Dir=%lu Speed=%u MaxPacket=%lu\n",
                        MpStatus,
                        EndpointProperties->TransferType,
                        EndpointProperties->Direction,
                        DeviceHandle->DeviceSpeed,
                        EndpointProperties->MaxPacketSize);
            }
        }
        else
        {
            MpStatus = MP_STATUS_NO_RESOURCES;
            Endpoint->HeaderBuffer = NULL;
        }

        if (MpStatus)
        {
            if (MpStatus == MP_STATUS_NO_BANDWIDTH)
            {
                USBDStatus = USBD_STATUS_NO_BANDWIDTH;
            }
            else if (MpStatus == MP_STATUS_NOT_SUPPORTED)
            {
                USBDStatus = USBD_STATUS_NOT_SUPPORTED;
            }
            else if (MpStatus == MP_STATUS_FAILURE)
            {
                /* Context or descriptor problem (e.g. xHCI AddressDevice CONTEXT_ERROR). */
                USBDStatus = USBD_STATUS_BAD_DESCRIPTOR;
            }
            else
            {
                USBDStatus = USBD_STATUS_INSUFFICIENT_RESOURCES;
            }
        }
    }

    if (UsbdStatus)
    {
        *UsbdStatus = USBDStatus;
    }

    Status = USBPORT_USBDStatusToNtStatus(NULL, USBDStatus);

    if (NT_SUCCESS(Status))
    {
        USBPORT_AddPipeHandle(DeviceHandle, PipeHandle);

        ExInterlockedInsertTailList(&FdoExtension->EndpointList,
                                    &Endpoint->EndpointLink,
                                    &FdoExtension->EndpointListSpinLock);

        PipeHandle->Endpoint = Endpoint;
        PipeHandle->Flags &= ~PIPE_HANDLE_FLAG_CLOSED;

        USBPORT_ASSERT_PASSIVE("USBPORT_OpenPipe exit success");
        USBPORT_LOG_IRQL("OpenPipe exit success");
        return Status;
    }

ExitWithError:

    if (Endpoint)
    {
        if (IsAllocatedBandwidth)
        {
            if (Packet->MiniPortFlags & USB_MINIPORT_FLAGS_USB2)
            {
                USBPORT_FreeBandwidthUSB2(FdoDevice, Endpoint);
            }
            else
            {
                USBPORT_FreeBandwidth(FdoDevice, Endpoint);
            }
        }

        if (Endpoint->TtExtension)
        {
            KeAcquireSpinLock(&FdoExtension->TtSpinLock, &OldIrql);
            RemoveEntryList(&Endpoint->TtLink);
            KeReleaseSpinLock(&FdoExtension->TtSpinLock, OldIrql);
        }

        ExFreePoolWithTag(Endpoint, USB_PORT_TAG);
    }

    DPRINT1("USBPORT_OpenPipe: Status - %lx\n", Status);
    USBPORT_ASSERT_PASSIVE("USBPORT_OpenPipe exit failure");
    USBPORT_LOG_IRQL("OpenPipe exit failure");
    return Status;
}

NTSTATUS
NTAPI
USBPORT_ReopenPipe(IN PDEVICE_OBJECT FdoDevice,
                   IN PUSBPORT_ENDPOINT Endpoint)
{
    PUSBPORT_DEVICE_EXTENSION FdoExtension;
    PUSBPORT_COMMON_BUFFER_HEADER HeaderBuffer;
    USBPORT_ENDPOINT_REQUIREMENTS EndpointRequirements = {0};
    PUSBPORT_REGISTRATION_PACKET Packet;
    KIRQL MiniportOldIrql;
    BOOLEAN IsDefaultControlPipe;
    NTSTATUS Status;

    DPRINT("USBPORT_ReopenPipe ... \n");

    FdoExtension = FdoDevice->DeviceExtension;
    Packet = &FdoExtension->MiniPortInterface->Packet;
    IsDefaultControlPipe =
        (Packet->MiniPortFlags & USB_MINIPORT_FLAGS_USB3) &&
        Endpoint->EndpointProperties.TransferType == USBPORT_TRANSFER_TYPE_CONTROL &&
        Endpoint->EndpointProperties.EndpointAddress == 0;

    while (TRUE)
    {
        if (!InterlockedIncrement(&Endpoint->LockCounter))
            break;

        InterlockedDecrement(&Endpoint->LockCounter);
        USBPORT_Wait(FdoDevice, 1);
    }

    KeAcquireSpinLock(&Endpoint->EndpointSpinLock, &Endpoint->EndpointOldIrql);
    KeAcquireSpinLockAtDpcLevel(&FdoExtension->MiniportSpinLock);

    Packet->SetEndpointState(FdoExtension->MiniPortExt,
                             Endpoint + 1,
                             USBPORT_ENDPOINT_REMOVE);

    KeReleaseSpinLockFromDpcLevel(&FdoExtension->MiniportSpinLock);
    KeReleaseSpinLock(&Endpoint->EndpointSpinLock, Endpoint->EndpointOldIrql);

    /*
     * The USB3 default control pipe is slot-owned on xHCI and stays on the
     * static EP0 ring while the packet size is updated. There is no asynchronous
     * remove transition to settle before CloseEndpoint/OpenEndpoint rebuilds
     * the software endpoint view.
     */
    if (!IsDefaultControlPipe)
    {
        USBPORT_Wait(FdoDevice, 2);
    }

        MiniportCloseEndpoint(FdoDevice, Endpoint);

        if (Endpoint->Flags & ENDPOINT_FLAG_ROOTHUB_EP0)
        {
            PUSBPORT_RHDEVICE_EXTENSION PdoExtension = USBPORT_GetRootHubExtension(FdoExtension);

            if (PdoExtension && PdoExtension->Endpoint == Endpoint)
            {
                PdoExtension->Endpoint = NULL;
            }
        }

    RtlZeroMemory(Endpoint + 1,
                  Packet->MiniPortEndpointSize);

    if (Endpoint->HeaderBuffer)
    {
        USBPORT_FreeCommonBuffer(FdoDevice, Endpoint->HeaderBuffer);
        Endpoint->HeaderBuffer = NULL;
    }

    KeAcquireSpinLock(&FdoExtension->MiniportSpinLock, &MiniportOldIrql);

    Packet->QueryEndpointRequirements(FdoExtension->MiniPortExt,
                                      &Endpoint->EndpointProperties,
                                      &EndpointRequirements);

    KeReleaseSpinLock(&FdoExtension->MiniportSpinLock, MiniportOldIrql);

    if (EndpointRequirements.HeaderBufferSize)
    {
        HeaderBuffer = USBPORT_AllocateCommonBuffer(FdoDevice,
                                                    EndpointRequirements.HeaderBufferSize);
    }
    else
    {
        HeaderBuffer = NULL;
    }

    if (HeaderBuffer || EndpointRequirements.HeaderBufferSize == 0)
    {
        Endpoint->HeaderBuffer = HeaderBuffer;
        Status = STATUS_SUCCESS;
    }
    else
    {
        Endpoint->HeaderBuffer = 0;
        Status = STATUS_INSUFFICIENT_RESOURCES;
    }

    if (Endpoint->HeaderBuffer && HeaderBuffer)
    {
        Endpoint->EndpointProperties.BufferVA = HeaderBuffer->VirtualAddress;
        Endpoint->EndpointProperties.BufferPA = HeaderBuffer->PhysicalAddress;
        Endpoint->EndpointProperties.BufferLength = HeaderBuffer->BufferLength;
    }

    if (NT_SUCCESS(Status))
    {
        USBPORT_ApplyEndpointLpmPolicy(&Endpoint->EndpointProperties,
                                       Endpoint->DeviceHandle);
        MiniportOpenEndpoint(FdoDevice, Endpoint);

        KeAcquireSpinLock(&Endpoint->EndpointSpinLock, &Endpoint->EndpointOldIrql);
        KeAcquireSpinLockAtDpcLevel(&Endpoint->StateChangeSpinLock);

        if (Endpoint->StateLast == USBPORT_ENDPOINT_ACTIVE)
        {
            KeReleaseSpinLockFromDpcLevel(&Endpoint->StateChangeSpinLock);
            KeAcquireSpinLockAtDpcLevel(&FdoExtension->MiniportSpinLock);

            Packet->SetEndpointState(FdoExtension->MiniPortExt,
                                     Endpoint + 1,
                                     USBPORT_ENDPOINT_ACTIVE);

            KeReleaseSpinLockFromDpcLevel(&FdoExtension->MiniportSpinLock);
        }
        else
        {
            KeReleaseSpinLockFromDpcLevel(&Endpoint->StateChangeSpinLock);
        }

        KeReleaseSpinLock(&Endpoint->EndpointSpinLock, Endpoint->EndpointOldIrql);
    }

    InterlockedDecrement(&Endpoint->LockCounter);

    return Status;
}

VOID
NTAPI
USBPORT_FlushClosedEndpointList(IN PDEVICE_OBJECT FdoDevice)
{
    PUSBPORT_DEVICE_EXTENSION  FdoExtension;
    KIRQL OldIrql;
    PLIST_ENTRY ClosedList;
    PUSBPORT_ENDPOINT Endpoint;

    DPRINT_CORE("USBPORT_FlushClosedEndpointList: ... \n");

    FdoExtension = FdoDevice->DeviceExtension;

    KeAcquireSpinLock(&FdoExtension->EndpointClosedSpinLock, &OldIrql);
    ClosedList = &FdoExtension->EndpointClosedList;

    while (!IsListEmpty(ClosedList))
    {
        Endpoint = CONTAINING_RECORD(ClosedList->Flink,
                                     USBPORT_ENDPOINT,
                                     CloseLink);

        RemoveHeadList(ClosedList);
        Endpoint->CloseLink.Flink = NULL;
        Endpoint->CloseLink.Blink = NULL;

        KeReleaseSpinLock(&FdoExtension->EndpointClosedSpinLock, OldIrql);

        USBPORT_DeleteEndpoint(FdoDevice, Endpoint);

        KeAcquireSpinLock(&FdoExtension->EndpointClosedSpinLock, &OldIrql);
    }

    KeReleaseSpinLock(&FdoExtension->EndpointClosedSpinLock, OldIrql);
}

VOID
NTAPI
USBPORT_InvalidateEndpointHandler(IN PDEVICE_OBJECT FdoDevice,
                                  IN PUSBPORT_ENDPOINT Endpoint,
                                  IN ULONG Type)
{
    PUSBPORT_DEVICE_EXTENSION FdoExtension;
    PUSBPORT_REGISTRATION_PACKET Packet;
    PLIST_ENTRY Entry;
    PLIST_ENTRY WorkerLink;
    PUSBPORT_ENDPOINT endpoint;
    KIRQL OldIrql;
    BOOLEAN IsAddEntry = FALSE;

    DPRINT_CORE("USBPORT_InvalidateEndpointHandler: Endpoint - %p, Type - %x\n",
                Endpoint,
                Type);

    FdoExtension = FdoDevice->DeviceExtension;
    Packet = &FdoExtension->MiniPortInterface->Packet;

    if (Endpoint)
    {
        WorkerLink = &Endpoint->WorkerLink;
        KeAcquireSpinLock(&FdoExtension->EndpointListSpinLock, &OldIrql);
        DPRINT_CORE("USBPORT_InvalidateEndpointHandler: KeAcquireSpinLock \n");

        if ((!WorkerLink->Flink || !WorkerLink->Blink) &&
            !(Endpoint->Flags & ENDPOINT_FLAG_IDLE) &&
            USBPORT_GetEndpointState(Endpoint) != USBPORT_ENDPOINT_CLOSED)
        {
            DPRINT_CORE("USBPORT_InvalidateEndpointHandler: InsertTailList \n");
            InsertTailList(&FdoExtension->WorkerList, WorkerLink);
            IsAddEntry = TRUE;
        }

        KeReleaseSpinLock(&FdoExtension->EndpointListSpinLock, OldIrql);

        if (Endpoint->Flags & ENDPOINT_FLAG_ROOTHUB_EP0)
            Type = INVALIDATE_ENDPOINT_WORKER_THREAD;
    }
    else
    {
        KeAcquireSpinLock(&FdoExtension->EndpointListSpinLock, &OldIrql);

        for (Entry = FdoExtension->EndpointList.Flink;
             Entry && Entry != &FdoExtension->EndpointList;
             Entry = Entry->Flink)
        {
            endpoint = CONTAINING_RECORD(Entry,
                                         USBPORT_ENDPOINT,
                                         EndpointLink);

            if (!endpoint->WorkerLink.Flink || !endpoint->WorkerLink.Blink)
            {
                if (!(endpoint->Flags & ENDPOINT_FLAG_IDLE) &&
                    !(endpoint->Flags & ENDPOINT_FLAG_ROOTHUB_EP0) &&
                    USBPORT_GetEndpointState(endpoint) != USBPORT_ENDPOINT_CLOSED)
                {
                    DPRINT_CORE("USBPORT_InvalidateEndpointHandler: InsertTailList \n");
                    InsertTailList(&FdoExtension->WorkerList, &endpoint->WorkerLink);
                    IsAddEntry = TRUE;
                }
            }
        }

        KeReleaseSpinLock(&FdoExtension->EndpointListSpinLock, OldIrql);
    }

    if (FdoExtension->Flags & USBPORT_FLAG_HC_SUSPEND)
    {
        Type = INVALIDATE_ENDPOINT_WORKER_THREAD;
    }
    else if (IsAddEntry == FALSE && Type == INVALIDATE_ENDPOINT_INT_NEXT_SOF)
    {
        Type = INVALIDATE_ENDPOINT_ONLY;
    }

    switch (Type)
    {
        case INVALIDATE_ENDPOINT_WORKER_THREAD:
            USBPORT_SignalWorkerThread(FdoDevice);
            break;

        case INVALIDATE_ENDPOINT_WORKER_DPC:
            KeInsertQueueDpc(&FdoExtension->WorkerRequestDpc, NULL, NULL);
            break;

        case INVALIDATE_ENDPOINT_INT_NEXT_SOF:
            KeAcquireSpinLock(&FdoExtension->MiniportSpinLock, &OldIrql);
            Packet->InterruptNextSOF(FdoExtension->MiniPortExt);
            KeReleaseSpinLock(&FdoExtension->MiniportSpinLock, OldIrql);
            break;
    }
}

ULONG
NTAPI
USBPORT_DmaEndpointPaused(IN PDEVICE_OBJECT FdoDevice,
                          IN PUSBPORT_ENDPOINT Endpoint)
{
    PUSBPORT_DEVICE_EXTENSION FdoExtension;
    PUSBPORT_REGISTRATION_PACKET Packet;
    PLIST_ENTRY Entry;
    PUSBPORT_TRANSFER Transfer;
    PURB Urb;
    ULONG Frame;
    ULONG CurrentFrame;
    ULONG CompletedLen = 0;
    KIRQL OldIrql;

    DPRINT_CORE("USBPORT_DmaEndpointPaused \n");

    FdoExtension = FdoDevice->DeviceExtension;
    Packet = &FdoExtension->MiniPortInterface->Packet;

    Entry = Endpoint->TransferList.Flink;

    if (Entry == &Endpoint->TransferList)
    {
        DPRINT_CORE("USBPORT_DmaEndpointPaused: TransferList empty, returning ACTIVE\n");
        return USBPORT_ENDPOINT_ACTIVE;
    }

    DPRINT("USBPORT_DmaEndpointPaused: Endpoint=%p Flags=0x%lx TransferList not empty\n",
           Endpoint, Endpoint->Flags);

    while (Entry && Entry != &Endpoint->TransferList)
    {
        Transfer = CONTAINING_RECORD(Entry,
                                     USBPORT_TRANSFER,
                                     TransferLink);

        /*
         * Skip transfers that are already completed (TRANSFER_FLAG_COMPLETED).
         * These are on the DoneTransferList and will be removed from
         * TransferList and completed by FlushDoneTransfers. Moving them
         * to CancelList here would cause double-completion.
         */
        if (Transfer->Flags & TRANSFER_FLAG_COMPLETED)
        {
            Entry = Transfer->TransferLink.Flink;
            continue;
        }

        if (Transfer->Flags & (TRANSFER_FLAG_CANCELED | TRANSFER_FLAG_ABORTED))
        {
            if (Transfer->Flags & TRANSFER_FLAG_ISO &&
                Transfer->Flags & TRANSFER_FLAG_SUBMITED &&
                !(Endpoint->Flags & ENDPOINT_FLAG_NUKE))
            {
                Urb = Transfer->Urb;

                Frame = Urb->UrbIsochronousTransfer.StartFrame +
                        Urb->UrbIsochronousTransfer.NumberOfPackets;

                KeAcquireSpinLock(&FdoExtension->MiniportSpinLock, &OldIrql);
                CurrentFrame = Packet->Get32BitFrameNumber(FdoExtension->MiniPortExt);
                KeReleaseSpinLock(&FdoExtension->MiniportSpinLock, OldIrql);

                if (Frame + 1 > CurrentFrame)
                {
                    return USBPORT_GetEndpointState(Endpoint);
                }
            }

            if ((Transfer->Flags & TRANSFER_FLAG_SUBMITED) &&
                 !(Endpoint->Flags & ENDPOINT_FLAG_NUKE))
            {
                KeAcquireSpinLock(&FdoExtension->MiniportSpinLock, &OldIrql);

                Packet->AbortTransfer(FdoExtension->MiniPortExt,
                                      Endpoint + 1,
                                      Transfer->MiniportTransfer,
                                      &CompletedLen);

                KeReleaseSpinLock(&FdoExtension->MiniportSpinLock, OldIrql);

                if (Transfer->Flags & TRANSFER_FLAG_ISO)
                {
                    USBPORT_FlushIsoTransfer(Transfer);
                }
                else
                {
                    Transfer->CompletedTransferLen = CompletedLen;
                }
            }

            DPRINT1("USBPORT_DmaEndpointPaused: moving ABORTED transfer %p to CancelList (Flags=0x%lx NUKE=%u)\n",
                    Transfer, Transfer->Flags, !!(Endpoint->Flags & ENDPOINT_FLAG_NUKE));

            RemoveEntryList(&Transfer->TransferLink);
            Entry = Transfer->TransferLink.Flink;

            if (Transfer->Flags & TRANSFER_FLAG_SPLITED)
            {
                USBPORT_CancelSplitTransfer(Transfer);
            }
            else
            {
                InsertTailList(&Endpoint->CancelList, &Transfer->TransferLink);
            }
        }
        else
        {
            Entry = Transfer->TransferLink.Flink;
        }
    }

    DPRINT_CORE("USBPORT_DmaEndpointPaused: done processing TransferList\n");

    return USBPORT_ENDPOINT_ACTIVE;
}

ULONG
NTAPI
USBPORT_DmaEndpointActive(IN PDEVICE_OBJECT FdoDevice,
                          IN PUSBPORT_ENDPOINT Endpoint)
{
    PUSBPORT_DEVICE_EXTENSION FdoExtension;
    PUSBPORT_REGISTRATION_PACKET Packet;
    PLIST_ENTRY Entry;
    PUSBPORT_TRANSFER Transfer;
    LARGE_INTEGER TimeOut;
    MPSTATUS MpStatus;
    KIRQL OldIrql;

    DPRINT_CORE("USBPORT_DmaEndpointActive \n");

    FdoExtension = FdoDevice->DeviceExtension;

    Entry = Endpoint->TransferList.Flink;

    while (Entry && Entry != &Endpoint->TransferList)
    {
        Transfer = CONTAINING_RECORD(Entry,
                                     USBPORT_TRANSFER,
                                     TransferLink);

        /* Skip completed transfers - they are on DoneTransferList and will
         * be removed from TransferList by FlushDoneTransfers. */
        if (Transfer->Flags & TRANSFER_FLAG_COMPLETED)
        {
            Entry = Transfer->TransferLink.Flink;
            continue;
        }

        if (Transfer->Flags & (TRANSFER_FLAG_CANCELED | TRANSFER_FLAG_ABORTED))
        {
            return USBPORT_ENDPOINT_PAUSED;
        }

        if (Transfer->Flags & TRANSFER_FLAG_SUBMITED)
        {
            return USBPORT_ENDPOINT_ACTIVE;
        }

        if (Endpoint &&
            (Endpoint->EndpointProperties.TransferType == USBPORT_TRANSFER_TYPE_BULK ||
             Transfer->TransferParameters.TransferBufferLength >= 64))
        {
            DPRINT("USBPORT_DmaEndpointActive: Ep=%p Transfer=%p Len=%lu Flags=0x%lx Submitted=%u\n",
                   Endpoint,
                   Transfer,
                   Transfer->TransferParameters.TransferBufferLength,
                   Transfer->TransferParameters.TransferFlags,
                   !!(Transfer->Flags & TRANSFER_FLAG_SUBMITED));
        }

        if (!(Transfer->Flags & TRANSFER_FLAG_SUBMITED) &&
             !(Endpoint->Flags & ENDPOINT_FLAG_NUKE))
        {
            KeAcquireSpinLock(&FdoExtension->MiniportSpinLock, &OldIrql);

            Packet = &FdoExtension->MiniPortInterface->Packet;

            if (Transfer->Flags & TRANSFER_FLAG_ISO)
            {
                MpStatus = Packet->SubmitIsoTransfer(FdoExtension->MiniPortExt,
                                                     Endpoint + 1,
                                                     &Transfer->TransferParameters,
                                                     Transfer->MiniportTransfer,
                                                     Transfer->IsoBlockPtr);
            }
            else
            {
                MpStatus = Packet->SubmitTransfer(FdoExtension->MiniPortExt,
                                                  Endpoint + 1,
                                                  &Transfer->TransferParameters,
                                                  Transfer->MiniportTransfer,
                                                  &Transfer->SgList);
            }

            KeReleaseSpinLock(&FdoExtension->MiniportSpinLock, OldIrql);

            if (Endpoint &&
                (Endpoint->EndpointProperties.TransferType == USBPORT_TRANSFER_TYPE_BULK ||
                 Transfer->TransferParameters.TransferBufferLength >= 64))
            {
                DPRINT("USBPORT_DmaEndpointActive: SubmitTransfer MpStatus=%lu\n",
                       MpStatus);
            }

            if (MpStatus)
            {
                USBD_STATUS USBDStatus;

                USBDStatus = USBPORT_MpStatusToUsbdStatus(MpStatus);

                DPRINT1("USBPORT_DmaEndpointActive: SubmitTransfer FAILED MpStatus=%lu USBDStatus=%x\n",
                        MpStatus,
                        USBDStatus);

                if (Transfer->Flags & TRANSFER_FLAG_ISO)
                    USBPORT_FailIsoTransfer(Transfer, USBDStatus, TRUE);
                else
                    USBPORT_QueueDoneTransfer(Transfer, USBDStatus, TRUE);

                return USBPORT_ENDPOINT_ACTIVE;
            }

            Transfer->Flags |= TRANSFER_FLAG_SUBMITED;
            KeQuerySystemTime(&Transfer->Time);

            TimeOut.QuadPart = 10000 * Transfer->TimeOut;
            Transfer->Time.QuadPart += TimeOut.QuadPart;

            return USBPORT_ENDPOINT_ACTIVE;
        }

        Entry = Transfer->TransferLink.Flink;
    }

    return USBPORT_ENDPOINT_ACTIVE;
}

VOID
NTAPI
USBPORT_DmaEndpointWorker(IN PUSBPORT_ENDPOINT Endpoint)
{
    PDEVICE_OBJECT FdoDevice;
    ULONG PrevState;
    ULONG EndpointState;
    BOOLEAN IsPaused = FALSE;
    BOOLEAN NeedRetry = FALSE;

    DPRINT_CORE("USBPORT_DmaEndpointWorker ... \n");

    FdoDevice = Endpoint->FdoDevice;

    KeAcquireSpinLock(&Endpoint->EndpointSpinLock, &Endpoint->EndpointOldIrql);

    PrevState = USBPORT_GetEndpointState(Endpoint);

    if (PrevState == USBPORT_ENDPOINT_PAUSED)
    {
        EndpointState = USBPORT_DmaEndpointPaused(FdoDevice, Endpoint);
    }
    else if (PrevState == USBPORT_ENDPOINT_ACTIVE)
    {
        EndpointState = USBPORT_DmaEndpointActive(FdoDevice, Endpoint);
    }
    else
    {
        DPRINT_CORE("USBPORT_DmaEndpointWorker: PrevState=%x (unknown/transitioning)\n",
                    PrevState);

        /*
         * State is UNKNOWN (transitioning between states) or unexpected.
         * If the endpoint has NUKE or ABORTING flags set, we must still
         * try to drain transfers. Process them as if we were in PAUSED state
         * to avoid indefinite stalls during device disconnect.
         */
        if (Endpoint->Flags & (ENDPOINT_FLAG_NUKE | ENDPOINT_FLAG_ABORTING))
        {
            DPRINT1("USBPORT_DmaEndpointWorker: state=%x but NUKE/ABORTING set, forcing PAUSED processing\n",
                    PrevState);
            EndpointState = USBPORT_DmaEndpointPaused(FdoDevice, Endpoint);
        }
        else
        {
            EndpointState = USBPORT_ENDPOINT_UNKNOWN;
            /*
             * If state is unknown and there are pending/active transfers,
             * schedule a retry to prevent stuck endpoints.
             */
            if (!IsListEmpty(&Endpoint->TransferList) ||
                !IsListEmpty(&Endpoint->PendingTransferList) ||
                !IsListEmpty(&Endpoint->CancelList))
            {
                NeedRetry = TRUE;
            }
        }
    }

    KeReleaseSpinLock(&Endpoint->EndpointSpinLock, Endpoint->EndpointOldIrql);

    USBPORT_FlushCancelList(Endpoint);

    KeAcquireSpinLock(&Endpoint->EndpointSpinLock, &Endpoint->EndpointOldIrql);

    if (EndpointState == PrevState)
    {
        if (EndpointState == USBPORT_ENDPOINT_PAUSED)
        {
            IsPaused = TRUE;
        }
    }
    else
    {
        /*
         * When NUKE or ABORTING is set and DmaEndpointPaused/Active wants
         * to transition the endpoint back to ACTIVE, suppress the transition.
         * The endpoint is being torn down by ClosePipe which will set REMOVE.
         *
         * Calling SetEndpointState(ACTIVE) here races with ClosePipe on
         * another CPU: SetEndpointState releases StateChangeSpinLock then
         * writes StateNext without holding any lock. ClosePipe's wait loop
         * can observe the intermediate state (StateLast==StateNext==PAUSED),
         * break out, and set REMOVE. Then our deferred StateNext=ACTIVE
         * write overwrites the REMOVE, corrupting the state machine.
         *
         * By staying in the current state (PAUSED), we let ClosePipe cleanly
         * transition to REMOVE without interference.
         */
        if ((Endpoint->Flags & (ENDPOINT_FLAG_NUKE | ENDPOINT_FLAG_ABORTING)) &&
            EndpointState == USBPORT_ENDPOINT_ACTIVE)
        {
            DPRINT1("USBPORT_DmaEndpointWorker: suppressing ACTIVE transition (NUKE/ABORTING), staying in state %lu\n",
                    PrevState);
            /* Stay in current state; signal worker so ClosePipe's REMOVE
             * transition can proceed without waiting for SOF. */
            IsPaused = TRUE;
        }
        else
        {
            USBPORT_SetEndpointState(Endpoint, EndpointState);
        }
    }

    KeReleaseSpinLock(&Endpoint->EndpointSpinLock, Endpoint->EndpointOldIrql);

    if (IsPaused || NeedRetry)
    {
       USBPORT_InvalidateEndpointHandler(FdoDevice,
                                         Endpoint,
                                         INVALIDATE_ENDPOINT_WORKER_THREAD);
    }

    DPRINT_CORE("USBPORT_DmaEndpointWorker exit \n");
}

BOOLEAN
NTAPI
USBPORT_EndpointWorker(IN PUSBPORT_ENDPOINT Endpoint,
                       IN BOOLEAN LockNotChecked)
{
    PDEVICE_OBJECT FdoDevice;
    PUSBPORT_DEVICE_EXTENSION FdoExtension;
    PUSBPORT_REGISTRATION_PACKET Packet;
    ULONG EndpointState;

    DPRINT_CORE("USBPORT_EndpointWorker: Endpoint - %p, LockNotChecked - %x\n",
           Endpoint,
           LockNotChecked);

    FdoDevice = Endpoint->FdoDevice;
    FdoExtension = FdoDevice->DeviceExtension;
    Packet = &FdoExtension->MiniPortInterface->Packet;

    if (LockNotChecked == FALSE)
    {
        if (InterlockedIncrement(&Endpoint->LockCounter))
        {
            InterlockedDecrement(&Endpoint->LockCounter);
            DPRINT_CORE("USBPORT_EndpointWorker: LockCounter > 0\n");
            return TRUE;
        }
    }

    ASSERT(KeGetCurrentIrql() == DISPATCH_LEVEL);

    KeAcquireSpinLockAtDpcLevel(&Endpoint->EndpointSpinLock);

    if (USBPORT_GetEndpointState(Endpoint) == USBPORT_ENDPOINT_CLOSED)
    {
        KeReleaseSpinLockFromDpcLevel(&Endpoint->EndpointSpinLock);
        InterlockedDecrement(&Endpoint->LockCounter);
        DPRINT_CORE("USBPORT_EndpointWorker: State == USBPORT_ENDPOINT_CLOSED. return FALSE\n");
        return FALSE;
    }

    if ((Endpoint->Flags & (ENDPOINT_FLAG_ROOTHUB_EP0 | ENDPOINT_FLAG_NUKE)) == 0)
    {
        KeAcquireSpinLockAtDpcLevel(&FdoExtension->MiniportSpinLock);
        Packet->PollEndpoint(FdoExtension->MiniPortExt, Endpoint + 1);
        KeReleaseSpinLockFromDpcLevel(&FdoExtension->MiniportSpinLock);
    }

    EndpointState = USBPORT_GetEndpointState(Endpoint);

    if (EndpointState == USBPORT_ENDPOINT_REMOVE)
    {
        KeAcquireSpinLockAtDpcLevel(&Endpoint->StateChangeSpinLock);
        Endpoint->StateLast = USBPORT_ENDPOINT_CLOSED;
        Endpoint->StateNext = USBPORT_ENDPOINT_CLOSED;
        KeReleaseSpinLockFromDpcLevel(&Endpoint->StateChangeSpinLock);

        KeReleaseSpinLockFromDpcLevel(&Endpoint->EndpointSpinLock);

        KeAcquireSpinLockAtDpcLevel(&FdoExtension->EndpointListSpinLock);

        ExInterlockedInsertTailList(&FdoExtension->EndpointClosedList,
                                    &Endpoint->CloseLink,
                                    &FdoExtension->EndpointClosedSpinLock);

        KeReleaseSpinLockFromDpcLevel(&FdoExtension->EndpointListSpinLock);

        InterlockedDecrement(&Endpoint->LockCounter);
        DPRINT_CORE("USBPORT_EndpointWorker: State == USBPORT_ENDPOINT_REMOVE. return FALSE\n");
        return FALSE;
    }

    if (!IsListEmpty(&Endpoint->PendingTransferList) ||
        !IsListEmpty(&Endpoint->TransferList) ||
        !IsListEmpty(&Endpoint->CancelList))
    {
        KeReleaseSpinLockFromDpcLevel(&Endpoint->EndpointSpinLock);

        EndpointState = USBPORT_GetEndpointState(Endpoint);

        KeAcquireSpinLockAtDpcLevel(&Endpoint->StateChangeSpinLock);
        if (EndpointState == Endpoint->StateNext)
        {
            KeReleaseSpinLockFromDpcLevel(&Endpoint->StateChangeSpinLock);

            if (Endpoint->EndpointWorker)
            {
                USBPORT_DmaEndpointWorker(Endpoint);
            }
            else
            {
                USBPORT_RootHubEndpointWorker(Endpoint);
            }

            USBPORT_FlushAbortList(Endpoint);

            InterlockedDecrement(&Endpoint->LockCounter);
            DPRINT_CORE("USBPORT_EndpointWorker: return FALSE\n");
            return FALSE;
        }

        /*
         * State mismatch: a state transition is in progress (StateLast !=
         * StateNext). During shutdown the SOF interrupt that normally
         * completes the transition may no longer fire, leaving the endpoint
         * stuck. If the endpoint has NUKE or ABORTING flags, or if
         * StateLast is already in a terminal state (REMOVE/CLOSED), force
         * the transition to complete so transfers can be drained instead
         * of deferring indefinitely and wasting CPU cycles.
         */
        if (Endpoint->StateLast >= USBPORT_ENDPOINT_REMOVE)
        {
            /* StateLast is terminal (REMOVE or CLOSED) but StateNext is
             * requesting a non-terminal transition (e.g. PAUSED). The
             * endpoint is already being torn down. Cancel the pending
             * transition by setting StateNext to match StateLast. */
            DPRINT1("USBPORT_EndpointWorker: Endpoint=%p terminal StateLast=%lu overrides StateNext=%lu\n",
                    Endpoint, Endpoint->StateLast, Endpoint->StateNext);
            Endpoint->StateNext = Endpoint->StateLast;
            KeReleaseSpinLockFromDpcLevel(&Endpoint->StateChangeSpinLock);
            InterlockedDecrement(&Endpoint->LockCounter);
            return FALSE;
        }

        if (Endpoint->Flags & (ENDPOINT_FLAG_NUKE | ENDPOINT_FLAG_ABORTING))
        {
            /* Force the transition to complete so the DMA worker can drain
             * outstanding transfers during device removal/shutdown. */
            DPRINT1("USBPORT_EndpointWorker: Endpoint=%p forcing transition StateLast=%lu->StateNext=%lu (NUKE/ABORTING)\n",
                    Endpoint, Endpoint->StateLast, Endpoint->StateNext);
            Endpoint->StateLast = Endpoint->StateNext;
            KeReleaseSpinLockFromDpcLevel(&Endpoint->StateChangeSpinLock);

            if (Endpoint->EndpointWorker)
            {
                USBPORT_DmaEndpointWorker(Endpoint);
            }
            else
            {
                USBPORT_RootHubEndpointWorker(Endpoint);
            }

            USBPORT_FlushAbortList(Endpoint);
            InterlockedDecrement(&Endpoint->LockCounter);
            return FALSE;
        }

        /* Normal in-progress transition; defer and retry */
        DPRINT("USBPORT_EndpointWorker: Endpoint=%p state mismatch State=%lu StateNext=%lu - deferring\n",
                Endpoint, EndpointState, Endpoint->StateNext);
        KeReleaseSpinLockFromDpcLevel(&Endpoint->StateChangeSpinLock);
        InterlockedDecrement(&Endpoint->LockCounter);

        DPRINT_CORE("USBPORT_EndpointWorker: return TRUE\n");
        return TRUE;
    }

    KeReleaseSpinLockFromDpcLevel(&Endpoint->EndpointSpinLock);

    USBPORT_FlushAbortList(Endpoint);

    InterlockedDecrement(&Endpoint->LockCounter);
    DPRINT_CORE("USBPORT_EndpointWorker: return FALSE\n");
    return FALSE;
}
