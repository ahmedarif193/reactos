/*
 * ReactOS Realtek 8139 Driver
 *
 * Copyright (C) 2013 Cameron Gutman
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 */

#include "nic.h"

#define NDEBUG
#include <debug.h>

ULONG DebugTraceLevel = MIN_TRACE;

static
NDIS_STATUS
CopyPacketToTransmitBuffer(
    IN PNDIS_PACKET Packet,
    OUT PUCHAR Destination,
    OUT PULONG TransmitLength)
{
    PNDIS_BUFFER CurrentBuffer;
    PVOID Source;
    UINT CurrentLength;
    UINT PacketLength;
    ULONG BytesCopied = 0;

    NdisGetFirstBufferFromPacketSafe(Packet, &CurrentBuffer, &Source, &CurrentLength, &PacketLength, NormalPagePriority);
    if (CurrentBuffer == NULL || PacketLength > MAXIMUM_FRAME_SIZE)
        return NDIS_STATUS_INVALID_LENGTH;

    while (CurrentBuffer != NULL)
    {
        if (Source == NULL && CurrentLength != 0)
            return NDIS_STATUS_RESOURCES;
        if (BytesCopied > PacketLength || CurrentLength > PacketLength - BytesCopied)
            return NDIS_STATUS_INVALID_LENGTH;

        if (CurrentLength != 0)
            RtlCopyMemory(Destination + BytesCopied, Source, CurrentLength);
        BytesCopied += CurrentLength;

        NdisGetNextBuffer(CurrentBuffer, &CurrentBuffer);
        if (CurrentBuffer != NULL)
            NdisQueryBufferSafe(CurrentBuffer, &Source, &CurrentLength, NormalPagePriority);
    }

    if (BytesCopied != PacketLength)
        return NDIS_STATUS_INVALID_LENGTH;

    *TransmitLength = max(PacketLength, MINIMUM_FRAME_SIZE);
    if (PacketLength < *TransmitLength)
        RtlFillMemory(Destination + PacketLength, *TransmitLength - PacketLength, 0x00);

    return NDIS_STATUS_SUCCESS;
}

NDIS_STATUS
NTAPI
MiniportReset (
    OUT PBOOLEAN AddressingReset,
    IN NDIS_HANDLE MiniportAdapterContext
    )
{
    *AddressingReset = FALSE;
    return NDIS_STATUS_FAILURE;
}

NDIS_STATUS
NTAPI
MiniportSend (
    IN NDIS_HANDLE MiniportAdapterContext,
    IN PNDIS_PACKET Packet,
    IN UINT Flags
    )
{
    PRTL_ADAPTER adapter = (PRTL_ADAPTER)MiniportAdapterContext;
    NDIS_STATUS status;
    ULONG transmitLength;
    ULONG transmitBuffer;
    PUCHAR transmitBufferVa;

    NdisAcquireSpinLock(&adapter->Lock);

    if (adapter->TxFull)
    {
        NDIS_DbgPrint(MIN_TRACE, ("All TX descriptors are full\n"));
        NdisReleaseSpinLock(&adapter->Lock);
        return NDIS_STATUS_RESOURCES;
    }

    NDIS_DbgPrint(MAX_TRACE, ("Sending packet on TX desc %d\n", adapter->CurrentTxDesc));

    //
    // Keep the packet in descriptor-owned DMA memory until the NIC is done.
    // Protocol buffers may be fragmented, above 4 GB, or reused as soon as
    // this serialized send handler returns success.
    //
    transmitBufferVa = adapter->TransmitBuffers + (TRANSMIT_BUFFER_STRIDE * adapter->CurrentTxDesc);
    status = CopyPacketToTransmitBuffer(Packet, transmitBufferVa, &transmitLength);
    if (status != NDIS_STATUS_SUCCESS)
    {
        NDIS_DbgPrint(MIN_TRACE, ("Unable to copy packet into transmit buffer (0x%x)\n", status));
        NdisReleaseSpinLock(&adapter->Lock);
        return status;
    }
    transmitBuffer = adapter->TransmitBuffersPa.LowPart + (TRANSMIT_BUFFER_STRIDE * adapter->CurrentTxDesc);

    NDIS_DbgPrint(MAX_TRACE, ("Sending %lu byte packet\n", transmitLength));

    status = NICTransmitPacket(adapter, adapter->CurrentTxDesc, transmitBuffer, transmitLength);
    if (status != NDIS_STATUS_SUCCESS)
    {
        NDIS_DbgPrint(MIN_TRACE, ("Transmit packet failed\n"));
        NdisReleaseSpinLock(&adapter->Lock);
        return status;
    }

    adapter->CurrentTxDesc++;
    adapter->CurrentTxDesc %= TX_DESC_COUNT;

    if (adapter->CurrentTxDesc == adapter->DirtyTxDesc)
    {
        NDIS_DbgPrint(MID_TRACE, ("All TX descriptors are full now\n"));
        adapter->TxFull = TRUE;
    }

    NdisReleaseSpinLock(&adapter->Lock);

    return NDIS_STATUS_SUCCESS;
}

