/*
 * PROJECT:     ReactOS RP1 Bus Driver
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * FILE:        drivers/bus/rp1/rp1.c
 * PURPOSE:     Raspberry Pi 5 RP1 southbridge child bus driver
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "rp1.h"
#include <initguid.h>
#include <wdmguid.h>

#define NDEBUG
#include <debug.h>

/* ------------------------------------------------------------------ */
/*  Static child descriptor table                                      */
/* ------------------------------------------------------------------ */

const RP1_CHILD_DESCRIPTOR Rp1Children[RP1_MAX_CHILDREN] =
{
    /* xHCI0 (DWC3 USB 3.0 #0) */
    {
        Rp1ChildXhci,
        RP1_XHCI0_OFFSET,
        RP1_XHCI0_SIZE,
        L"0",
        (USHORT)(RP1_XHCI_PCI_DEVICE_ID_BASE + RP1_CHILD_XHCI0),
        RP1_XHCI_PCI_PROGIF,
        PCI_SUBCLASS_SB_USB,
        PCI_CLASS_SERIAL_BUS_CTLR,
        L"RP1\\XHCI",
        L"RP1\\XHCI&REV_00",
        L"PCI\\CC_0C0330",
        L"RP1 xHCI USB 3.0 Controller",
        FALSE
    },

    /* xHCI1 (DWC3 USB 3.0 #1) */
    {
        Rp1ChildXhci,
        RP1_XHCI1_OFFSET,
        RP1_XHCI1_SIZE,
        L"1",
        (USHORT)(RP1_XHCI_PCI_DEVICE_ID_BASE + RP1_CHILD_XHCI1),
        RP1_XHCI_PCI_PROGIF,
        PCI_SUBCLASS_SB_USB,
        PCI_CLASS_SERIAL_BUS_CTLR,
        L"RP1\\XHCI",
        L"RP1\\XHCI&REV_00",
        L"PCI\\CC_0C0330",
        L"RP1 xHCI USB 3.0 Controller",
        FALSE
    },

    /* Ethernet (Cadence GEM) */
    {
        Rp1ChildEthernet,
        RP1_ETH_OFFSET,
        RP1_ETH_SIZE,
        L"2",
        RP1_ETH_PCI_DEVICE_ID,
        0,
        PCI_SUBCLASS_NET_ETHERNET_CTLR,
        PCI_CLASS_NETWORK_CTLR,
        L"RP1\\GEM",
        L"RP1\\GEM&REV_00",
        L"PCI\\CC_020000",
        L"RP1 Ethernet Controller",
        TRUE
    },
};

/* ------------------------------------------------------------------ */
/*  Forward declarations                                               */
/* ------------------------------------------------------------------ */

static DRIVER_ADD_DEVICE Rp1AddDevice;
static NTSTATUS NTAPI Rp1AddDevice(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDEVICE_OBJECT PhysicalDeviceObject);

static DRIVER_DISPATCH Rp1DispatchPnp;
static NTSTATUS NTAPI Rp1DispatchPnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp);

static DRIVER_DISPATCH Rp1DispatchPower;
static NTSTATUS NTAPI Rp1DispatchPower(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp);

static DRIVER_UNLOAD Rp1Unload;
static VOID NTAPI Rp1Unload(
    _In_ PDRIVER_OBJECT DriverObject);

/* ------------------------------------------------------------------ */
/*  Completion routine for synchronous forwarding                      */
/* ------------------------------------------------------------------ */

static
NTSTATUS
NTAPI
Rp1CompletionRoutine(
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ PIRP Irp,
    _In_ PVOID Context)
{
    PKEVENT Event = (PKEVENT)Context;

    UNREFERENCED_PARAMETER(DeviceObject);
    UNREFERENCED_PARAMETER(Irp);

    if (Event)
        KeSetEvent(Event, IO_NO_INCREMENT, FALSE);

    return STATUS_MORE_PROCESSING_REQUIRED;
}

static
NTSTATUS
Rp1SendIrpSynchronous(
    _In_ PDEVICE_OBJECT LowerDevice,
    _In_ PIRP Irp)
{
    KEVENT Event;
    NTSTATUS Status;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);

    IoCopyCurrentIrpStackLocationToNext(Irp);
    IoSetCompletionRoutine(Irp,
                           Rp1CompletionRoutine,
                           &Event,
                           TRUE, TRUE, TRUE);

    Status = IoCallDriver(LowerDevice, Irp);

    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = Irp->IoStatus.Status;
    }

    return Status;
}

PHYSICAL_ADDRESS
Rp1TranslateBarAddress(
    _In_ PHYSICAL_ADDRESS BusAddress)
{
    PHYSICAL_ADDRESS CpuAddress;

    CpuAddress = BusAddress;

    if (BusAddress.QuadPart >= BCM2712_PCIE2_PCI_MEM_BASE &&
        BusAddress.QuadPart < 0x100000000ULL)
    {
        CpuAddress.QuadPart = BusAddress.QuadPart -
                              BCM2712_PCIE2_PCI_MEM_BASE +
                              BCM2712_PCIE2_CPU_MEM_BASE;
    }

    return CpuAddress;
}

static
VOID
Rp1BuildSyntheticPciConfig(
    _In_ PRP1_PDO_EXTENSION PdoExt,
    _Out_ PPCI_COMMON_CONFIG PciConfig)
{
    const RP1_CHILD_DESCRIPTOR *Child = &Rp1Children[PdoExt->ChildIndex];

    RtlZeroMemory(PciConfig, sizeof(*PciConfig));

    /* Fill the per-child PCI config view exposed through BUS_INTERFACE_STANDARD. */
    PciConfig->VendorID = RP1_VENDOR_ID;
    PciConfig->DeviceID = Child->PciDeviceId;
    PciConfig->Command = PdoExt->PciCommand;
    PciConfig->Status = 0;
    PciConfig->RevisionID = 0;
    PciConfig->ProgIf = Child->ProgIf;
    PciConfig->SubClass = Child->SubClass;
    PciConfig->BaseClass = Child->BaseClass;
    PciConfig->CacheLineSize = PdoExt->CacheLineSize;
    PciConfig->LatencyTimer = PdoExt->LatencyTimer;
    PciConfig->HeaderType = PCI_DEVICE_TYPE;
    PciConfig->u.type0.BaseAddresses[0] =
        (ULONG)(PdoExt->MmioBusAddress.QuadPart & ~0xFULL);
    PciConfig->u.type0.SubVendorID = RP1_VENDOR_ID;
    PciConfig->u.type0.SubSystemID = Child->PciDeviceId;
    PciConfig->u.type0.InterruptLine = PdoExt->InterruptLine;
    PciConfig->u.type0.InterruptPin = 1;
}

static
VOID
NTAPI
Rp1BusInterfaceReference(
    _Inout_opt_ PVOID Context)
{
    PRP1_PDO_EXTENSION PdoExt = (PRP1_PDO_EXTENSION)Context;

    if (PdoExt && PdoExt->Self)
        ObReferenceObject(PdoExt->Self);
}

