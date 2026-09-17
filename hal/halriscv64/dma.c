/*
 * PROJECT:     ReactOS RISC-V HAL
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Direct, coherent bus-master DMA for QEMU virt PCI
 *
 * QEMU virt DMA uses guest physical addresses directly. This provider is
 * limited to that discovered host: a physical board needs its own DMA window,
 * coherency and IOMMU contract before HalGetAdapter can claim support.
 */

#include <ntifs.h>
#include "halp.h"

#define RISCV_DMA_TAG       'aDvR'
#define RISCV_DMA_SIGNATURE 0x52444D41UL

typedef struct _RISCV_DMA_ADAPTER
{
    DMA_ADAPTER Header;
    ULONG Signature;
    ULONG AddressBits;
} RISCV_DMA_ADAPTER;

static VOID NTAPI
HalpRiscvPutDmaAdapter(PDMA_ADAPTER DmaAdapter)
{
    RISCV_DMA_ADAPTER *Adapter = (RISCV_DMA_ADAPTER *)DmaAdapter;

    if (!Adapter || Adapter->Signature != RISCV_DMA_SIGNATURE)
        return;
    Adapter->Signature = 0;
    ExFreePoolWithTag(Adapter, RISCV_DMA_TAG);
}

static PVOID NTAPI
HalpRiscvAllocateCommonBuffer(PDMA_ADAPTER DmaAdapter, ULONG Length,
                             PPHYSICAL_ADDRESS LogicalAddress, BOOLEAN CacheEnabled)
{
    RISCV_DMA_ADAPTER *Adapter = (RISCV_DMA_ADAPTER *)DmaAdapter;
    PHYSICAL_ADDRESS Low, High, Boundary;
    PVOID Buffer;

    if (!Adapter || Adapter->Signature != RISCV_DMA_SIGNATURE ||
        !LogicalAddress || !Length)
        return NULL;
    Low.QuadPart = 0;
    High.QuadPart = Adapter->AddressBits == 64 ? MAXLONGLONG :
                    ((1ULL << Adapter->AddressBits) - 1);
    Boundary.QuadPart = 0;
    Buffer = MmAllocateContiguousMemorySpecifyCache(Length, Low, High,
                                                     Boundary,
                                                     CacheEnabled ? MmCached : MmNonCached);
    if (!Buffer)
        return NULL;
    *LogicalAddress = MmGetPhysicalAddress(Buffer);
    if (LogicalAddress->QuadPart < 0 ||
        (Adapter->AddressBits < 64 &&
         (ULONG64)LogicalAddress->QuadPart + Length - 1 >=
             (1ULL << Adapter->AddressBits)))
    {
        MmFreeContiguousMemory(Buffer);
        return NULL;
    }
    return Buffer;
}

static VOID NTAPI
HalpRiscvFreeCommonBuffer(PDMA_ADAPTER DmaAdapter, ULONG Length,
                         PHYSICAL_ADDRESS LogicalAddress, PVOID VirtualAddress,
                         BOOLEAN CacheEnabled)
{
    UNREFERENCED_PARAMETER(DmaAdapter);
    UNREFERENCED_PARAMETER(Length);
    UNREFERENCED_PARAMETER(LogicalAddress);
    UNREFERENCED_PARAMETER(CacheEnabled);
    if (VirtualAddress)
        MmFreeContiguousMemory(VirtualAddress);
}

static NTSTATUS NTAPI
HalpRiscvAllocateAdapterChannel(PDMA_ADAPTER DmaAdapter, PDEVICE_OBJECT DeviceObject,
                                ULONG NumberOfMapRegisters,
                                PDRIVER_CONTROL ExecutionRoutine, PVOID Context)
{
    KIRQL OldIrql = KeGetCurrentIrql();
    IO_ALLOCATION_ACTION Action;

    if (!DmaAdapter || !DeviceObject || !ExecutionRoutine ||
        !NumberOfMapRegisters || NumberOfMapRegisters > 64)
        return STATUS_INVALID_PARAMETER;
    if (OldIrql < DISPATCH_LEVEL)
        KeRaiseIrql(DISPATCH_LEVEL, &OldIrql);
    Action = ExecutionRoutine(DeviceObject, DeviceObject->CurrentIrp,
                              DmaAdapter, Context);
    if (KeGetCurrentIrql() > OldIrql)
        KeLowerIrql(OldIrql);
    if (Action != KeepObject && Action != DeallocateObject &&
        Action != DeallocateObjectKeepRegisters)
        return STATUS_INVALID_PARAMETER;
    return STATUS_SUCCESS;
}

static BOOLEAN NTAPI
HalpRiscvFlushAdapterBuffers(PDMA_ADAPTER DmaAdapter, PMDL Mdl,
                             PVOID MapRegisterBase, PVOID CurrentVa,
                             ULONG Length, BOOLEAN WriteToDevice)
{
    UNREFERENCED_PARAMETER(DmaAdapter);
    UNREFERENCED_PARAMETER(Mdl);
    UNREFERENCED_PARAMETER(MapRegisterBase);
    UNREFERENCED_PARAMETER(CurrentVa);
    UNREFERENCED_PARAMETER(Length);
    UNREFERENCED_PARAMETER(WriteToDevice);
    __asm__ __volatile__("fence iorw, iorw" ::: "memory");
    return TRUE;
}

static VOID NTAPI
HalpRiscvFreeAdapterChannel(PDMA_ADAPTER DmaAdapter)
{
    UNREFERENCED_PARAMETER(DmaAdapter);
}