VOID
NTAPI
MiniportHalt (
    IN NDIS_HANDLE MiniportAdapterContext
    )
{
    PRTL_ADAPTER adapter = (PRTL_ADAPTER)MiniportAdapterContext;
    NDIS_STATUS stopStatus;
    BOOLEAN freeDmaBuffers = TRUE;

    ASSERT(adapter != NULL);

    //
    // Prevent new interrupt work and drain queued DPCs before resetting the
    // bus-master engine. A successful reset is the hardware quiescence point.
    //
    if (adapter->IoBase != NULL)
        NICDisableInterrupts(adapter);

    if (adapter->InterruptRegistered != FALSE)
    {
        NdisMDeregisterInterrupt(&adapter->Interrupt);
        adapter->InterruptRegistered = FALSE;
        KeFlushQueuedDpcs();
    }

    if (adapter->IoBase != NULL && adapter->DmaActive)
    {
        stopStatus = NICSoftReset(adapter);
        if (stopStatus == NDIS_STATUS_SUCCESS)
            adapter->DmaActive = FALSE;
        else
            freeDmaBuffers = FALSE;
    }

    if (!freeDmaBuffers)
        NDIS_DbgPrint(MIN_TRACE, ("Unable to stop DMA; preserving common buffers\n"));

    if (adapter->ReceiveBuffer != NULL && freeDmaBuffers)
    {
        //
        // Disassociate our shared buffer before freeing it to avoid
        // NIC-induced memory corruption
        //
        if (adapter->IoBase != NULL)
            NICRemoveReceiveBuffer(adapter);

        NdisMFreeSharedMemory(adapter->MiniportAdapterHandle, adapter->ReceiveBufferLength, FALSE, adapter->ReceiveBuffer, adapter->ReceiveBufferPa);
    }

    if (adapter->TransmitBuffers != NULL && freeDmaBuffers)
    {
        NdisMFreeSharedMemory(adapter->MiniportAdapterHandle, TRANSMIT_BUFFER_LENGTH, FALSE, adapter->TransmitBuffers, adapter->TransmitBuffersPa);
    }

    if (adapter->IoBase != NULL)
    {
        //
        // Unregister the IO range
        //
        NdisMDeregisterIoPortRange(adapter->MiniportAdapterHandle,
                                   adapter->IoRangeStart,
                                   adapter->IoRangeLength,
                                   adapter->IoBase);
    }

    //
    // Destroy the adapter context
    //
    NdisFreeMemory(adapter, sizeof(*adapter), 0);
}