static
VOID
NTAPI
Rp1BusInterfaceDereference(
    _Inout_opt_ PVOID Context)
{
    PRP1_PDO_EXTENSION PdoExt = (PRP1_PDO_EXTENSION)Context;

    if (PdoExt && PdoExt->Self)
        ObDereferenceObject(PdoExt->Self);
}

static
BOOLEAN
NTAPI
Rp1TranslateBusAddress(
    _Inout_opt_ PVOID Context,
    _In_ PHYSICAL_ADDRESS BusAddress,
    _In_ ULONG Length,
    _Out_ PULONG AddressSpace,
    _Out_ PPHYSICAL_ADDRESS TranslatedAddress)
{
    PRP1_PDO_EXTENSION PdoExt = (PRP1_PDO_EXTENSION)Context;
    ULONGLONG Offset;

    if (!PdoExt || !TranslatedAddress || !AddressSpace)
        return FALSE;

    if (BusAddress.QuadPart >= PdoExt->MmioBusAddress.QuadPart &&
        BusAddress.QuadPart < PdoExt->MmioBusAddress.QuadPart + PdoExt->MmioLength)
    {
        Offset = BusAddress.QuadPart - PdoExt->MmioBusAddress.QuadPart;
        if (Length > PdoExt->MmioLength - Offset)
            return FALSE;

        TranslatedAddress->QuadPart = PdoExt->MmioPhysical.QuadPart + Offset;
        *AddressSpace = 0;
        return TRUE;
    }

    if (BusAddress.QuadPart >= PdoExt->MmioPhysical.QuadPart &&
        BusAddress.QuadPart < PdoExt->MmioPhysical.QuadPart + PdoExt->MmioLength)
    {
        *TranslatedAddress = BusAddress;
        *AddressSpace = 0;
        return TRUE;
    }

    return FALSE;
}

static
PDMA_ADAPTER
NTAPI
Rp1GetDmaAdapter(
    _Inout_opt_ PVOID Context,
    _In_ PDEVICE_DESCRIPTION DeviceDescriptor,
    _Out_ PULONG NumberOfMapRegisters)
{
    PRP1_PDO_EXTENSION PdoExt = (PRP1_PDO_EXTENSION)Context;
    PRP1_FDO_EXTENSION FdoExt;

    if (!PdoExt || !PdoExt->ParentFdo)
        return NULL;

    FdoExt = (PRP1_FDO_EXTENSION)PdoExt->ParentFdo->DeviceExtension;
    return IoGetDmaAdapter(FdoExt->PhysicalDevice,
                           DeviceDescriptor,
                           NumberOfMapRegisters);
}

static
ULONG
NTAPI
Rp1GetBusData(
    _Inout_opt_ PVOID Context,
    _In_ ULONG DataType,
    _Inout_updates_bytes_(Length) PVOID Buffer,
    _In_ ULONG Offset,
    _In_ ULONG Length)
{
    PRP1_PDO_EXTENSION PdoExt = (PRP1_PDO_EXTENSION)Context;
    PCI_COMMON_CONFIG PciConfig;
    ULONG Available;

    if (!PdoExt || !Buffer || DataType != PCI_WHICHSPACE_CONFIG)
        return 0;

    if (Offset >= sizeof(PciConfig))
        return 0;

    Rp1BuildSyntheticPciConfig(PdoExt, &PciConfig);

    Available = sizeof(PciConfig) - Offset;
    if (Length > Available)
        Length = Available;

    RtlCopyMemory(Buffer, (PUCHAR)&PciConfig + Offset, Length);
    return Length;
}

static
ULONG
NTAPI
Rp1SetBusData(
    _Inout_opt_ PVOID Context,
    _In_ ULONG DataType,
    _Inout_updates_bytes_(Length) PVOID Buffer,
    _In_ ULONG Offset,
    _In_ ULONG Length)
{
    PRP1_PDO_EXTENSION PdoExt = (PRP1_PDO_EXTENSION)Context;
    PCI_COMMON_CONFIG PciConfig;
    ULONG Available;

    if (!PdoExt || !Buffer || DataType != PCI_WHICHSPACE_CONFIG)
        return 0;

    if (Offset >= sizeof(PciConfig))
        return 0;

    Rp1BuildSyntheticPciConfig(PdoExt, &PciConfig);

    Available = sizeof(PciConfig) - Offset;
    if (Length > Available)
        Length = Available;

    RtlCopyMemory((PUCHAR)&PciConfig + Offset, Buffer, Length);

    PdoExt->PciCommand = PciConfig.Command;
    PdoExt->CacheLineSize = PciConfig.CacheLineSize;
    PdoExt->LatencyTimer = PciConfig.LatencyTimer;
    PdoExt->InterruptLine = PciConfig.u.type0.InterruptLine;

    return Length;
}

/* ------------------------------------------------------------------ */
/*  FDO: IRP_MN_START_DEVICE                                           */
/*  Parse PCI resources to find BAR1 and map it.                       */
/* ------------------------------------------------------------------ */

