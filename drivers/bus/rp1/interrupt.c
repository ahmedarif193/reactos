/*
 * PROJECT:     ReactOS RP1 Bus Driver
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * FILE:        drivers/bus/rp1/interrupt.c
 * PURPOSE:     Raspberry Pi 5 RP1 interrupt routing
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "rp1.h"
#include <wdmguid.h>

#define NDEBUG
#include <debug.h>

/* RP1 exposes one MSI-X vector for each internal interrupt source. */
#define RP1_INT_END                    61u
#define RP1_HW_IRQ_MASK                0x3fu

/* BCM2712 MIP0 is the interrupt presenter used by the RP1 PCIe root complex. */
#define BCM2712_MIP0_BASE              0x1000130000ULL
#define BCM2712_MIP_LENGTH             0x1000u
#define BCM2712_MIP0_MSG_ADDR          ((((ULONGLONG)0xff) << 32) | 0xfffff000ULL)
#define BCM2712_GIC_SPI_INTID_BASE     32u
#define BCM2712_MIP0_GIC_SPI_BASE      128u
#define BCM2712_MIP0_INTID_BASE        (BCM2712_GIC_SPI_INTID_BASE + BCM2712_MIP0_GIC_SPI_BASE)
#define BCM2712_MIP0_SPI_COUNT         64u

#define MIP_INT_CFGL_HOST              0x20u
#define MIP_INT_CFGH_HOST              0x30u
#define MIP_INT_MASKL_HOST             0x40u
#define MIP_INT_MASKH_HOST             0x50u
#define MIP_INT_MASKL_VPU              0x60u
#define MIP_INT_MASKH_VPU              0x70u
#define MIP_INT_STATUSL_HOST           0x80u
#define MIP_INT_STATUSH_HOST           0x90u

#define PCI_CAPABILITY_LIST            0x34u
#define PCI_CAP_ID_MSIX                0x11u
#define PCI_BASE_ADDRESS_0             0x10u
#define PCI_BASE_ADDRESS_SPACE_IO      0x00000001u
#define PCI_BASE_ADDRESS_MEM_TYPE_MASK 0x00000006u
#define PCI_BASE_ADDRESS_MEM_TYPE_64   0x00000004u
#define PCI_BASE_ADDRESS_MEM_MASK      (~0x0fUL)
#define PCI_COMMAND_OFFSET             0x04u
#define PCI_COMMAND_INTX_DISABLE       0x0400u
#define PCI_MSIX_FLAGS                 0x02u
#define PCI_MSIX_TABLE                 0x04u
#define PCI_MSIX_FLAGS_ENABLE          0x8000u
#define PCI_MSIX_FLAGS_MASKALL         0x4000u
#define PCI_MSIX_FLAGS_TABLE_SIZE      0x07ffu
#define PCI_MSIX_TABLE_BIR_MASK        0x00000007u
#define PCI_MSIX_TABLE_OFFSET_MASK     0xfffffff8u

typedef struct _RP1_MSIX_TABLE_ENTRY
{
    ULONG MessageAddressLow;
    ULONG MessageAddressHigh;
    ULONG MessageData;
    ULONG VectorControl;
} RP1_MSIX_TABLE_ENTRY, *PRP1_MSIX_TABLE_ENTRY;

static
NTSTATUS
Rp1QueryParentBusInterface(
    _In_ PRP1_FDO_EXTENSION FdoExt,
    _Out_ PBUS_INTERFACE_STANDARD BusInterface)
{
    KEVENT Event;
    IO_STATUS_BLOCK IoStatus;
    PIRP Irp;
    PIO_STACK_LOCATION Stack;
    NTSTATUS Status;

    if (!FdoExt || !FdoExt->LowerDevice || !BusInterface)
        return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(BusInterface, sizeof(*BusInterface));

    KeInitializeEvent(&Event, NotificationEvent, FALSE);
    Irp = IoBuildSynchronousFsdRequest(IRP_MJ_PNP,
                                       FdoExt->LowerDevice,
                                       NULL,
                                       0,
                                       NULL,
                                       &Event,
                                       &IoStatus);
    if (!Irp)
        return STATUS_INSUFFICIENT_RESOURCES;

    Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;

    Stack = IoGetNextIrpStackLocation(Irp);
    Stack->MajorFunction = IRP_MJ_PNP;
    Stack->MinorFunction = IRP_MN_QUERY_INTERFACE;
    Stack->Parameters.QueryInterface.InterfaceType = &GUID_BUS_INTERFACE_STANDARD;
    Stack->Parameters.QueryInterface.Size = sizeof(*BusInterface);
    Stack->Parameters.QueryInterface.Version = 1;
    Stack->Parameters.QueryInterface.Interface = (PINTERFACE)BusInterface;
    Stack->Parameters.QueryInterface.InterfaceSpecificData = NULL;

    Status = IoCallDriver(FdoExt->LowerDevice, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatus.Status;
    }

    if (!NT_SUCCESS(Status))
        return Status;

    if (!BusInterface->Context)
        BusInterface->Context = FdoExt->PhysicalDevice;

    if (!BusInterface->GetBusData || !BusInterface->SetBusData)
        return STATUS_NOT_SUPPORTED;

    return STATUS_SUCCESS;
}