NDIS_STATUS
NTAPI
MiniportInitialize (
    OUT PNDIS_STATUS OpenErrorStatus,
    OUT PUINT SelectedMediumIndex,
    IN PNDIS_MEDIUM MediumArray,
    IN UINT MediumArraySize,
    IN NDIS_HANDLE MiniportAdapterHandle,
    IN NDIS_HANDLE WrapperConfigurationContext
    )
{
    PRTL_ADAPTER adapter;
    NDIS_STATUS status;
    UINT i;
    PNDIS_RESOURCE_LIST resourceList;
    UINT resourceListSize;

    //
    // Make sure the medium is supported
    //
    for (i = 0; i < MediumArraySize; i++)
    {
        if (MediumArray[i] == NdisMedium802_3)
        {
            *SelectedMediumIndex = i;
            break;
        }
    }

    if (i == MediumArraySize)
    {
        NDIS_DbgPrint(MIN_TRACE, ("802.3 medium was not found in the medium array\n"));
        return NDIS_STATUS_UNSUPPORTED_MEDIA;
    }

    //
    // Allocate our adapter context
    //
    status = NdisAllocateMemoryWithTag((PVOID*)&adapter,
                                       sizeof(*adapter),
                                       ADAPTER_TAG);
    if (status != NDIS_STATUS_SUCCESS)
    {
        NDIS_DbgPrint(MIN_TRACE, ("Failed to allocate adapter context\n"));
        return NDIS_STATUS_RESOURCES;
    }

    RtlZeroMemory(adapter, sizeof(*adapter));
    adapter->MiniportAdapterHandle = MiniportAdapterHandle;
    NdisAllocateSpinLock(&adapter->Lock);

    //
    // Notify NDIS of some characteristics of our NIC
    //
    NdisMSetAttributesEx(MiniportAdapterHandle,
                         adapter,
                         0,
                         NDIS_ATTRIBUTE_BUS_MASTER,
                         NdisInterfacePci);

    //
    // Get our resources for IRQ and IO base information
    //
    resourceList = NULL;
    resourceListSize = 0;
    NdisMQueryAdapterResources(&status,
                               WrapperConfigurationContext,
                               resourceList,
                               &resourceListSize);
    if (status != NDIS_STATUS_RESOURCES)
    {
        NDIS_DbgPrint(MIN_TRACE, ("Unexpected failure of NdisMQueryAdapterResources #1\n"));
        status = NDIS_STATUS_FAILURE;
        goto Cleanup;
    }

    status = NdisAllocateMemoryWithTag((PVOID*)&resourceList,
                                resourceListSize,
                                RESOURCE_LIST_TAG);
    if (status != NDIS_STATUS_SUCCESS)
    {
        NDIS_DbgPrint(MIN_TRACE, ("Failed to allocate resource list\n"));
        goto Cleanup;
    }

    NdisMQueryAdapterResources(&status,
                               WrapperConfigurationContext,
                               resourceList,
                               &resourceListSize);
    if (status != NDIS_STATUS_SUCCESS)
    {
        NDIS_DbgPrint(MIN_TRACE, ("Unexpected failure of NdisMQueryAdapterResources #2\n"));
        goto Cleanup;
    }

    ASSERT(resourceList->Version == 1);
    ASSERT(resourceList->Revision == 1);

    for (i = 0; i < resourceList->Count; i++)
    {
        switch (resourceList->PartialDescriptors[i].Type)
        {
            case CmResourceTypePort:
                ASSERT(adapter->IoRangeStart == 0);

                ASSERT(resourceList->PartialDescriptors[i].u.Port.Start.HighPart == 0);

                adapter->IoRangeStart = resourceList->PartialDescriptors[i].u.Port.Start.LowPart;
                adapter->IoRangeLength = resourceList->PartialDescriptors[i].u.Port.Length;

                NDIS_DbgPrint(MID_TRACE, ("I/O port range is %p to %p\n",
                              adapter->IoRangeStart, adapter->IoRangeStart + adapter->IoRangeLength));
                break;

            case CmResourceTypeInterrupt:
                ASSERT(adapter->InterruptVector == 0);
                ASSERT(adapter->InterruptLevel == 0);

                adapter->InterruptVector = resourceList->PartialDescriptors[i].u.Interrupt.Vector;
                adapter->InterruptLevel = resourceList->PartialDescriptors[i].u.Interrupt.Level;
                adapter->InterruptShared = (resourceList->PartialDescriptors[i].ShareDisposition == CmResourceShareShared);
                adapter->InterruptFlags = resourceList->PartialDescriptors[i].Flags;

                NDIS_DbgPrint(MID_TRACE, ("IRQ vector is %d\n", adapter->InterruptVector));
                break;

            default:
                NDIS_DbgPrint(MIN_TRACE, ("Unrecognized resource type: 0x%x\n", resourceList->PartialDescriptors[i].Type));
                break;
        }
    }

    NdisFreeMemory(resourceList, resourceListSize, 0);
    resourceList = NULL;

    if (adapter->IoRangeStart == 0 || adapter->InterruptVector == 0)
    {
        NDIS_DbgPrint(MIN_TRACE, ("Adapter didn't receive enough resources\n"));
        status = NDIS_STATUS_RESOURCES;
        goto Cleanup;
    }

    //
    // Allocate the DMA resources
    //
    status = NdisMInitializeScatterGatherDma(MiniportAdapterHandle,
                                             FALSE, // RTL8139 only supports 32-bit addresses
                                             MAXIMUM_FRAME_SIZE);
    if (status != NDIS_STATUS_SUCCESS)
    {
        NDIS_DbgPrint(MIN_TRACE, ("Unable to configure DMA\n"));
        goto Cleanup;
    }

    adapter->ReceiveBufferLength = FULL_RECEIVE_BUFFER_SIZE;
    NdisMAllocateSharedMemory(MiniportAdapterHandle,
                              adapter->ReceiveBufferLength,
                              FALSE,
                              (PVOID*)&adapter->ReceiveBuffer,
                              &adapter->ReceiveBufferPa);
    if (adapter->ReceiveBuffer == NULL)
    {
        NDIS_DbgPrint(MIN_TRACE, ("Unable to allocate receive buffer\n"));
        status = NDIS_STATUS_RESOURCES;
        goto Cleanup;
    }
    if (adapter->ReceiveBufferPa.HighPart != 0 ||
        adapter->ReceiveBufferPa.LowPart > MAXULONG - (adapter->ReceiveBufferLength - 1))
    {
        NDIS_DbgPrint(MIN_TRACE, ("Receive buffer is outside the RTL8139 DMA range: 0x%I64x\n", adapter->ReceiveBufferPa.QuadPart));
        status = NDIS_STATUS_RESOURCES;
        goto Cleanup;
    }

    NdisMAllocateSharedMemory(MiniportAdapterHandle, TRANSMIT_BUFFER_LENGTH, FALSE, (PVOID*)&adapter->TransmitBuffers, &adapter->TransmitBuffersPa);
    if (adapter->TransmitBuffers == NULL)
    {
        NDIS_DbgPrint(MIN_TRACE, ("Unable to allocate transmit buffers\n"));
        status = NDIS_STATUS_RESOURCES;
        goto Cleanup;
    }
    if (adapter->TransmitBuffersPa.HighPart != 0 ||
        adapter->TransmitBuffersPa.LowPart > MAXULONG - (TRANSMIT_BUFFER_LENGTH - 1) ||
        (adapter->TransmitBuffersPa.LowPart & (sizeof(ULONG) - 1)) != 0)
    {
        NDIS_DbgPrint(MIN_TRACE, ("Transmit buffers are outside the RTL8139 DMA range: 0x%I64x\n", adapter->TransmitBuffersPa.QuadPart));
        status = NDIS_STATUS_RESOURCES;
        goto Cleanup;
    }

    //
    // Register the I/O port range and configure the NIC
    //
    status = NdisMRegisterIoPortRange((PVOID*)&adapter->IoBase,
                                      MiniportAdapterHandle,
                                      adapter->IoRangeStart,
                                      adapter->IoRangeLength);
    if (status != NDIS_STATUS_SUCCESS)
    {
        NDIS_DbgPrint(MIN_TRACE, ("Unable to register IO port range (0x%x)\n", status));
        goto Cleanup;
    }

    //
    // Adapter setup
    //
    status = NICPowerOn(adapter);
    if (status != NDIS_STATUS_SUCCESS)
    {
        NDIS_DbgPrint(MIN_TRACE, ("Unable to power on NIC (0x%x)\n", status));
        goto Cleanup;
    }

    status = NICSoftReset(adapter);
    if (status != NDIS_STATUS_SUCCESS)
    {
        NDIS_DbgPrint(MIN_TRACE, ("Unable to reset the NIC (0x%x)\n", status));
        goto Cleanup;
    }

    status = NICGetPermanentMacAddress(adapter, adapter->PermanentMacAddress);
    if (status != NDIS_STATUS_SUCCESS)
    {
        NDIS_DbgPrint(MIN_TRACE, ("Unable to get the fixed MAC address (0x%x)\n", status));
        goto Cleanup;
    }

    RtlCopyMemory(adapter->CurrentMacAddress, adapter->PermanentMacAddress, IEEE_802_ADDR_LENGTH);

    //
    // Update link state and speed
    //
    NICUpdateLinkStatus(adapter);

    status = NICRegisterReceiveBuffer(adapter);
    if (status != NDIS_STATUS_SUCCESS)
    {
        NDIS_DbgPrint(MIN_TRACE, ("Unable to setup receive buffer (0x%x)\n", status));
        goto Cleanup;
    }

    //
    // We're ready to handle interrupts now
    //
    status = NdisMRegisterInterrupt(&adapter->Interrupt,
                                    MiniportAdapterHandle,
                                    adapter->InterruptVector,
                                    adapter->InterruptLevel,
                                    TRUE, // We always want ISR calls
                                    adapter->InterruptShared,
                                    (adapter->InterruptFlags & CM_RESOURCE_INTERRUPT_LATCHED) ?
                                        NdisInterruptLatched : NdisInterruptLevelSensitive);
    if (status != NDIS_STATUS_SUCCESS)
    {
        NDIS_DbgPrint(MIN_TRACE, ("Unable to register interrupt (0x%x)\n", status));
        goto Cleanup;
    }

    adapter->InterruptRegistered = TRUE;

    //
    // Enable interrupts on the NIC
    //
    adapter->InterruptMask = DEFAULT_INTERRUPT_MASK;
    status = NICApplyInterruptMask(adapter);
    if (status != NDIS_STATUS_SUCCESS)
    {
        NDIS_DbgPrint(MIN_TRACE, ("Unable to apply interrupt mask (0x%x)\n", status));
        goto Cleanup;
    }

    //
    // Turn on TX and RX now
    //
    adapter->DmaActive = TRUE;
    status = NICEnableTxRx(adapter);
    if (status != NDIS_STATUS_SUCCESS)
    {
        NDIS_DbgPrint(MIN_TRACE, ("Unable to enable TX and RX (0x%x)\n", status));
        goto Cleanup;
    }

    return NDIS_STATUS_SUCCESS;

Cleanup:
    if (resourceList != NULL)
    {
        NdisFreeMemory(resourceList, resourceListSize, 0);
    }
    if (adapter != NULL)
    {
        MiniportHalt(adapter);
    }

    return status;
}

