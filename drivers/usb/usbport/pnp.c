/*
 * PROJECT:     ReactOS USB Port Driver
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     USBPort plug and play functions
 * COPYRIGHT:   Copyright 2017 Vadim Galyant <vgal@rambler.ru>
 */

#include "usbport.h"

#define NDEBUG
#include <debug.h>

#define NDEBUG_USBPORT_CORE
#include "usbdebug.h"

IO_COMPLETION_ROUTINE USBPORT_FdoStartCompletion;

#if (NTDDI_VERSION < NTDDI_VISTA)
NTKERNELAPI
NTSTATUS
NTAPI
IoConnectInterruptEx(
    _Inout_ PIO_CONNECT_INTERRUPT_PARAMETERS Parameters);

NTKERNELAPI
VOID
NTAPI
IoDisconnectInterruptEx(
    _In_ PIO_DISCONNECT_INTERRUPT_PARAMETERS Parameters);
#endif

typedef NTSTATUS (NTAPI *PFN_IO_CONNECT_INTERRUPT_EX)(PIO_CONNECT_INTERRUPT_PARAMETERS Parameters);
typedef VOID (NTAPI *PFN_IO_DISCONNECT_INTERRUPT_EX)(PIO_DISCONNECT_INTERRUPT_PARAMETERS Parameters);

static PFN_IO_CONNECT_INTERRUPT_EX UsbPortIoConnectInterruptEx = NULL;
static PFN_IO_DISCONNECT_INTERRUPT_EX UsbPortIoDisconnectInterruptEx = NULL;
static BOOLEAN UsbPortInterruptApiResolved = FALSE;

/*
 * USBPORT_ProgramMsixTable - Program MSI-X table entries for the USB controller
 *
 * This function programs the MSI-X table in the PCI device to deliver
 * interrupts to the correct CPU/vector. This is required because
 * IoConnectInterruptEx only sets up the IDT entries but doesn't program
 * the hardware MSI-X table.
 */