static
ULONG
Rp1GetConfig(
    _In_ PBUS_INTERFACE_STANDARD BusInterface,
    _In_reads_bytes_(Length) PVOID Buffer,
    _In_ ULONG Offset,
    _In_ ULONG Length)
{
    return BusInterface->GetBusData(BusInterface->Context,
                                    PCI_WHICHSPACE_CONFIG,
                                    Buffer,
                                    Offset,
                                    Length);
}

static
ULONG
Rp1SetConfig(
    _In_ PBUS_INTERFACE_STANDARD BusInterface,
    _In_reads_bytes_(Length) PVOID Buffer,
    _In_ ULONG Offset,
    _In_ ULONG Length)
{
    return BusInterface->SetBusData(BusInterface->Context,
                                    PCI_WHICHSPACE_CONFIG,
                                    Buffer,
                                    Offset,
                                    Length);
}

static
BOOLEAN
Rp1FindMsixCapability(
    _In_ PBUS_INTERFACE_STANDARD BusInterface,
    _Out_ PUCHAR CapabilityOffset)
{
    UCHAR Cap;
    ULONG Guard;

    if (Rp1GetConfig(BusInterface, &Cap, PCI_CAPABILITY_LIST, sizeof(Cap)) != sizeof(Cap))
        return FALSE;

    Cap &= ~3u;
    for (Guard = 0; Cap >= 0x40 && Guard < 48; Guard++)
    {
        UCHAR Id;
        UCHAR Next;

        if (Rp1GetConfig(BusInterface, &Id, Cap, sizeof(Id)) != sizeof(Id))
            return FALSE;

        if (Rp1GetConfig(BusInterface, &Next, Cap + 1, sizeof(Next)) != sizeof(Next))
            return FALSE;

        if (Id == PCI_CAP_ID_MSIX)
        {
            *CapabilityOffset = Cap;
            return TRUE;
        }

        Cap = Next & ~3u;
    }

    return FALSE;
}

static
BOOLEAN
Rp1ReadBarAddress(
    _In_ PBUS_INTERFACE_STANDARD BusInterface,
    _In_ ULONG BarIndex,
    _Out_ PULONGLONG BaseAddress)
{
    ULONG Offset;
    ULONG BarLo;
    ULONG BarHi = 0;
    ULONGLONG Base;

    if (!BaseAddress || BarIndex >= PCI_TYPE0_ADDRESSES)
        return FALSE;

    Offset = PCI_BASE_ADDRESS_0 + BarIndex * sizeof(ULONG);
    if (Rp1GetConfig(BusInterface, &BarLo, Offset, sizeof(BarLo)) != sizeof(BarLo))
        return FALSE;

    if (BarLo & PCI_BASE_ADDRESS_SPACE_IO)
        return FALSE;

    Base = (ULONGLONG)(BarLo & PCI_BASE_ADDRESS_MEM_MASK);
    if ((BarLo & PCI_BASE_ADDRESS_MEM_TYPE_MASK) == PCI_BASE_ADDRESS_MEM_TYPE_64)
    {
        if (BarIndex + 1 >= PCI_TYPE0_ADDRESSES)
            return FALSE;
        if (Rp1GetConfig(BusInterface, &BarHi, Offset + sizeof(ULONG), sizeof(BarHi)) != sizeof(BarHi))
            return FALSE;
        Base |= ((ULONGLONG)BarHi << 32);
    }

    *BaseAddress = Base;
    return Base != 0;
}

