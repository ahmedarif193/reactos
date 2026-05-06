/*
 * PROJECT:     ReactOS RP1 Bus Driver
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * FILE:        drivers/bus/rp1/rp1.c
 * PURPOSE:     Raspberry Pi 5 RP1 southbridge bus driver
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 *
 * DESCRIPTION:
 *   The RP1 is the southbridge IC on the Raspberry Pi 5 (BCM2712).
 *   It appears as PCI device VEN_1DE4&DEV_0001 and exposes a 4 MB
 *   BAR1 containing multiple on-chip peripherals (USB xHCI, Ethernet,
 *   GPIO, SPI, I2C, etc.).
 *
 *   This bus driver binds to the RP1 PCI function, maps BAR1, and
 *   creates child PDOs for the embedded xHCI (DWC3) USB 3.0 controllers.
 *   Each child PDO reports a PCI\CC_0C0330 compatible ID so that
 *   usbxhci.sys (via USBPORT) can bind to it.
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
    /* xHCI0 (DWC3 USB 3.0 #0) — only expose xHC0 to avoid xHC1 HCRST
     * disrupting shared USB PHY and disconnecting devices on xHC0 */
    {
        RP1_XHCI0_OFFSET,
        RP1_XHCI0_SIZE,
        L"0"
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
     * Parse translated resources to find BAR1 (memory) and interrupt.
     * The PCI bus driver provides the translated (CPU-visible) addresses.
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
                    FdoExt->Bar1Physical = Descriptor->u.Memory.Start;
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
     * Fix the BAR physical address.
     *
     * The PCI resource translator returns the PCI bus address (0xC0000000)
     * because the ACPI _CRS declares translation +0.  But the BCM2712
     * outbound window actually maps:
     *   CPU 0x1F00000000 → PCI 0xC0000000
     *
     * So CPU_PA = PCI_BA - 0xC0000000 + 0x1F00000000.
     * This is a firmware ACPI bug — _CRS should declare the correct
     * translation offset.
     */
    if (FdoExt->Bar1Physical.QuadPart < 0x100000000ULL)
    {
        PHYSICAL_ADDRESS CpuAddr;
        CpuAddr.QuadPart = FdoExt->Bar1Physical.QuadPart
                           - 0xC0000000ULL
                           + BCM2712_PCIE2_CPU_MEM_BASE;
        DPRINT1("RP1: BAR1 PCI=0x%I64x → CPU=0x%I64x length=0x%lx\n",
                FdoExt->Bar1Physical.QuadPart,
                CpuAddr.QuadPart,
                FdoExt->Bar1Length);
        FdoExt->Bar1Physical = CpuAddr;
    }
    else
    {
        DPRINT1("RP1: BAR1 physical=0x%I64x length=0x%lx (no translation needed)\n",
                FdoExt->Bar1Physical.QuadPart, FdoExt->Bar1Length);
    }

    /* Read the raw BAR1 value from PCI config to see the PCI bus address */
    {
        PCI_COMMON_CONFIG PciCfg;
        ULONG BytesRead;

        /* Query raw BAR through the PCI bus interface */
        BytesRead = HalGetBusDataByOffset(PCIConfiguration, 1, 0, &PciCfg, 0, PCI_COMMON_HDR_LENGTH);
        if (BytesRead >= PCI_COMMON_HDR_LENGTH)
        {
            DPRINT1("RP1: PCI config VID=0x%04x DID=0x%04x BAR0=0x%08lx BAR1=0x%08lx BAR2=0x%08lx BAR3=0x%08lx\n",
                    PciCfg.VendorID, PciCfg.DeviceID,
                    PciCfg.u.type0.BaseAddresses[0],
                    PciCfg.u.type0.BaseAddresses[1],
                    PciCfg.u.type0.BaseAddresses[2],
                    PciCfg.u.type0.BaseAddresses[3]);
        }
    }

    /*
     * Map BAR1 into kernel virtual address space.
     * We map the entire 4 MB region; child controllers will use
     * sub-ranges identified by their offsets.
     */
    FdoExt->Bar1Virtual = MmMapIoSpace(FdoExt->Bar1Physical,
                                        FdoExt->Bar1Length,
                                        MmNonCached);
    if (!FdoExt->Bar1Virtual)
    {
        DPRINT1("RP1: Failed to map BAR1 (phys=0x%I64x, len=0x%lx)\n",
                FdoExt->Bar1Physical.QuadPart, FdoExt->Bar1Length);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    DPRINT1("RP1: BAR1 mapped at VA %p\n", FdoExt->Bar1Virtual);

    /*
     * Enable USB clock domains.
     *
     * The firmware runs XHCI-STOP before OS handoff, which gates the
     * xHCI clock domains.  Without clocks enabled, all xHCI registers
     * read as 0.  We need to re-enable the USB microframe and suspend
     * clocks before child PDOs can access xHCI registers.
     *
     * Each clock has a CTRL register with CLK_CTRL_ENABLE at bit 11.
     * The divisor and source select are left at firmware defaults.
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
            DPRINT1("RP1: Enabled USBH0 microframe clock (was 0x%08lx)\n", Val);
        }

        /* Enable USBH0 suspend clock */
        Val = READ_REGISTER_ULONG((PULONG)(ClkBase + RP1_CLK_USBH0_SUSPEND_CTRL));
        if (!(Val & RP1_CLK_CTRL_ENABLE))
        {
            WRITE_REGISTER_ULONG((PULONG)(ClkBase + RP1_CLK_USBH0_SUSPEND_CTRL),
                                 Val | RP1_CLK_CTRL_ENABLE);
            DPRINT1("RP1: Enabled USBH0 suspend clock (was 0x%08lx)\n", Val);
        }

        /* Enable USBH1 microframe clock */
        Val = READ_REGISTER_ULONG((PULONG)(ClkBase + RP1_CLK_USBH1_MICROFRAME_CTRL));
        if (!(Val & RP1_CLK_CTRL_ENABLE))
        {
            WRITE_REGISTER_ULONG((PULONG)(ClkBase + RP1_CLK_USBH1_MICROFRAME_CTRL),
                                 Val | RP1_CLK_CTRL_ENABLE);
            DPRINT1("RP1: Enabled USBH1 microframe clock (was 0x%08lx)\n", Val);
        }

        /* Enable USBH1 suspend clock */
        Val = READ_REGISTER_ULONG((PULONG)(ClkBase + RP1_CLK_USBH1_SUSPEND_CTRL));
        if (!(Val & RP1_CLK_CTRL_ENABLE))
        {
            WRITE_REGISTER_ULONG((PULONG)(ClkBase + RP1_CLK_USBH1_SUSPEND_CTRL),
                                 Val | RP1_CLK_CTRL_ENABLE);
            DPRINT1("RP1: Enabled USBH1 suspend clock (was 0x%08lx)\n", Val);
        }

        /* Small delay for clocks to stabilize */
        KeStallExecutionProcessor(100);

        /* Dump all USB clock registers for debugging */
        DPRINT1("RP1: USB clocks: MF0_CTRL=0x%08lx MF1_CTRL=0x%08lx "
                "SP0_CTRL=0x%08lx SP1_CTRL=0x%08lx\n",
                READ_REGISTER_ULONG((PULONG)(ClkBase + RP1_CLK_USBH0_MICROFRAME_CTRL)),
                READ_REGISTER_ULONG((PULONG)(ClkBase + RP1_CLK_USBH1_MICROFRAME_CTRL)),
                READ_REGISTER_ULONG((PULONG)(ClkBase + RP1_CLK_USBH0_SUSPEND_CTRL)),
                READ_REGISTER_ULONG((PULONG)(ClkBase + RP1_CLK_USBH1_SUSPEND_CTRL)));

        /* Also dump raw memory at xHCI0 base to see if it's accessible */
        {
            PULONG Probe = (PULONG)((PUCHAR)FdoExt->Bar1Virtual + RP1_XHCI0_OFFSET);
            DPRINT1("RP1: xHCI0 raw probe: [0x00]=0x%08lx [0x04]=0x%08lx "
                    "[0xC100]=0x%08lx [0xC110]=0x%08lx [0xC120]=0x%08lx\n",
                    READ_REGISTER_ULONG(&Probe[0]),
                    READ_REGISTER_ULONG(&Probe[1]),
                    READ_REGISTER_ULONG((PULONG)((PUCHAR)Probe + 0xC100)),
                    READ_REGISTER_ULONG((PULONG)((PUCHAR)Probe + 0xC110)),
                    READ_REGISTER_ULONG((PULONG)((PUCHAR)Probe + 0xC120)));
        }
    }

    /*
     * Initialize DWC3 USB3 controllers.
     *
     * The firmware ran XHCI-STOP before OS handoff, leaving the DWC3
     * cores in an undefined state.  The xHCI registers at offset 0
     * are only valid after the DWC3 core is soft-reset and set to
     * host mode.
     *
     * Sequence (per Synopsys DWC3 databook and Linux dwc3/core.c):
     *   1. Read GSNPSID to verify core exists
     *   2. Assert CORESOFTRESET in GCTL
     *   3. Wait 100ms for internal clocks to synchronize
     *   4. Deassert CORESOFTRESET
     *   5. Set PRTCAPDIR to HOST in GCTL
     *   6. Configure USB2 PHY (GUSB2PHYCFG)
     *   7. Configure USB3 pipe (GUSB3PIPECTL)
     *   8. Verify xHCI capability registers at offset 0
     */
    {
        static const ULONG DwcOffsets[] = { RP1_XHCI0_OFFSET, RP1_XHCI1_OFFSET };
        ULONG Idx;

        for (Idx = 0; Idx < RTL_NUMBER_OF(DwcOffsets); Idx++)
        {
            PUCHAR DwcBase = (PUCHAR)FdoExt->Bar1Virtual + DwcOffsets[Idx];
            ULONG Reg, SnpsId;

            /* Step 1: Identify DWC3 core */
            SnpsId = READ_REGISTER_ULONG((PULONG)(DwcBase + DWC3_GSNPSID));
            if (SnpsId == 0 || SnpsId == 0xFFFFFFFF)
            {
                DPRINT1("RP1: DWC3[%lu] GSNPSID=0x%08lx — core not accessible, skipping\n",
                        Idx, SnpsId);
                continue;
            }
            DPRINT1("RP1: DWC3[%lu] GSNPSID=0x%08lx (rev %lu.%02lu%c)\n",
                    Idx, SnpsId,
                    (SnpsId >> 12) & 0xF,
                    (SnpsId >> 4) & 0xFF,
                    (SnpsId & 0xF) ? 'a' + (CHAR)(SnpsId & 0xF) - 1 : ' ');

            /*
             * Skip DWC3 soft reset — UEFI already initialized the DWC3 in host
             * mode with PHYs active. The xHCI driver (USBPORT/usbxhci) will
             * perform its own USBCMD.HCRST during StartDevice. Doing a DWC3
             * core reset here can leave the controller in an inconsistent state
             * where xHCI commands never complete (the command ring runs but
             * no completion events appear in the event ring).
             *
             * Just verify the current GCTL state and ensure host mode is set.
             */
            Reg = READ_REGISTER_ULONG((PULONG)(DwcBase + DWC3_GCTL));
            DPRINT1("RP1: DWC3[%lu] GCTL=0x%08lx (PRTCAP=%u, no soft reset)\n",
                    Idx, Reg, (Reg >> 12) & 3);

            /* Ensure host mode */
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

            /* Program GFLADJ — required for correct xHCI operation */
            {
                #define DWC3_GFLADJ 0xC630
                #define DWC3_GFLADJ_30MHZ_SDBND_SEL (1u << 7)
                #define DWC3_GFLADJ_30MHZ_MASK 0x3F
                ULONG Gfladj = READ_REGISTER_ULONG((PULONG)(DwcBase + DWC3_GFLADJ));
                Gfladj &= ~DWC3_GFLADJ_30MHZ_MASK;
                Gfladj |= DWC3_GFLADJ_30MHZ_SDBND_SEL | 0x20;
                WRITE_REGISTER_ULONG((PULONG)(DwcBase + DWC3_GFLADJ), Gfladj);
            }

            DPRINT1("RP1: DWC3[%lu] PHY+GFLADJ configured\n", Idx);

            /* Step 8: Verify xHCI capability registers */
            {
                ULONG CapHdr = READ_REGISTER_ULONG((PULONG)DwcBase);
                ULONG CapLen = CapHdr & 0xFF;
                ULONG HciVer = (CapHdr >> 16) & 0xFFFF;
                ULONG HccParams = READ_REGISTER_ULONG((PULONG)(DwcBase + 0x10));

                DPRINT1("RP1: DWC3[%lu] xHCI: CapLen=%lu HCIVer=0x%04lx "
                        "HccParams=0x%08lx (64bit=%lu)\n",
                        Idx, CapLen, HciVer, HccParams, HccParams & 1);

                if (CapLen == 0 || CapLen == 0xFF || HciVer < 0x0100)
                {
                    DPRINT1("RP1: DWC3[%lu] xHCI capability header invalid — "
                            "init may have failed\n", Idx);
                }
            }
        }
    }

    if (FoundInterrupt)
    {
        DPRINT1("RP1: Interrupt level=%lu vector=%lu\n",
                FdoExt->InterruptLevel, FdoExt->InterruptVector);
    }
    else
    {
        DPRINT1("RP1: WARNING: No interrupt in PCI resources\n");
    }

    FdoExt->Started = TRUE;

    return STATUS_SUCCESS;
}

