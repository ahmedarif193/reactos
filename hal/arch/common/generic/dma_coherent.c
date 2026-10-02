/*
 * PROJECT:     LiberNT HAL
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Scatter/gather lists for coherent DMA without map registers
 *
 * Only adapters that can map every supported transfer directly may use this
 * implementation. Their MapTransfer operation translates each CPU page into
 * the device address domain and enforces the adapter address limit.
 */

#include <ntifs.h>
#include "../include/haldma_coherent.h"

#define HAL_DMA_SG_TAG 'gSmD'

NTSTATUS NTAPI
HalpCoherentGetScatterGatherList(PDMA_ADAPTER DmaAdapter, PDEVICE_OBJECT DeviceObject,
                          PMDL Mdl, PVOID CurrentVa, ULONG Length,
                          PDRIVER_LIST_CONTROL ExecutionRoutine, PVOID Context,
                          BOOLEAN WriteToDevice)
{
    PSCATTER_GATHER_LIST List;
    ULONG Pages, Remaining, Mapped, Count = 0;
    ULONG_PTR Offset;
    PUCHAR Va = CurrentVa;
    PHYSICAL_ADDRESS Address;

    if (!DmaAdapter || !DeviceObject || !Mdl || !Length || !ExecutionRoutine ||
        KeGetCurrentIrql() != DISPATCH_LEVEL ||
        (ULONG_PTR)CurrentVa < (ULONG_PTR)MmGetMdlVirtualAddress(Mdl))
        return STATUS_INVALID_PARAMETER;
    Offset = (ULONG_PTR)CurrentVa - (ULONG_PTR)MmGetMdlVirtualAddress(Mdl);
    if (Offset >= Mdl->ByteCount || Length > Mdl->ByteCount - Offset)
        return STATUS_INVALID_BUFFER_SIZE;
    Pages = ADDRESS_AND_SIZE_TO_SPAN_PAGES(CurrentVa, Length);
    if (Pages > (MAXULONG - FIELD_OFFSET(SCATTER_GATHER_LIST, Elements)) /
                sizeof(SCATTER_GATHER_ELEMENT))
        return STATUS_INVALID_BUFFER_SIZE;
    List = ExAllocatePoolWithTag(NonPagedPool,
        FIELD_OFFSET(SCATTER_GATHER_LIST, Elements) + Pages * sizeof(SCATTER_GATHER_ELEMENT),
        HAL_DMA_SG_TAG);
    if (!List)
        return STATUS_INSUFFICIENT_RESOURCES;
    List->Reserved = 0;
    for (Remaining = Length; Remaining; Remaining -= Mapped, Va += Mapped)
    {
        Mapped = Remaining;
        Address = DmaAdapter->DmaOperations->MapTransfer(
            DmaAdapter, Mdl, NULL, Va, &Mapped, WriteToDevice);
        if (!Mapped)
        {
            ExFreePoolWithTag(List, HAL_DMA_SG_TAG);
            return STATUS_INVALID_PARAMETER;
        }
        if (Count && List->Elements[Count - 1].Address.QuadPart +
                     List->Elements[Count - 1].Length == Address.QuadPart)
        {
            List->Elements[Count - 1].Length += Mapped;
        }
        else
        {
            List->Elements[Count].Address = Address;
            List->Elements[Count].Length = Mapped;
            List->Elements[Count].Reserved = 0;
            Count++;
        }
    }
    List->NumberOfElements = Count;
    KeMemoryBarrier();
    ExecutionRoutine(DeviceObject, DeviceObject->CurrentIrp, List, Context);
    return STATUS_SUCCESS;
}

VOID NTAPI
HalpCoherentPutScatterGatherList(PDMA_ADAPTER DmaAdapter,
                          PSCATTER_GATHER_LIST List, BOOLEAN WriteToDevice)
{
    UNREFERENCED_PARAMETER(DmaAdapter);
    UNREFERENCED_PARAMETER(WriteToDevice);
    KeMemoryBarrier();
    ExFreePoolWithTag(List, HAL_DMA_SG_TAG);
}