static
VOID
Rp1ProgramBcm2712Mip(VOID)
{
    PHYSICAL_ADDRESS Phys;
    PVOID Base;
    ULONG CfgLow;
    ULONG CfgHigh;
    ULONG MaskHostLow;
    ULONG MaskHostHigh;
    ULONG MaskVpuLow;
    ULONG MaskVpuHigh;
    ULONG StatusLow;
    ULONG StatusHigh;

    Phys.QuadPart = BCM2712_MIP0_BASE;
    Base = MmMapIoSpace(Phys, BCM2712_MIP_LENGTH, MmNonCached);
    if (!Base)
    {
        DPRINT1("RP1: failed to map BCM2712 MIP0 at 0x%I64x\n", Phys.QuadPart);
        return;
    }

    WRITE_REGISTER_ULONG((PULONG)((PUCHAR)Base + MIP_INT_MASKL_HOST), 0);
    WRITE_REGISTER_ULONG((PULONG)((PUCHAR)Base + MIP_INT_MASKH_HOST), 0);
    WRITE_REGISTER_ULONG((PULONG)((PUCHAR)Base + MIP_INT_MASKL_VPU), ~0u);
    WRITE_REGISTER_ULONG((PULONG)((PUCHAR)Base + MIP_INT_MASKH_VPU), ~0u);
    WRITE_REGISTER_ULONG((PULONG)((PUCHAR)Base + MIP_INT_CFGL_HOST), ~0u);
    WRITE_REGISTER_ULONG((PULONG)((PUCHAR)Base + MIP_INT_CFGH_HOST), ~0u);

    CfgLow = READ_REGISTER_ULONG((PULONG)((PUCHAR)Base + MIP_INT_CFGL_HOST));
    CfgHigh = READ_REGISTER_ULONG((PULONG)((PUCHAR)Base + MIP_INT_CFGH_HOST));
    MaskHostLow = READ_REGISTER_ULONG((PULONG)((PUCHAR)Base + MIP_INT_MASKL_HOST));
    MaskHostHigh = READ_REGISTER_ULONG((PULONG)((PUCHAR)Base + MIP_INT_MASKH_HOST));
    MaskVpuLow = READ_REGISTER_ULONG((PULONG)((PUCHAR)Base + MIP_INT_MASKL_VPU));
    MaskVpuHigh = READ_REGISTER_ULONG((PULONG)((PUCHAR)Base + MIP_INT_MASKH_VPU));
    StatusLow = READ_REGISTER_ULONG((PULONG)((PUCHAR)Base + MIP_INT_STATUSL_HOST));
    StatusHigh = READ_REGISTER_ULONG((PULONG)((PUCHAR)Base + MIP_INT_STATUSH_HOST));

    MmUnmapIoSpace(Base, BCM2712_MIP_LENGTH);

    DPRINT("RP1: BCM2712 MIP0 initialized msg=0x%I64x gic-spi-base=%lu intid-base=%lu count=%lu cfg=0x%08lx:%08lx mask-host=0x%08lx:%08lx mask-vpu=0x%08lx:%08lx status=0x%08lx:%08lx\n",
           BCM2712_MIP0_MSG_ADDR,
           BCM2712_MIP0_GIC_SPI_BASE,
           BCM2712_MIP0_INTID_BASE,
           BCM2712_MIP0_SPI_COUNT,
           CfgHigh,
           CfgLow,
           MaskHostHigh,
           MaskHostLow,
           MaskVpuHigh,
           MaskVpuLow,
           StatusHigh,
           StatusLow);
}

