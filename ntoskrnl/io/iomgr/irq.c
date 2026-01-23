/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            ntoskrnl/io/iomgr/irq.c
 * PURPOSE:         I/O Wrappers (called Completion Ports) for Kernel Queues
 * PROGRAMMERS:     Alex Ionescu (alex.ionescu@reactos.org)
 */

/* INCLUDES *****************************************************************/

#include <ntoskrnl.h>

#define NDEBUG
#include <debug.h>

#if defined(_M_ARM64) || defined(__aarch64__)
/*
 * Forward declaration for HalGetMsiMessageAddressEx.
 * On ARM64 with GIC ITS, MSI addresses are programmed via the ITS TRANSLATER
 * register, not the x86-style APIC MSI address format.
 */
BOOLEAN
NTAPI
HalGetMsiMessageAddressEx(
    _In_ USHORT RequesterId,
    _In_ ULONGLONG Vector,
    _In_ ULONGLONG Affinity,
    _Out_ PULONG AddressLow,
    _Out_opt_ PULONG AddressHigh,
    _Out_ PUSHORT Data
    );
#endif

typedef struct _IO_MESSAGE_CONNECT_CONTEXT
{
    KSPIN_LOCK MessageLock;
    IO_INTERRUPT_MESSAGE_INFO MessageInfoHeader;
} IO_MESSAGE_CONNECT_CONTEXT, *PIO_MESSAGE_CONNECT_CONTEXT;

_IRQL_requires_same_
static
BOOLEAN
NTAPI
IopInterruptMessageServiceRoutine(
    _In_ PKINTERRUPT Interrupt,
    _In_ PVOID ServiceContext)
{
    PIO_INTERRUPT_MESSAGE_ENTRY Entry;

    Entry = (PIO_INTERRUPT_MESSAGE_ENTRY)ServiceContext;
    return Entry->MessageServiceRoutine(Interrupt,
                                        Entry->ServiceContext,
                                        Entry->MessageId);
}

static
PIO_INTERRUPT_MESSAGE_ENTRY
IopGetMessageEntriesFromInfo(
    _In_ PIO_INTERRUPT_MESSAGE_INFO MessageInfo)
{
    SIZE_T HeaderSize;
    PIO_MESSAGE_CONNECT_CONTEXT Context;

    Context = CONTAINING_RECORD(MessageInfo,
                                IO_MESSAGE_CONNECT_CONTEXT,
                                MessageInfoHeader);

    HeaderSize = sizeof(IO_MESSAGE_CONNECT_CONTEXT) +
                 (MessageInfo->MessageCount - 1) * sizeof(IO_INTERRUPT_MESSAGE_INFO_ENTRY);

    return (PIO_INTERRUPT_MESSAGE_ENTRY)((PUCHAR)Context + HeaderSize);
}

static
KAFFINITY
IopSelectMessageTarget(
    _In_ KAFFINITY Affinity,
    _In_ ULONG MessageId,
    _Out_opt_ PULONG ApicId)
{
    KAFFINITY Mask;
    ULONG Index = 0;
    ULONG BitCount = 0;
    ULONG TargetBit = 0;

    Mask = Affinity ? Affinity : KeActiveProcessors;

    /* Count available bits */
    for (KAFFINITY tmp = Mask; tmp; tmp >>= 1)
    {
        if (tmp & 1)
            BitCount++;
    }

    if (BitCount == 0)
    {
        if (ApicId)
            *ApicId = 0;
        return 1;
    }

    TargetBit = MessageId % BitCount;

    for (KAFFINITY tmp = Mask; tmp; tmp >>= 1, Index++)
    {
        if (!(tmp & 1))
            continue;

        if (TargetBit == 0)
            break;

        TargetBit--;
    }

    if (ApicId)
        *ApicId = Index;

    return (KAFFINITY)1 << Index;
}

/* FUNCTIONS *****************************************************************/

/*
 * @implemented
 */