static
NTSTATUS
USBPORT_ProgramMsixTable(
    _In_ PDEVICE_OBJECT FdoDevice,
    _In_ PIO_INTERRUPT_MESSAGE_INFO MessageInfo)
{
    PUSBPORT_DEVICE_EXTENSION FdoExtension;
    PBUS_INTERFACE_STANDARD BusInterface;
    PCI_COMMON_CONFIG PciConfig;
    ULONG BytesRead;
    UCHAR MsixCapOffset = 0;
    USHORT MsixControl;
    ULONG TableBar;
    ULONG TableOffset;
    PHYSICAL_ADDRESS MsixTablePhysical;
    PVOID MsixTableVa = NULL;
    ULONG i;

    FdoExtension = FdoDevice->DeviceExtension;
    BusInterface = &FdoExtension->BusInterface;

    /* Verify we have a valid bus interface */
    if (!BusInterface->GetBusData || !BusInterface->SetBusData)
    {
        DPRINT1("USBPORT_ProgramMsixTable: BusInterface not available\n");
        return STATUS_NOT_SUPPORTED;
    }

    /* Read PCI config to find MSI-X capability */
    BytesRead = BusInterface->GetBusData(BusInterface->Context,
                                         PCI_WHICHSPACE_CONFIG,
                                         &PciConfig,
                                         0,
                                         sizeof(PciConfig));
    if (BytesRead < PCI_COMMON_HDR_LENGTH)
    {
        DPRINT1("USBPORT_ProgramMsixTable: Failed to read PCI config\n");
        return STATUS_UNSUCCESSFUL;
    }

    /* Find MSI-X capability */
    if (PciConfig.Status & PCI_STATUS_CAPABILITIES_LIST)
    {
        UCHAR CapPtr = PciConfig.u.type0.CapabilitiesPtr & ~0x3;

        while (CapPtr != 0)
        {
            UCHAR CapId;
            BusInterface->GetBusData(BusInterface->Context,
                                     PCI_WHICHSPACE_CONFIG,
                                     &CapId,
                                     CapPtr,
                                     sizeof(CapId));
            if (CapId == 0x11) /* MSI-X */
            {
                MsixCapOffset = CapPtr;
                break;
            }
            BusInterface->GetBusData(BusInterface->Context,
                                     PCI_WHICHSPACE_CONFIG,
                                     &CapPtr,
                                     CapPtr + 1,
                                     sizeof(CapPtr));
            CapPtr &= ~0x3;
        }
    }

    if (MsixCapOffset == 0)
    {
        DPRINT1("USBPORT_ProgramMsixTable: MSI-X capability not found\n");
        return STATUS_NOT_SUPPORTED;
    }

    /* Read MSI-X control and table info */
    BusInterface->GetBusData(BusInterface->Context,
                             PCI_WHICHSPACE_CONFIG,
                             &MsixControl,
                             MsixCapOffset + 2,
                             sizeof(MsixControl));

    {
        ULONG TableInfo;
        BusInterface->GetBusData(BusInterface->Context,
                                 PCI_WHICHSPACE_CONFIG,
                                 &TableInfo,
                                 MsixCapOffset + 4,
                                 sizeof(TableInfo));
        TableBar = TableInfo & 0x7;
        TableOffset = TableInfo & ~0x7;
    }

    DPRINT1("USBPORT_ProgramMsixTable: MSI-X Cap at 0x%x, Control=0x%04x, TableBAR=%u, TableOffset=0x%x\n",
            MsixCapOffset, MsixControl, TableBar, TableOffset);

    /*
     * Use the physical address from the resource list instead of reading
     * from PCI config space. The PnP manager has already assigned and
     * translated the BAR addresses, and they are stored in our extension.
     * Note: We assume TableBAR=0 corresponds to the main memory BAR.
     */
    if (TableBar != 0)
    {
        DPRINT1("USBPORT_ProgramMsixTable: MSI-X table not in BAR0, unsupported (TableBAR=%u)\n", TableBar);
        return STATUS_NOT_SUPPORTED;
    }

    MsixTablePhysical.QuadPart = FdoExtension->MemoryBasePhysical.QuadPart + TableOffset;

    DPRINT1("USBPORT_ProgramMsixTable: MSI-X table at PA=0x%I64x (MemoryBase=0x%I64x + Offset=0x%x)\n",
            MsixTablePhysical.QuadPart, FdoExtension->MemoryBasePhysical.QuadPart, TableOffset);

    /* Map MSI-X table */
    if (FdoExtension->UsbPortResources.ResourceBase == NULL ||
        FdoExtension->UsbPortResources.IoSpaceLength < TableOffset + (MessageInfo->MessageCount * 16))
    {
        DPRINT1("USBPORT_ProgramMsixTable: register mapping does not cover MSI-X table\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    MsixTableVa = (PUCHAR)FdoExtension->UsbPortResources.ResourceBase + TableOffset;

    /* Program each MSI-X table entry */
    for (i = 0; i < MessageInfo->MessageCount; i++)
    {
        PIO_INTERRUPT_MESSAGE_INFO_ENTRY Entry = &MessageInfo->MessageInfo[i];
        volatile ULONG *TableEntry = (volatile ULONG *)((PUCHAR)MsixTableVa + (i * 16));

#if defined(_M_ARM64)
        /*
         * ARM64: Validate that the message address is a valid GIC ITS or GICv2m
         * address. If IoConnectInterruptEx couldn't get a proper ARM64 MSI address
         * from the HAL, it falls back to the x86 APIC address 0xFEE00000 which
         * doesn't work on ARM64. We must detect this and fail so the driver
         * falls back to line-based interrupts.
         */
        if ((Entry->MessageAddress.LowPart & 0xFFF00000) == 0xFEE00000 || Entry->MessageAddress.QuadPart == 0)
        {
            USHORT DisableControl;

            DPRINT1("USBPORT_ProgramMsixTable: Entry[%u] has invalid x86 APIC address 0x%I64x - MSI-X not available\n",
                    i, Entry->MessageAddress.QuadPart);

            /*
             * Explicitly disable MSI-X in the PCI device to prevent the miniport
             * driver from re-enabling it. Clear enable (bit 15), set mask (bit 14).
             */
            DisableControl = MsixControl & ~0x8000; /* Clear enable */
            DisableControl |= 0x4000;               /* Set function mask */
            BusInterface->SetBusData(BusInterface->Context,
                                     PCI_WHICHSPACE_CONFIG,
                                     &DisableControl,
                                     MsixCapOffset + 2,
                                     sizeof(DisableControl));
            DPRINT1("USBPORT_ProgramMsixTable: Disabled MSI-X (control=0x%04x)\n", DisableControl);

            return STATUS_NOT_SUPPORTED;
        }
#endif

        /* Message Address Low (offset 0) */
        TableEntry[0] = (ULONG)Entry->MessageAddress.LowPart;
        /* Message Address High (offset 4) */
        TableEntry[1] = (ULONG)Entry->MessageAddress.HighPart;
        /* Message Data (offset 8) */
        TableEntry[2] = Entry->MessageData;
        /* Vector Control (offset 12) - 0 = unmasked */
        TableEntry[3] = 0;

        DPRINT("USBPORT_ProgramMsixTable: Entry[%u]: Addr=0x%I64x Data=0x%x Vector=%u\n",
                i, Entry->MessageAddress.QuadPart, Entry->MessageData, Entry->Vector);
    }

    /* Enable MSI-X in the capability register */
    MsixControl |= 0x8000; /* Set MSI-X Enable bit */
    MsixControl &= ~0x4000; /* Clear Function Mask */

    BusInterface->SetBusData(BusInterface->Context,
                             PCI_WHICHSPACE_CONFIG,
                             &MsixControl,
                             MsixCapOffset + 2,
                             sizeof(MsixControl));

    /* Also disable INTx */
    {
        USHORT Command;
        BusInterface->GetBusData(BusInterface->Context,
                                 PCI_WHICHSPACE_CONFIG,
                                 &Command,
                                 FIELD_OFFSET(PCI_COMMON_CONFIG, Command),
                                 sizeof(Command));
        Command |= 0x400; /* Interrupt Disable */
        BusInterface->SetBusData(BusInterface->Context,
                                 PCI_WHICHSPACE_CONFIG,
                                 &Command,
                                 FIELD_OFFSET(PCI_COMMON_CONFIG, Command),
                                 sizeof(Command));
    }

    DPRINT1("USBPORT_ProgramMsixTable: Successfully programmed %u MSI-X entries\n",
            MessageInfo->MessageCount);

    return STATUS_SUCCESS;
}

/*
 * USBPORT_ProgramMsiTable - Program MSI capability registers for the USB controller
 *
 * This function programs the MSI capability in the PCI device when MSI-X is not
 * available. MSI provides a single message vector without a separate table - the
 * address and data are stored directly in PCI config space.
 *
 * MSI Capability Structure (PCI spec):
 *   Offset 0:   Capability ID (0x05)
 *   Offset 1:   Next capability pointer
 *   Offset 2-3: Message Control
 *               - Bit 0: MSI Enable
 *               - Bits 1-3: Multiple Message Capable (log2 of max vectors)
 *               - Bits 4-6: Multiple Message Enable (log2 of allocated vectors)
 *               - Bit 7: 64-bit Address Capable
 *               - Bit 8: Per-Vector Masking Capable (optional)
 *   Offset 4-7: Message Address (lower 32 bits)
 *   For 64-bit capable devices:
 *     Offset 8-11:  Message Upper Address
 *     Offset 12-13: Message Data
 *   For 32-bit only devices:
 *     Offset 8-9:   Message Data
 */
static
NTSTATUS
USBPORT_ProgramMsiTable(
    _In_ PDEVICE_OBJECT FdoDevice,
    _In_ PIO_INTERRUPT_MESSAGE_INFO MessageInfo)
{
    PUSBPORT_DEVICE_EXTENSION FdoExtension;
    PBUS_INTERFACE_STANDARD BusInterface;
    PCI_COMMON_CONFIG PciConfig;
    ULONG BytesRead;
    UCHAR MsiCapOffset = 0;
    USHORT MsiControl;
    BOOLEAN Is64BitCapable;
    ULONG MessageAddressOffset;
    ULONG MessageDataOffset;
    PIO_INTERRUPT_MESSAGE_INFO_ENTRY Entry;

    FdoExtension = FdoDevice->DeviceExtension;
    BusInterface = &FdoExtension->BusInterface;

    /* Verify we have a valid bus interface */
    if (!BusInterface->GetBusData || !BusInterface->SetBusData)
    {
        DPRINT1("USBPORT_ProgramMsiTable: BusInterface not available\n");
        return STATUS_NOT_SUPPORTED;
    }

    /* Read PCI config to find MSI capability */
    BytesRead = BusInterface->GetBusData(BusInterface->Context,
                                         PCI_WHICHSPACE_CONFIG,
                                         &PciConfig,
                                         0,
                                         sizeof(PciConfig));
    if (BytesRead < PCI_COMMON_HDR_LENGTH)
    {
        DPRINT1("USBPORT_ProgramMsiTable: Failed to read PCI config\n");
        return STATUS_UNSUCCESSFUL;
    }

    /* Find MSI capability (ID = 0x05) */
    if (PciConfig.Status & PCI_STATUS_CAPABILITIES_LIST)
    {
        UCHAR CapPtr = PciConfig.u.type0.CapabilitiesPtr & ~0x3;

        while (CapPtr != 0)
        {
            UCHAR CapId;
            BusInterface->GetBusData(BusInterface->Context,
                                     PCI_WHICHSPACE_CONFIG,
                                     &CapId,
                                     CapPtr,
                                     sizeof(CapId));
            if (CapId == 0x05) /* MSI */
            {
                MsiCapOffset = CapPtr;
                break;
            }
            BusInterface->GetBusData(BusInterface->Context,
                                     PCI_WHICHSPACE_CONFIG,
                                     &CapPtr,
                                     CapPtr + 1,
                                     sizeof(CapPtr));
            CapPtr &= ~0x3;
        }
    }

    if (MsiCapOffset == 0)
    {
        DPRINT1("USBPORT_ProgramMsiTable: MSI capability not found\n");
        return STATUS_NOT_SUPPORTED;
    }

    /* Read MSI Message Control */
    BusInterface->GetBusData(BusInterface->Context,
                             PCI_WHICHSPACE_CONFIG,
                             &MsiControl,
                             MsiCapOffset + 2,
                             sizeof(MsiControl));

    /* Check if device supports 64-bit addressing (bit 7) */
    Is64BitCapable = (MsiControl & 0x0080) != 0;

    DPRINT1("USBPORT_ProgramMsiTable: MSI Cap at 0x%x, Control=0x%04x, 64bit=%s\n",
            MsiCapOffset, MsiControl, Is64BitCapable ? "yes" : "no");

    /* MSI only supports one message in our case (use first entry) */
    if (MessageInfo->MessageCount == 0)
    {
        DPRINT1("USBPORT_ProgramMsiTable: No message entries available\n");
        return STATUS_INVALID_PARAMETER;
    }

    Entry = &MessageInfo->MessageInfo[0];

#if defined(_M_ARM64)
    /*
     * ARM64: Validate that the message address is a valid GIC ITS or GICv2m
     * address. If IoConnectInterruptEx couldn't get a proper ARM64 MSI address
     * from the HAL, it falls back to the x86 APIC address 0xFEE00000 which
     * doesn't work on ARM64.
     */
    if ((Entry->MessageAddress.LowPart & 0xFFF00000) == 0xFEE00000)
    {
        DPRINT1("USBPORT_ProgramMsiTable: Invalid x86 APIC address 0x%I64x - MSI not available\n",
                Entry->MessageAddress.QuadPart);
        return STATUS_NOT_SUPPORTED;
    }
#endif

    /* Calculate offsets based on 64-bit capability */
    MessageAddressOffset = MsiCapOffset + 4;
    if (Is64BitCapable)
    {
        MessageDataOffset = MsiCapOffset + 12; /* Upper address at +8, data at +12 */
    }
    else
    {
        MessageDataOffset = MsiCapOffset + 8; /* No upper address, data at +8 */
    }

    /* Program Message Address (lower 32 bits) */
    {
        ULONG AddressLow = Entry->MessageAddress.LowPart;
        BusInterface->SetBusData(BusInterface->Context,
                                 PCI_WHICHSPACE_CONFIG,
                                 &AddressLow,
                                 MessageAddressOffset,
                                 sizeof(AddressLow));
    }

    /* Program Message Upper Address if 64-bit capable */
    if (Is64BitCapable)
    {
        ULONG AddressHigh = Entry->MessageAddress.HighPart;
        BusInterface->SetBusData(BusInterface->Context,
                                 PCI_WHICHSPACE_CONFIG,
                                 &AddressHigh,
                                 MsiCapOffset + 8,
                                 sizeof(AddressHigh));
    }

    /* Program Message Data */
    {
        USHORT Data = (USHORT)Entry->MessageData;
        BusInterface->SetBusData(BusInterface->Context,
                                 PCI_WHICHSPACE_CONFIG,
                                 &Data,
                                 MessageDataOffset,
                                 sizeof(Data));
    }

    DPRINT1("USBPORT_ProgramMsiTable: Programmed Addr=0x%I64x Data=0x%x Vector=%u\n",
            Entry->MessageAddress.QuadPart, Entry->MessageData, Entry->Vector);

    /* Enable MSI: Set bit 0 of Message Control */
    MsiControl |= 0x0001;
    /* Set Multiple Message Enable to 0 (use only 1 vector) - bits 4-6 */
    MsiControl &= ~0x0070;

    BusInterface->SetBusData(BusInterface->Context,
                             PCI_WHICHSPACE_CONFIG,
                             &MsiControl,
                             MsiCapOffset + 2,
                             sizeof(MsiControl));

    /* Disable legacy INTx */
    {
        USHORT Command;
        BusInterface->GetBusData(BusInterface->Context,
                                 PCI_WHICHSPACE_CONFIG,
                                 &Command,
                                 FIELD_OFFSET(PCI_COMMON_CONFIG, Command),
                                 sizeof(Command));
        Command |= 0x400; /* Interrupt Disable (INTx) */
        BusInterface->SetBusData(BusInterface->Context,
                                 PCI_WHICHSPACE_CONFIG,
                                 &Command,
                                 FIELD_OFFSET(PCI_COMMON_CONFIG, Command),
                                 sizeof(Command));
    }

    DPRINT1("USBPORT_ProgramMsiTable: Successfully programmed MSI (Control=0x%04x)\n",
            MsiControl);

    return STATUS_SUCCESS;
}

#if defined(_M_ARM64)
/*
 * USBPORT_GetLegacyInterruptVector - Get legacy INTx vector for ARM64 fallback
 *
 * When MSI-X programming fails on ARM64 (e.g., when ITS is unavailable), this
 * function queries the PCI bus interface to get the device's legacy interrupt
 * line and translates it to a system vector using HalGetInterruptVector.
 *
 * Parameters:
 *   FdoDevice       - The FDO device object for the USB controller
 *   PdoDevice       - The PDO device object (physical device)
 *   OutVector       - Receives the translated system vector
 *   OutLevel        - Receives the IRQL level
 *   OutAffinity     - Receives the processor affinity
 *
 * Returns:
 *   STATUS_SUCCESS if the legacy interrupt vector was obtained
 *   STATUS_NOT_SUPPORTED if legacy interrupts are not available
 *   STATUS_INSUFFICIENT_RESOURCES on allocation failure
 */
static
NTSTATUS
USBPORT_GetLegacyInterruptVector(
    _In_ PDEVICE_OBJECT FdoDevice,
    _In_ PDEVICE_OBJECT PdoDevice,
    _Out_ PULONG OutVector,
    _Out_ PKIRQL OutLevel,
    _Out_ PKAFFINITY OutAffinity)
{
    BUS_INTERFACE_STANDARD BusInterface;
    KEVENT Event;
    NTSTATUS Status;
    PIRP Irp;
    IO_STATUS_BLOCK IoStatusBlock;
    PIO_STACK_LOCATION IrpSp;
    PDEVICE_OBJECT TargetDevice;
    UCHAR InterruptLine;
    ULONG BusNumber;
    ULONG ResultLength;
    ULONG Vector;
    KIRQL Irql;
    KAFFINITY Affinity;

    UNREFERENCED_PARAMETER(FdoDevice);

    *OutVector = 0;
    *OutLevel = 0;
    *OutAffinity = 0;

    if (!PdoDevice)
        return STATUS_INVALID_PARAMETER;

    /* Get the bus number */
    Status = IoGetDeviceProperty(PdoDevice,
                                 DevicePropertyBusNumber,
                                 sizeof(BusNumber),
                                 &BusNumber,
                                 &ResultLength);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("USBPORT_GetLegacyInterruptVector: Failed to get bus number (0x%lx)\n", Status);
        return Status;
    }

    /* Get the top of the device stack to send the query */
    TargetDevice = IoGetAttachedDeviceReference(PdoDevice);

    KeInitializeEvent(&Event, NotificationEvent, FALSE);

    /* Build IRP to query bus interface */
    Irp = IoBuildSynchronousFsdRequest(IRP_MJ_PNP,
                                        TargetDevice,
                                        NULL,
                                        0,
                                        NULL,
                                        &Event,
                                        &IoStatusBlock);
    if (!Irp)
    {
        ObDereferenceObject(TargetDevice);
        DPRINT1("USBPORT_GetLegacyInterruptVector: Failed to allocate IRP\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
    RtlZeroMemory(&BusInterface, sizeof(BusInterface));

    IrpSp = IoGetNextIrpStackLocation(Irp);
    IrpSp->MajorFunction = IRP_MJ_PNP;
    IrpSp->MinorFunction = IRP_MN_QUERY_INTERFACE;
    IrpSp->Parameters.QueryInterface.InterfaceType = &GUID_BUS_INTERFACE_STANDARD;
    IrpSp->Parameters.QueryInterface.Size = sizeof(BUS_INTERFACE_STANDARD);
    IrpSp->Parameters.QueryInterface.Version = 1;
    IrpSp->Parameters.QueryInterface.Interface = (PINTERFACE)&BusInterface;
    IrpSp->Parameters.QueryInterface.InterfaceSpecificData = NULL;

    Status = IoCallDriver(TargetDevice, Irp);
    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatusBlock.Status;
    }

    ObDereferenceObject(TargetDevice);

    if (!NT_SUCCESS(Status))
    {
        DPRINT1("USBPORT_GetLegacyInterruptVector: Bus interface query failed (0x%lx)\n", Status);
        return Status;
    }

    /*
     * Read the PCI common header from offset 0 with full length.
     *
     * On ARM64, the HAL's bus data hook translates the InterruptLine field
     * using ACPI _PRT routing tables ONLY when reading from offset 0 with
     * length >= PCI_COMMON_HDR_LENGTH (64 bytes). Reading just the
     * InterruptLine byte directly bypasses this translation and returns
     * the raw PCI config value (often 0xFF on UEFI systems).
     *
     * By reading the full header, we ensure the HAL applies the _PRT
     * translation and returns the correct GSI in the InterruptLine field.
     */
    if (!BusInterface.GetBusData)
    {
        if (BusInterface.InterfaceDereference)
            BusInterface.InterfaceDereference(BusInterface.Context);
        DPRINT1("USBPORT_GetLegacyInterruptVector: GetBusData not available\n");
        return STATUS_NOT_SUPPORTED;
    }

    {
        PCI_COMMON_CONFIG PciConfig;
        ULONG ReadLen;

        RtlZeroMemory(&PciConfig, sizeof(PciConfig));

        /* Read PCI common header from offset 0 to trigger HAL _PRT translation */
        ReadLen = BusInterface.GetBusData(BusInterface.Context,
                                          PCI_WHICHSPACE_CONFIG,
                                          &PciConfig,
                                          0,
                                          PCI_COMMON_HDR_LENGTH);

        /* Release the bus interface reference */
        if (BusInterface.InterfaceDereference)
            BusInterface.InterfaceDereference(BusInterface.Context);

        if (ReadLen < PCI_COMMON_HDR_LENGTH)
        {
            DPRINT1("USBPORT_GetLegacyInterruptVector: Failed to read PCI header (read %lu/%lu)\n",
                    ReadLen, (ULONG)PCI_COMMON_HDR_LENGTH);
            return STATUS_NOT_SUPPORTED;
        }

        InterruptLine = PciConfig.u.type0.InterruptLine;
        DPRINT1("USBPORT_GetLegacyInterruptVector: PCI Header read - InterruptLine=0x%02x Pin=%u\n",
                InterruptLine, PciConfig.u.type0.InterruptPin);
    }

    if (InterruptLine == 0xFF || InterruptLine == 0)
    {
        DPRINT1("USBPORT_GetLegacyInterruptVector: No legacy interrupt line (InterruptLine=0x%02x)\n",
                InterruptLine);
        return STATUS_NOT_SUPPORTED;
    }

    DPRINT1("USBPORT_GetLegacyInterruptVector: PCI InterruptLine=%u (0x%02x) Bus=%lu\n",
            InterruptLine, InterruptLine, BusNumber);

    /* Translate the interrupt line to system vector using HalGetInterruptVector.
     * On ARM64, the interrupt line from PCI config is typically a GSI (Global System Interrupt).
     * HalGetInterruptVector will translate it to the correct SPI vector for the GIC.
     */
    Vector = HalGetInterruptVector(PCIBus,
                                   BusNumber,
                                   InterruptLine,  /* BusInterruptLevel */
                                   InterruptLine,  /* BusInterruptVector */
                                   &Irql,
                                   &Affinity);

    if (Vector == 0)
    {
        DPRINT1("USBPORT_GetLegacyInterruptVector: HalGetInterruptVector returned 0\n");
        return STATUS_NOT_SUPPORTED;
    }

    DPRINT1("USBPORT_GetLegacyInterruptVector: Translated to Vector=%lu IRQL=%u Affinity=0x%lx\n",
            Vector, Irql, (ULONG)Affinity);

    *OutVector = Vector;
    *OutLevel = Irql;
    *OutAffinity = Affinity;

    return STATUS_SUCCESS;
}
#endif /* _M_ARM64 */

static
VOID
USBPORT_EnsureInterruptApis(VOID)
{
    UNICODE_STRING RoutineName;

    if (UsbPortInterruptApiResolved)
        return;

    RtlInitUnicodeString(&RoutineName, L"IoConnectInterruptEx");
    UsbPortIoConnectInterruptEx =
        (PFN_IO_CONNECT_INTERRUPT_EX)MmGetSystemRoutineAddress(&RoutineName);

    RtlInitUnicodeString(&RoutineName, L"IoDisconnectInterruptEx");
    UsbPortIoDisconnectInterruptEx =
        (PFN_IO_DISCONNECT_INTERRUPT_EX)MmGetSystemRoutineAddress(&RoutineName);

    if (!UsbPortIoConnectInterruptEx)
        UsbPortIoConnectInterruptEx = IoConnectInterruptEx;
    if (!UsbPortIoDisconnectInterruptEx)
        UsbPortIoDisconnectInterruptEx = IoDisconnectInterruptEx;

    UsbPortInterruptApiResolved = TRUE;
}

#define USBPORT_XHCI_HCCPARAMS_OFFSET 0x10
#define USBPORT_XHCI_HCC_64BIT_ADDR   0x00000001u

static
BOOLEAN
USBPORT_XhciReadHcc64Bit(
    _In_ PUSBPORT_RESOURCES Resources,
    _Out_ PBOOLEAN Supports64Bit)
{
    ULONG HccParams;

    if (!Resources || !Supports64Bit)
        return FALSE;

    if (!(Resources->ResourcesTypes & USBPORT_RESOURCES_MEMORY) ||
        !Resources->ResourceBase ||
        Resources->IoSpaceLength < (USBPORT_XHCI_HCCPARAMS_OFFSET + sizeof(ULONG)))
    {
        return FALSE;
    }

    HccParams = READ_REGISTER_ULONG((PULONG)((PUCHAR)Resources->ResourceBase +
                                             USBPORT_XHCI_HCCPARAMS_OFFSET));
    if (HccParams == 0 || HccParams == MAXULONG)
        return FALSE;

    *Supports64Bit = ((HccParams & USBPORT_XHCI_HCC_64BIT_ADDR) != 0);
    return TRUE;
}

static
BOOLEAN
USBPORT_DetermineDma64Bit(
    _In_ PUSBPORT_RESOURCES Resources,
    _In_ PUSBPORT_REGISTRATION_PACKET Packet)
{
    BOOLEAN Use64Bit = FALSE;

    if (Packet && (Packet->MiniPortFlags & USB_MINIPORT_FLAGS_USB3))
        Use64Bit = TRUE;

    if (Packet && Packet->MiniPortVersion == USB_MINIPORT_VERSION_XHCI)
    {
        BOOLEAN Xhci64 = FALSE;

        if (USBPORT_XhciReadHcc64Bit(Resources, &Xhci64))
            Use64Bit = Xhci64;
    }

    return Use64Bit;
}

static
VOID
USBPORT_CleanupTransportRegistrations(IN PUSBPORT_DEVICE_EXTENSION FdoExtension)
{
    KIRQL OldIrql;
    PLIST_ENTRY Entry;
    PUSBPORT_TRANSPORT_REGISTRATION Registration;
    PIRP PendingIrp;

    for (;;)
    {
        KeAcquireSpinLock(&FdoExtension->Aux.TransportListSpinLock, &OldIrql);
        if (IsListEmpty(&FdoExtension->Aux.TransportRegistrationList))
        {
            KeReleaseSpinLock(&FdoExtension->Aux.TransportListSpinLock, OldIrql);
            break;
        }

        Entry = RemoveHeadList(&FdoExtension->Aux.TransportRegistrationList);
        Registration = CONTAINING_RECORD(Entry,
                                         USBPORT_TRANSPORT_REGISTRATION,
                                         ListEntry);
        PendingIrp = Registration->PendingIrp;
        Registration->PendingIrp = NULL;
        KeReleaseSpinLock(&FdoExtension->Aux.TransportListSpinLock, OldIrql);

        if (PendingIrp)
        {
            USBPORT_CompleteTransportNotificationIrp(FdoExtension,
                                                     PendingIrp,
                                                     STATUS_CANCELLED,
                                                     FALSE);
        }

        ExFreePoolWithTag(Registration, USB_PORT_TAG);
    }
}

static
VOID
USBPORT_CleanupTimeSyncContexts(IN PUSBPORT_DEVICE_EXTENSION FdoExtension)
{
    KIRQL OldIrql;
    PLIST_ENTRY Entry;
    PUSBPORT_TIMESYNC_CONTEXT Context;

    for (;;)
    {
        KeAcquireSpinLock(&FdoExtension->Aux.TimeSyncSpinLock, &OldIrql);
        if (IsListEmpty(&FdoExtension->Aux.TimeSyncTrackingList))
        {
            KeReleaseSpinLock(&FdoExtension->Aux.TimeSyncSpinLock, OldIrql);
            break;
        }

        Entry = RemoveHeadList(&FdoExtension->Aux.TimeSyncTrackingList);
        KeReleaseSpinLock(&FdoExtension->Aux.TimeSyncSpinLock, OldIrql);

        Context = CONTAINING_RECORD(Entry,
                                    USBPORT_TIMESYNC_CONTEXT,
                                    ListEntry);
        ExFreePoolWithTag(Context, USB_PORT_TAG);
    }
}

NTSTATUS
NTAPI
USBPORT_FdoStartCompletion(IN PDEVICE_OBJECT DeviceObject,
                           IN PIRP Irp,
                           IN PVOID Context)
{
    KeSetEvent((PKEVENT)Context, EVENT_INCREMENT, FALSE);
    return STATUS_MORE_PROCESSING_REQUIRED;
}

NTSTATUS
NTAPI
USBPORT_RegisterDeviceInterface(IN PDEVICE_OBJECT PdoDevice,
                                IN PDEVICE_OBJECT DeviceObject,
                                IN CONST GUID *InterfaceClassGuid,
                                IN BOOLEAN Enable)
{
    PUSBPORT_RHDEVICE_EXTENSION DeviceExtension;
    PUNICODE_STRING SymbolicLinkName;
    NTSTATUS Status;

    DPRINT("USBPORT_RegisterDeviceInterface: Enable - %x\n", Enable);

    DeviceExtension = DeviceObject->DeviceExtension;
    SymbolicLinkName = &DeviceExtension->CommonExtension.SymbolicLinkName;

    if (Enable)
    {
        Status = IoRegisterDeviceInterface(PdoDevice,
                                           InterfaceClassGuid,
                                           NULL,
                                           SymbolicLinkName);

        if (NT_SUCCESS(Status))
        {
            DeviceExtension->CommonExtension.IsInterfaceEnabled = 1;

            Status = USBPORT_SetRegistryKeyValue(PdoDevice,
                                                 FALSE,
                                                 REG_SZ,
                                                 L"SymbolicName",
                                                 SymbolicLinkName->Buffer,
                                                 SymbolicLinkName->Length);

            if (NT_SUCCESS(Status))
            {
                DPRINT("USBPORT_RegisterDeviceInterface: LinkName  - %wZ\n",
                       &DeviceExtension->CommonExtension.SymbolicLinkName);

                Status = IoSetDeviceInterfaceState(SymbolicLinkName, TRUE);
            }
        }
    }
    else
    {
        /* Disable device interface */
        Status = IoSetDeviceInterfaceState(SymbolicLinkName, FALSE);

        if (NT_SUCCESS(Status))
        {
            RtlFreeUnicodeString(SymbolicLinkName);
            DeviceExtension->CommonExtension.IsInterfaceEnabled = 0; // Disabled interface
        }
    }

    return Status;
}

BOOLEAN
NTAPI
USBPORT_IsSelectiveSuspendEnabled(IN PDEVICE_OBJECT FdoDevice)
{
    PUSBPORT_DEVICE_EXTENSION FdoExtension;
    ULONG Disabled = 0;

    DPRINT("USBPORT_IsSelectiveSuspendEnabled: ... \n");

    FdoExtension = FdoDevice->DeviceExtension;
    FdoExtension->Flags &= ~USBPORT_FLAG_RH_STOPPED;

    USBPORT_GetRegistryKeyValueFullInfo(FdoDevice,
                                        FdoExtension->CommonExtension.LowerPdoDevice,
                                        TRUE,
                                        L"HcDisableSelectiveSuspend",
                                        sizeof(L"HcDisableSelectiveSuspend"),
                                        &Disabled,
                                        sizeof(Disabled));

    return (Disabled == 0);
}

NTSTATUS
NTAPI
USBPORT_GetConfigValue(IN PWSTR ValueName,
                       IN ULONG ValueType,
                       IN PVOID ValueData,
                       IN ULONG ValueLength,
                       IN PVOID Context,
                       IN PVOID EntryContext)
{
    NTSTATUS Status = STATUS_SUCCESS;

    DPRINT("USBPORT_GetConfigValue \n");

    if (ValueType == REG_DWORD)
    {
        *(PULONG)EntryContext = *(PULONG)ValueData;
    }
    else
    {
        Status = STATUS_INVALID_PARAMETER;
    }

    return Status;
}

NTSTATUS
NTAPI
USBPORT_GetDefaultBIOSx(IN PDEVICE_OBJECT FdoDevice,
                         IN PULONG UsbBIOSx,
                         IN PULONG DisableSelectiveSuspend,
                         IN PULONG DisableCcDetect,
                         IN PULONG IdleEpSupport,
                         IN PULONG IdleEpSupportEx,
                         IN PULONG SoftRetry)
{
    RTL_QUERY_REGISTRY_TABLE QueryTable[7];

    DPRINT("USBPORT_GetDefaultBIOS_X: ... \n");

    RtlZeroMemory(QueryTable, sizeof(QueryTable));

    *UsbBIOSx = 2;

    QueryTable[0].QueryRoutine = USBPORT_GetConfigValue;
    QueryTable[0].Flags = 0;
    QueryTable[0].Name = L"UsbBIOSx";
    QueryTable[0].EntryContext = UsbBIOSx;
    QueryTable[0].DefaultType = REG_DWORD;
    QueryTable[0].DefaultData = UsbBIOSx;
    QueryTable[0].DefaultLength = sizeof(ULONG);

    QueryTable[1].QueryRoutine = USBPORT_GetConfigValue;
    QueryTable[1].Flags = 0;
    QueryTable[1].Name = L"DisableSelectiveSuspend";
    QueryTable[1].EntryContext = DisableSelectiveSuspend;
    QueryTable[1].DefaultType = REG_DWORD;
    QueryTable[1].DefaultData = DisableSelectiveSuspend;
    QueryTable[1].DefaultLength = sizeof(ULONG);

    QueryTable[2].QueryRoutine = USBPORT_GetConfigValue;
    QueryTable[2].Flags = 0;
    QueryTable[2].Name = L"DisableCcDetect";
    QueryTable[2].EntryContext = DisableCcDetect;
    QueryTable[2].DefaultType = REG_DWORD;
    QueryTable[2].DefaultData = DisableCcDetect;
    QueryTable[2].DefaultLength = sizeof(ULONG);

    QueryTable[3].QueryRoutine = USBPORT_GetConfigValue;
    QueryTable[3].Flags = 0;
    QueryTable[3].Name = L"EnIdleEndpointSupport";
    QueryTable[3].EntryContext = IdleEpSupport;
    QueryTable[3].DefaultType = REG_DWORD;
    QueryTable[3].DefaultData = IdleEpSupport;
    QueryTable[3].DefaultLength = sizeof(ULONG);

    QueryTable[4].QueryRoutine = USBPORT_GetConfigValue;
    QueryTable[4].Flags = 0;
    QueryTable[4].Name = L"EnIdleEndpointSupportEx";
    QueryTable[4].EntryContext = IdleEpSupportEx;
    QueryTable[4].DefaultType = REG_DWORD;
    QueryTable[4].DefaultData = IdleEpSupportEx;
    QueryTable[4].DefaultLength = sizeof(ULONG);

    QueryTable[5].QueryRoutine = USBPORT_GetConfigValue;
    QueryTable[5].Flags = 0;
    QueryTable[5].Name = L"EnSoftRetry";
    QueryTable[5].EntryContext = SoftRetry;
    QueryTable[5].DefaultType = REG_DWORD;
    QueryTable[5].DefaultData = SoftRetry;
    QueryTable[5].DefaultLength = sizeof(ULONG);

    return RtlQueryRegistryValues(RTL_REGISTRY_SERVICES,
                                  L"usb",
                                  QueryTable,
                                  NULL,
                                  NULL);
}

NTSTATUS
NTAPI
USBPORT_IsCompanionController(IN PDEVICE_OBJECT DeviceObject,
                              IN BOOLEAN *IsCompanion)
{
    PDEVICE_OBJECT HighestDevice;
    PIRP Irp;
    KEVENT Event;
    PIO_STACK_LOCATION IoStack;
    PCI_DEVICE_PRESENT_INTERFACE PciInterface = {0};
    PCI_DEVICE_PRESENCE_PARAMETERS Parameters   = {0};
    IO_STATUS_BLOCK IoStatusBlock;
    NTSTATUS Status;
    BOOLEAN IsPresent;

    DPRINT("USBPORT_IsCompanionController: ... \n");

    *IsCompanion = FALSE;

    KeInitializeEvent(&Event, SynchronizationEvent, FALSE);

    HighestDevice = IoGetAttachedDeviceReference(DeviceObject);

    Irp = IoBuildSynchronousFsdRequest(IRP_MJ_PNP,
                                       HighestDevice,
                                       NULL,
                                       0,
                                       NULL,
                                       &Event,
                                       &IoStatusBlock);

    if (!Irp)
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
        ObDereferenceObject(HighestDevice);
        return Status;
    }

    IoStack = IoGetNextIrpStackLocation(Irp);

    Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
    Irp->IoStatus.Information = 0;

    IoStack->MinorFunction = IRP_MN_QUERY_INTERFACE;

    IoStack->Parameters.QueryInterface.InterfaceType = &GUID_PCI_DEVICE_PRESENT_INTERFACE;
    IoStack->Parameters.QueryInterface.Size = sizeof(PCI_DEVICE_PRESENT_INTERFACE);
    IoStack->Parameters.QueryInterface.Version = 1;
    IoStack->Parameters.QueryInterface.Interface = (PINTERFACE)&PciInterface;
    IoStack->Parameters.QueryInterface.InterfaceSpecificData = 0;

    Status = IoCallDriver(HighestDevice, Irp);

    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
        Status = IoStatusBlock.Status;
    }

    if (!NT_SUCCESS(Status))
    {
        DPRINT1("USBPORT_IsCompanionController: query interface failed\\n");
        ObDereferenceObject(HighestDevice);
        return Status;
    }

    DPRINT("USBPORT_IsCompanionController: query interface succeeded\n");

    if (PciInterface.Size < sizeof(PCI_DEVICE_PRESENT_INTERFACE))
    {
        DPRINT1("USBPORT_IsCompanionController: old version\n");
        ObDereferenceObject(HighestDevice);
        return Status;
    }

    Parameters.Size = sizeof(PCI_DEVICE_PRESENT_INTERFACE);

    Parameters.BaseClass = PCI_CLASS_SERIAL_BUS_CTLR;
    Parameters.SubClass = PCI_SUBCLASS_SB_USB;
    Parameters.ProgIf = PCI_INTERFACE_USB_ID_EHCI;

    Parameters.Flags = PCI_USE_LOCAL_BUS |
                       PCI_USE_LOCAL_DEVICE |
                       PCI_USE_CLASS_SUBCLASS |
                       PCI_USE_PROGIF;

    IsPresent = (PciInterface.IsDevicePresentEx)(PciInterface.Context,
                                                 &Parameters);

    if (IsPresent)
    {
        DPRINT("USBPORT_IsCompanionController: Present EHCI controller for FDO - %p\n",
               DeviceObject);
    }
    else
    {
        DPRINT("USBPORT_IsCompanionController: No EHCI controller for FDO - %p\n",
               DeviceObject);
    }

    *IsCompanion = IsPresent;

    (PciInterface.InterfaceDereference)(PciInterface.Context);

    ObDereferenceObject(HighestDevice);

    return Status;
}