static
NTSTATUS
Rp1ProgramMsixTable(
    _In_ PBUS_INTERFACE_STANDARD BusInterface)
{
    UCHAR Cap;
    USHORT Control;
    ULONG TableInfo;
    ULONG TableSize;
    ULONG Bir;
    ULONG TableOffset;
    ULONGLONG BarBase;
    PHYSICAL_ADDRESS TablePhys;
    PHYSICAL_ADDRESS MapPhys;
    ULONGLONG PageOffset;
    ULONG MapLength;
    PVOID Mapping;
    PRP1_MSIX_TABLE_ENTRY Entry;
    ULONG Index;
    USHORT Command;
    ULONG EthAddrLow;
    ULONG EthAddrHigh;
    ULONG EthData;
    ULONG EthVectorControl;

    if (!Rp1FindMsixCapability(BusInterface, &Cap))
    {
        DPRINT1("RP1: MSI-X capability not found\n");
        return STATUS_NOT_SUPPORTED;
    }

    if (Rp1GetConfig(BusInterface, &Control, Cap + PCI_MSIX_FLAGS, sizeof(Control)) != sizeof(Control))
        return STATUS_DEVICE_PROTOCOL_ERROR;
    if (Rp1GetConfig(BusInterface, &TableInfo, Cap + PCI_MSIX_TABLE, sizeof(TableInfo)) != sizeof(TableInfo))
        return STATUS_DEVICE_PROTOCOL_ERROR;

    TableSize = (Control & PCI_MSIX_FLAGS_TABLE_SIZE) + 1;
    Bir = TableInfo & PCI_MSIX_TABLE_BIR_MASK;
    TableOffset = TableInfo & PCI_MSIX_TABLE_OFFSET_MASK;

    if (TableSize < RP1_INT_END)
    {
        DPRINT1("RP1: MSI-X table too small (%lu, need %lu)\n", TableSize, RP1_INT_END);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    if (!Rp1ReadBarAddress(BusInterface, Bir, &BarBase))
    {
        DPRINT1("RP1: failed to resolve MSI-X table BAR%lu\n", Bir);
        return STATUS_DEVICE_PROTOCOL_ERROR;
    }

    TablePhys.QuadPart = BarBase + TableOffset;
    TablePhys = Rp1TranslateBarAddress(TablePhys);

    PageOffset = TablePhys.QuadPart & (PAGE_SIZE - 1);
    MapPhys.QuadPart = TablePhys.QuadPart - PageOffset;
    MapLength = (ULONG)(PageOffset + RP1_INT_END * sizeof(RP1_MSIX_TABLE_ENTRY));

    Mapping = MmMapIoSpace(MapPhys, MapLength, MmNonCached);
    if (!Mapping)
    {
        DPRINT1("RP1: failed to map MSI-X table phys=0x%I64x len=0x%lx\n",
                MapPhys.QuadPart,
                MapLength);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Control |= PCI_MSIX_FLAGS_MASKALL;
    Rp1SetConfig(BusInterface, &Control, Cap + PCI_MSIX_FLAGS, sizeof(Control));

    Entry = (PRP1_MSIX_TABLE_ENTRY)((PUCHAR)Mapping + PageOffset);
    for (Index = 0; Index < RP1_INT_END; Index++)
    {
        WRITE_REGISTER_ULONG(&Entry[Index].MessageAddressLow,
                             (ULONG)(BCM2712_MIP0_MSG_ADDR & 0xffffffffu));
        WRITE_REGISTER_ULONG(&Entry[Index].MessageAddressHigh,
                             (ULONG)(BCM2712_MIP0_MSG_ADDR >> 32));
        WRITE_REGISTER_ULONG(&Entry[Index].MessageData, Index & RP1_HW_IRQ_MASK);
        WRITE_REGISTER_ULONG(&Entry[Index].VectorControl, 0);
    }

    EthAddrLow = READ_REGISTER_ULONG(&Entry[RP1_INT_ETH].MessageAddressLow);
    EthAddrHigh = READ_REGISTER_ULONG(&Entry[RP1_INT_ETH].MessageAddressHigh);
    EthData = READ_REGISTER_ULONG(&Entry[RP1_INT_ETH].MessageData);
    EthVectorControl = READ_REGISTER_ULONG(&Entry[RP1_INT_ETH].VectorControl);

    MmUnmapIoSpace(Mapping, MapLength);

    Control |= PCI_MSIX_FLAGS_ENABLE;
    Control &= ~PCI_MSIX_FLAGS_MASKALL;
    Rp1SetConfig(BusInterface, &Control, Cap + PCI_MSIX_FLAGS, sizeof(Control));

    if (Rp1GetConfig(BusInterface, &Command, PCI_COMMAND_OFFSET, sizeof(Command)) == sizeof(Command))
    {
        Command |= PCI_COMMAND_INTX_DISABLE;
        Rp1SetConfig(BusInterface, &Command, PCI_COMMAND_OFFSET, sizeof(Command));
    }

    DPRINT("RP1: MSI-X table programmed entries=%lu table=0x%I64x BAR%lu+0x%lx msg=0x%I64x eth-entry=%08lx:%08lx data=0x%08lx vc=0x%08lx control=0x%04x\n",
           RP1_INT_END,
           TablePhys.QuadPart,
           Bir,
           TableOffset,
           BCM2712_MIP0_MSG_ADDR,
           EthAddrHigh,
           EthAddrLow,
           EthData,
           EthVectorControl,
           Control);

    return STATUS_SUCCESS;
}

static
ULONG
Rp1MsiVectorFromHwirq(
    _In_ ULONG Hwirq)
{
    return BCM2712_MIP0_INTID_BASE + Hwirq;
}

NTSTATUS
Rp1InitializeInterrupts(
    _In_ PRP1_FDO_EXTENSION FdoExt)
{
    BUS_INTERFACE_STANDARD BusInterface;
    NTSTATUS Status;

    if (!FdoExt)
        return STATUS_INVALID_PARAMETER;

    Status = Rp1QueryParentBusInterface(FdoExt, &BusInterface);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("RP1: parent PCI bus interface unavailable (0x%08lx)\n", Status);
        return Status;
    }

    Rp1ProgramBcm2712Mip();

    Status = Rp1ProgramMsixTable(&BusInterface);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("RP1: MSI-X programming failed (0x%08lx)\n", Status);
        return Status;
    }

    FdoExt->MsixReady = TRUE;
    FdoExt->MsiVectorBase = BCM2712_MIP0_INTID_BASE;
    FdoExt->MsiVectorCount = BCM2712_MIP0_SPI_COUNT;
    return STATUS_SUCCESS;
}

