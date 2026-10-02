/*
 * PROJECT:     LiberNT PowerPC HAL
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Coherent bus-master DMA through the PReP PCI window
 *
 * The loader describes the PCI address of physical page zero. Both common
 * buffers and request mappings must apply that window, independently of the
 * processor's pointer width.
 */

#include <ntifs.h>
#include "halp.h"
#include "../arch/common/include/haldma_coherent.h"

#define PPC_DMA_TAG       'aDvR'
#define PPC_DMA_SIGNATURE 0x52444D41UL

typedef struct _PPC_DMA_ADAPTER
{
    DMA_ADAPTER Header;
    ULONG Signature;
    ULONG AddressBits;
} PPC_DMA_ADAPTER;

static VOID NTAPI
HalpPpcPutDmaAdapter(PDMA_ADAPTER DmaAdapter)
{
    PPC_DMA_ADAPTER *Adapter = (PPC_DMA_ADAPTER *)DmaAdapter;

    if (!Adapter || Adapter->Signature != PPC_DMA_SIGNATURE)
        return;
    Adapter->Signature = 0;
    ExFreePoolWithTag(Adapter, PPC_DMA_TAG);
}

static PVOID NTAPI
HalpPpcAllocateCommonBuffer(PDMA_ADAPTER DmaAdapter, ULONG Length,
                             PPHYSICAL_ADDRESS LogicalAddress, BOOLEAN CacheEnabled)
{
    PPC_DMA_ADAPTER *Adapter = (PPC_DMA_ADAPTER *)DmaAdapter;
    PHYSICAL_ADDRESS Low, High, Boundary;
    PVOID Buffer;

    if (!Adapter || Adapter->Signature != PPC_DMA_SIGNATURE ||
        !LogicalAddress || !Length)
        return NULL;
    Low.QuadPart = 0;
    High.QuadPart = Adapter->AddressBits == 64 ? MAXLONGLONG :
                    ((1ULL << Adapter->AddressBits) - 1);
    if ((ULONGLONG)High.QuadPart < HalpPpcDmaOffset)
        return NULL;
    High.QuadPart -= HalpPpcDmaOffset;
    Boundary.QuadPart = 0;
    Buffer = MmAllocateContiguousMemorySpecifyCache(Length, Low, High,
                                                     Boundary,
                                                     CacheEnabled ? MmCached : MmNonCached);
    if (!Buffer)
        return NULL;
    *LogicalAddress = MmGetPhysicalAddress(Buffer);
    LogicalAddress->QuadPart += HalpPpcDmaOffset;
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
HalpPpcFreeCommonBuffer(PDMA_ADAPTER DmaAdapter, ULONG Length,
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
HalpPpcAllocateAdapterChannel(PDMA_ADAPTER DmaAdapter, PDEVICE_OBJECT DeviceObject,
                                ULONG NumberOfMapRegisters,
                                PDRIVER_CONTROL ExecutionRoutine, PVOID Context)
{
    KIRQL OldIrql = KeGetCurrentIrql();
    IO_ALLOCATION_ACTION Action;

    if (!DmaAdapter || ((PPC_DMA_ADAPTER *)DmaAdapter)->Signature != PPC_DMA_SIGNATURE ||
        !DeviceObject || !ExecutionRoutine || OldIrql > DISPATCH_LEVEL ||
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

NTSTATUS NTAPI
HalAllocateAdapterChannel(PADAPTER_OBJECT AdapterObject, PWAIT_CONTEXT_BLOCK Wcb,
                          ULONG NumberOfMapRegisters, PDRIVER_CONTROL ExecutionRoutine)
{
    if (!Wcb) return STATUS_INVALID_PARAMETER;
    return HalpPpcAllocateAdapterChannel((PDMA_ADAPTER)AdapterObject,
                                         Wcb->DeviceObject, NumberOfMapRegisters,
                                         ExecutionRoutine, Wcb->DeviceContext);
}

static BOOLEAN NTAPI
HalpPpcFlushAdapterBuffers(PDMA_ADAPTER DmaAdapter, PMDL Mdl,
                             PVOID MapRegisterBase, PVOID CurrentVa,
                             ULONG Length, BOOLEAN WriteToDevice)
{
    UNREFERENCED_PARAMETER(DmaAdapter);
    UNREFERENCED_PARAMETER(Mdl);
    UNREFERENCED_PARAMETER(MapRegisterBase);
    UNREFERENCED_PARAMETER(CurrentVa);
    UNREFERENCED_PARAMETER(Length);
    UNREFERENCED_PARAMETER(WriteToDevice);
    __asm__ __volatile__("sync" ::: "memory");
    return TRUE;
}

static VOID NTAPI
HalpPpcFreeAdapterChannel(PDMA_ADAPTER DmaAdapter)
{
    UNREFERENCED_PARAMETER(DmaAdapter);
}

static VOID NTAPI
HalpPpcFreeMapRegisters(PDMA_ADAPTER DmaAdapter, PVOID MapRegisterBase,
                          ULONG NumberOfMapRegisters)
{
    UNREFERENCED_PARAMETER(DmaAdapter);
    UNREFERENCED_PARAMETER(MapRegisterBase);
    UNREFERENCED_PARAMETER(NumberOfMapRegisters);
}

static PHYSICAL_ADDRESS NTAPI
HalpPpcMapTransfer(PDMA_ADAPTER DmaAdapter, PMDL Mdl,
                     PVOID MapRegisterBase, PVOID CurrentVa,
                     PULONG Length, BOOLEAN WriteToDevice)
{
    PPC_DMA_ADAPTER *Adapter = (PPC_DMA_ADAPTER *)DmaAdapter;
    PHYSICAL_ADDRESS Address;
    ULONG_PTR Offset;
    ULONG PageIndex;

    UNREFERENCED_PARAMETER(MapRegisterBase);
    UNREFERENCED_PARAMETER(WriteToDevice);
    Address.QuadPart = 0;
    if (!Adapter || Adapter->Signature != PPC_DMA_SIGNATURE ||
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
                       (Offset & (PAGE_SIZE - 1)) + HalpPpcDmaOffset;
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
HalpPpcGetDmaAlignment(PDMA_ADAPTER DmaAdapter)
{
    UNREFERENCED_PARAMETER(DmaAdapter);
    return 1;
}

static ULONG NTAPI
HalpPpcReadDmaCounter(PDMA_ADAPTER DmaAdapter)
{
    UNREFERENCED_PARAMETER(DmaAdapter);
    return 0;
}

static DMA_OPERATIONS HalpPpcDmaOperations =
{
    .Size = FIELD_OFFSET(DMA_OPERATIONS, CalculateScatterGatherList),
    .PutDmaAdapter = HalpPpcPutDmaAdapter,
    .AllocateCommonBuffer = HalpPpcAllocateCommonBuffer,
    .FreeCommonBuffer = HalpPpcFreeCommonBuffer,
    .AllocateAdapterChannel = HalpPpcAllocateAdapterChannel,
    .FlushAdapterBuffers = HalpPpcFlushAdapterBuffers,
    .FreeAdapterChannel = HalpPpcFreeAdapterChannel,
    .FreeMapRegisters = HalpPpcFreeMapRegisters,
    .MapTransfer = HalpPpcMapTransfer,
    .GetDmaAlignment = HalpPpcGetDmaAlignment,
    .ReadDmaCounter = HalpPpcReadDmaCounter,
    .GetScatterGatherList = HalpCoherentGetScatterGatherList,
    .PutScatterGatherList = HalpCoherentPutScatterGatherList
};

PADAPTER_OBJECT NTAPI
HalGetAdapter(PDEVICE_DESCRIPTION DeviceDescription, PULONG NumberOfMapRegisters)
{
    PPC_DMA_ADAPTER *Adapter;
    ULONG FirstBus, LastBus;

    if (NumberOfMapRegisters)
        *NumberOfMapRegisters = 0;
    if (!DeviceDescription || !DeviceDescription->Master ||
        DeviceDescription->Version > DEVICE_DESCRIPTION_VERSION3 ||
        DeviceDescription->InterfaceType != PCIBus ||
        !HalpPpcPciDmaCoherent() ||
        !HalpPpcGetPciBusRange(&FirstBus, &LastBus))
        return NULL;
    if (DeviceDescription->BusNumber < FirstBus ||
        DeviceDescription->BusNumber > LastBus)
        return NULL;

    Adapter = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Adapter), PPC_DMA_TAG);
    if (!Adapter)
        return NULL;
    Adapter->Header.Version = DeviceDescription->Version;
    Adapter->Header.Size = sizeof(*Adapter);
    Adapter->Header.DmaOperations = &HalpPpcDmaOperations;
    Adapter->Signature = PPC_DMA_SIGNATURE;
    Adapter->AddressBits = DeviceDescription->Dma64BitAddresses ? 64 : 32;
    if (NumberOfMapRegisters)
        *NumberOfMapRegisters = 64;
    return (PADAPTER_OBJECT)Adapter;
}

/* Legacy HAL DMA entry points: the adapter object is the one HalGetAdapter
 * returned, so they forward to its operations. */
PVOID
NTAPI
HalAllocateCommonBuffer(
    _In_ PADAPTER_OBJECT AdapterObject,
    _In_ ULONG Length,
    _Out_ PPHYSICAL_ADDRESS LogicalAddress,
    _In_ BOOLEAN CacheEnabled)
{
    return HalpPpcAllocateCommonBuffer((PDMA_ADAPTER)AdapterObject, Length,
                                         LogicalAddress, CacheEnabled);
}

VOID
NTAPI
HalFreeCommonBuffer(
    _In_ PADAPTER_OBJECT AdapterObject,
    _In_ ULONG Length,
    _In_ PHYSICAL_ADDRESS LogicalAddress,
    _In_ PVOID VirtualAddress,
    _In_ BOOLEAN CacheEnabled)
{
    HalpPpcFreeCommonBuffer((PDMA_ADAPTER)AdapterObject, Length, LogicalAddress,
                              VirtualAddress, CacheEnabled);
}

/* HalGetAdapter only admits coherent DMA: there is no cache state to flush. */
VOID
NTAPI
HalFlushCommonBuffer(
    _In_ PADAPTER_OBJECT AdapterObject,
    _In_ PVOID VirtualAddress,
    _In_ PHYSICAL_ADDRESS LogicalAddress,
    _In_ ULONG Length,
    _In_ BOOLEAN WriteToDevice)
{
    UNREFERENCED_PARAMETER(AdapterObject);
    UNREFERENCED_PARAMETER(VirtualAddress);
    UNREFERENCED_PARAMETER(LogicalAddress);
    UNREFERENCED_PARAMETER(Length);
    UNREFERENCED_PARAMETER(WriteToDevice);
}

ULONG
NTAPI
HalReadDmaCounter(
    _In_ PADAPTER_OBJECT AdapterObject)
{
    return HalpPpcReadDmaCounter((PDMA_ADAPTER)AdapterObject);
}

/* Bus-master PCI DMA has no crash-dump map registers to reserve. */
PVOID
NTAPI
HalAllocateCrashDumpRegisters(
    _In_ PADAPTER_OBJECT AdapterObject,
    _Inout_ PULONG NumberOfMapRegisters)
{
    UNREFERENCED_PARAMETER(AdapterObject);
    if (NumberOfMapRegisters)
        *NumberOfMapRegisters = 0;
    return NULL;
}

BOOLEAN
NTAPI
IoFlushAdapterBuffers(
    _In_ PADAPTER_OBJECT AdapterObject,
    _In_ PMDL Mdl,
    _In_ PVOID MapRegisterBase,
    _In_ PVOID CurrentVa,
    _In_ ULONG Length,
    _In_ BOOLEAN WriteToDevice)
{
    return HalpPpcFlushAdapterBuffers((PDMA_ADAPTER)AdapterObject, Mdl, MapRegisterBase,
                                        CurrentVa, Length, WriteToDevice);
}

VOID
NTAPI
IoFreeAdapterChannel(
    _In_ PADAPTER_OBJECT AdapterObject)
{
    HalpPpcFreeAdapterChannel((PDMA_ADAPTER)AdapterObject);
}

VOID
NTAPI
IoFreeMapRegisters(
    _In_ PADAPTER_OBJECT AdapterObject,
    _In_ PVOID MapRegisterBase,
    _In_ ULONG NumberOfMapRegisters)
{
    HalpPpcFreeMapRegisters((PDMA_ADAPTER)AdapterObject, MapRegisterBase,
                              NumberOfMapRegisters);
}

PHYSICAL_ADDRESS
NTAPI
IoMapTransfer(
    _In_ PADAPTER_OBJECT AdapterObject,
    _In_ PMDL Mdl,
    _In_ PVOID MapRegisterBase,
    _In_ PVOID CurrentVa,
    _Inout_ PULONG Length,
    _In_ BOOLEAN WriteToDevice)
{
    return HalpPpcMapTransfer((PDMA_ADAPTER)AdapterObject, Mdl, MapRegisterBase,
                                CurrentVa, Length, WriteToDevice);
}