NTSTATUS
NTAPI
IoConnectInterrupt(OUT PKINTERRUPT *InterruptObject,
                   IN PKSERVICE_ROUTINE ServiceRoutine,
                   IN PVOID ServiceContext,
                   IN PKSPIN_LOCK SpinLock,
                   IN ULONG Vector,
                   IN KIRQL Irql,
                   IN KIRQL SynchronizeIrql,
                   IN KINTERRUPT_MODE InterruptMode,
                   IN BOOLEAN ShareVector,
                   IN KAFFINITY ProcessorEnableMask,
                   IN BOOLEAN FloatingSave)
{
    PKINTERRUPT Interrupt;
    PKINTERRUPT InterruptUsed;
    PIO_INTERRUPT IoInterrupt;
    BOOLEAN FirstRun;
    CCHAR Count = 0;
    KAFFINITY Affinity;

    PAGED_CODE();

    /* Assume failure */
    *InterruptObject = NULL;

    /* Get the affinity */
    Affinity = ProcessorEnableMask & KeActiveProcessors;
    while (Affinity)
    {
        /* Increase count */
        if (Affinity & 1) Count++;
        Affinity >>= 1;
    }

    /* Make sure we have a valid CPU count */
    if (!Count) return STATUS_INVALID_PARAMETER;

    /* Allocate the array of I/O interrupts */
    IoInterrupt = ExAllocatePoolZero(NonPagedPool,
                                     (Count - 1) * sizeof(KINTERRUPT) +
                                     sizeof(IO_INTERRUPT),
                                     TAG_IO_INTERRUPT);
    if (!IoInterrupt) return STATUS_INSUFFICIENT_RESOURCES;

    /* Use the structure's spinlock, if none was provided */
    if (!SpinLock)
    {
        SpinLock = &IoInterrupt->SpinLock;
        KeInitializeSpinLock(SpinLock);
    }

    /* We first start with a built-in interrupt inside the I/O structure */
    Interrupt = (PKINTERRUPT)(IoInterrupt + 1);
    FirstRun = TRUE;

    /* Now create all the interrupts */
    Affinity = ProcessorEnableMask & KeActiveProcessors;
    for (Count = 0; Affinity; Count++, Affinity >>= 1)
    {
        /* Check if it's enabled for this CPU */
        if (!(Affinity & 1))
            continue;

        /* Check which one we will use */
        InterruptUsed = FirstRun ? &IoInterrupt->FirstInterrupt : Interrupt;

        /* Initialize it */
        KeInitializeInterrupt(InterruptUsed,
                              ServiceRoutine,
                              ServiceContext,
                              SpinLock,
                              Vector,
                              Irql,
                              SynchronizeIrql,
                              InterruptMode,
                              ShareVector,
                              Count,
                              FloatingSave);

        /* Connect it */
        if (!KeConnectInterrupt(InterruptUsed))
        {
            /* Check how far we got */
            if (FirstRun)
            {
                /* We failed early so just free this */
                ExFreePoolWithTag(IoInterrupt, TAG_IO_INTERRUPT);
            }
            else
            {
                /* Far enough, so disconnect everything */
                IoDisconnectInterrupt(&IoInterrupt->FirstInterrupt);
            }

            /* And fail */
            return STATUS_INVALID_PARAMETER;
        }

        /* Now we've used up our First Run */
        if (FirstRun)
        {
            FirstRun = FALSE;
        }
        else
        {
            /* Move on to the next one */
            IoInterrupt->Interrupt[(UCHAR)Count] = Interrupt++;
        }
    }

    /* Return success */
    *InterruptObject = &IoInterrupt->FirstInterrupt;
    return STATUS_SUCCESS;
}

/*
 * @implemented
 */
VOID
NTAPI
IoDisconnectInterrupt(PKINTERRUPT InterruptObject)
{
    PIO_INTERRUPT IoInterrupt;
    ULONG i;

    PAGED_CODE();

    /* Get the I/O interrupt */
    IoInterrupt = CONTAINING_RECORD(InterruptObject,
                                    IO_INTERRUPT,
                                    FirstInterrupt);

    /* Disconnect the first one */
    KeDisconnectInterrupt(&IoInterrupt->FirstInterrupt);

    /* Now disconnect the others */
    for (i = 0; i < KeNumberProcessors; i++)
    {
        /* Make sure one was registered */
        if (!IoInterrupt->Interrupt[i])
            continue;

        /* Disconnect it */
        KeDisconnectInterrupt(IoInterrupt->Interrupt[i]);
    }

    /* Free the I/O interrupt */
    ExFreePoolWithTag(IoInterrupt, TAG_IO_INTERRUPT);
}

/*
 * MSI-X table entry structure (16 bytes per entry)
 */
typedef struct _MSIX_TABLE_ENTRY {
    ULONG MessageAddressLow;
    ULONG MessageAddressHigh;
    ULONG MessageData;
    ULONG VectorControl;
} MSIX_TABLE_ENTRY, *PMSIX_TABLE_ENTRY;