static VOID NTAPI
HalpRiscvFreeMapRegisters(PDMA_ADAPTER DmaAdapter, PVOID MapRegisterBase,
                          ULONG NumberOfMapRegisters)
{
    UNREFERENCED_PARAMETER(DmaAdapter);
    UNREFERENCED_PARAMETER(MapRegisterBase);
    UNREFERENCED_PARAMETER(NumberOfMapRegisters);
}

static PHYSICAL_ADDRESS NTAPI
HalpRiscvMapTransfer(PDMA_ADAPTER DmaAdapter, PMDL Mdl,
                     PVOID MapRegisterBase, PVOID CurrentVa,
                     PULONG Length, BOOLEAN WriteToDevice)
{
    RISCV_DMA_ADAPTER *Adapter = (RISCV_DMA_ADAPTER *)DmaAdapter;
    PHYSICAL_ADDRESS Address;
    ULONG_PTR Offset;
    ULONG PageIndex;

    UNREFERENCED_PARAMETER(MapRegisterBase);
    UNREFERENCED_PARAMETER(WriteToDevice);
    Address.QuadPart = 0;
    if (!Adapter || Adapter->Signature != RISCV_DMA_SIGNATURE ||
        !Length || !*Length || !Mdl)
        return Address;

    if (Mdl->StartVa)
    {
        if ((ULONG_PTR)CurrentVa < (ULONG_PTR)Mdl->StartVa)
            goto Fail;
        Offset = (ULONG_PTR)CurrentVa - (ULONG_PTR)Mdl->StartVa;
    }
    else
    {
        Offset = CurrentVa ? (ULONG_PTR)CurrentVa : Mdl->ByteOffset;
    }
    if (Offset < Mdl->ByteOffset ||
        Offset - Mdl->ByteOffset >= Mdl->ByteCount)
        goto Fail;
    PageIndex = Offset >> PAGE_SHIFT;
    Address.QuadPart = ((ULONG64)MmGetMdlPfnArray(Mdl)[PageIndex] << PAGE_SHIFT) +
                       (Offset & (PAGE_SIZE - 1));
    *Length = min(*Length, PAGE_SIZE - (ULONG)(Offset & (PAGE_SIZE - 1)));
    *Length = min(*Length, Mdl->ByteCount - (ULONG)(Offset - Mdl->ByteOffset));
    if (Address.QuadPart < 0 ||
        (Adapter->AddressBits < 64 &&
         (ULONG64)Address.QuadPart + *Length - 1 >=
             (1ULL << Adapter->AddressBits)))
        goto Fail;
    return Address;

Fail:
    *Length = 0;
    Address.QuadPart = 0;
    return Address;
}

static ULONG NTAPI
HalpRiscvGetDmaAlignment(PDMA_ADAPTER DmaAdapter)
{
    UNREFERENCED_PARAMETER(DmaAdapter);
    return 1;
}

static ULONG NTAPI
HalpRiscvReadDmaCounter(PDMA_ADAPTER DmaAdapter)
{
    UNREFERENCED_PARAMETER(DmaAdapter);
    return 0;
}

static DMA_OPERATIONS HalpRiscvDmaOperations =
{
    .Size = FIELD_OFFSET(DMA_OPERATIONS, GetScatterGatherList),
    .PutDmaAdapter = HalpRiscvPutDmaAdapter,
    .AllocateCommonBuffer = HalpRiscvAllocateCommonBuffer,
    .FreeCommonBuffer = HalpRiscvFreeCommonBuffer,
    .AllocateAdapterChannel = HalpRiscvAllocateAdapterChannel,
    .FlushAdapterBuffers = HalpRiscvFlushAdapterBuffers,
    .FreeAdapterChannel = HalpRiscvFreeAdapterChannel,
    .FreeMapRegisters = HalpRiscvFreeMapRegisters,
    .MapTransfer = HalpRiscvMapTransfer,
    .GetDmaAlignment = HalpRiscvGetDmaAlignment,
    .ReadDmaCounter = HalpRiscvReadDmaCounter
};

PADAPTER_OBJECT NTAPI
HalGetAdapter(PDEVICE_DESCRIPTION DeviceDescription, PULONG NumberOfMapRegisters)
{
    RISCV_DMA_ADAPTER *Adapter;
    ULONG FirstBus, LastBus;

    if (NumberOfMapRegisters)
        *NumberOfMapRegisters = 0;
    if (!DeviceDescription || !DeviceDescription->Master ||
        DeviceDescription->Version > DEVICE_DESCRIPTION_VERSION3 ||
        DeviceDescription->InterfaceType != PCIBus ||
        !HalpRiscvPciDmaCoherent() ||
        !HalpRiscvGetPciBusRange(&FirstBus, &LastBus))
        return NULL;
    if (DeviceDescription->BusNumber < FirstBus ||
        DeviceDescription->BusNumber > LastBus)
        return NULL;

    Adapter = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Adapter), RISCV_DMA_TAG);
    if (!Adapter)
        return NULL;
    Adapter->Header.Version = DeviceDescription->Version;
    Adapter->Header.Size = sizeof(*Adapter);
    Adapter->Header.DmaOperations = &HalpRiscvDmaOperations;
    Adapter->Signature = RISCV_DMA_SIGNATURE;
    Adapter->AddressBits = DeviceDescription->Dma64BitAddresses ? 64 : 32;
    if (NumberOfMapRegisters)
        *NumberOfMapRegisters = 64;
    return (PADAPTER_OBJECT)Adapter;
}