NTSTATUS
NTAPI
DriverEntry (
    IN PDRIVER_OBJECT DriverObject,
    IN PUNICODE_STRING RegistryPath
    )
{
    NDIS_HANDLE wrapperHandle;
    NDIS_MINIPORT_CHARACTERISTICS characteristics;
    NDIS_STATUS status;

    RtlZeroMemory(&characteristics, sizeof(characteristics));
    characteristics.MajorNdisVersion = NDIS_MINIPORT_MAJOR_VERSION;
    characteristics.MinorNdisVersion = NDIS_MINIPORT_MINOR_VERSION;
    characteristics.CheckForHangHandler = NULL;
    characteristics.DisableInterruptHandler = NULL;
    characteristics.EnableInterruptHandler = NULL;
    characteristics.HaltHandler = MiniportHalt;
    characteristics.HandleInterruptHandler = MiniportHandleInterrupt;
    characteristics.InitializeHandler = MiniportInitialize;
    characteristics.ISRHandler = MiniportISR;
    characteristics.QueryInformationHandler = MiniportQueryInformation;
    characteristics.ReconfigureHandler = NULL;
    characteristics.ResetHandler = MiniportReset;
    characteristics.SendHandler = MiniportSend;
    characteristics.SetInformationHandler = MiniportSetInformation;
    characteristics.TransferDataHandler = NULL;
    characteristics.ReturnPacketHandler = NULL;
    characteristics.SendPacketsHandler = NULL;
    characteristics.AllocateCompleteHandler = NULL;

    NdisMInitializeWrapper(&wrapperHandle, DriverObject, RegistryPath, NULL);
    if (!wrapperHandle)
    {
        return NDIS_STATUS_FAILURE;
    }

    status = NdisMRegisterMiniport(wrapperHandle, &characteristics, sizeof(characteristics));
    if (status != NDIS_STATUS_SUCCESS)
    {
        NdisTerminateWrapper(wrapperHandle, 0);
        return NDIS_STATUS_FAILURE;
    }

    return NDIS_STATUS_SUCCESS;
}