/* ------------------------------------------------------------------ */
/*  FDO: IRP_MN_QUERY_DEVICE_RELATIONS (BusRelations)                  */
/*  Create child PDOs for xHCI controllers.                            */
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
        IoSkipCurrentIrpStackLocation(Irp);
        return IoCallDriver(FdoExt->LowerDevice, Irp);
    }

    /* Create child PDOs if they don't exist yet */
    for (i = 0; i < RP1_MAX_CHILDREN; i++)
    {
        if (FdoExt->ChildPdo[i])
            continue;

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
        PdoExt->ParentFdo = DeviceObject;
        PdoExt->ChildIndex = i;

        /* Compute physical address for this child's MMIO region */
        PdoExt->MmioPhysical.QuadPart =
            FdoExt->Bar1Physical.QuadPart + Rp1Children[i].Offset;
        PdoExt->MmioLength = Rp1Children[i].Length;

        /* Pass interrupt info from parent */
        PdoExt->InterruptLevel = FdoExt->InterruptLevel;
        PdoExt->InterruptVector = FdoExt->InterruptVector;
        PdoExt->InterruptAffinity = FdoExt->InterruptAffinity;

        FdoExt->ChildPdo[i]->Flags &= ~DO_DEVICE_INITIALIZING;

        FdoExt->ChildCount++;

        DPRINT1("RP1: Created child PDO %lu: MMIO phys=0x%I64x len=0x%lx\n",
                i, PdoExt->MmioPhysical.QuadPart, PdoExt->MmioLength);
    }

    /* Build DEVICE_RELATIONS structure */
    Size = sizeof(DEVICE_RELATIONS) +
           (FdoExt->ChildCount - 1) * sizeof(PDEVICE_OBJECT);

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
        if (FdoExt->ChildPdo[i])
        {
            ObReferenceObject(FdoExt->ChildPdo[i]);
            Relations->Objects[Relations->Count++] = FdoExt->ChildPdo[i];
        }
    }

    Irp->IoStatus.Information = (ULONG_PTR)Relations;
    Irp->IoStatus.Status = STATUS_SUCCESS;

    IoSkipCurrentIrpStackLocation(Irp);
    return IoCallDriver(FdoExt->LowerDevice, Irp);
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
            DPRINT("RP1: FDO IRP_MN_START_DEVICE\n");
            Status = Rp1FdoStartDevice(DeviceObject, Irp);
            Irp->IoStatus.Status = Status;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;

        case IRP_MN_QUERY_DEVICE_RELATIONS:
            if (IrpSp->Parameters.QueryDeviceRelations.Type == BusRelations)
            {
                DPRINT("RP1: FDO IRP_MN_QUERY_DEVICE_RELATIONS (BusRelations)\n");
                return Rp1FdoQueryBusRelations(DeviceObject, Irp);
            }
            /* Fall through for other relation types */
            break;

        case IRP_MN_REMOVE_DEVICE:
            DPRINT("RP1: FDO IRP_MN_REMOVE_DEVICE\n");
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
/*  usbxhci.sys to bind via the PCI\CC_0C0330 compatible ID.          */
/* ------------------------------------------------------------------ */