static
NTSTATUS
Rp1FdoStartDevice(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PRP1_FDO_EXTENSION FdoExt;
    PCM_RESOURCE_LIST AllocatedResourcesTranslated;
    PCM_PARTIAL_RESOURCE_LIST PartialList;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptor;
    PIO_STACK_LOCATION IrpSp;
    NTSTATUS Status;
    ULONG i;
    BOOLEAN FoundMemory = FALSE;
    BOOLEAN FoundInterrupt = FALSE;

    FdoExt = (PRP1_FDO_EXTENSION)DeviceObject->DeviceExtension;
    IrpSp = IoGetCurrentIrpStackLocation(Irp);

    /* First, pass the IRP down to the PCI bus driver so it programs BARs */
    Status = Rp1SendIrpSynchronous(FdoExt->LowerDevice, Irp);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("RP1: Lower driver failed START_DEVICE (0x%08lx)\n", Status);
        return Status;
    }

    /*
     * Parse the parent PCI resources to find BAR1 and the shared interrupt.
     */
    AllocatedResourcesTranslated = IrpSp->Parameters.StartDevice.AllocatedResourcesTranslated;

    if (!AllocatedResourcesTranslated || AllocatedResourcesTranslated->Count == 0)
    {
        DPRINT1("RP1: No translated resources provided\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    PartialList = &AllocatedResourcesTranslated->List[0].PartialResourceList;
    Descriptor = &PartialList->PartialDescriptors[0];

    for (i = 0; i < PartialList->Count; i++, Descriptor++)
    {
        switch (Descriptor->Type)
        {
            case CmResourceTypeMemory:
                /*
                 * RP1 has multiple BARs. BAR0 is the PCIe-to-AXI bridge config
                 * space (small, ~64K). BAR1 is the 4 MB peripheral space.
                 * We want the largest memory region, which is BAR1.
                 */
                if (!FoundMemory || Descriptor->u.Memory.Length > FdoExt->Bar1Length)
                {
                    FdoExt->Bar1BusAddress = Descriptor->u.Memory.Start;
                    FdoExt->Bar1Length = Descriptor->u.Memory.Length;
                    FoundMemory = TRUE;
                }
                break;

            case CmResourceTypeInterrupt:
                if (!FoundInterrupt)
                {
                    FdoExt->InterruptLevel = Descriptor->u.Interrupt.Level;
                    FdoExt->InterruptVector = Descriptor->u.Interrupt.Vector;
                    FdoExt->InterruptAffinity = Descriptor->u.Interrupt.Affinity;
                    FoundInterrupt = TRUE;
                }
                break;

            default:
                break;
        }
    }

    if (!FoundMemory)
    {
        DPRINT1("RP1: No memory resource found in PCI resources\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /*
     * Keep bus-relative PCI addresses in child resources.  The CPU-visible
     * address is only used for mapping the parent BAR.
     */
    FdoExt->Bar1CpuAddress = Rp1TranslateBarAddress(FdoExt->Bar1BusAddress);

    if (FdoExt->Bar1Length < RP1_BAR1_SIZE)
    {
        DPRINT1("RP1: BAR1 resource too small: bus=0x%I64x cpu=0x%I64x length=0x%lx\n",
                FdoExt->Bar1BusAddress.QuadPart,
                FdoExt->Bar1CpuAddress.QuadPart,
                FdoExt->Bar1Length);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /*
     * Map BAR1 into kernel virtual address space.
     * We map the entire 4 MB region; child controllers will use
     * sub-ranges identified by their offsets.
     */
    FdoExt->Bar1Virtual = MmMapIoSpace(FdoExt->Bar1CpuAddress,
                                        FdoExt->Bar1Length,
                                        MmNonCached);
    if (!FdoExt->Bar1Virtual)
    {
        DPRINT1("RP1: Failed to map BAR1 (phys=0x%I64x, len=0x%lx)\n",
                FdoExt->Bar1CpuAddress.QuadPart, FdoExt->Bar1Length);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /*
     * Enable RP1 child clock domains.  Firmware may gate the xHCI clocks
     * before OS handoff; child PDOs require the USB and Ethernet clocks
     * before their register blocks are usable.
     */
    {
        PUCHAR ClkBase = (PUCHAR)FdoExt->Bar1Virtual + RP1_CLOCKS_OFFSET;
        ULONG Val;

        /* Enable USBH0 microframe clock */
        Val = READ_REGISTER_ULONG((PULONG)(ClkBase + RP1_CLK_USBH0_MICROFRAME_CTRL));
        if (!(Val & RP1_CLK_CTRL_ENABLE))
        {
            WRITE_REGISTER_ULONG((PULONG)(ClkBase + RP1_CLK_USBH0_MICROFRAME_CTRL),
                                 Val | RP1_CLK_CTRL_ENABLE);
        }

        /* Enable USBH0 suspend clock */
        Val = READ_REGISTER_ULONG((PULONG)(ClkBase + RP1_CLK_USBH0_SUSPEND_CTRL));
        if (!(Val & RP1_CLK_CTRL_ENABLE))
        {
            WRITE_REGISTER_ULONG((PULONG)(ClkBase + RP1_CLK_USBH0_SUSPEND_CTRL),
                                 Val | RP1_CLK_CTRL_ENABLE);
        }

        /* Enable USBH1 microframe clock */
        Val = READ_REGISTER_ULONG((PULONG)(ClkBase + RP1_CLK_USBH1_MICROFRAME_CTRL));
        if (!(Val & RP1_CLK_CTRL_ENABLE))
        {
            WRITE_REGISTER_ULONG((PULONG)(ClkBase + RP1_CLK_USBH1_MICROFRAME_CTRL),
                                 Val | RP1_CLK_CTRL_ENABLE);
        }

        /* Enable USBH1 suspend clock */
        Val = READ_REGISTER_ULONG((PULONG)(ClkBase + RP1_CLK_USBH1_SUSPEND_CTRL));
        if (!(Val & RP1_CLK_CTRL_ENABLE))
        {
            WRITE_REGISTER_ULONG((PULONG)(ClkBase + RP1_CLK_USBH1_SUSPEND_CTRL),
                                 Val | RP1_CLK_CTRL_ENABLE);
        }

        /* Enable Ethernet MAC clock */
        Val = READ_REGISTER_ULONG((PULONG)(ClkBase + RP1_CLK_ETH_CTRL));
        if (!(Val & RP1_CLK_CTRL_ENABLE))
        {
            WRITE_REGISTER_ULONG((PULONG)(ClkBase + RP1_CLK_ETH_CTRL),
                                 Val | RP1_CLK_CTRL_ENABLE);
        }

        /* Enable Ethernet timestamp clock */
        Val = READ_REGISTER_ULONG((PULONG)(ClkBase + RP1_CLK_ETH_TSU_CTRL));
        if (!(Val & RP1_CLK_CTRL_ENABLE))
        {
            WRITE_REGISTER_ULONG((PULONG)(ClkBase + RP1_CLK_ETH_TSU_CTRL),
                                 Val | RP1_CLK_CTRL_ENABLE);
        }

        /* Allow the clock enables to take effect. */
        KeStallExecutionProcessor(100);
    }

    /*
     * Prepare each DWC3 core for its child xHCI driver.  Keep reset ownership
     * with xHCI; this bus driver only selects host mode and clears PHY suspend
     * state.
     */
    {
        static const ULONG DwcOffsets[] = { RP1_XHCI0_OFFSET, RP1_XHCI1_OFFSET };
        ULONG Idx;
        ULONG PresentCount = 0;

        RtlZeroMemory(FdoExt->ChildPresent, sizeof(FdoExt->ChildPresent));

        for (Idx = 0; Idx < RTL_NUMBER_OF(DwcOffsets); Idx++)
        {
            PUCHAR DwcBase = (PUCHAR)FdoExt->Bar1Virtual + DwcOffsets[Idx];
            ULONG Reg, SnpsId;

            /* Verify DWC3 core identity. */
            SnpsId = READ_REGISTER_ULONG((PULONG)(DwcBase + DWC3_GSNPSID));
            if (SnpsId == 0 || SnpsId == 0xFFFFFFFF)
            {
                DPRINT1("RP1: DWC3[%lu] GSNPSID=0x%08lx, core not accessible\n",
                        Idx,
                        SnpsId);
                continue;
            }

            /* Select host mode without issuing a DWC3 core reset. */
            Reg = READ_REGISTER_ULONG((PULONG)(DwcBase + DWC3_GCTL));
            if ((Reg & DWC3_GCTL_PRTCAPDIR_MASK) != DWC3_GCTL_PRTCAP_HOST)
            {
                Reg &= ~DWC3_GCTL_PRTCAPDIR_MASK;
                Reg |= DWC3_GCTL_PRTCAP_HOST;
                WRITE_REGISTER_ULONG((PULONG)(DwcBase + DWC3_GCTL), Reg);
            }

            /* Configure USB2 PHY: disable suspend during init */
            Reg = READ_REGISTER_ULONG((PULONG)(DwcBase + DWC3_GUSB2PHYCFG(0)));
            Reg &= ~DWC3_GUSB2PHYCFG_SUSPHY;
            Reg &= ~DWC3_GUSB2PHYCFG_ENBLSLPM;
            WRITE_REGISTER_ULONG((PULONG)(DwcBase + DWC3_GUSB2PHYCFG(0)), Reg);

            /* Configure USB3 PIPE: disable suspend */
            Reg = READ_REGISTER_ULONG((PULONG)(DwcBase + DWC3_GUSB3PIPECTL(0)));
            Reg &= ~DWC3_GUSB3PIPECTL_SUSPHY;
            Reg &= ~DWC3_GUSB3PIPECTL_UX_EXIT_PX;
            WRITE_REGISTER_ULONG((PULONG)(DwcBase + DWC3_GUSB3PIPECTL(0)), Reg);

            /* Program GFLADJ for 30 MHz reference timing. */
            {
                ULONG Gfladj = READ_REGISTER_ULONG((PULONG)(DwcBase + DWC3_GFLADJ));
                Gfladj &= ~DWC3_GFLADJ_30MHZ_MASK;
                Gfladj |= DWC3_GFLADJ_30MHZ_SDBND_SEL | 0x20;
                WRITE_REGISTER_ULONG((PULONG)(DwcBase + DWC3_GFLADJ), Gfladj);
            }

            /* Verify xHCI capability registers. */
            {
                ULONG CapHdr = READ_REGISTER_ULONG((PULONG)DwcBase);
                ULONG CapLen = CapHdr & 0xFF;
                ULONG HciVer = (CapHdr >> 16) & 0xFFFF;

                if (CapLen == 0 || CapLen == 0xFF || HciVer < 0x0100)
                {
                    DPRINT1("RP1: DWC3[%lu] xHCI capability header invalid\n",
                            Idx);
                    continue;
                }

                FdoExt->ChildPresent[Idx] = TRUE;
                PresentCount++;
            }
        }

        if (PresentCount == 0)
        {
            DPRINT1("RP1: no usable DWC3 xHCI controllers found\n");
            MmUnmapIoSpace(FdoExt->Bar1Virtual, FdoExt->Bar1Length);
            FdoExt->Bar1Virtual = NULL;
            return STATUS_NO_SUCH_DEVICE;
        }
    }

    if (FdoExt->Bar1Length >= RP1_ETH_OFFSET + RP1_ETH_SIZE)
    {
        Status = Rp1InitializeInterrupts(FdoExt);
        if (!NT_SUCCESS(Status))
        {
            DPRINT1("RP1: internal MSI-X setup failed (0x%08lx), keeping legacy interrupt resources\n",
                    Status);
        }

        FdoExt->ChildPresent[RP1_CHILD_ETHERNET] = TRUE;
        Rp1EnableEthernetInterruptRoute(FdoExt);
        DPRINT("RP1: Ethernet child present at BAR1+0x%lx length 0x%lx\n",
               RP1_ETH_OFFSET,
               RP1_ETH_SIZE);
    }

    if (!FoundInterrupt)
    {
        DPRINT1("RP1: WARNING: No interrupt in PCI resources\n");
    }

    FdoExt->Started = TRUE;

    return STATUS_SUCCESS;
}

/* ------------------------------------------------------------------ */
/*  FDO: IRP_MN_QUERY_DEVICE_RELATIONS (BusRelations)                  */
/*  Create child PDOs for verified RP1 peripherals.                    */
/* ------------------------------------------------------------------ */

static
NTSTATUS
Rp1FdoQueryBusRelations(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PRP1_FDO_EXTENSION FdoExt;
    PDEVICE_RELATIONS Relations;
    PRP1_PDO_EXTENSION PdoExt;
    NTSTATUS Status;
    ULONG i;
    ULONG Size;

    FdoExt = (PRP1_FDO_EXTENSION)DeviceObject->DeviceExtension;

    if (!FdoExt->Started)
    {
        DPRINT1("RP1: BusRelations queried before START_DEVICE\n");
        Irp->IoStatus.Status = STATUS_SUCCESS;
        Irp->IoStatus.Information = 0;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_SUCCESS;
    }

    FdoExt->ChildCount = 0;

    /* Create child PDOs for controllers that were verified at START_DEVICE. */
    for (i = 0; i < RP1_MAX_CHILDREN; i++)
    {
        if (!FdoExt->ChildPresent[i])
            continue;

        if (FdoExt->ChildPdo[i])
        {
            FdoExt->ChildCount++;
            continue;
        }

        Status = IoCreateDevice(DeviceObject->DriverObject,
                                sizeof(RP1_PDO_EXTENSION),
                                NULL,
                                FILE_DEVICE_CONTROLLER,
                                FILE_AUTOGENERATED_DEVICE_NAME,
                                FALSE,
                                &FdoExt->ChildPdo[i]);

        if (!NT_SUCCESS(Status))
        {
            DPRINT1("RP1: Failed to create child PDO %lu (0x%08lx)\n", i, Status);
            continue;
        }

        PdoExt = (PRP1_PDO_EXTENSION)FdoExt->ChildPdo[i]->DeviceExtension;
        RtlZeroMemory(PdoExt, sizeof(RP1_PDO_EXTENSION));

        PdoExt->Common.IsFdo = FALSE;
        PdoExt->Self = FdoExt->ChildPdo[i];
        PdoExt->ParentFdo = DeviceObject;
        PdoExt->ChildIndex = i;

        /* Compute physical address for this child's MMIO region */
        PdoExt->MmioBusAddress.QuadPart =
            FdoExt->Bar1BusAddress.QuadPart + Rp1Children[i].Offset;
        PdoExt->MmioPhysical.QuadPart =
            FdoExt->Bar1CpuAddress.QuadPart + Rp1Children[i].Offset;
        PdoExt->MmioLength = Rp1Children[i].Length;

        if (!Rp1GetChildInterrupt(FdoExt,
                                  i,
                                  &PdoExt->InterruptLevel,
                                  &PdoExt->InterruptVector,
                                  &PdoExt->InterruptAffinity,
                                  &PdoExt->InterruptFlags,
                                  &PdoExt->InterruptShareDisposition,
                                  &PdoExt->InterruptLine))
        {
            PdoExt->InterruptLine = 0xFF;
        }
        PdoExt->PciCommand = PCI_ENABLE_MEMORY_SPACE | PCI_ENABLE_BUS_MASTER;

        FdoExt->ChildPdo[i]->Flags &= ~DO_DEVICE_INITIALIZING;

        FdoExt->ChildCount++;

    }

    /* Build DEVICE_RELATIONS structure */
    Size = FIELD_OFFSET(DEVICE_RELATIONS, Objects) +
           FdoExt->ChildCount * sizeof(PDEVICE_OBJECT);

    Relations = ExAllocatePoolWithTag(PagedPool, Size, RP1_TAG);
    if (!Relations)
    {
        DPRINT1("RP1: Failed to allocate DEVICE_RELATIONS\n");
        Irp->IoStatus.Status = STATUS_INSUFFICIENT_RESOURCES;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Relations->Count = 0;
    for (i = 0; i < RP1_MAX_CHILDREN; i++)
    {
        if (FdoExt->ChildPresent[i] && FdoExt->ChildPdo[i])
        {
            ObReferenceObject(FdoExt->ChildPdo[i]);
            Relations->Objects[Relations->Count++] = FdoExt->ChildPdo[i];
        }
    }

    Irp->IoStatus.Information = (ULONG_PTR)Relations;
    Irp->IoStatus.Status = STATUS_SUCCESS;

    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return STATUS_SUCCESS;
}

/* ------------------------------------------------------------------ */
/*  FDO: IRP_MN_REMOVE_DEVICE                                         */
/* ------------------------------------------------------------------ */

static
NTSTATUS
Rp1FdoRemoveDevice(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PRP1_FDO_EXTENSION FdoExt;
    ULONG i;

    FdoExt = (PRP1_FDO_EXTENSION)DeviceObject->DeviceExtension;

    /* Delete child PDOs */
    for (i = 0; i < RP1_MAX_CHILDREN; i++)
    {
        if (FdoExt->ChildPdo[i])
        {
            IoDeleteDevice(FdoExt->ChildPdo[i]);
            FdoExt->ChildPdo[i] = NULL;
        }
    }
    FdoExt->ChildCount = 0;

    /* Unmap BAR1 */
    if (FdoExt->Bar1Virtual)
    {
        MmUnmapIoSpace(FdoExt->Bar1Virtual, FdoExt->Bar1Length);
        FdoExt->Bar1Virtual = NULL;
    }

    FdoExt->Started = FALSE;

    /* Pass the IRP down */
    Irp->IoStatus.Status = STATUS_SUCCESS;
    IoSkipCurrentIrpStackLocation(Irp);
    IoCallDriver(FdoExt->LowerDevice, Irp);

    /* Detach and delete the FDO */
    IoDetachDevice(FdoExt->LowerDevice);
    IoDeleteDevice(DeviceObject);

    return STATUS_SUCCESS;
}

/* ------------------------------------------------------------------ */
/*  FDO PnP dispatch                                                   */
/* ------------------------------------------------------------------ */

NTSTATUS
Rp1FdoPnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PRP1_FDO_EXTENSION FdoExt;
    PIO_STACK_LOCATION IrpSp;
    NTSTATUS Status;

    FdoExt = (PRP1_FDO_EXTENSION)DeviceObject->DeviceExtension;
    IrpSp = IoGetCurrentIrpStackLocation(Irp);

    switch (IrpSp->MinorFunction)
    {
        case IRP_MN_START_DEVICE:
            Status = Rp1FdoStartDevice(DeviceObject, Irp);
            Irp->IoStatus.Status = Status;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;

        case IRP_MN_QUERY_DEVICE_RELATIONS:
            if (IrpSp->Parameters.QueryDeviceRelations.Type == BusRelations)
            {
                return Rp1FdoQueryBusRelations(DeviceObject, Irp);
            }
            /* Fall through for other relation types */
            break;

        case IRP_MN_REMOVE_DEVICE:
            return Rp1FdoRemoveDevice(DeviceObject, Irp);

        case IRP_MN_QUERY_STOP_DEVICE:
        case IRP_MN_QUERY_REMOVE_DEVICE:
            Irp->IoStatus.Status = STATUS_SUCCESS;
            break;

        case IRP_MN_CANCEL_STOP_DEVICE:
        case IRP_MN_CANCEL_REMOVE_DEVICE:
            Irp->IoStatus.Status = STATUS_SUCCESS;
            break;

        case IRP_MN_STOP_DEVICE:
            /* Unmap BAR1 on stop */
            if (FdoExt->Bar1Virtual)
            {
                MmUnmapIoSpace(FdoExt->Bar1Virtual, FdoExt->Bar1Length);
                FdoExt->Bar1Virtual = NULL;
            }
            FdoExt->Started = FALSE;
            Irp->IoStatus.Status = STATUS_SUCCESS;
            break;

        case IRP_MN_SURPRISE_REMOVAL:
            Irp->IoStatus.Status = STATUS_SUCCESS;
            break;

        default:
            break;
    }

    /* Pass unhandled IRPs down the stack */
    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(FdoExt->LowerDevice, Irp);
}

/* ------------------------------------------------------------------ */
/*  PDO: IRP_MN_QUERY_ID                                               */
/*  Return hardware/compatible/instance/device IDs that allow          */
/*  class or hardware drivers to bind to RP1 child PDOs.               */
/* ------------------------------------------------------------------ */

static
NTSTATUS
Rp1PdoQueryId(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp,
    _In_ PIO_STACK_LOCATION IrpSp)
{
    PRP1_PDO_EXTENSION PdoExt;
    const RP1_CHILD_DESCRIPTOR *Child;
    PWCHAR Buffer;
    SIZE_T Size;

    PdoExt = (PRP1_PDO_EXTENSION)DeviceObject->DeviceExtension;
    Child = &Rp1Children[PdoExt->ChildIndex];

    switch (IrpSp->Parameters.QueryId.IdType)
    {
        case BusQueryDeviceID:
        {
            Size = (wcslen(Child->DeviceId) + 1) * sizeof(WCHAR);
            Buffer = ExAllocatePoolWithTag(PagedPool, Size, RP1_TAG);
            if (!Buffer)
                return STATUS_INSUFFICIENT_RESOURCES;

            RtlCopyMemory(Buffer, Child->DeviceId, Size);
            Irp->IoStatus.Information = (ULONG_PTR)Buffer;
            return STATUS_SUCCESS;
        }

        case BusQueryHardwareIDs:
        {
            /*
             * Hardware IDs describe the RP1 child itself.  PCI class
             * compatible IDs are reported separately.
             */
            SIZE_T HardwareIdSize = (wcslen(Child->HardwareId) + 1) * sizeof(WCHAR);
            SIZE_T DeviceIdSize = (wcslen(Child->DeviceId) + 1) * sizeof(WCHAR);

            Size = HardwareIdSize + DeviceIdSize + sizeof(WCHAR);
            Buffer = ExAllocatePoolWithTag(PagedPool, Size, RP1_TAG);
            if (!Buffer)
                return STATUS_INSUFFICIENT_RESOURCES;

            RtlZeroMemory(Buffer, Size);
            RtlCopyMemory(Buffer, Child->HardwareId, HardwareIdSize);
            RtlCopyMemory((PUCHAR)Buffer + HardwareIdSize,
                          Child->DeviceId,
                          DeviceIdSize);

            Irp->IoStatus.Information = (ULONG_PTR)Buffer;
            return STATUS_SUCCESS;
        }

        case BusQueryCompatibleIDs:
        {
            SIZE_T CompatibleIdSize;

            if (!Child->CompatibleId)
                return STATUS_NOT_SUPPORTED;

            CompatibleIdSize = (wcslen(Child->CompatibleId) + 1) * sizeof(WCHAR);
            Size = CompatibleIdSize + sizeof(WCHAR);
            Buffer = ExAllocatePoolWithTag(PagedPool, Size, RP1_TAG);
            if (!Buffer)
                return STATUS_INSUFFICIENT_RESOURCES;

            RtlZeroMemory(Buffer, Size);
            RtlCopyMemory(Buffer, Child->CompatibleId, CompatibleIdSize);

            Irp->IoStatus.Information = (ULONG_PTR)Buffer;
            return STATUS_SUCCESS;
        }

        case BusQueryInstanceID:
        {
            PCWSTR InstanceId = Child->InstanceId;
            Size = (wcslen(InstanceId) + 1) * sizeof(WCHAR);

            Buffer = ExAllocatePoolWithTag(PagedPool, Size, RP1_TAG);
            if (!Buffer)
                return STATUS_INSUFFICIENT_RESOURCES;

            RtlCopyMemory(Buffer, InstanceId, Size);
            Irp->IoStatus.Information = (ULONG_PTR)Buffer;
            return STATUS_SUCCESS;
        }

        default:
            return Irp->IoStatus.Status;
    }
}

/* ------------------------------------------------------------------ */
/*  PDO: IRP_MN_QUERY_DEVICE_TEXT                                      */
/* ------------------------------------------------------------------ */

static
NTSTATUS
Rp1PdoQueryDeviceText(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp,
    _In_ PIO_STACK_LOCATION IrpSp)
{
    PRP1_PDO_EXTENSION PdoExt;
    const RP1_CHILD_DESCRIPTOR *Child;
    PWCHAR Buffer;
    SIZE_T Size;

    PdoExt = (PRP1_PDO_EXTENSION)DeviceObject->DeviceExtension;
    Child = &Rp1Children[PdoExt->ChildIndex];

    if (IrpSp->Parameters.QueryDeviceText.DeviceTextType == DeviceTextDescription)
    {
        Size = (wcslen(Child->Description) + 1) * sizeof(WCHAR);
        Buffer = ExAllocatePoolWithTag(PagedPool, Size, RP1_TAG);
        if (!Buffer)
            return STATUS_INSUFFICIENT_RESOURCES;

        RtlCopyMemory(Buffer, Child->Description, Size);
        Irp->IoStatus.Information = (ULONG_PTR)Buffer;
        return STATUS_SUCCESS;
    }

    return Irp->IoStatus.Status;
}

/* ------------------------------------------------------------------ */
/*  PDO: IRP_MN_QUERY_RESOURCES                                        */
/*  Return the assigned (boot) resources for the child controller.     */
/* ------------------------------------------------------------------ */

static
NTSTATUS
Rp1PdoQueryResources(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PRP1_PDO_EXTENSION PdoExt;
    PCM_RESOURCE_LIST ResourceList;
    PCM_PARTIAL_RESOURCE_LIST PartialList;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptor;
    ULONG ListSize;
    ULONG ResCount;

    PdoExt = (PRP1_PDO_EXTENSION)DeviceObject->DeviceExtension;

    /*
     * Each child controller gets:
     * 1) A CmResourceTypeMemory descriptor for its MMIO range
     * 2) A CmResourceTypeInterrupt descriptor (shared with parent)
     */
    ResCount = 1; /* Memory */
    if (PdoExt->InterruptVector != 0)
        ResCount++; /* Interrupt */

    ListSize = FIELD_OFFSET(CM_RESOURCE_LIST,
                            List[0].PartialResourceList.PartialDescriptors) +
               ResCount * sizeof(CM_PARTIAL_RESOURCE_DESCRIPTOR);

    ResourceList = ExAllocatePoolWithTag(PagedPool, ListSize, RP1_TAG);
    if (!ResourceList)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(ResourceList, ListSize);
    ResourceList->Count = 1;
    ResourceList->List[0].InterfaceType = Internal;
    ResourceList->List[0].BusNumber = 0;

    PartialList = &ResourceList->List[0].PartialResourceList;
    PartialList->Version = 1;
    PartialList->Revision = 1;
    PartialList->Count = ResCount;

    /* Memory resource */
    Descriptor = &PartialList->PartialDescriptors[0];
    Descriptor->Type = CmResourceTypeMemory;
    Descriptor->ShareDisposition = CmResourceShareDeviceExclusive;
    Descriptor->Flags = CM_RESOURCE_MEMORY_READ_WRITE;
    Descriptor->u.Memory.Start = PdoExt->MmioPhysical;
    Descriptor->u.Memory.Length = PdoExt->MmioLength;

    /*
     * RP1 child controllers share the parent interrupt.  Keep Level and Vector
     * equal because this ARM64 HAL reports GSIs directly as interrupt vectors.
     */
    if (PdoExt->InterruptVector != 0)
    {
        Descriptor++;
        Descriptor->Type = CmResourceTypeInterrupt;
        Descriptor->ShareDisposition = PdoExt->InterruptShareDisposition;
        Descriptor->Flags = PdoExt->InterruptFlags;
        Descriptor->u.Interrupt.Level = PdoExt->InterruptLevel;
        Descriptor->u.Interrupt.Vector = PdoExt->InterruptVector;
        Descriptor->u.Interrupt.Affinity = PdoExt->InterruptAffinity;
    }

    Irp->IoStatus.Information = (ULONG_PTR)ResourceList;

    return STATUS_SUCCESS;
}

/* ------------------------------------------------------------------ */
/*  PDO: IRP_MN_QUERY_RESOURCE_REQUIREMENTS                           */
/*  Return the resource requirements for the child controller.         */
/* ------------------------------------------------------------------ */

static
NTSTATUS
Rp1PdoQueryResourceRequirements(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PRP1_PDO_EXTENSION PdoExt;
    PIO_RESOURCE_REQUIREMENTS_LIST ReqList;
    PIO_RESOURCE_DESCRIPTOR Descriptor;
    ULONG ListSize;
    ULONG ResCount;

    PdoExt = (PRP1_PDO_EXTENSION)DeviceObject->DeviceExtension;

    /*
     * Resources: one memory range + optionally one interrupt.
     * The memory range is fixed (from BAR1 offset), not relocatable.
     */
    ResCount = 1; /* Memory */
    if (PdoExt->InterruptVector != 0)
        ResCount++; /* Interrupt */

    ListSize = sizeof(IO_RESOURCE_REQUIREMENTS_LIST) +
               (ResCount - 1) * sizeof(IO_RESOURCE_DESCRIPTOR);

    ReqList = ExAllocatePoolWithTag(PagedPool, ListSize, RP1_TAG);
    if (!ReqList)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(ReqList, ListSize);
    ReqList->ListSize = ListSize;
    ReqList->InterfaceType = Internal;
    ReqList->BusNumber = 0;
    ReqList->SlotNumber = 0;
    ReqList->AlternativeLists = 1;
    ReqList->List[0].Version = 1;
    ReqList->List[0].Revision = 1;
    ReqList->List[0].Count = ResCount;

    /* Memory resource descriptor */
    Descriptor = &ReqList->List[0].Descriptors[0];
    Descriptor->Option = IO_RESOURCE_PREFERRED;
    Descriptor->Type = CmResourceTypeMemory;
    Descriptor->ShareDisposition = CmResourceShareDeviceExclusive;
    Descriptor->Flags = CM_RESOURCE_MEMORY_READ_WRITE;
    Descriptor->u.Memory.Length = PdoExt->MmioLength;
    Descriptor->u.Memory.Alignment = 1;
    Descriptor->u.Memory.MinimumAddress = PdoExt->MmioPhysical;
    Descriptor->u.Memory.MaximumAddress.QuadPart =
        PdoExt->MmioPhysical.QuadPart + PdoExt->MmioLength - 1;

    /* Interrupt resource descriptor */
    if (PdoExt->InterruptVector != 0)
    {
        Descriptor++;
        Descriptor->Option = IO_RESOURCE_PREFERRED;
        Descriptor->Type = CmResourceTypeInterrupt;
        Descriptor->ShareDisposition = PdoExt->InterruptShareDisposition;
        Descriptor->Flags = PdoExt->InterruptFlags;
        Descriptor->u.Interrupt.MinimumVector = PdoExt->InterruptVector;
        Descriptor->u.Interrupt.MaximumVector = PdoExt->InterruptVector;
    }

    Irp->IoStatus.Information = (ULONG_PTR)ReqList;

    return STATUS_SUCCESS;
}

/* ------------------------------------------------------------------ */
/*  PDO: IRP_MN_QUERY_CAPABILITIES                                     */
/* ------------------------------------------------------------------ */

static
NTSTATUS
Rp1PdoQueryCapabilities(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp,
    _In_ PIO_STACK_LOCATION IrpSp)
{
    PRP1_PDO_EXTENSION PdoExt;
    PDEVICE_CAPABILITIES Caps;

    PdoExt = (PRP1_PDO_EXTENSION)DeviceObject->DeviceExtension;

    Caps = IrpSp->Parameters.DeviceCapabilities.Capabilities;

    if (Caps->Version != 1 || Caps->Size < sizeof(DEVICE_CAPABILITIES))
        return STATUS_UNSUCCESSFUL;

    /* This device cannot be physically removed */
    Caps->Removable = FALSE;
    Caps->EjectSupported = FALSE;
    Caps->SurpriseRemovalOK = FALSE;

    Caps->UniqueID = Rp1Children[PdoExt->ChildIndex].UniqueId;
    Caps->Address = PdoExt->ChildIndex;
    Caps->UINumber = MAXULONG;

    /* D-state mapping: always D0 */
    Caps->DeviceState[PowerSystemWorking] = PowerDeviceD0;
    Caps->DeviceState[PowerSystemSleeping1] = PowerDeviceD3;
    Caps->DeviceState[PowerSystemSleeping2] = PowerDeviceD3;
    Caps->DeviceState[PowerSystemSleeping3] = PowerDeviceD3;
    Caps->DeviceState[PowerSystemHibernate] = PowerDeviceD3;
    Caps->DeviceState[PowerSystemShutdown] = PowerDeviceD3;

    return STATUS_SUCCESS;
}

/* ------------------------------------------------------------------ */
/*  PDO: IRP_MN_QUERY_DEVICE_RELATIONS (TargetDeviceRelation)          */
/* ------------------------------------------------------------------ */

static
NTSTATUS
Rp1PdoQueryDeviceRelations(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp,
    _In_ PIO_STACK_LOCATION IrpSp)
{
    PDEVICE_RELATIONS Relations;

    if (IrpSp->Parameters.QueryDeviceRelations.Type != TargetDeviceRelation)
        return Irp->IoStatus.Status;

    Relations = ExAllocatePoolWithTag(PagedPool,
                                      sizeof(DEVICE_RELATIONS),
                                      RP1_TAG);
    if (!Relations)
        return STATUS_INSUFFICIENT_RESOURCES;

    Relations->Count = 1;
    Relations->Objects[0] = DeviceObject;
    ObReferenceObject(DeviceObject);

    Irp->IoStatus.Information = (ULONG_PTR)Relations;
    return STATUS_SUCCESS;
}

/* ------------------------------------------------------------------ */
/*  PDO: IRP_MN_QUERY_BUS_INFORMATION                                  */
/* ------------------------------------------------------------------ */

static
NTSTATUS
Rp1PdoQueryBusInformation(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PPNP_BUS_INFORMATION BusInfo;

    UNREFERENCED_PARAMETER(DeviceObject);

    BusInfo = ExAllocatePoolWithTag(PagedPool,
                                    sizeof(PNP_BUS_INFORMATION),
                                    RP1_TAG);
    if (!BusInfo)
        return STATUS_INSUFFICIENT_RESOURCES;

    RtlZeroMemory(BusInfo, sizeof(PNP_BUS_INFORMATION));
    BusInfo->BusTypeGuid = GUID_BUS_TYPE_INTERNAL;
    BusInfo->LegacyBusType = Internal;
    BusInfo->BusNumber = 0;

    Irp->IoStatus.Information = (ULONG_PTR)BusInfo;
    return STATUS_SUCCESS;
}

/* ------------------------------------------------------------------ */
/*  PDO: IRP_MN_QUERY_INTERFACE (BUS_INTERFACE_STANDARD)               */
/*  Provides PCI-like config access for RP1 child PDOs.                 */
/* ------------------------------------------------------------------ */

/*
 * PDO: IRP_MN_QUERY_INTERFACE (BUS_INTERFACE_STANDARD)
 *
 * Function drivers use BUS_INTERFACE_STANDARD for config-space and DMA
 * adapter queries.  RP1 child devices are BAR slices inside one PCI
 * function, so expose a per-child config view.
 */
static
NTSTATUS
Rp1PdoQueryInterface(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp,
    _In_ PIO_STACK_LOCATION IrpSp)
{
    PRP1_PDO_EXTENSION PdoExt;
    PBUS_INTERFACE_STANDARD BusInterface;

    /* Only handle GUID_BUS_INTERFACE_STANDARD */
    if (RtlCompareMemory(IrpSp->Parameters.QueryInterface.InterfaceType,
                         &GUID_BUS_INTERFACE_STANDARD,
                         sizeof(GUID)) != sizeof(GUID))
    {
        return Irp->IoStatus.Status; /* Not handled */
    }

    PdoExt = (PRP1_PDO_EXTENSION)DeviceObject->DeviceExtension;
    if (IrpSp->Parameters.QueryInterface.Version < 1)
        return STATUS_NOT_SUPPORTED;

    if (IrpSp->Parameters.QueryInterface.Size < sizeof(BUS_INTERFACE_STANDARD))
        return STATUS_BUFFER_TOO_SMALL;

    BusInterface = (PBUS_INTERFACE_STANDARD)IrpSp->Parameters.QueryInterface.Interface;
    if (!BusInterface)
        return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(BusInterface, sizeof(*BusInterface));
    BusInterface->Size = sizeof(BUS_INTERFACE_STANDARD);
    BusInterface->Version = 1;
    BusInterface->Context = PdoExt;
    BusInterface->InterfaceReference = Rp1BusInterfaceReference;
    BusInterface->InterfaceDereference = Rp1BusInterfaceDereference;
    BusInterface->TranslateBusAddress = Rp1TranslateBusAddress;
    BusInterface->GetDmaAdapter = Rp1GetDmaAdapter;
    BusInterface->SetBusData = Rp1SetBusData;
    BusInterface->GetBusData = Rp1GetBusData;

    Rp1BusInterfaceReference(PdoExt);
    return STATUS_SUCCESS;
}

/* ------------------------------------------------------------------ */
/*  PDO PnP dispatch                                                   */
/* ------------------------------------------------------------------ */

static
NTSTATUS
Rp1PdoRemoveDevice(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PRP1_PDO_EXTENSION PdoExt;
    PRP1_FDO_EXTENSION FdoExt;

    PdoExt = (PRP1_PDO_EXTENSION)DeviceObject->DeviceExtension;

    if (PdoExt->ParentFdo)
    {
        FdoExt = (PRP1_FDO_EXTENSION)PdoExt->ParentFdo->DeviceExtension;
        if (PdoExt->ChildIndex < RP1_MAX_CHILDREN &&
            FdoExt->ChildPdo[PdoExt->ChildIndex] == DeviceObject)
        {
            FdoExt->ChildPdo[PdoExt->ChildIndex] = NULL;
            if (FdoExt->ChildCount != 0)
                FdoExt->ChildCount--;
        }
    }

    Irp->IoStatus.Status = STATUS_SUCCESS;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    IoDeleteDevice(DeviceObject);

    return STATUS_SUCCESS;
}

NTSTATUS
Rp1PdoPnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PIO_STACK_LOCATION IrpSp;
    NTSTATUS Status;

    IrpSp = IoGetCurrentIrpStackLocation(Irp);

    switch (IrpSp->MinorFunction)
    {
        case IRP_MN_START_DEVICE:
            Status = STATUS_SUCCESS;
            break;

        case IRP_MN_STOP_DEVICE:
            Status = STATUS_SUCCESS;
            break;

        case IRP_MN_REMOVE_DEVICE:
            return Rp1PdoRemoveDevice(DeviceObject, Irp);

        case IRP_MN_QUERY_STOP_DEVICE:
        case IRP_MN_QUERY_REMOVE_DEVICE:
            Status = STATUS_SUCCESS;
            break;

        case IRP_MN_CANCEL_STOP_DEVICE:
        case IRP_MN_CANCEL_REMOVE_DEVICE:
            Status = STATUS_SUCCESS;
            break;

        case IRP_MN_SURPRISE_REMOVAL:
            Status = STATUS_SUCCESS;
            break;

        case IRP_MN_QUERY_ID:
            Status = Rp1PdoQueryId(DeviceObject, Irp, IrpSp);
            break;

        case IRP_MN_QUERY_DEVICE_TEXT:
            Status = Rp1PdoQueryDeviceText(DeviceObject, Irp, IrpSp);
            break;

        case IRP_MN_QUERY_RESOURCES:
            Status = Rp1PdoQueryResources(DeviceObject, Irp);
            break;

        case IRP_MN_QUERY_RESOURCE_REQUIREMENTS:
            Status = Rp1PdoQueryResourceRequirements(DeviceObject, Irp);
            break;

        case IRP_MN_QUERY_CAPABILITIES:
            Status = Rp1PdoQueryCapabilities(DeviceObject, Irp, IrpSp);
            break;

        case IRP_MN_QUERY_DEVICE_RELATIONS:
            Status = Rp1PdoQueryDeviceRelations(DeviceObject, Irp, IrpSp);
            break;

        case IRP_MN_QUERY_BUS_INFORMATION:
            Status = Rp1PdoQueryBusInformation(DeviceObject, Irp);
            break;

        case IRP_MN_QUERY_INTERFACE:
            Status = Rp1PdoQueryInterface(DeviceObject, Irp, IrpSp);
            break;

        case IRP_MN_QUERY_PNP_DEVICE_STATE:
            Status = STATUS_SUCCESS;
            break;

        default:
            Status = Irp->IoStatus.Status;
            break;
    }

    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}

/* ------------------------------------------------------------------ */
/*  Top-level PnP dispatch: route to FDO or PDO                        */
/* ------------------------------------------------------------------ */

static
NTSTATUS
NTAPI
Rp1DispatchPnp(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PRP1_COMMON_EXTENSION CommonExt;

    CommonExt = (PRP1_COMMON_EXTENSION)DeviceObject->DeviceExtension;

    if (CommonExt->IsFdo)
        return Rp1FdoPnp(DeviceObject, Irp);
    else
        return Rp1PdoPnp(DeviceObject, Irp);
}

/* ------------------------------------------------------------------ */
/*  Power dispatch                                                     */
/* ------------------------------------------------------------------ */

static
NTSTATUS
NTAPI
Rp1DispatchPower(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp)
{
    PRP1_COMMON_EXTENSION CommonExt;

    CommonExt = (PRP1_COMMON_EXTENSION)DeviceObject->DeviceExtension;

    PoStartNextPowerIrp(Irp);

    if (CommonExt->IsFdo)
    {
        PRP1_FDO_EXTENSION FdoExt = (PRP1_FDO_EXTENSION)DeviceObject->DeviceExtension;
        IoSkipCurrentIrpStackLocation(Irp);
        return PoCallDriver(FdoExt->LowerDevice, Irp);
    }
    else
    {
        /* PDO: just succeed power IRPs */
        Irp->IoStatus.Status = STATUS_SUCCESS;
        IoCompleteRequest(Irp, IO_NO_INCREMENT);
        return STATUS_SUCCESS;
    }
}

/* ------------------------------------------------------------------ */
/*  AddDevice: create FDO and attach to RP1 PCI PDO stack              */
/* ------------------------------------------------------------------ */

static
NTSTATUS
NTAPI
Rp1AddDevice(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PDEVICE_OBJECT PhysicalDeviceObject)
{
    PDEVICE_OBJECT Fdo;
    PRP1_FDO_EXTENSION FdoExt;
    NTSTATUS Status;

    if (!PhysicalDeviceObject)
        return STATUS_SUCCESS;

    Status = IoCreateDevice(DriverObject,
                            sizeof(RP1_FDO_EXTENSION),
                            NULL,
                            FILE_DEVICE_BUS_EXTENDER,
                            FILE_DEVICE_SECURE_OPEN,
                            FALSE,
                            &Fdo);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("RP1: IoCreateDevice failed (0x%08lx)\n", Status);
        return Status;
    }

    FdoExt = (PRP1_FDO_EXTENSION)Fdo->DeviceExtension;
    RtlZeroMemory(FdoExt, sizeof(RP1_FDO_EXTENSION));

    FdoExt->Common.IsFdo = TRUE;
    FdoExt->PhysicalDevice = PhysicalDeviceObject;

    FdoExt->LowerDevice = IoAttachDeviceToDeviceStack(Fdo, PhysicalDeviceObject);
    if (!FdoExt->LowerDevice)
    {
        DPRINT1("RP1: IoAttachDeviceToDeviceStack failed\n");
        IoDeleteDevice(Fdo);
        return STATUS_NO_SUCH_DEVICE;
    }

    Fdo->Flags |= DO_POWER_PAGABLE;
    Fdo->Flags &= ~DO_DEVICE_INITIALIZING;

    return STATUS_SUCCESS;
}

/* ------------------------------------------------------------------ */
/*  Unload                                                             */
/* ------------------------------------------------------------------ */

static
VOID
NTAPI
Rp1Unload(
    _In_ PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);
}

/* ------------------------------------------------------------------ */
/*  DriverEntry                                                        */
/* ------------------------------------------------------------------ */

NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    UNREFERENCED_PARAMETER(RegistryPath);

    DriverObject->MajorFunction[IRP_MJ_PNP] = Rp1DispatchPnp;
    DriverObject->MajorFunction[IRP_MJ_POWER] = Rp1DispatchPower;
    DriverObject->DriverExtension->AddDevice = Rp1AddDevice;
    DriverObject->DriverUnload = Rp1Unload;

    return STATUS_SUCCESS;
}