#define PCI_CAPABILITY_ID_MSIX 0x11

/*
 * IopProgramMsixTable - Program MSI-X table entries for a device
 *
 * This function programs the MSI-X table in the device's BAR with the
 * message address and data for each interrupt vector.
 *
 * Parameters:
 *   PhysicalDeviceObject - The PDO for the PCI device
 *   MessageInfo - Information about the message-based interrupts
 *   ResourceList - Allocated resources containing memory BAR info
 *
 * Returns:
 *   STATUS_SUCCESS on success, error status otherwise
 */
static __attribute__((unused))
NTSTATUS
IopProgramMsixTable(
    _In_ PDEVICE_OBJECT PhysicalDeviceObject,
    _In_ PIO_INTERRUPT_MESSAGE_INFO MessageInfo,
    _In_ PCM_RESOURCE_LIST ResourceList)
{
    NTSTATUS Status;
    BUS_INTERFACE_STANDARD BusInterface;
    UCHAR CapOffset;
    UCHAR CapId;
    UCHAR NextCap;
    USHORT MsixControl;
    ULONG TableOffsetBir;
    ULONG TableBar;
    ULONG TableOffset;
    PHYSICAL_ADDRESS MsixBarPhysical;
    SIZE_T MsixBarLength = 0;
    PVOID MsixBarVirtual = NULL;
    PMSIX_TABLE_ENTRY MsixTable;
    ULONG i;
    PCM_FULL_RESOURCE_DESCRIPTOR FullDesc;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR PartialDesc;
    BOOLEAN FoundBar = FALSE;

    /* Get BUS_INTERFACE_STANDARD from the PDO */
    Status = IoGetDeviceProperty(PhysicalDeviceObject,
                                 DevicePropertyBusTypeGuid,
                                 0,
                                 NULL,
                                 NULL);

    /* Query for BUS_INTERFACE_STANDARD */
    {
        IO_STATUS_BLOCK IoStatus;
        KEVENT Event;
        PIRP Irp;
        PIO_STACK_LOCATION IrpStack;

        KeInitializeEvent(&Event, NotificationEvent, FALSE);

        Irp = IoBuildSynchronousFsdRequest(IRP_MJ_PNP,
                                           PhysicalDeviceObject,
                                           NULL,
                                           0,
                                           NULL,
                                           &Event,
                                           &IoStatus);
        if (!Irp)
            return STATUS_INSUFFICIENT_RESOURCES;

        IrpStack = IoGetNextIrpStackLocation(Irp);
        IrpStack->MinorFunction = IRP_MN_QUERY_INTERFACE;
        IrpStack->Parameters.QueryInterface.InterfaceType = &GUID_BUS_INTERFACE_STANDARD;
        IrpStack->Parameters.QueryInterface.Size = sizeof(BUS_INTERFACE_STANDARD);
        IrpStack->Parameters.QueryInterface.Version = 1;
        IrpStack->Parameters.QueryInterface.Interface = (PINTERFACE)&BusInterface;
        IrpStack->Parameters.QueryInterface.InterfaceSpecificData = NULL;

        Irp->IoStatus.Status = STATUS_NOT_SUPPORTED;

        Status = IoCallDriver(PhysicalDeviceObject, Irp);
        if (Status == STATUS_PENDING)
        {
            KeWaitForSingleObject(&Event, Executive, KernelMode, FALSE, NULL);
            Status = IoStatus.Status;
        }

        if (!NT_SUCCESS(Status))
        {
            DPRINT1("IopProgramMsixTable: Failed to get BUS_INTERFACE_STANDARD: 0x%x\n", Status);
            return Status;
        }
    }

    /* Find MSI-X capability */
    CapOffset = 0;
    BusInterface.GetBusData(BusInterface.Context, PCI_WHICHSPACE_CONFIG,
                            &CapOffset, 0x34, sizeof(CapOffset));

    while (CapOffset != 0 && CapOffset != 0xFF)
    {
        BusInterface.GetBusData(BusInterface.Context, PCI_WHICHSPACE_CONFIG,
                                &CapId, CapOffset, sizeof(CapId));

        if (CapId == PCI_CAPABILITY_ID_MSIX)
            break;

        BusInterface.GetBusData(BusInterface.Context, PCI_WHICHSPACE_CONFIG,
                                &NextCap, CapOffset + 1, sizeof(NextCap));
        CapOffset = NextCap;
    }

    if (CapOffset == 0 || CapOffset == 0xFF || CapId != PCI_CAPABILITY_ID_MSIX)
    {
        DPRINT1("IopProgramMsixTable: MSI-X capability not found\n");
        BusInterface.InterfaceDereference(BusInterface.Context);
        return STATUS_NOT_SUPPORTED;
    }

    /* Read MSI-X control and table offset/BIR */
    BusInterface.GetBusData(BusInterface.Context, PCI_WHICHSPACE_CONFIG,
                            &MsixControl, CapOffset + 2, sizeof(MsixControl));
    BusInterface.GetBusData(BusInterface.Context, PCI_WHICHSPACE_CONFIG,
                            &TableOffsetBir, CapOffset + 4, sizeof(TableOffsetBir));

    TableBar = TableOffsetBir & 0x7;
    TableOffset = TableOffsetBir & ~0x7;

    DPRINT("IopProgramMsixTable: MSI-X Cap at 0x%x, Control=0x%04x, TableBAR=%u, TableOffset=0x%x\n",
           CapOffset, MsixControl, TableBar, TableOffset);

    /* Find the BAR in the resources */
    FullDesc = &ResourceList->List[0];
    for (i = 0; i < ResourceList->Count && !FoundBar; i++, FullDesc++)
    {
        PCM_PARTIAL_RESOURCE_LIST PartialList = &FullDesc->PartialResourceList;
        ULONG j;
        ULONG MemoryBarIndex = 0;

        for (j = 0; j < PartialList->Count; j++)
        {
            PartialDesc = &PartialList->PartialDescriptors[j];
            if (PartialDesc->Type == CmResourceTypeMemory)
            {
                if (MemoryBarIndex == TableBar)
                {
                    MsixBarPhysical = PartialDesc->u.Memory.Start;
                    MsixBarLength = PartialDesc->u.Memory.Length;
                    FoundBar = TRUE;
                    break;
                }
                MemoryBarIndex++;
            }
        }
    }

    if (!FoundBar)
    {
        DPRINT1("IopProgramMsixTable: Could not find BAR%u in resources\n", TableBar);
        BusInterface.InterfaceDereference(BusInterface.Context);
        return STATUS_RESOURCE_DATA_NOT_FOUND;
    }

    /* Map the MSI-X table BAR */
    MsixBarVirtual = MmMapIoSpace(MsixBarPhysical, MsixBarLength, MmNonCached);
    if (!MsixBarVirtual)
    {
        DPRINT1("IopProgramMsixTable: Failed to map MSI-X BAR\n");
        BusInterface.InterfaceDereference(BusInterface.Context);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* Program each table entry */
    MsixTable = (PMSIX_TABLE_ENTRY)((PUCHAR)MsixBarVirtual + TableOffset);

    for (i = 0; i < MessageInfo->MessageCount; i++)
    {
        PIO_INTERRUPT_MESSAGE_INFO_ENTRY InfoEntry = &MessageInfo->MessageInfo[i];

        DPRINT("IopProgramMsixTable: Entry[%u]: Addr=0x%I64x Data=0x%x Vector=%u\n",
               i, InfoEntry->MessageAddress.QuadPart, InfoEntry->MessageData, InfoEntry->Vector);

        MsixTable[i].MessageAddressLow = (ULONG)InfoEntry->MessageAddress.LowPart;
        MsixTable[i].MessageAddressHigh = (ULONG)InfoEntry->MessageAddress.HighPart;
        MsixTable[i].MessageData = InfoEntry->MessageData;
        MsixTable[i].VectorControl = 0;  /* Unmask the vector */
    }

    /* Enable MSI-X if not already enabled */
    if (!(MsixControl & 0x8000))
    {
        MsixControl |= 0x8000;  /* Set MSI-X Enable bit */
        MsixControl &= ~0x4000; /* Clear Function Mask */
        BusInterface.SetBusData(BusInterface.Context, PCI_WHICHSPACE_CONFIG,
                                &MsixControl, CapOffset + 2, sizeof(MsixControl));
        DPRINT("IopProgramMsixTable: Enabled MSI-X (Control=0x%04x)\n", MsixControl);
    }

    /* Unmap and cleanup */
    MmUnmapIoSpace(MsixBarVirtual, MsixBarLength);
    BusInterface.InterfaceDereference(BusInterface.Context);

    DPRINT("IopProgramMsixTable: Successfully programmed %u MSI-X entries\n", MessageInfo->MessageCount);
    return STATUS_SUCCESS;
}

NTSTATUS
IopConnectInterruptExFullySpecific(
    _Inout_ PIO_CONNECT_INTERRUPT_PARAMETERS Parameters)
{
    NTSTATUS Status;

    PAGED_CODE();

    /* Fallback to standard IoConnectInterrupt */
    Status = IoConnectInterrupt(Parameters->FullySpecified.InterruptObject,
                                Parameters->FullySpecified.ServiceRoutine,
                                Parameters->FullySpecified.ServiceContext,
                                Parameters->FullySpecified.SpinLock,
                                Parameters->FullySpecified.Vector,
                                Parameters->FullySpecified.Irql,
                                Parameters->FullySpecified.SynchronizeIrql,
                                Parameters->FullySpecified.InterruptMode,
                                Parameters->FullySpecified.ShareVector,
                                Parameters->FullySpecified.ProcessorEnableMask,
                                Parameters->FullySpecified.FloatingSave);
    if (!NT_SUCCESS(Status))
        DPRINT1("IopConnectInterruptExFullySpecific() failed: 0x%lx\n", Status);
    return Status;
}

NTSTATUS
NTAPI
IoConnectInterruptEx(
    _Inout_ PIO_CONNECT_INTERRUPT_PARAMETERS Parameters)
{
    PIO_MESSAGE_CONNECT_CONTEXT Context;
    PIO_INTERRUPT_MESSAGE_INFO MessageInfo;
    PIO_INTERRUPT_MESSAGE_ENTRY MessageEntries;
    PCM_RESOURCE_LIST ResourceList = NULL;
    PCM_FULL_RESOURCE_DESCRIPTOR FullDescriptor;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR Descriptor = NULL;
    ULONG Length = 0;
    ULONG MessageCount;
    ULONG i;
    KIRQL ConnectIrql;
    KIRQL ConnectSyncIrql;
    KINTERRUPT_MODE Mode;
    KAFFINITY Affinity;
    KSPIN_LOCK *SpinLock;
    NTSTATUS Status;

    PAGED_CODE();

    switch (Parameters->Version)
    {
        case CONNECT_FULLY_SPECIFIED:
            return IopConnectInterruptExFullySpecific(Parameters);
        case CONNECT_FULLY_SPECIFIED_GROUP:
            //TODO: We don't do anything for the group type
            return IopConnectInterruptExFullySpecific(Parameters);
        case CONNECT_MESSAGE_BASED:
            if (!Parameters->MessageBased.ConnectionContext.InterruptMessageTable ||
                !Parameters->MessageBased.MessageServiceRoutine ||
                !Parameters->MessageBased.PhysicalDeviceObject)
            {
                return STATUS_INVALID_PARAMETER;
            }

            Status = IoGetDeviceProperty(Parameters->MessageBased.PhysicalDeviceObject,
                                         DevicePropertyAllocatedResources,
                                         0,
                                         NULL,
                                         &Length);
            if (Status != STATUS_BUFFER_TOO_SMALL || Length == 0)
                return Status;

            ResourceList = ExAllocatePoolWithTag(PagedPool, Length, TAG_IO_INTERRUPT);
            if (!ResourceList)
                return STATUS_INSUFFICIENT_RESOURCES;

            Status = IoGetDeviceProperty(Parameters->MessageBased.PhysicalDeviceObject,
                                         DevicePropertyAllocatedResources,
                                         Length,
                                         ResourceList,
                                         &Length);
            if (!NT_SUCCESS(Status))
            {
                ExFreePoolWithTag(ResourceList, TAG_IO_INTERRUPT);
                return Status;
            }

            FullDescriptor = &ResourceList->List[0];
            for (i = 0; i < ResourceList->Count && !Descriptor; i++, FullDescriptor++)
            {
                PCM_PARTIAL_RESOURCE_LIST PartialList = &FullDescriptor->PartialResourceList;
                ULONG j;

                for (j = 0; j < PartialList->Count; j++)
                {
                    if (PartialList->PartialDescriptors[j].Type == CmResourceTypeInterrupt &&
                        (PartialList->PartialDescriptors[j].Flags & CM_RESOURCE_INTERRUPT_MESSAGE))
                    {
                        Descriptor = &PartialList->PartialDescriptors[j];
                        break;
                    }
                }
            }

            if (!Descriptor)
            {
                ExFreePoolWithTag(ResourceList, TAG_IO_INTERRUPT);
                return STATUS_NOT_FOUND;
            }

            MessageCount = Descriptor->u.MessageInterrupt.Raw.MessageCount;
            if (MessageCount == 0)
                MessageCount = 1;

            {
                SIZE_T MessageInfoSize;
                SIZE_T TotalSize;

                MessageInfoSize = sizeof(IO_MESSAGE_CONNECT_CONTEXT) +
                                  (MessageCount - 1) * sizeof(IO_INTERRUPT_MESSAGE_INFO_ENTRY);
                TotalSize = MessageInfoSize +
                            (SIZE_T)MessageCount * sizeof(IO_INTERRUPT_MESSAGE_ENTRY);

                Context = ExAllocatePoolZero(NonPagedPool,
                                             TotalSize,
                                             TAG_IO_INTERRUPT);
                if (!Context)
                {
                    ExFreePoolWithTag(ResourceList, TAG_IO_INTERRUPT);
                    return STATUS_INSUFFICIENT_RESOURCES;
                }

                MessageInfo = &Context->MessageInfoHeader;
                MessageEntries = (PIO_INTERRUPT_MESSAGE_ENTRY)((PUCHAR)Context + MessageInfoSize);
            }

            KeInitializeSpinLock(&Context->MessageLock);

            MessageInfo->UnifiedIrql = (KIRQL)Descriptor->u.MessageInterrupt.Translated.Level;
            MessageInfo->MessageCount = MessageCount;

            /*
             * MSI/MSI-X interrupts are inherently edge-triggered (latched).
             * The LATCHED flag may not be set in the resource descriptor,
             * but we must always use Latched mode to prevent interrupt storms.
             */
            Mode = Latched;
            Affinity = Descriptor->u.MessageInterrupt.Translated.Affinity ?
                       Descriptor->u.MessageInterrupt.Translated.Affinity :
                       KeActiveProcessors;
            SpinLock = Parameters->MessageBased.SpinLock ?
                       Parameters->MessageBased.SpinLock :
                       &Context->MessageLock;

            ConnectIrql = (KIRQL)Descriptor->u.MessageInterrupt.Translated.Level;
            ConnectSyncIrql = Parameters->MessageBased.SynchronizeIrql ?
                              Parameters->MessageBased.SynchronizeIrql :
                              ConnectIrql;
            if (ConnectSyncIrql < ConnectIrql)
                ConnectSyncIrql = ConnectIrql;

            for (i = 0; i < MessageCount; i++)
            {
                ULONG Vector;
                PIO_INTERRUPT_MESSAGE_INFO_ENTRY InfoEntry;
                PIO_INTERRUPT_MESSAGE_ENTRY Entry;
                KAFFINITY TargetMask;
                ULONG ApicId;

                Vector = Descriptor->u.MessageInterrupt.Translated.Vector + i;
                InfoEntry = &MessageInfo->MessageInfo[i];
                Entry = &MessageEntries[i];

                Entry->MessageServiceRoutine = Parameters->MessageBased.MessageServiceRoutine;
                Entry->ServiceContext = Parameters->MessageBased.ServiceContext;
                Entry->MessageId = i;

                TargetMask = IopSelectMessageTarget(Affinity, i, &ApicId);

#if defined(_M_ARM64) || defined(__aarch64__)
                /*
                 * On ARM64 with GIC ITS, MSI addresses are assigned via the ITS.
                 * Query the HAL to get the proper MSI address (GITS_TRANSLATER)
                 * and data (EventID) for this interrupt vector.
                 */
                {
                    ULONG BusNumber = 0;
                    ULONG DeviceAddress = 0;
                    ULONG BufLen;
                    USHORT RequesterId;
                    ULONG AddrLow, AddrHigh = 0;
                    USHORT MsiData;

                    /* Get PCI bus number and device address to form RequesterId */
                    BufLen = sizeof(BusNumber);
                    IoGetDeviceProperty(Parameters->MessageBased.PhysicalDeviceObject,
                                        DevicePropertyBusNumber,
                                        BufLen,
                                        &BusNumber,
                                        &BufLen);

                    BufLen = sizeof(DeviceAddress);
                    IoGetDeviceProperty(Parameters->MessageBased.PhysicalDeviceObject,
                                        DevicePropertyAddress,
                                        BufLen,
                                        &DeviceAddress,
                                        &BufLen);

                    /* RequesterId format: (Bus << 8) | (Device << 3) | Function
                     * DeviceAddress format: (Device << 16) | Function */
                    RequesterId = (USHORT)((BusNumber << 8) |
                                           ((DeviceAddress >> 16) << 3) |
                                           (DeviceAddress & 0x7));

                    DPRINT1("IoConnectInterruptEx[ARM64]: Vector=%u ReqId=0x%04x (Bus=%u Dev=%u Fn=%u)\n",
                            Vector, RequesterId, BusNumber,
                            (DeviceAddress >> 16) & 0x1F, DeviceAddress & 0x7);

                    if (HalGetMsiMessageAddressEx(RequesterId,
                                                  (ULONGLONG)Vector,
                                                  Affinity,
                                                  &AddrLow,
                                                  &AddrHigh,
                                                  &MsiData))
                    {
                        InfoEntry->MessageAddress.LowPart = AddrLow;
                        InfoEntry->MessageAddress.HighPart = AddrHigh;
                        InfoEntry->MessageData = MsiData;
                        DPRINT1("IoConnectInterruptEx[ARM64]: MsiAddr=0x%08x%08x Data=0x%x\n",
                                AddrHigh, AddrLow, MsiData);
                    }
                    else
                    {
                        /* Fall back to generic x86-style MSI address */
                        DPRINT1("IoConnectInterruptEx[ARM64]: HalGetMsiMessageAddressEx failed, using fallback\n");
                        InfoEntry->MessageAddress.QuadPart = 0xFEE00000 | ((ULONGLONG)ApicId << 12);
                        InfoEntry->MessageData = Vector & 0xFF;
                    }
                }
#else
                /* x86/x64: Use APIC MSI address format */
                InfoEntry->MessageAddress.QuadPart = 0xFEE00000 | ((ULONGLONG)ApicId << 12);
                InfoEntry->MessageData = Vector & 0xFF;
#endif
                InfoEntry->TargetProcessorSet = TargetMask;
                InfoEntry->Vector = Vector;
                InfoEntry->Irql = ConnectIrql;
                InfoEntry->Mode = Mode;
                InfoEntry->Polarity = InterruptActiveHigh;

                KeInitializeInterrupt(&Entry->Interrupt,
                                      IopInterruptMessageServiceRoutine,
                                      Entry,
                                      SpinLock,
                                      Vector,
                                      ConnectIrql,
                                      ConnectSyncIrql,
                                      Mode,
                                      FALSE,
                                      0,
                                      Parameters->MessageBased.FloatingSave);

                if (!KeConnectInterrupt(&Entry->Interrupt))
                {
                    ULONG k;

                    for (k = 0; k < i; k++)
                        KeDisconnectInterrupt(&MessageEntries[k].Interrupt);

                    ExFreePoolWithTag(Context, TAG_IO_INTERRUPT);
                    ExFreePoolWithTag(ResourceList, TAG_IO_INTERRUPT);
                    return STATUS_INVALID_PARAMETER;
                }

                InfoEntry->InterruptObject = &Entry->Interrupt;
            }

            /*
             * Note: MSI-X table programming is handled by individual drivers or
             * driver frameworks (like NDIS) that call device-specific setup routines.
             * The kernel provides the vector information, but actual hardware
             * table programming is done at the driver level.
             */

            ExFreePoolWithTag(ResourceList, TAG_IO_INTERRUPT);

            *Parameters->MessageBased.ConnectionContext.InterruptMessageTable = MessageInfo;
            return STATUS_SUCCESS;

        case CONNECT_LINE_BASED:
        {
            PCM_PARTIAL_RESOURCE_DESCRIPTOR IntDesc = NULL;
            KINTERRUPT_MODE IntMode;
            BOOLEAN ShareVector;
            KAFFINITY IntAffinity;
            KIRQL IntIrql;
            ULONG IntVector;

            if (!Parameters->LineBased.PhysicalDeviceObject ||
                !Parameters->LineBased.InterruptObject ||
                !Parameters->LineBased.ServiceRoutine)
            {
                return STATUS_INVALID_PARAMETER;
            }

            Status = IoGetDeviceProperty(Parameters->LineBased.PhysicalDeviceObject,
                                         DevicePropertyAllocatedResources,
                                         0,
                                         NULL,
                                         &Length);
            if (Status != STATUS_BUFFER_TOO_SMALL || Length == 0)
                return Status;

            ResourceList = ExAllocatePoolWithTag(PagedPool, Length, TAG_IO_INTERRUPT);
            if (!ResourceList)
                return STATUS_INSUFFICIENT_RESOURCES;

            Status = IoGetDeviceProperty(Parameters->LineBased.PhysicalDeviceObject,
                                         DevicePropertyAllocatedResources,
                                         Length,
                                         ResourceList,
                                         &Length);
            if (!NT_SUCCESS(Status))
            {
                ExFreePoolWithTag(ResourceList, TAG_IO_INTERRUPT);
                return Status;
            }

            FullDescriptor = &ResourceList->List[0];
            for (i = 0; i < ResourceList->Count && !IntDesc; i++, FullDescriptor++)
            {
                PCM_PARTIAL_RESOURCE_LIST PartialList = &FullDescriptor->PartialResourceList;
                ULONG j;

                for (j = 0; j < PartialList->Count; j++)
                {
                    if (PartialList->PartialDescriptors[j].Type == CmResourceTypeInterrupt &&
                        !(PartialList->PartialDescriptors[j].Flags & CM_RESOURCE_INTERRUPT_MESSAGE))
                    {
                        IntDesc = &PartialList->PartialDescriptors[j];
                        break;
                    }
                }
            }

            if (!IntDesc)
            {
                ExFreePoolWithTag(ResourceList, TAG_IO_INTERRUPT);
                return STATUS_NOT_FOUND;
            }

            IntMode = (IntDesc->Flags & CM_RESOURCE_INTERRUPT_LATCHED) ?
                      Latched : LevelSensitive;
            ShareVector = (IntDesc->ShareDisposition == CmResourceShareShared);
            IntAffinity = IntDesc->u.Interrupt.Affinity ?
                          IntDesc->u.Interrupt.Affinity :
                          KeActiveProcessors;
            IntIrql = (KIRQL)IntDesc->u.Interrupt.Level;
            IntVector = IntDesc->u.Interrupt.Vector;

            Status = IoConnectInterrupt(Parameters->LineBased.InterruptObject,
                                        Parameters->LineBased.ServiceRoutine,
                                        Parameters->LineBased.ServiceContext,
                                        Parameters->LineBased.SpinLock,
                                        IntVector,
                                        IntIrql,
                                        Parameters->LineBased.SynchronizeIrql ?
                                            Parameters->LineBased.SynchronizeIrql :
                                            IntIrql,
                                        IntMode,
                                        ShareVector,
                                        IntAffinity,
                                        Parameters->LineBased.FloatingSave);

            ExFreePoolWithTag(ResourceList, TAG_IO_INTERRUPT);
            return Status;
        }
    }

    return STATUS_SUCCESS;
}

VOID
NTAPI
IoDisconnectInterruptEx(
    _In_ PIO_DISCONNECT_INTERRUPT_PARAMETERS Parameters)
{
    PAGED_CODE();

    switch (Parameters->Version)
    {
        case CONNECT_MESSAGE_BASED:
        {
            PIO_INTERRUPT_MESSAGE_INFO MessageInfo;
            PIO_INTERRUPT_MESSAGE_ENTRY MessageEntries;
            ULONG i;

            MessageInfo = Parameters->ConnectionContext.InterruptMessageTable;
            if (!MessageInfo)
                return;

            MessageEntries = IopGetMessageEntriesFromInfo(MessageInfo);
            for (i = 0; i < MessageInfo->MessageCount; i++)
            {
                if (MessageEntries[i].Interrupt.ServiceRoutine)
                    KeDisconnectInterrupt(&MessageEntries[i].Interrupt);
            }

            ExFreePoolWithTag(CONTAINING_RECORD(MessageInfo,
                                                IO_MESSAGE_CONNECT_CONTEXT,
                                                MessageInfoHeader),
                              TAG_IO_INTERRUPT);
            break;
        }

        case CONNECT_LINE_BASED:
        case CONNECT_FULLY_SPECIFIED:
        case CONNECT_FULLY_SPECIFIED_GROUP:
        default:
            if (Parameters->ConnectionContext.InterruptObject)
                IoDisconnectInterrupt(Parameters->ConnectionContext.InterruptObject);
            break;
    }
}

/* EOF */