static
NTSTATUS
Rp1PdoQueryId(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp,
    _In_ PIO_STACK_LOCATION IrpSp)
{
    PRP1_PDO_EXTENSION PdoExt;
    PWCHAR Buffer;
    SIZE_T Size;

    PdoExt = (PRP1_PDO_EXTENSION)DeviceObject->DeviceExtension;

    switch (IrpSp->Parameters.QueryId.IdType)
    {
        case BusQueryDeviceID:
        {
            /*
             * Device ID: PCI\VEN_1DE4&DEV_0001&CC_0C0330
             * This uniquely identifies the RP1 xHCI controller.
             */
            static const WCHAR DeviceId[] = L"PCI\\VEN_1DE4&DEV_0001&CC_0C0330";

            Size = sizeof(DeviceId);
            Buffer = ExAllocatePoolWithTag(PagedPool, Size, RP1_TAG);
            if (!Buffer)
                return STATUS_INSUFFICIENT_RESOURCES;

            RtlCopyMemory(Buffer, DeviceId, Size);
            Irp->IoStatus.Information = (ULONG_PTR)Buffer;
            DPRINT("RP1: PDO[%lu] DeviceID: %S\n", PdoExt->ChildIndex, Buffer);
            return STATUS_SUCCESS;
        }

        case BusQueryHardwareIDs:
        {
            /*
             * Hardware IDs: multi-string list (double-null terminated).
             *
             * Provide a specific RP1 xHCI ID first, then the generic
             * PCI class code ID that usbport.inf uses to match xHCI.
             *
             * The PCI\CC_0C0330 ID matches the [GenericMfg] section
             * in usbport.inf which installs usbxhci.sys.
             */
            static const WCHAR HwId0[] = L"PCI\\VEN_1DE4&DEV_0001&CC_0C0330";
            static const WCHAR HwId1[] = L"PCI\\CC_0C0330";

            Size = sizeof(HwId0) + sizeof(HwId1) + sizeof(WCHAR); /* Extra null */
            Buffer = ExAllocatePoolWithTag(PagedPool, Size, RP1_TAG);
            if (!Buffer)
                return STATUS_INSUFFICIENT_RESOURCES;

            RtlZeroMemory(Buffer, Size);
            RtlCopyMemory(Buffer, HwId0, sizeof(HwId0));
            RtlCopyMemory((PUCHAR)Buffer + sizeof(HwId0), HwId1, sizeof(HwId1));
            /* Double null terminator is already zero from RtlZeroMemory */

            Irp->IoStatus.Information = (ULONG_PTR)Buffer;
            DPRINT("RP1: PDO[%lu] HardwareIDs: %S ; %S\n",
                   PdoExt->ChildIndex,
                   Buffer,
                   (PWCHAR)((PUCHAR)Buffer + sizeof(HwId0)));
            return STATUS_SUCCESS;
        }

        case BusQueryCompatibleIDs:
        {
            /*
             * Compatible IDs: PCI\CC_0C0330 (xHCI class code).
             * This is the fallback that usbport.inf uses to match.
             */
            static const WCHAR CompatId[] = L"PCI\\CC_0C0330";

            Size = sizeof(CompatId) + sizeof(WCHAR); /* Extra null for multi-string */
            Buffer = ExAllocatePoolWithTag(PagedPool, Size, RP1_TAG);
            if (!Buffer)
                return STATUS_INSUFFICIENT_RESOURCES;

            RtlZeroMemory(Buffer, Size);
            RtlCopyMemory(Buffer, CompatId, sizeof(CompatId));

            Irp->IoStatus.Information = (ULONG_PTR)Buffer;
            DPRINT("RP1: PDO[%lu] CompatibleIDs: %S\n", PdoExt->ChildIndex, Buffer);
            return STATUS_SUCCESS;
        }

        case BusQueryInstanceID:
        {
            /*
             * Instance ID: unique per-child ("0" or "1").
             */
            PCWSTR InstanceId = Rp1Children[PdoExt->ChildIndex].InstanceId;
            Size = (wcslen(InstanceId) + 1) * sizeof(WCHAR);

            Buffer = ExAllocatePoolWithTag(PagedPool, Size, RP1_TAG);
            if (!Buffer)
                return STATUS_INSUFFICIENT_RESOURCES;

            RtlCopyMemory(Buffer, InstanceId, Size);
            Irp->IoStatus.Information = (ULONG_PTR)Buffer;
            DPRINT("RP1: PDO[%lu] InstanceID: %S\n", PdoExt->ChildIndex, Buffer);
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
    PWCHAR Buffer;
    static const WCHAR Description[] = L"RP1 xHCI USB 3.0 Controller";

    PdoExt = (PRP1_PDO_EXTENSION)DeviceObject->DeviceExtension;

    if (IrpSp->Parameters.QueryDeviceText.DeviceTextType == DeviceTextDescription)
    {
        Buffer = ExAllocatePoolWithTag(PagedPool, sizeof(Description), RP1_TAG);
        if (!Buffer)
            return STATUS_INSUFFICIENT_RESOURCES;

        RtlCopyMemory(Buffer, Description, sizeof(Description));
        Irp->IoStatus.Information = (ULONG_PTR)Buffer;
        DPRINT("RP1: PDO[%lu] DeviceText: %S\n", PdoExt->ChildIndex, Buffer);
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
    ResourceList->List[0].InterfaceType = PCIBus;
    ResourceList->List[0].BusNumber = 0;

    PartialList = &ResourceList->List[0].PartialResourceList;
    PartialList->Version = 1;
    PartialList->Revision = 1;
    PartialList->Count = ResCount;

    /* Memory resource */
    Descriptor = &PartialList->PartialDescriptors[0];
    Descriptor->Type = CmResourceTypeMemory;
    Descriptor->ShareDisposition = CmResourceShareShared;
    Descriptor->Flags = CM_RESOURCE_MEMORY_READ_WRITE;
    Descriptor->u.Memory.Start = PdoExt->MmioPhysical;
    Descriptor->u.Memory.Length = PdoExt->MmioLength;

    /* Interrupt resource (raw = bus-relative: Level and Vector are both the GSI).
     *
     * The parent's translated resources have Level=IRQL and Vector=system vector.
     * For raw child resources, PnP re-translates via HalGetInterruptVector which
     * uses the Level field as the GSI. We must pass the GIC SPI number (which is
     * the parent's system vector, equal to the GSI on ARM64), not the IRQL. */
    if (PdoExt->InterruptVector != 0)
    {
        Descriptor++;
        Descriptor->Type = CmResourceTypeInterrupt;
        Descriptor->ShareDisposition = CmResourceShareShared;
        Descriptor->Flags = CM_RESOURCE_INTERRUPT_LEVEL_SENSITIVE;
        Descriptor->u.Interrupt.Level = PdoExt->InterruptVector;
        Descriptor->u.Interrupt.Vector = PdoExt->InterruptVector;
        Descriptor->u.Interrupt.Affinity = PdoExt->InterruptAffinity;
    }

    Irp->IoStatus.Information = (ULONG_PTR)ResourceList;

    DPRINT("RP1: PDO[%lu] QueryResources: mem=0x%I64x+0x%lx irq=%lu\n",
           PdoExt->ChildIndex,
           PdoExt->MmioPhysical.QuadPart,
           PdoExt->MmioLength,
           PdoExt->InterruptVector);

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
    ReqList->InterfaceType = PCIBus;
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
    Descriptor->ShareDisposition = CmResourceShareShared;
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
        Descriptor->ShareDisposition = CmResourceShareShared;
        Descriptor->Flags = CM_RESOURCE_INTERRUPT_LEVEL_SENSITIVE;
        Descriptor->u.Interrupt.MinimumVector = PdoExt->InterruptVector;
        Descriptor->u.Interrupt.MaximumVector = PdoExt->InterruptVector;
    }

    Irp->IoStatus.Information = (ULONG_PTR)ReqList;

    DPRINT("RP1: PDO[%lu] QueryResourceRequirements: mem=0x%I64x+0x%lx irq=%lu\n",
           PdoExt->ChildIndex,
           PdoExt->MmioPhysical.QuadPart,
           PdoExt->MmioLength,
           PdoExt->InterruptVector);

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
    PDEVICE_CAPABILITIES Caps;

    UNREFERENCED_PARAMETER(DeviceObject);

    Caps = IrpSp->Parameters.DeviceCapabilities.Capabilities;

    if (Caps->Version != 1 || Caps->Size < sizeof(DEVICE_CAPABILITIES))
        return STATUS_UNSUCCESSFUL;

    /* This device cannot be physically removed */
    Caps->Removable = FALSE;
    Caps->EjectSupported = FALSE;
    Caps->SurpriseRemovalOK = FALSE;

    /* Unique ID is provided by instance ID */
    Caps->UniqueID = TRUE;

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

    /*
     * Report PCIBus as the interface type so that upper drivers
     * (USBPORT/usbxhci) behave as if attached to a PCI bus.
     * This is critical for resource arbitration and DMA setup.
     */
    RtlZeroMemory(BusInfo, sizeof(PNP_BUS_INFORMATION));
    BusInfo->LegacyBusType = PCIBus;
    BusInfo->BusNumber = 0;
    /* BusTypeGuid left as zero GUID */

    Irp->IoStatus.Information = (ULONG_PTR)BusInfo;
    return STATUS_SUCCESS;
}

/* ------------------------------------------------------------------ */
/*  PDO: IRP_MN_QUERY_INTERFACE (BUS_INTERFACE_STANDARD)               */
/*  Provides fake PCI bus interface so USBPORT can read "config space" */
/* ------------------------------------------------------------------ */

/*
 * PDO: IRP_MN_QUERY_INTERFACE (BUS_INTERFACE_STANDARD)
 *
 * Forward the query to the parent RP1 PCI device's PDO.
 * USBPORT needs BUS_INTERFACE_STANDARD to read PCI config space
 * and get a DMA adapter.  Since our xHCI children are sub-devices
 * within the RP1 PCI function, we delegate to RP1's real PCI config
 * space and DMA adapter — no faking needed.
 */
static
NTSTATUS
Rp1PdoQueryInterface(
    _In_ PDEVICE_OBJECT DeviceObject,
    _Inout_ PIRP Irp,
    _In_ PIO_STACK_LOCATION IrpSp)
{
    PRP1_PDO_EXTENSION PdoExt;
    PRP1_FDO_EXTENSION FdoExt;
    KEVENT Event;
    IO_STATUS_BLOCK IoStatusBlock;
    PIRP SubIrp;
    PIO_STACK_LOCATION SubIrpSp;
    NTSTATUS Status;

    /* Only handle GUID_BUS_INTERFACE_STANDARD */
    if (RtlCompareMemory(IrpSp->Parameters.QueryInterface.InterfaceType,
                         &GUID_BUS_INTERFACE_STANDARD,
                         sizeof(GUID)) != sizeof(GUID))
    {
        return Irp->IoStatus.Status; /* Not handled */
    }

    PdoExt = (PRP1_PDO_EXTENSION)DeviceObject->DeviceExtension;
    FdoExt = (PRP1_FDO_EXTENSION)PdoExt->ParentFdo->DeviceExtension;

    /*
     * Build a new IRP_MN_QUERY_INTERFACE and send it to the parent
     * RP1 PCI device's lower device (the PCI PDO).  This returns
     * RP1's real BUS_INTERFACE_STANDARD with real config space access,
     * real DMA adapter, and real bus address translation.
     */
    KeInitializeEvent(&Event, SynchronizationEvent, FALSE);

    SubIrp = IoBuildSynchronousFsdRequest(IRP_MJ_PNP,
                                          FdoExt->LowerDevice,
                                          NULL, 0, NULL,
                                          &Event, &IoStatusBlock);
    if (!SubIrp)
        return STATUS_INSUFFICIENT_RESOURCES;

    SubIrp->IoStatus.Status = STATUS_NOT_SUPPORTED;
    SubIrpSp = IoGetNextIrpStackLocation(SubIrp);
    SubIrpSp->MinorFunction = IRP_MN_QUERY_INTERFACE;
    SubIrpSp->Parameters.QueryInterface = IrpSp->Parameters.QueryInterface;

    Status = IoCallDriver(FdoExt->LowerDevice, SubIrp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatusBlock.Status;
    }

    if (NT_SUCCESS(Status))
    {
        DPRINT1("RP1: Forwarded PCI bus interface from parent for child PDO\n");
    }

    return Status;
}

/* ------------------------------------------------------------------ */
/*  PDO PnP dispatch                                                   */
/* ------------------------------------------------------------------ */

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
            DPRINT("RP1: PDO IRP_MN_START_DEVICE\n");
            Status = STATUS_SUCCESS;
            break;

        case IRP_MN_STOP_DEVICE:
            DPRINT("RP1: PDO IRP_MN_STOP_DEVICE\n");
            Status = STATUS_SUCCESS;
            break;

        case IRP_MN_REMOVE_DEVICE:
            DPRINT("RP1: PDO IRP_MN_REMOVE_DEVICE\n");
            Status = STATUS_SUCCESS;
            break;

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
            DPRINT("RP1: PDO unhandled IRP_MN_%lu\n",
                   (ULONG)IrpSp->MinorFunction);
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

    DPRINT1("RP1: AddDevice called\n");

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

    DPRINT1("RP1: AddDevice complete, FDO=%p LDO=%p\n", Fdo, FdoExt->LowerDevice);

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
    DPRINT("RP1: Unload\n");
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

    DPRINT1("RP1: DriverEntry - Raspberry Pi 5 RP1 Southbridge Bus Driver\n");

    DriverObject->MajorFunction[IRP_MJ_PNP] = Rp1DispatchPnp;
    DriverObject->MajorFunction[IRP_MJ_POWER] = Rp1DispatchPower;
    DriverObject->DriverExtension->AddDevice = Rp1AddDevice;
    DriverObject->DriverUnload = Rp1Unload;

    return STATUS_SUCCESS;
}
