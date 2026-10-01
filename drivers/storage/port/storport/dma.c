/*
 * PROJECT:     LiberNT Storport Driver
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Bus-master DMA buffers owned by the adapter
 */

#include "precomp.h"

typedef struct _PORT_DMA_BUFFER
{
    LIST_ENTRY Entry;
    PVOID VirtualAddress;
    PHYSICAL_ADDRESS LogicalAddress;
    ULONG Length;
    BOOLEAN CacheEnabled;
} PORT_DMA_BUFFER, *PPORT_DMA_BUFFER;

static KIRQL
PortLockDmaBuffers(_In_ PFDO_DEVICE_EXTENSION FdoExtension)
{
    KIRQL OldIrql, Level = KeGetCurrentIrql();

    /* Miniports can resolve command buffers from their ISR. Exclude that
     * ISR when a lower-IRQL allocation updates the same list. */
    Level = max(Level, DISPATCH_LEVEL);
    Level = max(Level, FdoExtension->InterruptIrql);
    KeRaiseIrql(Level, &OldIrql);
    KeAcquireSpinLockAtDpcLevel(&FdoExtension->DmaBufferLock);
    return OldIrql;
}

NTSTATUS
PortInitializeDma(
    _In_ PFDO_DEVICE_EXTENSION FdoExtension,
    _In_ PPORT_CONFIGURATION_INFORMATION Config)
{
    DEVICE_DESCRIPTION Description = {0};
    ULONG MapRegisters;

    if (FdoExtension->DmaAdapter)
        return STATUS_SUCCESS;

    Description.Version = DEVICE_DESCRIPTION_VERSION2;
    Description.Master = TRUE;
    Description.ScatterGather = TRUE;
    Description.Dma32BitAddresses = TRUE;
    Description.Dma64BitAddresses =
        Config->Dma64BitAddresses == SCSI_DMA64_MINIPORT_SUPPORTED;
    Description.BusNumber = Config->SystemIoBusNumber;
    Description.InterfaceType = Config->AdapterInterfaceType;
    Description.MaximumLength = Config->MaximumTransferLength;
    Description.DmaWidth = Width32Bits;
    Description.DmaSpeed = Compatible;

    FdoExtension->DmaAdapter = IoGetDmaAdapter(FdoExtension->PhysicalDevice,
                                              &Description, &MapRegisters);
    if (!FdoExtension->DmaAdapter)
        return STATUS_NOT_SUPPORTED;

    InitializeListHead(&FdoExtension->DmaBuffers);
    KeInitializeSpinLock(&FdoExtension->DmaBufferLock);
    return STATUS_SUCCESS;
}

PVOID
PortAllocateDmaBuffer(
    _In_ PFDO_DEVICE_EXTENSION FdoExtension,
    _In_ ULONG Length,
    _In_ BOOLEAN CacheEnabled,
    _Out_ PPHYSICAL_ADDRESS LogicalAddress)
{
    PPORT_DMA_BUFFER Buffer;
    PDMA_ADAPTER Adapter = FdoExtension->DmaAdapter;
    KIRQL OldIrql;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    if (!Adapter || !Length || KeGetCurrentIrql() != PASSIVE_LEVEL)
        return NULL;
    Buffer = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Buffer), TAG_DMA_BUFFER);
    if (!Buffer)
        return NULL;

    /* The HAL supplies a device-visible address and the cache policy. CPU
     * physical addresses need not be usable by a PCI bus master. */
    Buffer->VirtualAddress = Adapter->DmaOperations->AllocateCommonBuffer(
        Adapter, Length, &Buffer->LogicalAddress, CacheEnabled);
    if (!Buffer->VirtualAddress)
    {
        ExFreePoolWithTag(Buffer, TAG_DMA_BUFFER);
        return NULL;
    }
    Buffer->Length = Length;
    Buffer->CacheEnabled = CacheEnabled;
    *LogicalAddress = Buffer->LogicalAddress;
    OldIrql = PortLockDmaBuffers(FdoExtension);
    InsertTailList(&FdoExtension->DmaBuffers, &Buffer->Entry);
    KeReleaseSpinLock(&FdoExtension->DmaBufferLock, OldIrql);
    return Buffer->VirtualAddress;
}

VOID
PortFreeDmaBuffer(
    _In_ PFDO_DEVICE_EXTENSION FdoExtension,
    _In_ PVOID Address)
{
    PLIST_ENTRY Entry;
    PPORT_DMA_BUFFER Buffer = NULL;
    PDMA_ADAPTER Adapter = FdoExtension->DmaAdapter;
    KIRQL OldIrql;

    ASSERT(KeGetCurrentIrql() == PASSIVE_LEVEL);
    OldIrql = PortLockDmaBuffers(FdoExtension);
    for (Entry = FdoExtension->DmaBuffers.Flink;
         Entry != &FdoExtension->DmaBuffers; Entry = Entry->Flink)
    {
        PPORT_DMA_BUFFER Candidate = CONTAINING_RECORD(Entry, PORT_DMA_BUFFER, Entry);
        if (Candidate->VirtualAddress == Address)
        {
            Buffer = Candidate;
            RemoveEntryList(Entry);
            break;
        }
    }
    KeReleaseSpinLock(&FdoExtension->DmaBufferLock, OldIrql);
    ASSERT(Buffer != NULL);
    if (!Buffer)
        return;
    Adapter->DmaOperations->FreeCommonBuffer(Adapter, Buffer->Length,
        Buffer->LogicalAddress, Buffer->VirtualAddress, Buffer->CacheEnabled);
    ExFreePoolWithTag(Buffer, TAG_DMA_BUFFER);
}

BOOLEAN
PortGetDmaAddress(
    _In_ PFDO_DEVICE_EXTENSION FdoExtension,
    _In_ PVOID Address,
    _Out_ PPHYSICAL_ADDRESS LogicalAddress,
    _Out_ PULONG Length)
{
    PLIST_ENTRY Entry;
    KIRQL OldIrql;
    BOOLEAN Found = FALSE;

    if (!FdoExtension->DmaAdapter)
        return FALSE;
    OldIrql = PortLockDmaBuffers(FdoExtension);
    for (Entry = FdoExtension->DmaBuffers.Flink;
         Entry != &FdoExtension->DmaBuffers; Entry = Entry->Flink)
    {
        PPORT_DMA_BUFFER Buffer = CONTAINING_RECORD(Entry, PORT_DMA_BUFFER, Entry);
        ULONG_PTR Offset = (ULONG_PTR)Address - (ULONG_PTR)Buffer->VirtualAddress;
        if ((ULONG_PTR)Address >= (ULONG_PTR)Buffer->VirtualAddress &&
            Offset < Buffer->Length)
        {
            LogicalAddress->QuadPart = Buffer->LogicalAddress.QuadPart + Offset;
            *Length = Buffer->Length - Offset;
            Found = TRUE;
            break;
        }
    }
    KeReleaseSpinLock(&FdoExtension->DmaBufferLock, OldIrql);
    return Found;
}