NTSTATUS
NTAPI
USBPORT_QueryPciBusInterface(IN PDEVICE_OBJECT FdoDevice)
{
    PUSBPORT_DEVICE_EXTENSION FdoExtension;
    PBUS_INTERFACE_STANDARD BusInterface;
    PIO_STACK_LOCATION IoStack;
    IO_STATUS_BLOCK IoStatusBlock;
    PDEVICE_OBJECT HighestDevice;
    KEVENT Event;
    PIRP Irp;
    NTSTATUS Status;

    DPRINT("USBPORT_QueryPciBusInterface: ...  \n");

    FdoExtension = FdoDevice->DeviceExtension;
    BusInterface = &FdoExtension->BusInterface;

    RtlZeroMemory(BusInterface, sizeof(BUS_INTERFACE_STANDARD));
    KeInitializeEvent(&Event, SynchronizationEvent, FALSE);
    HighestDevice = IoGetAttachedDeviceReference(FdoDevice);

    Irp = IoBuildSynchronousFsdRequest(IRP_MJ_PNP,
                                       HighestDevice,
                                       NULL,
                                       0,
                                       NULL,
                                       &Event,
                                       &IoStatusBlock);

    if (Irp)
    {
        IoStack = IoGetNextIrpStackLocation(Irp);

        Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
        Irp->IoStatus.Information = 0;

        IoStack->MinorFunction = IRP_MN_QUERY_INTERFACE;

        IoStack->Parameters.QueryInterface.InterfaceType = &GUID_BUS_INTERFACE_STANDARD;
        IoStack->Parameters.QueryInterface.Size = sizeof(BUS_INTERFACE_STANDARD);
        IoStack->Parameters.QueryInterface.Version = 1;
        IoStack->Parameters.QueryInterface.Interface = (PINTERFACE)BusInterface;
        IoStack->Parameters.QueryInterface.InterfaceSpecificData = 0;

        Status = IoCallDriver(HighestDevice, Irp);

        if (Status == STATUS_PENDING)
        {
            KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
            Status = IoStatusBlock.Status;
        }
    }
    else
    {
        Status = STATUS_INSUFFICIENT_RESOURCES;
    }

    ObDereferenceObject(HighestDevice);

    DPRINT("USBPORT_QueryPciBusInterface: return Status - %x\n", Status);

    return Status;
}