static
VOID
Rp1MsixCfgSet(
    _In_ PRP1_FDO_EXTENSION FdoExt,
    _In_ ULONG Hwirq,
    _In_ ULONG Value)
{
    PUCHAR Base;

    if (!FdoExt->Bar1Virtual)
        return;

    Base = (PUCHAR)FdoExt->Bar1Virtual + RP1_PCIE_APBS_OFFSET;
    WRITE_REGISTER_ULONG((PULONG)(Base + RP1_REG_SET_OFFSET + RP1_MSIX_CFG(Hwirq)),
                         Value);
}

VOID
Rp1EnableEthernetInterruptRoute(
    _In_ PRP1_FDO_EXTENSION FdoExt)
{
    ULONG Value;
    PUCHAR Base;

    if (!FdoExt->Bar1Virtual)
        return;

    Rp1MsixCfgSet(FdoExt,
                  RP1_INT_ETH,
                  RP1_MSIX_CFG_ENABLE | RP1_MSIX_CFG_IACK_EN | RP1_MSIX_CFG_IACK);

    Base = (PUCHAR)FdoExt->Bar1Virtual + RP1_PCIE_APBS_OFFSET;
    Value = READ_REGISTER_ULONG((PULONG)(Base + RP1_MSIX_CFG(RP1_INT_ETH)));
    DPRINT("RP1: Ethernet interrupt route hwirq=%lu cfg=0x%08lx parent-vector=%lu msi-vector=%lu\n",
           RP1_INT_ETH,
           Value,
           FdoExt->InterruptVector,
           Rp1MsiVectorFromHwirq(RP1_INT_ETH));
}

BOOLEAN
Rp1GetChildInterrupt(
    _In_ PRP1_FDO_EXTENSION FdoExt,
    _In_ ULONG ChildIndex,
    _Out_ PULONG Level,
    _Out_ PULONG Vector,
    _Out_ PKAFFINITY Affinity,
    _Out_ PULONG Flags,
    _Out_ CM_SHARE_DISPOSITION *ShareDisposition,
    _Out_ PUCHAR InterruptLine)
{
    ULONG MessageVector;

    if (!FdoExt || !Level || !Vector || !Affinity || !Flags ||
        !ShareDisposition || !InterruptLine)
    {
        return FALSE;
    }

    if (FdoExt->MsixReady && ChildIndex == RP1_CHILD_ETHERNET)
    {
        MessageVector = Rp1MsiVectorFromHwirq(RP1_INT_ETH);
        if (MessageVector == 0)
            return FALSE;

        *Level = FdoExt->RawInterruptLevel ? FdoExt->RawInterruptLevel : MessageVector;
        *Vector = MessageVector;
        *Affinity = FdoExt->RawInterruptAffinity ? FdoExt->RawInterruptAffinity : 1;
        *Flags = CM_RESOURCE_INTERRUPT_LATCHED | CM_RESOURCE_INTERRUPT_MESSAGE;
        *ShareDisposition = CmResourceShareDeviceExclusive;
        *InterruptLine = (MessageVector <= 0xff) ? (UCHAR)MessageVector : 0xff;
        return TRUE;
    }

    if (!FdoExt->RawInterruptValid || (FdoExt->RawInterruptLevel == 0 && FdoExt->RawInterruptVector == 0))
        return FALSE;

    *Level = FdoExt->RawInterruptLevel ? FdoExt->RawInterruptLevel : FdoExt->RawInterruptVector;
    *Vector = FdoExt->RawInterruptVector;
    *Affinity = FdoExt->RawInterruptAffinity;
    *Flags = FdoExt->RawInterruptFlags;
    *ShareDisposition = FdoExt->RawInterruptShareDisposition;
    *InterruptLine = (*Level <= 0xff) ? (UCHAR)*Level : 0xff;
    return TRUE;
}