NTSTATUS
NTAPI
USBPORT_QueryCapabilities(IN PDEVICE_OBJECT FdoDevice,
                          IN PDEVICE_CAPABILITIES Capabilities)
{
    PUSBPORT_DEVICE_EXTENSION FdoExtention;
    PIRP Irp;
    NTSTATUS Status;
    PIO_STACK_LOCATION IoStack;
    KEVENT Event;

    DPRINT("USBPORT_QueryCapabilities: ... \n");

    FdoExtention = FdoDevice->DeviceExtension;

    RtlZeroMemory(Capabilities, sizeof(DEVICE_CAPABILITIES));

    Capabilities->Size     = sizeof(DEVICE_CAPABILITIES);
    Capabilities->Version  = 1;
    Capabilities->Address  = MAXULONG;
    Capabilities->UINumber = MAXULONG;

    Irp = IoAllocateIrp(FdoExtention->CommonExtension.LowerDevice->StackSize,
                        FALSE);

    if (!Irp)
    {
        DPRINT1("USBPORT_QueryCapabilities: No resources - IoAllocateIrp!\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    IoStack = IoGetCurrentIrpStackLocation(Irp);
    Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;

    IoStack = IoGetNextIrpStackLocation(Irp);
    IoStack->MajorFunction = IRP_MJ_PNP;
    IoStack->MinorFunction = IRP_MN_QUERY_CAPABILITIES;

    KeInitializeEvent(&Event, NotificationEvent, FALSE);

    IoSetCompletionRoutine(Irp,
                          USBPORT_FdoStartCompletion,
                          &Event,
                          TRUE,
                          TRUE,
                          TRUE);

    IoStack->Parameters.DeviceCapabilities.Capabilities = Capabilities;

    Status = IoCallDriver(FdoExtention->CommonExtension.LowerDevice, Irp);

    if (Status == STATUS_PENDING)
    {
        KeWaitForSingleObject(&Event, Suspended, KernelMode, FALSE, NULL);
        Status = Irp->IoStatus.Status;
    }

    if (NT_SUCCESS(Status) && Capabilities)
    {
        USBPORT_DumpingCapabilities(Capabilities);
    }

    IoFreeIrp(Irp);

    return Status;
}

NTSTATUS
NTAPI
USBPORT_CreateLegacySymbolicLink(IN PDEVICE_OBJECT FdoDevice)
{
    PUSBPORT_DEVICE_EXTENSION FdoExtension;
    WCHAR CharName[255] = {0};
    WCHAR CharDosName[255] = {0};
    UNICODE_STRING DeviceName;
    NTSTATUS Status;
    USHORT DosLength;
    PWSTR DosBuffer;

    FdoExtension = FdoDevice->DeviceExtension;

    RtlStringCbPrintfW(CharName,
                       sizeof(CharName),
                       L"\\Device\\USBFDO-%d",
                       FdoExtension->FdoNameNumber);

    RtlInitUnicodeString(&DeviceName, CharName);

    RtlStringCbPrintfW(CharDosName,
                       sizeof(CharDosName),
                       L"\\DosDevices\\HCD%d",
                       FdoExtension->FdoNameNumber);

    DosLength = (USHORT)((wcslen(CharDosName) + 1) * sizeof(WCHAR));

    DosBuffer = ExAllocatePoolWithTag(PagedPool,
                                      DosLength,
                                      USB_PORT_TAG);

    if (!DosBuffer)
    {
        DPRINT1("USBPORT_CreateLegacySymbolicLink: Allocation failed\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(DosBuffer, DosLength);
    RtlCopyMemory(DosBuffer, CharDosName, DosLength);

    FdoExtension->DosDeviceSymbolicName.Buffer = DosBuffer;
    FdoExtension->DosDeviceSymbolicName.MaximumLength = DosLength;
    FdoExtension->DosDeviceSymbolicName.Length = DosLength - sizeof(WCHAR);

    DPRINT("USBPORT_CreateLegacySymbolicLink: DeviceName - %wZ, DosSymbolicName - %wZ\n",
           &DeviceName,
           &FdoExtension->DosDeviceSymbolicName);

    Status = IoCreateSymbolicLink(&FdoExtension->DosDeviceSymbolicName,
                                  &DeviceName);

    if (NT_SUCCESS(Status))
    {
        FdoExtension->Flags |= USBPORT_FLAG_DOS_SYMBOLIC_NAME;
    }
    else
    {
        DPRINT1("USBPORT_CreateLegacySymbolicLink: IoCreateSymbolicLink failed - %lx\n",
                Status);
        if (FdoExtension->DosDeviceSymbolicName.Buffer)
        {
            ExFreePoolWithTag(FdoExtension->DosDeviceSymbolicName.Buffer, USB_PORT_TAG);
            FdoExtension->DosDeviceSymbolicName.Buffer = NULL;
            FdoExtension->DosDeviceSymbolicName.MaximumLength = 0;
            FdoExtension->DosDeviceSymbolicName.Length = 0;
        }
    }

    return Status;
}

NTSTATUS
NTAPI
USBPORT_StopDevice(IN PDEVICE_OBJECT FdoDevice)
{
    PUSBPORT_DEVICE_EXTENSION FdoExtension;
    PUSBPORT_REGISTRATION_PACKET Packet;
    PUSBPORT_RESOURCES UsbPortResources;

    DPRINT("USBPORT_StopDevice: FdoDevice - %p\n", FdoDevice);

    FdoExtension = FdoDevice->DeviceExtension;
    Packet = FdoExtension->MiniPortInterface ?
             &FdoExtension->MiniPortInterface->Packet :
             NULL;
    UsbPortResources = &FdoExtension->UsbPortResources;

    if (FdoExtension->MiniPortFlags & USBPORT_MPFLAG_INTERRUPTS_ENABLED)
    {
        USBPORT_MiniportInterrupts(FdoDevice, FALSE);
        FdoExtension->MiniPortFlags &= ~(USBPORT_MPFLAG_INTERRUPTS_ENABLED |
                                         USBPORT_MPFLAG_SUSPENDED);
    }

    USBPORT_StopControllerTimer(FdoExtension);
    /*
     * TimerSoftInterrupt is only initialized when USBPORT_SoftInterrupt
     * has been used. Guard KeCancelTimer so we do not assert on an
     * uninitialized KTIMER when StartDevice fails early. Any non-zero
     * object type here indicates an initialized dispatcher header.
     */
    if (FdoExtension->TimerSoftInterrupt.Header.Type != 0)
    {
        KeCancelTimer(&FdoExtension->TimerSoftInterrupt);
    }
    FdoExtension->TimerFlags = 0;

    if (Packet && (FdoExtension->Flags & USBPORT_FLAG_HC_STARTED) &&
        Packet->StopController)
    {
        Packet->StopController(FdoExtension->MiniPortExt, FALSE);
    }

    FdoExtension->Flags &= ~(USBPORT_FLAG_HC_STARTED |
                             USBPORT_FLAG_INTERRUPT_ENABLED |
                             USBPORT_FLAG_LEGACY_SUPPORT |
                             USBPORT_FLAG_HC_POLLING |
                             USBPORT_FLAG_HC_SUSPEND |
                             USBPORT_FLAG_RH_INIT_CALLBACK);

    if (FdoExtension->CommonExtension.IsInterfaceEnabled)
    {
        USBPORT_RegisterDeviceInterface(FdoExtension->CommonExtension.LowerPdoDevice,
                                        FdoDevice,
                                        &GUID_DEVINTERFACE_USB_HOST_CONTROLLER,
                                        FALSE);
    }

    if ((FdoExtension->Flags & USBPORT_FLAG_DOS_SYMBOLIC_NAME) &&
        FdoExtension->DosDeviceSymbolicName.Buffer)
    {
        IoDeleteSymbolicLink(&FdoExtension->DosDeviceSymbolicName);
        ExFreePoolWithTag(FdoExtension->DosDeviceSymbolicName.Buffer, USB_PORT_TAG);
        FdoExtension->DosDeviceSymbolicName.Buffer = NULL;
        FdoExtension->DosDeviceSymbolicName.MaximumLength = 0;
        FdoExtension->DosDeviceSymbolicName.Length = 0;
        FdoExtension->Flags &= ~USBPORT_FLAG_DOS_SYMBOLIC_NAME;
    }

    USBPORT_FlushMapTransfers(FdoDevice);

    if (FdoExtension->InterruptMessageInfo)
    {
        if (UsbPortIoDisconnectInterruptEx)
        {
            IO_DISCONNECT_INTERRUPT_PARAMETERS DisconnectParameters;
            RtlZeroMemory(&DisconnectParameters, sizeof(DisconnectParameters));
            DisconnectParameters.Version = CONNECT_MESSAGE_BASED;
            DisconnectParameters.ConnectionContext.InterruptMessageTable =
                FdoExtension->InterruptMessageInfo;
            UsbPortIoDisconnectInterruptEx(&DisconnectParameters);
        }
        FdoExtension->InterruptMessageInfo = NULL;
        FdoExtension->MessageInterruptsEnabled = FALSE;
        FdoExtension->MessageInterruptCount = 0;
        FdoExtension->InterruptObject = NULL;
    }
    else if (FdoExtension->InterruptObject)
    {
        IoDisconnectInterrupt(FdoExtension->InterruptObject);
        FdoExtension->InterruptObject = NULL;
    }

    FdoExtension->Flags &= ~USBPORT_FLAG_INT_CONNECTED;

    if (FdoExtension->MiniPortCommonBuffer)
    {
        USBPORT_FreeCommonBuffer(FdoDevice, FdoExtension->MiniPortCommonBuffer);
        FdoExtension->MiniPortCommonBuffer = NULL;
    }

    if (FdoExtension->ActiveIrpTable)
    {
        ExFreePoolWithTag(FdoExtension->ActiveIrpTable, USB_PORT_TAG);
        FdoExtension->ActiveIrpTable = NULL;
    }

    if (FdoExtension->PendingIrpTable)
    {
        ExFreePoolWithTag(FdoExtension->PendingIrpTable, USB_PORT_TAG);
        FdoExtension->PendingIrpTable = NULL;
    }

    if (FdoExtension->DmaAdapter)
    {
        FdoExtension->DmaAdapter->DmaOperations->PutDmaAdapter(FdoExtension->DmaAdapter);
        FdoExtension->DmaAdapter = NULL;
        FdoExtension->NumberMapRegs = 0;
    }

    if ((UsbPortResources->ResourcesTypes & USBPORT_RESOURCES_MEMORY) &&
        UsbPortResources->ResourceBase)
    {
        MmUnmapIoSpace(UsbPortResources->ResourceBase,
                       UsbPortResources->IoSpaceLength);
    }

    RtlZeroMemory(UsbPortResources, sizeof(USBPORT_RESOURCES));

    if (FdoExtension->BusInterface.InterfaceDereference &&
        FdoExtension->BusInterface.Context)
    {
        FdoExtension->BusInterface.InterfaceDereference(FdoExtension->BusInterface.Context);
    }

    RtlZeroMemory(&FdoExtension->BusInterface, sizeof(BUS_INTERFACE_STANDARD));

    FdoExtension->CommonExtension.DevicePowerState = PowerDeviceD3;

    USBPORT_CleanupTransportRegistrations(FdoExtension);
    USBPORT_CleanupTimeSyncContexts(FdoExtension);
    FdoExtension->Aux.NextTimeSyncId = 1;

    return STATUS_SUCCESS;
}

static
VOID
USBPORT_StopRootHub(IN PDEVICE_OBJECT PdoDevice)
{
    PUSBPORT_RHDEVICE_EXTENSION PdoExtension;
    PDEVICE_OBJECT FdoDevice;
    PUSBPORT_DEVICE_EXTENSION FdoExtension;
    PIRP WakeIrp = NULL;
    KIRQL OldIrql;

    PdoExtension = PdoDevice->DeviceExtension;
    FdoDevice = PdoExtension->FdoDevice;

    DPRINT1("USBPORT_StopRootHub: PDO=%p FDO=%p\n", PdoDevice, FdoDevice);

    if (!FdoDevice)
    {
        goto CleanupDeviceHandle;
    }

    FdoExtension = FdoDevice->DeviceExtension;

    if (FdoExtension->Flags & USBPORT_FLAG_RH_STOPPED)
    {
        DPRINT1("USBPORT_StopRootHub: root hub already marked stopped (Flags=0x%lx)\n",
                FdoExtension->Flags);
    }

    if (FdoExtension->MiniPortInterface &&
        FdoExtension->MiniPortInterface->Packet.RH_DisableIrq)
    {
        DPRINT1("USBPORT_StopRootHub: disabling miniport root hub interrupts\n");
        FdoExtension->MiniPortInterface->Packet.RH_DisableIrq(FdoExtension->MiniPortExt);
    }

    if (PdoExtension->CommonExtension.IsInterfaceEnabled)
    {
        USBPORT_RegisterDeviceInterface(PdoDevice,
                                        PdoDevice,
                                        &GUID_DEVINTERFACE_USB_HUB,
                                        FALSE);
    }

    FdoExtension->Flags |= USBPORT_FLAG_RH_STOPPED;
    USBPORT_StopControllerTimer(FdoExtension);

    KeAcquireSpinLock(&FdoExtension->PowerWakeSpinLock, &OldIrql);

    if (PdoExtension->WakeIrp && IoSetCancelRoutine(PdoExtension->WakeIrp, NULL))
    {
        WakeIrp = PdoExtension->WakeIrp;
        PdoExtension->WakeIrp = NULL;
    }

    KeReleaseSpinLock(&FdoExtension->PowerWakeSpinLock, OldIrql);

    if (WakeIrp)
    {
        WakeIrp->IoStatus.Status = STATUS_CANCELLED;
        WakeIrp->IoStatus.Information = 0;
        IoCompleteRequest(WakeIrp, IO_NO_INCREMENT);
    }

    if (USBPORT_ValidateDeviceHandle(FdoDevice, &PdoExtension->DeviceHandle))
    {
        DPRINT1("USBPORT_StopRootHub: removing handle %p\n",
                &PdoExtension->DeviceHandle);
        USBPORT_RemoveDevice(FdoDevice, &PdoExtension->DeviceHandle, 0);
    }
    else
    {
        DPRINT1("USBPORT_StopRootHub: handle %p no longer on DeviceHandle list\n",
                &PdoExtension->DeviceHandle);
    }

CleanupDeviceHandle:
    RtlZeroMemory(&PdoExtension->DeviceHandle, sizeof(USBPORT_DEVICE_HANDLE));
    InitializeListHead(&PdoExtension->DeviceHandle.PipeHandleList);
    InitializeListHead(&PdoExtension->DeviceHandle.TtList);

    if (PdoExtension->RootHubDescriptors)
    {
        ExFreePoolWithTag(PdoExtension->RootHubDescriptors, USB_PORT_TAG);
        PdoExtension->RootHubDescriptors = NULL;
    }

    if (PdoExtension->RootHubCallbackData)
    {
        USBPORT_DereferenceRootHubCallbackData(PdoExtension->RootHubCallbackData);
        PdoExtension->RootHubCallbackData = NULL;
    }

    PdoExtension->Endpoint = NULL;
    PdoExtension->ConfigurationValue = 0;
    PdoExtension->RootHubInitCallback = NULL;
    PdoExtension->RootHubInitContext = NULL;
    PdoExtension->CommonExtension.DevicePowerState = PowerDeviceD3;
}

NTSTATUS
NTAPI
USBPORT_StartDevice(IN PDEVICE_OBJECT FdoDevice,
                    IN PUSBPORT_RESOURCES UsbPortResources)
{
    PUSBPORT_DEVICE_EXTENSION FdoExtension;
    PUSBPORT_REGISTRATION_PACKET Packet;
    NTSTATUS Status;
    PCI_COMMON_CONFIG PciConfig;
    ULONG BytesRead;
    DEVICE_DESCRIPTION DeviceDescription;
    PDMA_ADAPTER DmaAdapter = NULL;
    ULONG MiniPortStatus;
    PUSBPORT_COMMON_BUFFER_HEADER HeaderBuffer;
    ULONG ResultLength;
    ULONG DisableSelectiveSuspend = 0;
    ULONG DisableCcDetect = 0;
    ULONG IdleEpSupport = 0;
    ULONG IdleEpSupportEx = 0;
    ULONG SoftRetry = 0;
    ULONG Limit2GB = 0;
    ULONG TotalBusBandwidth = 0;
    BOOLEAN IsCompanion = FALSE;
    BOOLEAN Use64BitDma = FALSE;
    ULONG LegacyBIOS;
    ULONG MiniportFlags;
    ULONG ix;

    DPRINT("USBPORT_StartDevice: FdoDevice - %p, UsbPortResources - %p\n",
           FdoDevice,
           UsbPortResources);

    FdoExtension = FdoDevice->DeviceExtension;
    Packet = &FdoExtension->MiniPortInterface->Packet;

    Status = USBPORT_QueryPciBusInterface(FdoDevice);

    if (NT_SUCCESS(Status))
    {
        BytesRead = (*FdoExtension->BusInterface.GetBusData)(FdoExtension->BusInterface.Context,
                                                             PCI_WHICHSPACE_CONFIG,
                                                             &PciConfig,
                                                             0,
                                                             PCI_COMMON_HDR_LENGTH);
    }
    else
    {
        BytesRead = 0;
    }

    if (BytesRead == PCI_COMMON_HDR_LENGTH)
    {
        FdoExtension->VendorID = PciConfig.VendorID;
        FdoExtension->DeviceID = PciConfig.DeviceID;
        FdoExtension->RevisionID = PciConfig.RevisionID;
        FdoExtension->ProgIf = PciConfig.ProgIf;
        FdoExtension->SubClass = PciConfig.SubClass;
        FdoExtension->BaseClass = PciConfig.BaseClass;
    }
    else
    {
        /* No PCI parent: an ACPI-enumerated controller (e.g. an xHCI exposed
         * via _HID under ACPI) has no PCI config space, so either the bus
         * interface query failed or GetBusData returned fewer than a full
         * PCI header. The firmware has already programmed MEM decode / bus
         * mastering and the HAL provides the DMA window, so tolerate this and
         * continue with zeroed identity fields instead of failing the start.
         * Modelled on the null-guard in USBPORT_GetSetConfigSpaceData. */
        DPRINT1("USBPORT_StartDevice: no PCI config space (ACPI-enumerated controller), continuing\n");
        FdoExtension->VendorID = 0;
        FdoExtension->DeviceID = 0;
        FdoExtension->RevisionID = 0;
        FdoExtension->ProgIf = 0;
        FdoExtension->SubClass = 0;
        FdoExtension->BaseClass = 0;
    }

    RtlZeroMemory(&DeviceDescription, sizeof(DeviceDescription));

    DeviceDescription.Version = DEVICE_DESCRIPTION_VERSION;
    DeviceDescription.Master = TRUE;
    DeviceDescription.ScatterGather = TRUE;
    Use64BitDma = USBPORT_DetermineDma64Bit(UsbPortResources, Packet);
    if (Use64BitDma)
    {
        DeviceDescription.Dma32BitAddresses = FALSE;
        DeviceDescription.Dma64BitAddresses = TRUE;
        DeviceDescription.DmaWidth = Width64Bits;
    }
    else
    {
        DeviceDescription.Dma32BitAddresses = TRUE;
        DeviceDescription.Dma64BitAddresses = FALSE;
        DeviceDescription.DmaWidth = Width32Bits;
    }
    UsbPortResources->Reserved &= ~USBPORT_RES_DMA_ADDR_MASK;
    UsbPortResources->Reserved |= Use64BitDma ? USBPORT_RES_DMA_ADDR_64BIT
                                              : USBPORT_RES_DMA_ADDR_32BIT;
    DeviceDescription.InterfaceType = PCIBus;
    DeviceDescription.DmaSpeed = Compatible;
    DeviceDescription.MaximumLength = MAXULONG;

    DmaAdapter = IoGetDmaAdapter(FdoExtension->CommonExtension.LowerPdoDevice,
                                 &DeviceDescription,
                                 &FdoExtension->NumberMapRegs);

    FdoExtension->DmaAdapter = DmaAdapter;

    if (!DmaAdapter)
    {
        DPRINT1("USBPORT_StartDevice: Failed to get DmaAdapter!\n");
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto ExitWithError;
    }

    Status = USBPORT_CreateWorkerThread(FdoDevice);
    if (!NT_SUCCESS(Status))
        goto ExitWithError;

    Status = USBPORT_QueryCapabilities(FdoDevice, &FdoExtension->Capabilities);
    if (!NT_SUCCESS(Status))
        goto ExitWithError;

    FdoExtension->PciDeviceNumber = FdoExtension->Capabilities.Address >> 16;
    FdoExtension->PciFunctionNumber = FdoExtension->Capabilities.Address & 0xFFFF;

    Status = IoGetDeviceProperty(FdoExtension->CommonExtension.LowerPdoDevice,
                                 DevicePropertyBusNumber,
                                 sizeof(ULONG),
                                 &FdoExtension->BusNumber,
                                 &ResultLength);

    if (!NT_SUCCESS(Status))
        goto ExitWithError;

    KeInitializeSpinLock(&FdoExtension->EndpointListSpinLock);
    KeInitializeSpinLock(&FdoExtension->EpStateChangeSpinLock);
    KeInitializeSpinLock(&FdoExtension->EndpointClosedSpinLock);
    KeInitializeSpinLock(&FdoExtension->DeviceHandleSpinLock);
    KeInitializeSpinLock(&FdoExtension->IdleIoCsqSpinLock);
    KeInitializeSpinLock(&FdoExtension->BadRequestIoCsqSpinLock);
    KeInitializeSpinLock(&FdoExtension->MapTransferSpinLock);
    KeInitializeSpinLock(&FdoExtension->FlushTransferSpinLock);
    KeInitializeSpinLock(&FdoExtension->FlushPendingTransferSpinLock);
    KeInitializeSpinLock(&FdoExtension->DoneTransferSpinLock);
    KeInitializeSpinLock(&FdoExtension->WorkerThreadEventSpinLock);
    KeInitializeSpinLock(&FdoExtension->MiniportSpinLock);
    KeInitializeSpinLock(&FdoExtension->TimerFlagsSpinLock);
    KeInitializeEvent(&FdoExtension->TimerDpcEvent, NotificationEvent, TRUE);
    KeInitializeSpinLock(&FdoExtension->PowerWakeSpinLock);
    KeInitializeSpinLock(&FdoExtension->SetPowerD0SpinLock);
    KeInitializeSpinLock(&FdoExtension->RootHubCallbackSpinLock);
    KeInitializeSpinLock(&FdoExtension->TtSpinLock);
    KeInitializeSpinLock(&FdoExtension->Aux.TransportListSpinLock);
    KeInitializeSpinLock(&FdoExtension->Aux.TimeSyncSpinLock);

    KeInitializeDpc(&FdoExtension->IsrDpc, USBPORT_IsrDpc, FdoDevice);

    KeInitializeDpc(&FdoExtension->TransferFlushDpc,
                    USBPORT_TransferFlushDpc,
                    FdoDevice);

    KeInitializeDpc(&FdoExtension->WorkerRequestDpc,
                    USBPORT_WorkerRequestDpc,
                    FdoDevice);

    KeInitializeDpc(&FdoExtension->HcWakeDpc,
                    USBPORT_HcWakeDpc,
                    FdoDevice);

    IoCsqInitialize(&FdoExtension->IdleIoCsq,
                    USBPORT_InsertIdleIrp,
                    USBPORT_RemoveIdleIrp,
                    USBPORT_PeekNextIdleIrp,
                    USBPORT_AcquireIdleLock,
                    USBPORT_ReleaseIdleLock,
                    USBPORT_CompleteCanceledIdleIrp);

    IoCsqInitialize(&FdoExtension->BadRequestIoCsq,
                    USBPORT_InsertBadRequest,
                    USBPORT_RemoveBadRequest,
                    USBPORT_PeekNextBadRequest,
                    USBPORT_AcquireBadRequestLock,
                    USBPORT_ReleaseBadRequestLock,
                    USBPORT_CompleteCanceledBadRequest);

    FdoExtension->IsrDpcCounter = -1;
    FdoExtension->IsrDpcHandlerCounter = -1;
    FdoExtension->IdleLockCounter = -1;
    FdoExtension->BadRequestLockCounter = -1;
    FdoExtension->ChirpRootPortLock = -1;

    FdoExtension->RHInitCallBackLock = 0;

    FdoExtension->UsbAddressBitMap[0] = 1;
    FdoExtension->UsbAddressBitMap[1] = 0;
    FdoExtension->UsbAddressBitMap[2] = 0;
    FdoExtension->UsbAddressBitMap[3] = 0;

    USBPORT_GetDefaultBIOSx(FdoDevice,
                            &FdoExtension->UsbBIOSx,
                            &DisableSelectiveSuspend,
                            &DisableCcDetect,
                            &IdleEpSupport,
                            &IdleEpSupportEx,
                            &SoftRetry);

    if (DisableSelectiveSuspend)
        FdoExtension->Flags |= USBPORT_FLAG_BIOS_DISABLE_SS;

    if (!DisableSelectiveSuspend &&
        USBPORT_IsSelectiveSuspendEnabled(FdoDevice))
    {
        FdoExtension->Flags |= USBPORT_FLAG_SELECTIVE_SUSPEND;
    }

    MiniportFlags = Packet->MiniPortFlags;

    if (MiniportFlags & USB_MINIPORT_FLAGS_POLLING)
        FdoExtension->Flags |= USBPORT_FLAG_HC_POLLING;

    if (MiniportFlags & USB_MINIPORT_FLAGS_WAKE_SUPPORT)
        FdoExtension->Flags |= USBPORT_FLAG_HC_WAKE_SUPPORT;

    if (MiniportFlags & USB_MINIPORT_FLAGS_DISABLE_SS)
        FdoExtension->Flags = (FdoExtension->Flags & ~USBPORT_FLAG_SELECTIVE_SUSPEND) |
                              USBPORT_FLAG_BIOS_DISABLE_SS;

    USBPORT_SetRegistryKeyValue(FdoExtension->CommonExtension.LowerPdoDevice,
                                TRUE,
                                REG_DWORD,
                                L"EnIdleEndpointSupport",
                                &IdleEpSupport,
                                sizeof(IdleEpSupport));

    USBPORT_SetRegistryKeyValue(FdoExtension->CommonExtension.LowerPdoDevice,
                                TRUE,
                                REG_DWORD,
                                L"EnIdleEndpointSupportEx",
                                &IdleEpSupportEx,
                                sizeof(IdleEpSupportEx));

    USBPORT_SetRegistryKeyValue(FdoExtension->CommonExtension.LowerPdoDevice,
                                TRUE,
                                REG_DWORD,
                                L"EnSoftRetry",
                                &SoftRetry,
                                sizeof(SoftRetry));

    USBPORT_GetRegistryKeyValueFullInfo(FdoDevice,
                                        FdoExtension->CommonExtension.LowerPdoDevice,
                                        TRUE,
                                        L"CommonBuffer2GBLimit",
                                        sizeof(L"CommonBuffer2GBLimit"),
                                        &Limit2GB,
                                        sizeof(Limit2GB));

    FdoExtension->CommonBufferLimit = (Limit2GB != 0);

    if (FdoExtension->BaseClass == PCI_CLASS_SERIAL_BUS_CTLR &&
        FdoExtension->SubClass == PCI_SUBCLASS_SB_USB  &&
        FdoExtension->ProgIf < PCI_INTERFACE_USB_ID_EHCI)
    {
        Status = USBPORT_IsCompanionController(FdoDevice, &IsCompanion);

        if (!NT_SUCCESS(Status))
        {
            if (IsCompanion)
            {
                FdoExtension->Flags |= USBPORT_FLAG_COMPANION_HC;
            }
            else
            {
                FdoExtension->Flags &= ~USBPORT_FLAG_COMPANION_HC;
            }
        }
    }

    if (DisableCcDetect)
    {
        FdoExtension->Flags &= ~USBPORT_FLAG_COMPANION_HC;
    }

    TotalBusBandwidth = Packet->MiniPortBusBandwidth;
    FdoExtension->TotalBusBandwidth = TotalBusBandwidth;

    USBPORT_GetRegistryKeyValueFullInfo(FdoDevice,
                                        FdoExtension->CommonExtension.LowerPdoDevice,
                                        TRUE,
                                        L"TotalBusBandwidth",
                                        sizeof(L"TotalBusBandwidth"),
                                        &TotalBusBandwidth,
                                        sizeof(TotalBusBandwidth));

    if (TotalBusBandwidth != FdoExtension->TotalBusBandwidth)
    {
        FdoExtension->TotalBusBandwidth = TotalBusBandwidth;
    }

    for (ix = 0; ix < USB2_FRAMES; ix++)
    {
        FdoExtension->Bandwidth[ix] = FdoExtension->TotalBusBandwidth -
                                      FdoExtension->TotalBusBandwidth / 10;
    }

    FdoExtension->ActiveIrpTable = ExAllocatePoolWithTag(NonPagedPool,
                                                         sizeof(USBPORT_IRP_TABLE),
                                                         USB_PORT_TAG);

    if (!FdoExtension->ActiveIrpTable)
    {
        DPRINT1("USBPORT_StartDevice: Allocate ActiveIrpTable failed!\n");
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto ExitWithError;
    }

    RtlZeroMemory(FdoExtension->ActiveIrpTable, sizeof(USBPORT_IRP_TABLE));

    FdoExtension->PendingIrpTable = ExAllocatePoolWithTag(NonPagedPool,
                                                          sizeof(USBPORT_IRP_TABLE),
                                                          USB_PORT_TAG);

    if (!FdoExtension->PendingIrpTable)
    {
        DPRINT1("USBPORT_StartDevice: Allocate PendingIrpTable failed!\n");
        Status = STATUS_INSUFFICIENT_RESOURCES;
        goto ExitWithError;
    }

    RtlZeroMemory(FdoExtension->PendingIrpTable, sizeof(USBPORT_IRP_TABLE));

    if (!UsbPortResources->ShareVector &&
        (Packet->MiniPortFlags & USB_MINIPORT_FLAGS_USB3) &&
        !(UsbPortResources->InterruptFlags & CM_RESOURCE_INTERRUPT_MESSAGE))
    {
        DPRINT1("USBPORT_StartDevice: forcing shared IRQ for xHCI controller\n");
        UsbPortResources->ShareVector = TRUE;
    }

    USBPORT_EnsureInterruptApis();

    if ((UsbPortResources->InterruptFlags & CM_RESOURCE_INTERRUPT_MESSAGE) &&
        UsbPortResources->InterruptMessageCount != 0 &&
        UsbPortIoConnectInterruptEx)
    {
        IO_CONNECT_INTERRUPT_PARAMETERS ConnectParameters;

        RtlZeroMemory(&ConnectParameters, sizeof(ConnectParameters));
        ConnectParameters.Version = CONNECT_MESSAGE_BASED;
        ConnectParameters.MessageBased.PhysicalDeviceObject =
            FdoExtension->CommonExtension.LowerPdoDevice;
        ConnectParameters.MessageBased.ConnectionContext.InterruptMessageTable =
            &FdoExtension->InterruptMessageInfo;
        ConnectParameters.MessageBased.MessageServiceRoutine =
            USBPORT_MessageInterruptService;
        ConnectParameters.MessageBased.ServiceContext = FdoDevice;
        ConnectParameters.MessageBased.SpinLock = NULL;
        ConnectParameters.MessageBased.SynchronizeIrql =
            UsbPortResources->InterruptLevel;
        ConnectParameters.MessageBased.FloatingSave = FALSE;
        ConnectParameters.MessageBased.FallBackServiceRoutine =
            USBPORT_InterruptService;

        Status = UsbPortIoConnectInterruptEx(&ConnectParameters);
        if (NT_SUCCESS(Status) && FdoExtension->InterruptMessageInfo)
        {
            NTSTATUS MsixStatus;

            FdoExtension->InterruptObject =
                FdoExtension->InterruptMessageInfo->MessageInfo[0].InterruptObject;
            FdoExtension->MessageInterruptsEnabled = TRUE;
            FdoExtension->MessageInterruptCount =
                (UCHAR)FdoExtension->InterruptMessageInfo->MessageCount;
            FdoExtension->Flags |= USBPORT_FLAG_INT_CONNECTED;

            /* Program MSI-X table with vector information */
            MsixStatus = USBPORT_ProgramMsixTable(
                FdoDevice,
                FdoExtension->InterruptMessageInfo);
            if (!NT_SUCCESS(MsixStatus))
            {
                NTSTATUS MsiStatus;

                DPRINT1("USBPORT_StartDevice: MSI-X table programming failed (0x%lx), trying MSI fallback\n",
                        MsixStatus);

                /*
                 * MSI-X programming failed - try MSI as intermediate fallback.
                 * MSI uses the same message info from IoConnectInterruptEx but
                 * programs the simpler MSI capability instead of MSI-X table.
                 */
                MsiStatus = USBPORT_ProgramMsiTable(
                    FdoDevice,
                    FdoExtension->InterruptMessageInfo);

                if (NT_SUCCESS(MsiStatus))
                {
                    DPRINT1("USBPORT_StartDevice: MSI fallback successful\n");
                    /* Keep the message-based interrupt connected, MSI is now active */
                }
                else
                {
                    DPRINT1("USBPORT_StartDevice: MSI fallback also failed (0x%lx), falling back to INTx\n",
                            MsiStatus);

                    /*
                     * Both MSI-X and MSI programming failed - disconnect message-based
                     * interrupt and fall back to legacy line-based INTx.
                     */
                    DPRINT1("USBPORT_StartDevice: Disconnecting message interrupt...\n");
                    if (UsbPortIoDisconnectInterruptEx)
                    {
                        IO_DISCONNECT_INTERRUPT_PARAMETERS DisconnectParameters;
                        RtlZeroMemory(&DisconnectParameters, sizeof(DisconnectParameters));
                        DisconnectParameters.Version = CONNECT_MESSAGE_BASED;
                        DisconnectParameters.ConnectionContext.InterruptMessageTable =
                            FdoExtension->InterruptMessageInfo;
                        UsbPortIoDisconnectInterruptEx(&DisconnectParameters);
                        DPRINT1("USBPORT_StartDevice: Message interrupt disconnected\n");
                    }
                    else
                    {
                        DPRINT1("USBPORT_StartDevice: WARNING: UsbPortIoDisconnectInterruptEx is NULL!\n");
                    }

                    FdoExtension->InterruptObject = NULL;
                    FdoExtension->InterruptMessageInfo = NULL;
                    FdoExtension->MessageInterruptsEnabled = FALSE;
                    FdoExtension->MessageInterruptCount = 0;
                    FdoExtension->Flags &= ~USBPORT_FLAG_INT_CONNECTED;
                }
            }
            else
            {
                DPRINT("USBPORT_StartDevice: MSI-X table programmed successfully\n");
            }
        }
        else
        {
            NTSTATUS ConnectStatus = Status;

            if (FdoExtension->InterruptMessageInfo && UsbPortIoDisconnectInterruptEx)
            {
                IO_DISCONNECT_INTERRUPT_PARAMETERS DisconnectParameters;
                RtlZeroMemory(&DisconnectParameters, sizeof(DisconnectParameters));
                DisconnectParameters.Version = CONNECT_MESSAGE_BASED;
                DisconnectParameters.ConnectionContext.InterruptMessageTable =
                    FdoExtension->InterruptMessageInfo;
                UsbPortIoDisconnectInterruptEx(&DisconnectParameters);
            }

            FdoExtension->InterruptMessageInfo = NULL;
            FdoExtension->MessageInterruptsEnabled = FALSE;
            FdoExtension->MessageInterruptCount = 0;
            Status = STATUS_UNSUCCESSFUL;

            DPRINT1("USBPORT_StartDevice: IoConnectInterruptEx failed (Status=0x%lx MsgInfo=%p)\n",
                    ConnectStatus,
                    FdoExtension->InterruptMessageInfo);
        }
    }
    else
    {
        Status = STATUS_NOT_SUPPORTED;
    }

    if (!(FdoExtension->Flags & USBPORT_FLAG_INT_CONNECTED))
    {
        ULONG FallbackVector = UsbPortResources->InterruptVector;
        KIRQL FallbackLevel = UsbPortResources->InterruptLevel;
        KAFFINITY FallbackAffinity = UsbPortResources->InterruptAffinity;

#if defined(_M_ARM64)
        /*
         * On ARM64, if the resources contain a message interrupt (LPI vector)
         * but MSI-X programming failed, we need to fall back to the legacy
         * INTx interrupt line. The LPI vector (e.g., 8192+) cannot be used
         * with IoConnectInterrupt because it requires ITS device mapping.
         *
         * Query the PCI bus interface to get the legacy interrupt line and
         * translate it to a system vector using HalGetInterruptVector.
         */
        if (UsbPortResources->InterruptFlags & CM_RESOURCE_INTERRUPT_MESSAGE)
        {
            ULONG LegacyVector = 0;
            KIRQL LegacyLevel = 0;
            KAFFINITY LegacyAffinity = 0;
            NTSTATUS LegacyStatus;

            DPRINT1("USBPORT_StartDevice: ARM64 MSI-X fallback - querying legacy INTx\n");

            LegacyStatus = USBPORT_GetLegacyInterruptVector(
                FdoDevice,
                FdoExtension->CommonExtension.LowerPdoDevice,
                &LegacyVector,
                &LegacyLevel,
                &LegacyAffinity);

            if (NT_SUCCESS(LegacyStatus) && LegacyVector != 0)
            {
                DPRINT1("USBPORT_StartDevice: Using legacy INTx Vector=%lu Level=%u Affinity=0x%lx\n",
                        LegacyVector, LegacyLevel, (ULONG)LegacyAffinity);
                FallbackVector = LegacyVector;
                FallbackLevel = LegacyLevel;
                FallbackAffinity = LegacyAffinity;
                /* Legacy INTx is level-triggered, shared */
                UsbPortResources->InterruptMode = LevelSensitive;
                UsbPortResources->ShareVector = TRUE;
            }
            else
            {
                DPRINT1("USBPORT_StartDevice: Failed to get legacy INTx (0x%lx), trying original vector\n",
                        LegacyStatus);
            }
        }
#endif /* _M_ARM64 */

        Status = IoConnectInterrupt(&FdoExtension->InterruptObject,
                                    USBPORT_InterruptService,
                                    (PVOID)FdoDevice,
                                    0,
                                    FallbackVector,
                                    FallbackLevel,
                                    FallbackLevel,
                                    UsbPortResources->InterruptMode,
                                    UsbPortResources->ShareVector,
                                    FallbackAffinity,
                                    0);

        if (!NT_SUCCESS(Status))
        {
            DPRINT1("USBPORT_StartDevice: IoConnectInterrupt failed (Vector=%lu)!\n", FallbackVector);
            goto ExitWithError;
        }

        FdoExtension->Flags |= USBPORT_FLAG_INT_CONNECTED;
    }

    if (Packet->MiniPortExtensionSize)
    {
        RtlZeroMemory(FdoExtension->MiniPortExt, Packet->MiniPortExtensionSize);
    }

    if (Packet->MiniPortResourcesSize)
    {
        HeaderBuffer = USBPORT_AllocateCommonBuffer(FdoDevice,
                                                    Packet->MiniPortResourcesSize);

        if (!HeaderBuffer)
        {
            DPRINT1("USBPORT_StartDevice: Failed to AllocateCommonBuffer!\n");
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto ExitWithError;
        }

        UsbPortResources->StartVA = HeaderBuffer->VirtualAddress;
        UsbPortResources->StartPA = HeaderBuffer->PhysicalAddress;

        FdoExtension->MiniPortCommonBuffer = HeaderBuffer;
    }
    else
    {
        FdoExtension->MiniPortCommonBuffer = NULL;
    }

    /*
     * Provide the lower device object to the miniport.
     * This allows miniport drivers to send IOCTLs (e.g., ACPI _OSC)
     * to the device stack below the FDO.
     */
    UsbPortResources->LowerDeviceObject = FdoExtension->CommonExtension.LowerDevice;

    /*
     * Provide PCI location information to the miniport.
     * This allows miniport drivers to look up their ACPI device node
     * using AcpiFindPciDeviceInNamespace() for _OSC evaluation.
     */
    UsbPortResources->PciSegment = 0;  /* Assume segment 0 for most systems */
    UsbPortResources->PciBusNumber = FdoExtension->BusNumber;
    UsbPortResources->PciDeviceNumber = FdoExtension->PciDeviceNumber;
    UsbPortResources->PciFunctionNumber = FdoExtension->PciFunctionNumber;

    MiniPortStatus = Packet->StartController(FdoExtension->MiniPortExt,
                                             UsbPortResources);

    if (UsbPortResources->LegacySupport)
    {
        FdoExtension->Flags |= USBPORT_FLAG_LEGACY_SUPPORT;
        LegacyBIOS = 1;
    }
    else
    {
        LegacyBIOS = 0;
    }

    USBPORT_SetRegistryKeyValue(FdoExtension->CommonExtension.LowerPdoDevice,
                                FALSE,
                                REG_DWORD,
                                L"DetectedLegacyBIOS",
                                &LegacyBIOS,
                                sizeof(LegacyBIOS));

    if (MiniPortStatus)
    {
        DPRINT1("USBPORT_StartDevice: Failed to Start MiniPort. MiniPortStatus - %x\n",
                MiniPortStatus);

        switch (MiniPortStatus)
        {
            case MP_STATUS_NO_RESOURCES:
                Status = STATUS_INSUFFICIENT_RESOURCES;
                break;

            case MP_STATUS_NO_BANDWIDTH:
                Status = STATUS_INSUFFICIENT_RESOURCES;
                break;

            case MP_STATUS_HW_ERROR:
                Status = STATUS_DEVICE_HARDWARE_ERROR;
                break;

            case MP_STATUS_NOT_SUPPORTED:
                Status = STATUS_NOT_SUPPORTED;
                break;

            case MP_STATUS_ERROR:
            case MP_STATUS_FAILURE:
            case MP_STATUS_RESERVED1:
            case MP_STATUS_UNSUCCESSFUL:
            default:
                Status = STATUS_UNSUCCESSFUL;
                break;
        }

        if (FdoExtension->Flags & USBPORT_FLAG_INT_CONNECTED)
        {
            if (FdoExtension->InterruptMessageInfo && UsbPortIoDisconnectInterruptEx)
            {
                IO_DISCONNECT_INTERRUPT_PARAMETERS DisconnectParameters;
                RtlZeroMemory(&DisconnectParameters, sizeof(DisconnectParameters));
                DisconnectParameters.Version = CONNECT_MESSAGE_BASED;
                DisconnectParameters.ConnectionContext.InterruptMessageTable =
                    FdoExtension->InterruptMessageInfo;
                UsbPortIoDisconnectInterruptEx(&DisconnectParameters);
                FdoExtension->InterruptMessageInfo = NULL;
                FdoExtension->MessageInterruptsEnabled = FALSE;
                FdoExtension->MessageInterruptCount = 0;
            }
            else if (FdoExtension->InterruptObject)
            {
                IoDisconnectInterrupt(FdoExtension->InterruptObject);
            }

            FdoExtension->InterruptObject = NULL;
            FdoExtension->Flags &= ~USBPORT_FLAG_INT_CONNECTED;
        }

        if (FdoExtension->MiniPortCommonBuffer)
        {
            USBPORT_FreeCommonBuffer(FdoDevice, FdoExtension->MiniPortCommonBuffer);
            FdoExtension->MiniPortCommonBuffer = NULL;
        }

        goto ExitWithError;
    }
    else
    {
        FdoExtension->MiniPortFlags |= USBPORT_MPFLAG_INTERRUPTS_ENABLED;
        USBPORT_MiniportInterrupts(FdoDevice, TRUE);
    }

    FdoExtension->TimerValue = 500;
    USBPORT_StartTimer((PVOID)FdoDevice, 500);

    Status = USBPORT_RegisterDeviceInterface(FdoExtension->CommonExtension.LowerPdoDevice,
                                             FdoDevice,
                                             &GUID_DEVINTERFACE_USB_HOST_CONTROLLER,
                                             TRUE);

    if (!NT_SUCCESS(Status))
    {
        DPRINT1("USBPORT_StartDevice: RegisterDeviceInterface failed!\n");
        goto ExitWithError;
    }

    USBPORT_CreateLegacySymbolicLink(FdoDevice);

    FdoExtension->Flags |= USBPORT_FLAG_HC_STARTED;

    DPRINT("USBPORT_StartDevice: Exit Status - %p\n", Status);
    return Status;

ExitWithError:
    USBPORT_StopWorkerThread(FdoDevice);
    USBPORT_StopDevice(FdoDevice);

    DPRINT1("USBPORT_StartDevice: ExitWithError Status - %lx\n", Status);
    return Status;
}

NTSTATUS
NTAPI
USBPORT_ParseResources(IN PDEVICE_OBJECT FdoDevice,
                       IN PIRP Irp,
                       IN PUSBPORT_RESOURCES UsbPortResources)
{
    PCM_RESOURCE_LIST AllocatedResourcesTranslated;
    PCM_PARTIAL_RESOURCE_LIST ResourceList;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR PartialDescriptor;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR PortDescriptor = NULL;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR MemoryDescriptor = NULL;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR InterruptDescriptor = NULL;
    PIO_STACK_LOCATION IoStack;
    ULONG ix;
    NTSTATUS Status = STATUS_SUCCESS;

    DPRINT("USBPORT_ParseResources: ... \n");

    IoStack = IoGetCurrentIrpStackLocation(Irp);
    AllocatedResourcesTranslated = IoStack->Parameters.StartDevice.AllocatedResourcesTranslated;

    if (AllocatedResourcesTranslated)
    {
        RtlZeroMemory(UsbPortResources, sizeof(USBPORT_RESOURCES));

        ResourceList = &AllocatedResourcesTranslated->List[0].PartialResourceList;

        PartialDescriptor = &ResourceList->PartialDescriptors[0];

        for (ix = 0; ix < ResourceList->Count; ++ix)
        {
            if (PartialDescriptor->Type == CmResourceTypePort)
            {
                if (!PortDescriptor)
                    PortDescriptor = PartialDescriptor;
            }
            else if (PartialDescriptor->Type == CmResourceTypeInterrupt)
            {
                /* Prefer message interrupts over legacy if both are present. */
                if (!InterruptDescriptor)
                {
                    InterruptDescriptor = PartialDescriptor;
                }
                else if (!(InterruptDescriptor->Flags & CM_RESOURCE_INTERRUPT_MESSAGE) &&
                         (PartialDescriptor->Flags & CM_RESOURCE_INTERRUPT_MESSAGE))
                {
                    InterruptDescriptor = PartialDescriptor;
                }
            }
            else if (PartialDescriptor->Type == CmResourceTypeMemory)
            {
                if (!MemoryDescriptor)
                    MemoryDescriptor = PartialDescriptor;
            }

            PartialDescriptor += 1;
        }

        if (PortDescriptor)
        {
            if (PortDescriptor->Flags & CM_RESOURCE_PORT_IO)
            {
                UsbPortResources->ResourceBase = (PVOID)(ULONG_PTR)PortDescriptor->u.Port.Start.QuadPart;
            }
            else
            {
                UsbPortResources->ResourceBase = MmMapIoSpace(PortDescriptor->u.Port.Start,
                                                              PortDescriptor->u.Port.Length,
                                                              0);
            }

            UsbPortResources->IoSpaceLength = PortDescriptor->u.Port.Length;

            if (UsbPortResources->ResourceBase)
            {
                UsbPortResources->ResourcesTypes |= USBPORT_RESOURCES_PORT;
            }
            else
            {
                Status = STATUS_NONE_MAPPED;
            }
        }

        if (MemoryDescriptor && NT_SUCCESS(Status))
        {
            PUSBPORT_DEVICE_EXTENSION FdoExtension = FdoDevice->DeviceExtension;

            UsbPortResources->IoSpaceLength = MemoryDescriptor->u.Memory.Length;

            UsbPortResources->ResourceBase = MmMapIoSpace(MemoryDescriptor->u.Memory.Start,
                                                          MemoryDescriptor->u.Memory.Length,
                                                          0);

            if (UsbPortResources->ResourceBase)
            {
                UsbPortResources->ResourcesTypes |= USBPORT_RESOURCES_MEMORY;
                /* Store physical address for MSI-X table programming */
                FdoExtension->MemoryBasePhysical = MemoryDescriptor->u.Memory.Start;
            }
            else
            {
                Status = STATUS_NONE_MAPPED;
            }
        }

        if (InterruptDescriptor && NT_SUCCESS(Status))
        {
            UsbPortResources->ResourcesTypes |= USBPORT_RESOURCES_INTERRUPT;
            UsbPortResources->InterruptFlags = InterruptDescriptor->Flags;
            UsbPortResources->InterruptMessageCount = 0;

            if (InterruptDescriptor->Flags & CM_RESOURCE_INTERRUPT_MESSAGE)
            {
#if (NTDDI_VERSION >= NTDDI_LONGHORN)
                UsbPortResources->InterruptVector =
                    InterruptDescriptor->u.MessageInterrupt.Translated.Vector;
                UsbPortResources->InterruptLevel =
                    (KIRQL)InterruptDescriptor->u.MessageInterrupt.Translated.Level;
                UsbPortResources->InterruptAffinity =
                    InterruptDescriptor->u.MessageInterrupt.Translated.Affinity;

                if (InterruptDescriptor->u.MessageInterrupt.Raw.MessageCount)
                {
                    UsbPortResources->InterruptMessageCount =
                        InterruptDescriptor->u.MessageInterrupt.Raw.MessageCount;
                }
                else
                {
                    UsbPortResources->InterruptMessageCount = 1;
                }
#else
                UsbPortResources->InterruptVector = InterruptDescriptor->u.Interrupt.Vector;
                UsbPortResources->InterruptLevel = InterruptDescriptor->u.Interrupt.Level;
                UsbPortResources->InterruptAffinity = InterruptDescriptor->u.Interrupt.Affinity;
                UsbPortResources->InterruptMessageCount = 1;
#endif
            }
            else
            {
                UsbPortResources->InterruptVector = InterruptDescriptor->u.Interrupt.Vector;
                UsbPortResources->InterruptLevel = InterruptDescriptor->u.Interrupt.Level;
                UsbPortResources->InterruptAffinity = InterruptDescriptor->u.Interrupt.Affinity;
            }

            UsbPortResources->ShareVector =
                (InterruptDescriptor->ShareDisposition == CmResourceShareShared);

            /*
             * MSI/MSI-X interrupts are inherently edge-triggered. The LATCHED flag
             * may not be set in the resource descriptor, but we must use edge-
             * triggered mode to prevent interrupt storms.
             */
            if (InterruptDescriptor->Flags & CM_RESOURCE_INTERRUPT_MESSAGE)
            {
                UsbPortResources->InterruptMode = Latched;
                UsbPortResources->ShareVector = FALSE; /* MSI can't be shared */
            }
            else
            {
                UsbPortResources->InterruptMode =
                    (InterruptDescriptor->Flags & CM_RESOURCE_INTERRUPT_LATCHED) ?
                        Latched : LevelSensitive;
            }
        }
    }
    else
    {
        Status = STATUS_NONE_MAPPED;
    }

    return Status;
}

NTSTATUS
NTAPI
USBPORT_CreatePdo(IN PDEVICE_OBJECT FdoDevice,
                  OUT PDEVICE_OBJECT *RootHubPdo)
{
    PUSBPORT_DEVICE_EXTENSION FdoExtension;
    PUSBPORT_ROOT_HUB_CALLBACK_DATA CallbackData;
    BOOLEAN CallbackDataReferenced = FALSE;
    PUSBPORT_RHDEVICE_EXTENSION PdoExtension;
    UNICODE_STRING DeviceName;
    ULONG DeviceNumber = 0;
    PDEVICE_OBJECT DeviceObject = NULL;
    WCHAR CharDeviceName[64];
    NTSTATUS Status = STATUS_SUCCESS;

    DPRINT("USBPORT_CreatePdo: FdoDevice - %p, RootHubPdo - %p\n",
           FdoDevice,
           RootHubPdo);

    FdoExtension = FdoDevice->DeviceExtension;

    CallbackData = FdoExtension->RootHubCallbackData;

    if (!CallbackData)
    {
        CallbackData = ExAllocatePoolWithTag(NonPagedPool,
                                             sizeof(USBPORT_ROOT_HUB_CALLBACK_DATA),
                                             USB_PORT_TAG);

        if (!CallbackData)
        {
            *RootHubPdo = NULL;
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        RtlZeroMemory(CallbackData, sizeof(*CallbackData));
        CallbackData->RefCount = 1;
        KeInitializeSpinLock(&CallbackData->Lock);
        FdoExtension->RootHubCallbackData = CallbackData;
    }

    do
    {
        RtlStringCbPrintfW(CharDeviceName,
                           sizeof(CharDeviceName),
                           L"\\Device\\USBPDO-%d",
                           DeviceNumber);

        RtlInitUnicodeString(&DeviceName, CharDeviceName);

        DPRINT("USBPORT_CreatePdo: DeviceName - %wZ\n", &DeviceName);

        Status = IoCreateDevice(FdoExtension->MiniPortInterface->DriverObject,
                                sizeof(USBPORT_RHDEVICE_EXTENSION),
                                &DeviceName,
                                FILE_DEVICE_BUS_EXTENDER,
                                0,
                                FALSE,
                                &DeviceObject);

        ++DeviceNumber;
    }
    while (Status == STATUS_OBJECT_NAME_COLLISION);

    if (!NT_SUCCESS(Status))
    {
        *RootHubPdo = NULL;
        DPRINT1("USBPORT_CreatePdo: Filed create HubPdo!\n");
        return Status;
    }

    if (DeviceObject)
    {
        PdoExtension = DeviceObject->DeviceExtension;

        RtlZeroMemory(PdoExtension, sizeof(USBPORT_RHDEVICE_EXTENSION));

        PdoExtension->CommonExtension.SelfDevice = DeviceObject;
        PdoExtension->CommonExtension.IsPDO = TRUE;

        PdoExtension->FdoDevice = FdoDevice;
        PdoExtension->PdoNameNumber = DeviceNumber;
        PdoExtension->RootHubCallbackData = CallbackData;
        USBPORT_ReferenceRootHubCallbackData(CallbackData);
        CallbackDataReferenced = TRUE;

        USBPORT_AdjustDeviceCapabilities(FdoDevice, DeviceObject);

        DeviceObject->StackSize = FdoDevice->StackSize;

        DeviceObject->Flags |= DO_POWER_PAGABLE;
        DeviceObject->Flags &= ~DO_DEVICE_INITIALIZING;
    }
    else
    {
        Status = STATUS_UNSUCCESSFUL;
    }

    if (!NT_SUCCESS(Status))
    {
        if (CallbackDataReferenced)
        {
            USBPORT_DereferenceRootHubCallbackData(CallbackData);
        }

        *RootHubPdo = NULL;
    }
    else
    {
        *RootHubPdo = DeviceObject;
    }

    if (NT_SUCCESS(Status) && DeviceObject)
    {
        FdoExtension->RootHubPdoExtension = DeviceObject->DeviceExtension;
#if DBG
        DPRINT1("USBPORT_CreatePdo: HubPdo=%p DevExt=%p\n",
                DeviceObject,
                FdoExtension->RootHubPdoExtension);
#else
        DPRINT("USBPORT_CreatePdo: HubPdo - %p\n", DeviceObject);
#endif
    }
    else if (!NT_SUCCESS(Status))
    {
        FdoExtension->RootHubPdoExtension = NULL;
        DPRINT("USBPORT_CreatePdo: HubPdo - %p\n", DeviceObject);
    }
    else
    {
        DPRINT("USBPORT_CreatePdo: HubPdo - %p\n", DeviceObject);
    }

    return Status;
}

NTSTATUS
NTAPI
USBPORT_FdoPnP(IN PDEVICE_OBJECT FdoDevice,
               IN PIRP Irp)
{
    PUSBPORT_DEVICE_EXTENSION FdoExtension;
    PUSBPORT_COMMON_DEVICE_EXTENSION FdoCommonExtension;
    PUSBPORT_REGISTRATION_PACKET Packet;
    PUSBPORT_RESOURCES UsbPortResources;
    PIO_STACK_LOCATION IoStack;
    UCHAR Minor;
    KEVENT Event;
    NTSTATUS Status;
    DEVICE_RELATION_TYPE RelationType;
    PDEVICE_RELATIONS DeviceRelations;
    PDEVICE_OBJECT RootHubPdo;

    FdoExtension = FdoDevice->DeviceExtension;
    FdoCommonExtension = &FdoExtension->CommonExtension;
    UsbPortResources = &FdoExtension->UsbPortResources;
    Packet = &FdoExtension->MiniPortInterface->Packet;

    IoStack = IoGetCurrentIrpStackLocation(Irp);
    Minor = IoStack->MinorFunction;

    DPRINT("USBPORT_FdoPnP: FdoDevice - %p, Minor - %x\n", FdoDevice, Minor);

    RelationType = IoStack->Parameters.QueryDeviceRelations.Type;

    switch (Minor)
    {
        case IRP_MN_START_DEVICE:
            DPRINT("IRP_MN_START_DEVICE\n");

            KeInitializeEvent(&Event, NotificationEvent, FALSE);

            IoCopyCurrentIrpStackLocationToNext(Irp);

            IoSetCompletionRoutine(Irp,
                                   USBPORT_FdoStartCompletion,
                                   &Event,
                                   TRUE,
                                   TRUE,
                                   TRUE);

            Status = IoCallDriver(FdoCommonExtension->LowerDevice,
                                  Irp);

            if (Status == STATUS_PENDING)
            {
                KeWaitForSingleObject(&Event,
                                      Suspended,
                                      KernelMode,
                                      FALSE,
                                      NULL);

                Status = Irp->IoStatus.Status;
            }

            if (!NT_SUCCESS(Status))
            {
                goto Exit;
            }

            Status = USBPORT_ParseResources(FdoDevice,
                                            Irp,
                                            UsbPortResources);

            if (!NT_SUCCESS(Status))
            {
                FdoCommonExtension->PnpStateFlags |= USBPORT_PNP_STATE_STOPPED;
                goto Exit;
            }

            Status = USBPORT_StartDevice(FdoDevice, UsbPortResources);

            if (!NT_SUCCESS(Status))
            {
                FdoCommonExtension->PnpStateFlags |= USBPORT_PNP_STATE_STOPPED;
                goto Exit;
            }

            FdoCommonExtension->PnpStateFlags &= ~USBPORT_PNP_STATE_NOT_INIT;
            FdoCommonExtension->PnpStateFlags |= USBPORT_PNP_STATE_STARTED;

            FdoCommonExtension->DevicePowerState = PowerDeviceD0;

            if (Packet->MiniPortFlags & USB_MINIPORT_FLAGS_USB2)
            {
                USBPORT_AddUSB2Fdo(FdoDevice);
            }
            else
            {
                USBPORT_AddUSB1Fdo(FdoDevice);
            }

Exit:
            Irp->IoStatus.Status = Status;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return Status;

        case IRP_MN_QUERY_REMOVE_DEVICE:
            DPRINT("IRP_MN_QUERY_REMOVE_DEVICE\n");
            if (Packet->MiniPortFlags & USB_MINIPORT_FLAGS_USB2)
            {
                DPRINT1("USBPORT_FdoPnP: Haction registry write FIXME\n");
            }

            Irp->IoStatus.Status = STATUS_SUCCESS;
            goto ForwardIrp;

        case IRP_MN_REMOVE_DEVICE:
            DPRINT("USBPORT_FdoPnP: IRP_MN_REMOVE_DEVICE\n");
            FdoCommonExtension->PnpStateFlags |= USBPORT_PNP_STATE_FAILED;

            if (FdoCommonExtension->PnpStateFlags & USBPORT_PNP_STATE_STARTED &&
               !(FdoCommonExtension->PnpStateFlags & USBPORT_PNP_STATE_NOT_INIT))
            {
                USBPORT_StopWorkerThread(FdoDevice);
                USBPORT_StopDevice(FdoDevice);
                FdoCommonExtension->PnpStateFlags |= USBPORT_PNP_STATE_NOT_INIT;
            }

            if (FdoExtension->Flags & USBPORT_FLAG_REGISTERED_FDO)
            {
                USBPORT_RemoveUSBxFdo(FdoDevice);
            }

            Irp->IoStatus.Status = STATUS_SUCCESS;
            IoSkipCurrentIrpStackLocation(Irp);
            Status = IoCallDriver(FdoCommonExtension->LowerDevice, Irp);

            IoDetachDevice(FdoCommonExtension->LowerDevice);

            RootHubPdo = FdoExtension->RootHubPdo;

            IoDeleteDevice(FdoDevice);

            if (RootHubPdo)
            {
                PUSBPORT_RHDEVICE_EXTENSION RootHubExtension = RootHubPdo->DeviceExtension;
                if (RootHubExtension && RootHubExtension->RootHubCallbackData)
                {
                    USBPORT_DereferenceRootHubCallbackData(RootHubExtension->RootHubCallbackData);
                    RootHubExtension->RootHubCallbackData = NULL;
                }
                IoDeleteDevice(RootHubPdo);
                FdoExtension->RootHubPdo = NULL;
                FdoExtension->RootHubPdoExtension = NULL;
            }

            if (FdoExtension->RootHubCallbackData)
            {
                USBPORT_DereferenceRootHubCallbackData(FdoExtension->RootHubCallbackData);
                FdoExtension->RootHubCallbackData = NULL;
            }

            return Status;

        case IRP_MN_CANCEL_REMOVE_DEVICE:
            DPRINT("IRP_MN_CANCEL_REMOVE_DEVICE\n");
            Irp->IoStatus.Status = STATUS_SUCCESS;
            goto ForwardIrp;

        case IRP_MN_STOP_DEVICE:
            DPRINT("IRP_MN_STOP_DEVICE\n");
            if (FdoCommonExtension->PnpStateFlags & USBPORT_PNP_STATE_STARTED)
            {
                USBPORT_StopWorkerThread(FdoDevice);
                USBPORT_StopDevice(FdoDevice);
                if (FdoExtension->Flags & USBPORT_FLAG_REGISTERED_FDO)
                {
                    USBPORT_RemoveUSBxFdo(FdoDevice);
                }
                FdoCommonExtension->PnpStateFlags &= ~USBPORT_PNP_STATE_STARTED;
                FdoCommonExtension->PnpStateFlags |= USBPORT_PNP_STATE_NOT_INIT;
                FdoCommonExtension->PnpStateFlags |= USBPORT_PNP_STATE_STOPPED;
            }

            Irp->IoStatus.Status = STATUS_SUCCESS;
            goto ForwardIrp;

        case IRP_MN_QUERY_STOP_DEVICE:
            DPRINT("IRP_MN_QUERY_STOP_DEVICE\n");
            Irp->IoStatus.Status = STATUS_SUCCESS;
            goto ForwardIrp;

        case IRP_MN_CANCEL_STOP_DEVICE:
            DPRINT("IRP_MN_CANCEL_STOP_DEVICE\n");
            Irp->IoStatus.Status = STATUS_SUCCESS;
            goto ForwardIrp;

        case IRP_MN_QUERY_DEVICE_RELATIONS:
            DPRINT("IRP_MN_QUERY_DEVICE_RELATIONS\n");
            if (RelationType == BusRelations)
            {
                if (!(FdoCommonExtension->PnpStateFlags & USBPORT_PNP_STATE_STARTED))
                {
                    DeviceRelations = ExAllocatePoolWithTag(PagedPool,
                                                            sizeof(DEVICE_RELATIONS),
                                                            USB_PORT_TAG);

                    if (!DeviceRelations)
                    {
                        Status = STATUS_INSUFFICIENT_RESOURCES;
                        Irp->IoStatus.Status = Status;
                        IoCompleteRequest(Irp, IO_NO_INCREMENT);
                        return Status;
                    }

                    DeviceRelations->Count = 0;
                    DeviceRelations->Objects[0] = NULL;

                    Irp->IoStatus.Status = STATUS_SUCCESS;
                    Irp->IoStatus.Information = (ULONG_PTR)DeviceRelations;
                    IoCompleteRequest(Irp, IO_NO_INCREMENT);
                    return STATUS_SUCCESS;
                }

                DeviceRelations = ExAllocatePoolWithTag(PagedPool,
                                                        sizeof(DEVICE_RELATIONS),
                                                        USB_PORT_TAG);

                if (!DeviceRelations)
                {
                    Status = STATUS_INSUFFICIENT_RESOURCES;
                    Irp->IoStatus.Status = Status;
                    IoCompleteRequest(Irp, IO_NO_INCREMENT);
                    return Status;
                }

                DeviceRelations->Count = 0;
                DeviceRelations->Objects[0] = NULL;

                if (!FdoExtension->RootHubPdo)
                {
                    Status = USBPORT_CreatePdo(FdoDevice,
                                               &FdoExtension->RootHubPdo);

                    if (!NT_SUCCESS(Status))
                    {
                        ExFreePoolWithTag(DeviceRelations, USB_PORT_TAG);
                        goto ForwardIrp;
                    }
                }
                else
                {
                    Status = STATUS_SUCCESS;
                }

                FdoExtension->RootHubPdoExtension = USBPORT_GetRootHubExtension(FdoExtension);

                DeviceRelations->Count = 1;
                DeviceRelations->Objects[0] = FdoExtension->RootHubPdo;

                ObReferenceObject(FdoExtension->RootHubPdo);
                Irp->IoStatus.Information = (ULONG_PTR)DeviceRelations;
            }
            else
            {
                if (RelationType == RemovalRelations)
                {
                    DPRINT1("USBPORT_FdoPnP: FIXME IRP_MN_QUERY_DEVICE_RELATIONS/RemovalRelations\n");
                }

                goto ForwardIrp;
            }

            Irp->IoStatus.Status = Status;
            goto ForwardIrp;

        case IRP_MN_QUERY_INTERFACE:
            DPRINT("IRP_MN_QUERY_INTERFACE\n");
            goto ForwardIrp;

        case IRP_MN_QUERY_CAPABILITIES:
            DPRINT("IRP_MN_QUERY_CAPABILITIES\n");
            goto ForwardIrp;

        case IRP_MN_QUERY_RESOURCES:
            DPRINT("IRP_MN_QUERY_RESOURCES\n");
            goto ForwardIrp;

        case IRP_MN_QUERY_RESOURCE_REQUIREMENTS:
            DPRINT("IRP_MN_QUERY_RESOURCE_REQUIREMENTS\n");
            goto ForwardIrp;

        case IRP_MN_QUERY_DEVICE_TEXT:
            DPRINT("IRP_MN_QUERY_DEVICE_TEXT\n");
            goto ForwardIrp;

        case IRP_MN_FILTER_RESOURCE_REQUIREMENTS:
            DPRINT("IRP_MN_FILTER_RESOURCE_REQUIREMENTS\n");
            goto ForwardIrp;

        case IRP_MN_READ_CONFIG:
            DPRINT("IRP_MN_READ_CONFIG\n");
            goto ForwardIrp;

        case IRP_MN_WRITE_CONFIG:
            DPRINT("IRP_MN_WRITE_CONFIG\n");
            goto ForwardIrp;

        case IRP_MN_EJECT:
            DPRINT("IRP_MN_EJECT\n");
            goto ForwardIrp;

        case IRP_MN_SET_LOCK:
            DPRINT("IRP_MN_SET_LOCK\n");
            goto ForwardIrp;

        case IRP_MN_QUERY_ID:
            DPRINT("IRP_MN_QUERY_ID\n");
            goto ForwardIrp;

        case IRP_MN_QUERY_PNP_DEVICE_STATE:
            DPRINT("IRP_MN_QUERY_PNP_DEVICE_STATE\n");
            goto ForwardIrp;

        case IRP_MN_QUERY_BUS_INFORMATION:
            DPRINT("IRP_MN_QUERY_BUS_INFORMATION\n");
            goto ForwardIrp;

        case IRP_MN_DEVICE_USAGE_NOTIFICATION:
            DPRINT("IRP_MN_DEVICE_USAGE_NOTIFICATION\n");
            goto ForwardIrp;

        case IRP_MN_SURPRISE_REMOVAL:
            DPRINT1("IRP_MN_SURPRISE_REMOVAL\n");
            if (!(FdoCommonExtension->PnpStateFlags & USBPORT_PNP_STATE_FAILED))
            {
                USBPORT_InvalidateControllerHandler(FdoDevice,
                                                    USBPORT_INVALIDATE_CONTROLLER_SURPRISE_REMOVE);
            }

            if (FdoCommonExtension->PnpStateFlags & USBPORT_PNP_STATE_STARTED)
            {
                USBPORT_StopWorkerThread(FdoDevice);
                USBPORT_StopDevice(FdoDevice);
                if (FdoExtension->Flags & USBPORT_FLAG_REGISTERED_FDO)
                {
                    USBPORT_RemoveUSBxFdo(FdoDevice);
                }

                FdoCommonExtension->PnpStateFlags &= ~USBPORT_PNP_STATE_STARTED;
                FdoCommonExtension->PnpStateFlags |= USBPORT_PNP_STATE_FAILED;
            }
            goto ForwardIrp;

        default:
            DPRINT("unknown IRP_MN_???\n");
ForwardIrp:
            /* forward irp to next device object */
            IoSkipCurrentIrpStackLocation(Irp);
            break;
    }

    return IoCallDriver(FdoCommonExtension->LowerDevice, Irp);
}

PVOID
NTAPI
USBPORT_GetDeviceHwIds(IN PDEVICE_OBJECT FdoDevice,
                       IN USHORT VendorID,
                       IN USHORT DeviceID,
                       IN USHORT RevisionID)
{
    PUSBPORT_DEVICE_EXTENSION FdoExtension;
    PUSBPORT_REGISTRATION_PACKET Packet;
    PVOID Id;
    WCHAR Buffer[300] = {0};
    SIZE_T Length = 0;
    size_t Remaining = sizeof(Buffer);
    PWCHAR EndBuffer;

    FdoExtension = FdoDevice->DeviceExtension;
    Packet = &FdoExtension->MiniPortInterface->Packet;

    DPRINT("USBPORT_GetDeviceHwIds: FdoDevice - %p, Packet->MiniPortFlags - %p\n",
           FdoDevice,
           Packet->MiniPortFlags);

    if (Packet->MiniPortFlags & USB_MINIPORT_FLAGS_USB2)
    {
        RtlStringCbPrintfExW(Buffer,
                             Remaining,
                             &EndBuffer,
                             &Remaining,
                             0,
                             L"USB\\ROOT_HUB20&VID%04x&PID%04x&REV%04x",
                             VendorID,
                             DeviceID,
                             RevisionID);

        EndBuffer++;
        Remaining -= sizeof(UNICODE_NULL);

        RtlStringCbPrintfExW(EndBuffer,
                             Remaining,
                             &EndBuffer,
                             &Remaining,
                             0,
                             L"USB\\ROOT_HUB20&VID%04x&PID%04x",
                             VendorID,
                             DeviceID);

        EndBuffer++;
        Remaining -= sizeof(UNICODE_NULL);

        RtlStringCbPrintfExW(EndBuffer,
                             Remaining,
                             NULL,
                             &Remaining,
                             0,
                             L"USB\\ROOT_HUB20");
    }
    else
    {
        RtlStringCbPrintfExW(Buffer,
                             Remaining,
                             &EndBuffer,
                             &Remaining,
                             0,
                             L"USB\\ROOT_HUB&VID%04x&PID%04x&REV%04x",
                             VendorID,
                             DeviceID,
                             RevisionID);

        EndBuffer++;
        Remaining -= sizeof(UNICODE_NULL);

        RtlStringCbPrintfExW(EndBuffer,
                             Remaining,
                             &EndBuffer,
                             &Remaining,
                             0,
                             L"USB\\ROOT_HUB&VID%04x&PID%04x",
                             VendorID,
                             DeviceID);

        EndBuffer++;
        Remaining -= sizeof(UNICODE_NULL);

        RtlStringCbPrintfExW(EndBuffer,
                             Remaining,
                             NULL,
                             &Remaining,
                             0,
                             L"USB\\ROOT_HUB");
    }

    Length = (sizeof(Buffer) - Remaining + 2 * sizeof(UNICODE_NULL));

     /* for debug only */
    if (FALSE)
    {
        DPRINT("Hardware IDs:\n");
        USBPORT_DumpingIDs(Buffer);
    }

    Id = ExAllocatePoolWithTag(PagedPool, Length, USB_PORT_TAG);

    if (!Id)
        return NULL;

    RtlMoveMemory(Id, Buffer, Length);

    return Id;
}

NTSTATUS
NTAPI
USBPORT_PdoPnP(IN PDEVICE_OBJECT PdoDevice,
               IN PIRP Irp)
{
    PUSBPORT_RHDEVICE_EXTENSION PdoExtension;
    PUSBPORT_COMMON_DEVICE_EXTENSION PdoCommonExtension;
    PDEVICE_OBJECT FdoDevice;
    PUSBPORT_DEVICE_EXTENSION FdoExtension;
    PIO_STACK_LOCATION IoStack;
    UCHAR Minor;
    NTSTATUS Status;
    PPNP_BUS_INFORMATION BusInformation;
    PDEVICE_CAPABILITIES DeviceCapabilities;

    PdoExtension = PdoDevice->DeviceExtension;
    PdoCommonExtension = &PdoExtension->CommonExtension;

    FdoDevice = PdoExtension->FdoDevice;
    FdoExtension = FdoDevice->DeviceExtension;

    IoStack = IoGetCurrentIrpStackLocation(Irp);
    Minor = IoStack->MinorFunction;

    Status = Irp->IoStatus.Status;

    DPRINT("USBPORT_PdoPnP: PdoDevice - %p, Minor - %x\n", PdoDevice, Minor);

    switch (Minor)
    {
        case IRP_MN_START_DEVICE:
            DPRINT("IRP_MN_START_DEVICE\n");

            Status = USBPORT_RootHubCreateDevice(FdoDevice, PdoDevice);

            if (NT_SUCCESS(Status))
            {
                Status = USBPORT_RegisterDeviceInterface(PdoDevice,
                                                         PdoDevice,
                                                         &GUID_DEVINTERFACE_USB_HUB,
                                                         TRUE);

                if (NT_SUCCESS(Status))
                {
                    PdoCommonExtension->DevicePowerState = PowerDeviceD0;
                    PdoCommonExtension->PnpStateFlags &= ~USBPORT_PNP_STATE_STOPPED;
                    PdoCommonExtension->PnpStateFlags &= ~USBPORT_PNP_STATE_FAILED;
                    PdoCommonExtension->PnpStateFlags &= ~USBPORT_PNP_STATE_NOT_INIT;
                    PdoCommonExtension->PnpStateFlags |= USBPORT_PNP_STATE_STARTED;
                }
            }

            break;

        case IRP_MN_QUERY_REMOVE_DEVICE:
            DPRINT("USBPORT_PdoPnP: IRP_MN_QUERY_REMOVE_DEVICE\n");
            Status = STATUS_SUCCESS;
            break;

        case IRP_MN_REMOVE_DEVICE:
            DPRINT("USBPORT_PdoPnP: IRP_MN_REMOVE_DEVICE\n");

            USBPORT_StopRootHub(PdoDevice);

            PdoCommonExtension->PnpStateFlags &= ~USBPORT_PNP_STATE_STARTED;
            PdoCommonExtension->PnpStateFlags |= USBPORT_PNP_STATE_NOT_INIT |
                                                 USBPORT_PNP_STATE_STOPPED |
                                                 USBPORT_PNP_STATE_FAILED;

            Irp->IoStatus.Status = STATUS_SUCCESS;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            IoDeleteDevice(PdoDevice);
            return STATUS_SUCCESS;

        case IRP_MN_CANCEL_REMOVE_DEVICE:
            DPRINT("IRP_MN_CANCEL_REMOVE_DEVICE\n");
            Status = STATUS_SUCCESS;
            break;

        case IRP_MN_STOP_DEVICE:
            DPRINT("USBPORT_PdoPnP: IRP_MN_STOP_DEVICE\n");

            if (PdoCommonExtension->PnpStateFlags & USBPORT_PNP_STATE_STARTED)
            {
                USBPORT_StopRootHub(PdoDevice);
                PdoCommonExtension->PnpStateFlags &= ~USBPORT_PNP_STATE_STARTED;
                PdoCommonExtension->PnpStateFlags |= USBPORT_PNP_STATE_NOT_INIT |
                                                     USBPORT_PNP_STATE_STOPPED;
            }

            Status = STATUS_SUCCESS;
            break;

        case IRP_MN_QUERY_STOP_DEVICE:
            DPRINT("IRP_MN_QUERY_STOP_DEVICE\n");
            Status = STATUS_SUCCESS;
            break;

        case IRP_MN_CANCEL_STOP_DEVICE:
            DPRINT("IRP_MN_CANCEL_STOP_DEVICE\n");
            Status = STATUS_SUCCESS;
            break;

        case IRP_MN_QUERY_DEVICE_RELATIONS:
        {
            PDEVICE_RELATIONS DeviceRelations;

            DPRINT("IRP_MN_QUERY_DEVICE_RELATIONS\n");
            if (IoStack->Parameters.QueryDeviceRelations.Type != TargetDeviceRelation)
            {
                break;
            }

            DeviceRelations = ExAllocatePoolWithTag(PagedPool,
                                                    sizeof(DEVICE_RELATIONS),
                                                    USB_PORT_TAG);

            if (!DeviceRelations)
            {
                Status = STATUS_INSUFFICIENT_RESOURCES;
                Irp->IoStatus.Information = 0;
                break;
            }

            DeviceRelations->Count = 1;
            DeviceRelations->Objects[0] = PdoDevice;

            ObReferenceObject(PdoDevice);

            Status = STATUS_SUCCESS;
            Irp->IoStatus.Information = (ULONG_PTR)DeviceRelations;
            break;
        }

        case IRP_MN_QUERY_INTERFACE:
            DPRINT("IRP_MN_QUERY_INTERFACE\n");
            Status = USBPORT_PdoQueryInterface(FdoDevice, PdoDevice, Irp);
            break;

        case IRP_MN_QUERY_CAPABILITIES:
            DPRINT("IRP_MN_QUERY_CAPABILITIES\n");

            DeviceCapabilities = IoStack->Parameters.DeviceCapabilities.Capabilities;

            RtlCopyMemory(DeviceCapabilities,
                          &PdoExtension->Capabilities,
                          sizeof(DEVICE_CAPABILITIES));

            Status = STATUS_SUCCESS;
            break;

        case IRP_MN_QUERY_RESOURCES:
            DPRINT("USBPORT_PdoPnP: IRP_MN_QUERY_RESOURCES\n");
            break;

        case IRP_MN_QUERY_RESOURCE_REQUIREMENTS:
            DPRINT("IRP_MN_QUERY_RESOURCE_REQUIREMENTS\n");
            break;

        case IRP_MN_QUERY_DEVICE_TEXT:
            DPRINT("IRP_MN_QUERY_DEVICE_TEXT\n");
            break;

        case IRP_MN_FILTER_RESOURCE_REQUIREMENTS:
            DPRINT("IRP_MN_FILTER_RESOURCE_REQUIREMENTS\n");
            break;

        case IRP_MN_READ_CONFIG:
            DPRINT("IRP_MN_READ_CONFIG\n");
            DPRINT1("USBPORT_PdoPnP: IRP_MN_READ_CONFIG not supported\n");
            Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return STATUS_NOT_SUPPORTED;

        case IRP_MN_WRITE_CONFIG:
            DPRINT("IRP_MN_WRITE_CONFIG\n");
            DPRINT1("USBPORT_PdoPnP: IRP_MN_WRITE_CONFIG not supported\n");
            Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return STATUS_NOT_SUPPORTED;

        case IRP_MN_EJECT:
            DPRINT("IRP_MN_EJECT\n");
            DPRINT1("USBPORT_PdoPnP: IRP_MN_EJECT not supported\n");
            Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return STATUS_NOT_SUPPORTED;

        case IRP_MN_SET_LOCK:
            DPRINT("IRP_MN_SET_LOCK\n");
            DPRINT1("USBPORT_PdoPnP: IRP_MN_SET_LOCK not supported\n");
            Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;
            IoCompleteRequest(Irp, IO_NO_INCREMENT);
            return STATUS_NOT_SUPPORTED;

        case IRP_MN_QUERY_ID:
        {
            ULONG IdType;
            LONG Length;
            WCHAR Buffer[64] = {0};
            PVOID Id;

            Status = STATUS_SUCCESS;
            IdType = IoStack->Parameters.QueryId.IdType;

            DPRINT("IRP_MN_QUERY_ID/Type %x\n", IdType);

            if (IdType == BusQueryDeviceID)
            {
                PUSBPORT_REGISTRATION_PACKET Packet;
                Packet = &FdoExtension->MiniPortInterface->Packet;

                if (Packet->MiniPortFlags & USB_MINIPORT_FLAGS_USB2)
                {
                    RtlStringCbPrintfW(Buffer,
                                       sizeof(Buffer),
                                       L"USB\\ROOT_HUB20");
                }
                else
                {
                    RtlStringCbPrintfW(Buffer,
                                       sizeof(Buffer),
                                       L"USB\\ROOT_HUB");
                }

                Length = (LONG)(wcslen(Buffer) + 1);

                Id = ExAllocatePoolWithTag(PagedPool,
                                           Length * sizeof(WCHAR),
                                           USB_PORT_TAG);

                if (Id)
                {
                    RtlZeroMemory(Id, Length * sizeof(WCHAR));
                    RtlStringCbCopyW(Id, Length * sizeof(WCHAR), Buffer);

                    DPRINT("BusQueryDeviceID - %S, TotalLength - %hu\n",
                           Id,
                           Length);
                }

                Irp->IoStatus.Information = (ULONG_PTR)Id;
                break;
            }

            if (IdType == BusQueryHardwareIDs)
            {
                Id = USBPORT_GetDeviceHwIds(FdoDevice,
                                            FdoExtension->VendorID,
                                            FdoExtension->DeviceID,
                                            FdoExtension->RevisionID);

                Irp->IoStatus.Information = (ULONG_PTR)Id;
                break;
            }

            if (IdType == BusQueryCompatibleIDs ||
                IdType == BusQueryInstanceID)
            {
                Irp->IoStatus.Information = 0;
                break;
            }

            /* IdType == BusQueryDeviceSerialNumber */
            Status = Irp->IoStatus.Status;
            break;
        }

        case IRP_MN_QUERY_PNP_DEVICE_STATE:
            DPRINT("IRP_MN_QUERY_PNP_DEVICE_STATE\n");
            Status = STATUS_SUCCESS;
            break;

        case IRP_MN_QUERY_BUS_INFORMATION:
            DPRINT("IRP_MN_QUERY_BUS_INFORMATION\n");

            /* Allocate buffer for bus information */
            BusInformation = ExAllocatePoolWithTag(PagedPool,
                                                   sizeof(PNP_BUS_INFORMATION),
                                                   USB_PORT_TAG);

            if (!BusInformation)
            {
                /* No memory */
                Status = STATUS_INSUFFICIENT_RESOURCES;
                break;
            }

            RtlZeroMemory(BusInformation, sizeof(PNP_BUS_INFORMATION));

            /* Copy BUS GUID */
            RtlMoveMemory(&BusInformation->BusTypeGuid,
                          &GUID_BUS_TYPE_USB,
                          sizeof(GUID));

            /* Set bus type */
            BusInformation->LegacyBusType = PNPBus;
            BusInformation->BusNumber = 0;

            Status = STATUS_SUCCESS;
            Irp->IoStatus.Information = (ULONG_PTR)BusInformation;
            break;

        case IRP_MN_DEVICE_USAGE_NOTIFICATION:
            DPRINT("IRP_MN_DEVICE_USAGE_NOTIFICATION\n");
            break;

        case IRP_MN_SURPRISE_REMOVAL:
            DPRINT("USBPORT_PdoPnP: IRP_MN_SURPRISE_REMOVAL\n");

            USBPORT_StopRootHub(PdoDevice);

            PdoCommonExtension->PnpStateFlags &= ~USBPORT_PNP_STATE_STARTED;
            PdoCommonExtension->PnpStateFlags |= USBPORT_PNP_STATE_FAILED |
                                                 USBPORT_PNP_STATE_NOT_INIT;

            Status = STATUS_SUCCESS;
            break;

        default:
            DPRINT("unknown IRP_MN_???\n");
            break;
    }

    Irp->IoStatus.Status = Status;
    IoCompleteRequest(Irp, IO_NO_INCREMENT);
    return Status;
}
