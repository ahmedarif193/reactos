/*
 * COPYRIGHT:       2026 Ahmed ARIF (arif.ing@outlook.com)
 * PROJECT:     ReactOS NDIS library
 * FILE:        ndis/miniport6_int.c
 * PURPOSE:     NDIS 6.x Interrupt Handling APIs
 * PROGRAMMERS: ReactOS Development Team
 * NOTES:       This file implements the NDIS 6.x interrupt handling model
 *              which supports both legacy line-based interrupts and MSI/MSI-X
 *              message-signaled interrupts.
 */

#include "ndissys.h"

#if NDIS_SUPPORT_NDIS6

/*
 * Pool tags for interrupt allocations
 */
#define NDIS6_INT_TAG   'tNIn'   /* nINt - NDIS 6.x Interrupt */
#define NDIS6_DPC_TAG   'DNIn'   /* nIND - NDIS 6.x Interrupt DPC */

/*
 * Maximum number of MSI-X messages supported
 */
#define NDIS6_MAX_MSI_MESSAGES  256

/*
 * Internal structure to track NDIS 6.x interrupt registrations
 */
typedef struct _NDIS6_INTERRUPT_BLOCK {
    NDIS_OBJECT_HEADER Header;
    NDIS_HANDLE MiniportHandle;
    NDIS_HANDLE MiniportInterruptContext;
    PKINTERRUPT InterruptObject;
    KDPC InterruptDpc;
    NDIS_MINIPORT_INTERRUPT_CHARACTERISTICS Characteristics;
    NDIS_INTERRUPT_TYPE InterruptType;
    ULONG MessageCount;
    BOOLEAN Connected;
    KSPIN_LOCK Lock;
    /* DPC context for current DPC processing */
    PVOID DpcContext;
    /* Event for synchronization during deregistration */
    KEVENT DisconnectEvent;
    volatile LONG DpcPending;
    /* For MSI-X: Array of interrupt objects (one per message) */
    PKINTERRUPT *MessageInterruptObjects;
    /* For MSI-X: Array of DPCs (one per message) */
    PKDPC MessageDpcs;
    /* For MSI-X: Message info from PCI bus */
    PIO_INTERRUPT_MESSAGE_INFO MessageInfo;
} NDIS6_INTERRUPT_BLOCK, *PNDIS6_INTERRUPT_BLOCK;

/*
 * Forward declarations for internal ISR and DPC routines
 */
static BOOLEAN NTAPI
Ndis6iInterruptIsr(
    _In_ PKINTERRUPT Interrupt,
    _In_ PVOID ServiceContext);

static VOID NTAPI
Ndis6iInterruptDpc(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2);

static BOOLEAN NTAPI
Ndis6iMessageIsr(
    _In_ PKINTERRUPT Interrupt,
    _In_ PVOID ServiceContext,
    _In_ ULONG MessageId);

static VOID NTAPI
Ndis6iMessageDpc(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2);

/*
 * Ndis6iInterruptIsr
 * Internal ISR for line-based interrupts
 *
 * Parameters:
 *   Interrupt - Pointer to the interrupt object
 *   ServiceContext - Pointer to our NDIS6_INTERRUPT_BLOCK
 *
 * Returns:
 *   TRUE if the interrupt was claimed by the miniport
 */
static BOOLEAN NTAPI
Ndis6iInterruptIsr(
    _In_ PKINTERRUPT Interrupt,
    _In_ PVOID ServiceContext)
{
    PNDIS6_INTERRUPT_BLOCK IntBlock = (PNDIS6_INTERRUPT_BLOCK)ServiceContext;
    BOOLEAN QueueDefaultInterruptDpc = FALSE;
    ULONG TargetProcessors = 0;
    BOOLEAN Claimed = FALSE;

    UNREFERENCED_PARAMETER(Interrupt);

    DPRINT("Ndis6iInterruptIsr called\n");

    /* Call the miniport's ISR handler */
    if (IntBlock->Characteristics.InterruptHandler != NULL)
    {
        Claimed = IntBlock->Characteristics.InterruptHandler(
            IntBlock->MiniportInterruptContext,
            &QueueDefaultInterruptDpc,
            &TargetProcessors);
    }

    if (Claimed && QueueDefaultInterruptDpc)
    {
        /* Queue the DPC */
        InterlockedIncrement(&IntBlock->DpcPending);
        KeInsertQueueDpc(&IntBlock->InterruptDpc, NULL, NULL);
    }

    return Claimed;
}

/*
 * Ndis6iInterruptDpc
 * Internal DPC for line-based interrupts
 *
 * Parameters:
 *   Dpc - Pointer to the DPC object
 *   DeferredContext - Pointer to our NDIS6_INTERRUPT_BLOCK
 *   SystemArgument1 - DPC context (or NULL)
 *   SystemArgument2 - Reserved
 */
static VOID NTAPI
Ndis6iInterruptDpc(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    PNDIS6_INTERRUPT_BLOCK IntBlock = (PNDIS6_INTERRUPT_BLOCK)DeferredContext;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument2);

    DPRINT("Ndis6iInterruptDpc called\n");

    if (IntBlock == NULL)
    {
        return;
    }

    /* Call the miniport's DPC handler */
    if (IntBlock->Characteristics.InterruptDpcHandler != NULL)
    {
        IntBlock->Characteristics.InterruptDpcHandler(
            IntBlock->MiniportInterruptContext,
            SystemArgument1,  /* MiniportDpcContext */
            NULL,             /* ReceiveThrottleParameters */
            NULL);            /* NdisReserved2 */
    }

    /* Decrement pending DPC count */
    if (InterlockedDecrement(&IntBlock->DpcPending) == 0)
    {
        /* Signal if waiting for disconnect */
        KeSetEvent(&IntBlock->DisconnectEvent, IO_NO_INCREMENT, FALSE);
    }
}

/*
 * Ndis6iMessageIsr
 * Internal ISR for MSI/MSI-X interrupts
 * Note: Currently unused, reserved for full MSI-X support implementation
 *
 * Parameters:
 *   Interrupt - Pointer to the interrupt object
 *   ServiceContext - Pointer to our NDIS6_INTERRUPT_BLOCK
 *   MessageId - The MSI message ID
 *
 * Returns:
 *   TRUE if the interrupt was claimed by the miniport
 */
#ifdef __GNUC__
__attribute__((unused))
#endif
static BOOLEAN NTAPI
Ndis6iMessageIsr(
    _In_ PKINTERRUPT Interrupt,
    _In_ PVOID ServiceContext,
    _In_ ULONG MessageId)
{
    PNDIS6_INTERRUPT_BLOCK IntBlock = (PNDIS6_INTERRUPT_BLOCK)ServiceContext;
    BOOLEAN QueueDefaultInterruptDpc = FALSE;
    ULONG TargetProcessors = 0;
    BOOLEAN Claimed = FALSE;

    UNREFERENCED_PARAMETER(Interrupt);

    DPRINT("Ndis6iMessageIsr called, MessageId=%lu\n", MessageId);

    /* Call the miniport's MSI ISR handler */
    if (IntBlock->Characteristics.MessageInterruptHandler != NULL)
    {
        Claimed = IntBlock->Characteristics.MessageInterruptHandler(
            IntBlock->MiniportInterruptContext,
            MessageId,
            &QueueDefaultInterruptDpc,
            &TargetProcessors);
    }

    if (Claimed && QueueDefaultInterruptDpc)
    {
        /* Queue the DPC for this message */
        if (IntBlock->MessageDpcs != NULL && MessageId < IntBlock->MessageCount)
        {
            InterlockedIncrement(&IntBlock->DpcPending);
            KeInsertQueueDpc(&IntBlock->MessageDpcs[MessageId],
                             UlongToPtr(MessageId),
                             NULL);
        }
    }

    return Claimed;
}

/*
 * Ndis6iMessageDpc
 * Internal DPC for MSI/MSI-X interrupts
 *
 * Parameters:
 *   Dpc - Pointer to the DPC object
 *   DeferredContext - Pointer to our NDIS6_INTERRUPT_BLOCK
 *   SystemArgument1 - Message ID
 *   SystemArgument2 - Reserved
 */
static VOID NTAPI
Ndis6iMessageDpc(
    _In_ PKDPC Dpc,
    _In_opt_ PVOID DeferredContext,
    _In_opt_ PVOID SystemArgument1,
    _In_opt_ PVOID SystemArgument2)
{
    PNDIS6_INTERRUPT_BLOCK IntBlock = (PNDIS6_INTERRUPT_BLOCK)DeferredContext;
    ULONG MessageId;

    UNREFERENCED_PARAMETER(Dpc);
    UNREFERENCED_PARAMETER(SystemArgument2);

    DPRINT("Ndis6iMessageDpc called\n");

    if (IntBlock == NULL)
    {
        return;
    }

    /* Extract MessageId from SystemArgument1 */
    MessageId = PtrToUlong(SystemArgument1);

    /* Call the miniport's MSI DPC handler */
    if (IntBlock->Characteristics.MessageInterruptDpcHandler != NULL)
    {
        IntBlock->Characteristics.MessageInterruptDpcHandler(
            IntBlock->MiniportInterruptContext,
            MessageId,
            IntBlock->DpcContext,  /* MiniportDpcContext */
            NULL,                  /* ReceiveThrottleParameters */
            NULL);                 /* NdisReserved2 */
    }

    /* Decrement pending DPC count */
    if (InterlockedDecrement(&IntBlock->DpcPending) == 0)
    {
        /* Signal if waiting for disconnect */
        KeSetEvent(&IntBlock->DisconnectEvent, IO_NO_INCREMENT, FALSE);
    }
}

/*
 * Ndis6iConnectLineBased
 * Internal function to connect a line-based interrupt
 *
 * Parameters:
 *   IntBlock - Pointer to the interrupt block
 *   AdapterBlock - Pointer to the adapter's miniport block
 *
 * Returns:
 *   NDIS_STATUS_SUCCESS on success
 */
static NDIS_STATUS
Ndis6iConnectLineBased(
    _Inout_ PNDIS6_INTERRUPT_BLOCK IntBlock,
    _In_ PNDIS_MINIPORT_BLOCK AdapterBlock)
{
    NTSTATUS Status;
    ULONG MappedIrq;
    KIRQL Dirql;
    KAFFINITY Affinity;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR InterruptDesc = NULL;
    PCM_RESOURCE_LIST ResourceList;
    ULONG i;

    DPRINT("Ndis6iConnectLineBased called\n");

    /* Find the interrupt resource from the allocated resources */
    ResourceList = AdapterBlock->AllocatedResourcesTranslated;
    if (ResourceList == NULL)
    {
        DPRINT1("No translated resources available\n");
        return NDIS_STATUS_RESOURCE_CONFLICT;
    }

    /* Search for an interrupt resource */
    for (i = 0; i < ResourceList->List[0].PartialResourceList.Count; i++)
    {
        PCM_PARTIAL_RESOURCE_DESCRIPTOR Desc =
            &ResourceList->List[0].PartialResourceList.PartialDescriptors[i];

        if (Desc->Type == CmResourceTypeInterrupt)
        {
            InterruptDesc = Desc;
            break;
        }
    }

    if (InterruptDesc == NULL)
    {
        DPRINT("No interrupt resource found\n");
        return NDIS_STATUS_RESOURCE_CONFLICT;
    }

    /* Get the mapped IRQ */
    MappedIrq = InterruptDesc->u.Interrupt.Vector;
    Dirql = (KIRQL)InterruptDesc->u.Interrupt.Level;
    Affinity = InterruptDesc->u.Interrupt.Affinity;

    DPRINT("Connecting to interrupt Vector=0x%x, Level=%d, Affinity=0x%lx\n",
        MappedIrq, Dirql, (ULONG)Affinity);

    /* Initialize the DPC */
    KeInitializeDpc(&IntBlock->InterruptDpc, Ndis6iInterruptDpc, IntBlock);

    /* Connect the interrupt */
    Status = IoConnectInterrupt(
        &IntBlock->InterruptObject,
        (PKSERVICE_ROUTINE)Ndis6iInterruptIsr,
        IntBlock,
        &IntBlock->Lock,
        MappedIrq,
        Dirql,
        Dirql,
        (InterruptDesc->Flags & CM_RESOURCE_INTERRUPT_LATCHED) ?
            Latched : LevelSensitive,
        (InterruptDesc->ShareDisposition == CmResourceShareShared),
        Affinity,
        FALSE);

    if (!NT_SUCCESS(Status))
    {
        DPRINT("IoConnectInterrupt failed: 0x%x\n", Status);

        if (Status == STATUS_INSUFFICIENT_RESOURCES)
        {
            return NDIS_STATUS_RESOURCE_CONFLICT;
        }
        return NDIS_STATUS_FAILURE;
    }

    IntBlock->InterruptType = NDIS_CONNECT_LINE_BASED;
    IntBlock->Connected = TRUE;

    DPRINT("Line-based interrupt connected successfully\n");

    return NDIS_STATUS_SUCCESS;
}

/*
 * Ndis6iConnectMessageBased
 * Internal function to connect MSI/MSI-X interrupts
 *
 * Parameters:
 *   IntBlock - Pointer to the interrupt block
 *   AdapterBlock - Pointer to the adapter's miniport block
 *
 * Returns:
 *   NDIS_STATUS_SUCCESS on success
 *   NDIS_STATUS_NOT_SUPPORTED if MSI is not available
 */
static NDIS_STATUS
Ndis6iConnectMessageBased(
    _Inout_ PNDIS6_INTERRUPT_BLOCK IntBlock,
    _In_ PNDIS_MINIPORT_BLOCK AdapterBlock)
{
    NTSTATUS Status;
    PCM_RESOURCE_LIST ResourceList;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR MessageDesc = NULL;
    ULONG i;
    ULONG MessageCount = 0;

    DPRINT("Ndis6iConnectMessageBased called\n");

    /* Check if MSI handlers are provided */
    if (IntBlock->Characteristics.MessageInterruptHandler == NULL ||
        IntBlock->Characteristics.MessageInterruptDpcHandler == NULL)
    {
        DPRINT("MSI handlers not provided\n");
        return NDIS_STATUS_NOT_SUPPORTED;
    }

    /* Find the message interrupt resource from the allocated resources */
    ResourceList = AdapterBlock->AllocatedResourcesTranslated;
    if (ResourceList == NULL)
    {
        DPRINT1("No translated resources available\n");
        return NDIS_STATUS_NOT_SUPPORTED;
    }

    /* Search for a message interrupt resource */
    for (i = 0; i < ResourceList->List[0].PartialResourceList.Count; i++)
    {
        PCM_PARTIAL_RESOURCE_DESCRIPTOR Desc =
            &ResourceList->List[0].PartialResourceList.PartialDescriptors[i];

        if (Desc->Type == CmResourceTypeInterrupt &&
            (Desc->Flags & CM_RESOURCE_INTERRUPT_MESSAGE))
        {
            MessageDesc = Desc;
            /* For MSI-X, count the number of messages */
            MessageCount++;
        }
    }

    if (MessageDesc == NULL || MessageCount == 0)
    {
        DPRINT("No MSI resources found, falling back to line-based\n");
        return NDIS_STATUS_NOT_SUPPORTED;
    }

    /* Limit message count */
    if (MessageCount > NDIS6_MAX_MSI_MESSAGES)
    {
        MessageCount = NDIS6_MAX_MSI_MESSAGES;
    }

    IntBlock->MessageCount = MessageCount;

    /* Allocate array of interrupt objects */
    IntBlock->MessageInterruptObjects = ExAllocatePoolWithTag(
        NonPagedPool,
        sizeof(PKINTERRUPT) * MessageCount,
        NDIS6_INT_TAG);

    if (IntBlock->MessageInterruptObjects == NULL)
    {
        DPRINT("Failed to allocate interrupt object array\n");
        return NDIS_STATUS_RESOURCES;
    }

    RtlZeroMemory(IntBlock->MessageInterruptObjects, sizeof(PKINTERRUPT) * MessageCount);

    /* Allocate array of DPCs */
    IntBlock->MessageDpcs = ExAllocatePoolWithTag(
        NonPagedPool,
        sizeof(KDPC) * MessageCount,
        NDIS6_DPC_TAG);

    if (IntBlock->MessageDpcs == NULL)
    {
        ExFreePoolWithTag(IntBlock->MessageInterruptObjects, NDIS6_INT_TAG);
        IntBlock->MessageInterruptObjects = NULL;
        DPRINT1("Failed to allocate DPC array\n");
        return NDIS_STATUS_RESOURCES;
    }

    /* Initialize DPCs */
    for (i = 0; i < MessageCount; i++)
    {
        KeInitializeDpc(&IntBlock->MessageDpcs[i], Ndis6iMessageDpc, IntBlock);
    }

    /*
     * Note: Full MSI-X support requires IoConnectInterruptEx with
     * CONNECT_MESSAGE_BASED parameters. For now, we'll use the
     * legacy method for single MSI or fall back to line-based.
     *
     * TODO: Implement full IoConnectInterruptEx support when available.
     */

    /* For now, connect using legacy method for the first message */
    {
        ULONG MappedIrq = 0;
        KIRQL Dirql = 0;
        KAFFINITY Affinity = 0;
        BOOLEAN FoundResource = FALSE;

        /* Find the first message interrupt descriptor again */
        for (i = 0; i < ResourceList->List[0].PartialResourceList.Count; i++)
        {
            PCM_PARTIAL_RESOURCE_DESCRIPTOR Desc =
                &ResourceList->List[0].PartialResourceList.PartialDescriptors[i];

            if (Desc->Type == CmResourceTypeInterrupt &&
                (Desc->Flags & CM_RESOURCE_INTERRUPT_MESSAGE))
            {
                MappedIrq = Desc->u.MessageInterrupt.Translated.Vector;
                Dirql = (KIRQL)Desc->u.MessageInterrupt.Translated.Level;
                Affinity = Desc->u.MessageInterrupt.Translated.Affinity;
                FoundResource = TRUE;
                break;
            }
        }

        if (!FoundResource)
        {
            DPRINT("No message interrupt resource found\n");
            ExFreePoolWithTag(IntBlock->MessageDpcs, NDIS6_DPC_TAG);
            ExFreePoolWithTag(IntBlock->MessageInterruptObjects, NDIS6_INT_TAG);
            IntBlock->MessageDpcs = NULL;
            IntBlock->MessageInterruptObjects = NULL;
            return NDIS_STATUS_FAILURE;
        }

        Status = IoConnectInterrupt(
            &IntBlock->MessageInterruptObjects[0],
            (PKSERVICE_ROUTINE)Ndis6iInterruptIsr,  /* Use line-based ISR as fallback */
            IntBlock,
            &IntBlock->Lock,
            MappedIrq,
            Dirql,
            Dirql,
            LevelSensitive,
            TRUE,  /* Shared for MSI */
            Affinity,
            FALSE);

        if (!NT_SUCCESS(Status))
        {
            DPRINT("IoConnectInterrupt for MSI failed: 0x%x\n", Status);
            ExFreePoolWithTag(IntBlock->MessageDpcs, NDIS6_DPC_TAG);
            ExFreePoolWithTag(IntBlock->MessageInterruptObjects, NDIS6_INT_TAG);
            IntBlock->MessageDpcs = NULL;
            IntBlock->MessageInterruptObjects = NULL;
            return NDIS_STATUS_FAILURE;
        }
    }

    IntBlock->InterruptType = NDIS_CONNECT_MESSAGE_BASED;
    IntBlock->Connected = TRUE;

    /* Update the characteristics with the interrupt type */
    IntBlock->Characteristics.InterruptType = NDIS_CONNECT_MESSAGE_BASED;

    DPRINT("Message-based interrupt connected successfully, MessageCount=%lu\n",
        MessageCount);

    return NDIS_STATUS_SUCCESS;
}

/*
 * Ndis6iDisconnectInterrupt
 * Internal function to disconnect an interrupt
 *
 * Parameters:
 *   IntBlock - Pointer to the interrupt block
 */
static VOID
Ndis6iDisconnectInterrupt(
    _Inout_ PNDIS6_INTERRUPT_BLOCK IntBlock)
{
    ULONG i;

    DPRINT("Ndis6iDisconnectInterrupt called\n");

    if (!IntBlock->Connected)
    {
        return;
    }

    /* Wait for any pending DPCs to complete */
    if (IntBlock->DpcPending > 0)
    {
        LARGE_INTEGER Timeout;
        Timeout.QuadPart = -10000000LL; /* 1 second timeout */

        KeWaitForSingleObject(&IntBlock->DisconnectEvent,
                              Executive,
                              KernelMode,
                              FALSE,
                              &Timeout);
    }

    /* Disconnect based on interrupt type */
    if (IntBlock->InterruptType == NDIS_CONNECT_MESSAGE_BASED)
    {
        /* Disconnect all message interrupts */
        if (IntBlock->MessageInterruptObjects != NULL)
        {
            for (i = 0; i < IntBlock->MessageCount; i++)
            {
                if (IntBlock->MessageInterruptObjects[i] != NULL)
                {
                    IoDisconnectInterrupt(IntBlock->MessageInterruptObjects[i]);
                    IntBlock->MessageInterruptObjects[i] = NULL;
                }
            }
            ExFreePoolWithTag(IntBlock->MessageInterruptObjects, NDIS6_INT_TAG);
            IntBlock->MessageInterruptObjects = NULL;
        }

        /* Free DPC array */
        if (IntBlock->MessageDpcs != NULL)
        {
            ExFreePoolWithTag(IntBlock->MessageDpcs, NDIS6_DPC_TAG);
            IntBlock->MessageDpcs = NULL;
        }
    }
    else
    {
        /* Disconnect line-based interrupt */
        if (IntBlock->InterruptObject != NULL)
        {
            IoDisconnectInterrupt(IntBlock->InterruptObject);
            IntBlock->InterruptObject = NULL;
        }
    }

    IntBlock->Connected = FALSE;

    DPRINT("Interrupt disconnected\n");
}

/*
 * Helper function to check if a handle is an NDIS 6.x device object
 * Returns the stored resource list if it's NDIS 6.x, NULL otherwise
 */
static PCM_RESOURCE_LIST
Ndis6iGetResourceList(
    IN NDIS_HANDLE MiniportAdapterHandle)
{
    PDEVICE_OBJECT DeviceObject;

    /*
     * For NDIS 6.x, MiniportAdapterHandle is a PDEVICE_OBJECT.
     * For NDIS 5.x, it's a PLOGICAL_ADAPTER.
     *
     * We can distinguish them by checking if the object looks like
     * a DEVICE_OBJECT (Type == IO_TYPE_DEVICE == 3).
     *
     * For NDIS 6.x device objects created by Ndis6AddDevice:
     *   DeviceExtension[5] contains the AllocatedResourcesTranslated copy
     */
    DeviceObject = (PDEVICE_OBJECT)MiniportAdapterHandle;

    /* Check if this looks like a device object */
    if (DeviceObject != NULL &&
        DeviceObject->Type == IO_TYPE_DEVICE &&
        DeviceObject->DeviceExtension != NULL)
    {
        /* For NDIS 6.x, check if we have stored resources */
        PCM_RESOURCE_LIST ResourceList = ((PVOID*)DeviceObject->DeviceExtension)[5];
        if (ResourceList != NULL)
        {
            return ResourceList;
        }
    }

    return NULL;
}

/*
 * MSI-X table entry structure (16 bytes per entry)
 */
typedef struct _NDIS_MSIX_TABLE_ENTRY {
    ULONG MessageAddressLow;
    ULONG MessageAddressHigh;
    ULONG MessageData;
    ULONG VectorControl;
} NDIS_MSIX_TABLE_ENTRY, *PNDIS_MSIX_TABLE_ENTRY;

#define NDIS_PCI_CAPABILITY_ID_MSIX 0x11

/*
 * Ndis6iProgramMsixTable - Program MSI-X table for the device
 *
 * This function programs the MSI-X table in the device's BAR with the
 * message address and data for interrupt delivery.
 */
static NTSTATUS
Ndis6iProgramMsixTable(
    _In_ PDEVICE_OBJECT PhysicalDeviceObject,
    _In_ ULONG Vector,
    _In_ KIRQL Level,
    _In_ KAFFINITY Affinity,
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
    PNDIS_MSIX_TABLE_ENTRY MsixTable;
    ULONG i;
    PCM_FULL_RESOURCE_DESCRIPTOR FullDesc;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR PartialDesc;
    BOOLEAN FoundBar = FALSE;
    ULONG ApicId;
    ULONGLONG MessageAddress;

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
        {
            /* Define the GUID locally since wdmguid.h may not be included */
            static const GUID BusInterfaceStandardGuid =
                { 0x496B8280, 0x6F25, 0x11D0, { 0xBE, 0xAF, 0x08, 0x00, 0x2B, 0xE2, 0x09, 0x2F } };
            IrpStack->Parameters.QueryInterface.InterfaceType = &BusInterfaceStandardGuid;
        }
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
            DPRINT1("NDIS6: Ndis6iProgramMsixTable: Failed to get BUS_INTERFACE_STANDARD: 0x%x\n", Status);
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

        if (CapId == NDIS_PCI_CAPABILITY_ID_MSIX)
            break;

        BusInterface.GetBusData(BusInterface.Context, PCI_WHICHSPACE_CONFIG,
                                &NextCap, CapOffset + 1, sizeof(NextCap));
        CapOffset = NextCap;
    }

    if (CapOffset == 0 || CapOffset == 0xFF || CapId != NDIS_PCI_CAPABILITY_ID_MSIX)
    {
        DPRINT("NDIS6: Ndis6iProgramMsixTable: MSI-X capability not found\n");
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

    DPRINT("NDIS6: MSI-X Cap at 0x%x, Control=0x%04x, TableBAR=%u, TableOffset=0x%x\n",
             CapOffset, MsixControl, TableBar, TableOffset);

    /*
     * Read the actual BAR address from PCI config space.
     * The BIR (BAR Indicator Register) tells us which BAR contains the MSI-X table.
     * PCI BARs start at config offset 0x10, each is 4 bytes (32-bit) or 8 bytes (64-bit).
     */
    {
        ULONG BarOffset = 0x10 + (TableBar * 4);
        ULONG BarValue = 0;
        PHYSICAL_ADDRESS TargetBarPhysical;

        BusInterface.GetBusData(BusInterface.Context, PCI_WHICHSPACE_CONFIG,
                                &BarValue, BarOffset, sizeof(BarValue));

        /* Check if it's a memory BAR (bit 0 = 0) */
        if (BarValue & 1)
        {
            DPRINT1("NDIS6: BAR%u is I/O space, not memory\n", TableBar);
            BusInterface.InterfaceDereference(BusInterface.Context);
            return STATUS_NOT_SUPPORTED;
        }

        /* Get the physical address (mask off type bits) */
        TargetBarPhysical.QuadPart = (BarValue & ~0xFULL);

        /* Check if it's a 64-bit BAR */
        if ((BarValue & 0x6) == 0x4)
        {
            ULONG BarHigh = 0;
            BusInterface.GetBusData(BusInterface.Context, PCI_WHICHSPACE_CONFIG,
                                    &BarHigh, BarOffset + 4, sizeof(BarHigh));
            TargetBarPhysical.HighPart = BarHigh;
        }

        DPRINT1("NDIS6: BAR%u physical address from PCI config: 0x%I64x\n",
                 TableBar, TargetBarPhysical.QuadPart);

        /* Now find this BAR in the allocated resources */
        FullDesc = &ResourceList->List[0];
        for (i = 0; i < ResourceList->Count && !FoundBar; i++, FullDesc++)
        {
            PCM_PARTIAL_RESOURCE_LIST PartialList = &FullDesc->PartialResourceList;
            ULONG j;

            for (j = 0; j < PartialList->Count; j++)
            {
                PartialDesc = &PartialList->PartialDescriptors[j];
                if (PartialDesc->Type == CmResourceTypeMemory)
                {
                    /* Check if this resource matches the BAR's physical address */
                    if (PartialDesc->u.Memory.Start.QuadPart == TargetBarPhysical.QuadPart)
                    {
                        MsixBarPhysical = PartialDesc->u.Memory.Start;
                        MsixBarLength = PartialDesc->u.Memory.Length;
                        FoundBar = TRUE;
                        DPRINT("NDIS6: Found MSI-X BAR resource at 0x%I64x, len=%u\n",
                                 MsixBarPhysical.QuadPart, (ULONG)MsixBarLength);
                        break;
                    }
                }
            }
        }
    }

    if (!FoundBar)
    {
        DPRINT1("NDIS6: Ndis6iProgramMsixTable: Could not find BAR%u in resources\n", TableBar);
        BusInterface.InterfaceDereference(BusInterface.Context);
        return STATUS_RESOURCE_DATA_NOT_FOUND;
    }

    /* Map the MSI-X table BAR */
    MsixBarVirtual = MmMapIoSpace(MsixBarPhysical, MsixBarLength, MmNonCached);
    if (!MsixBarVirtual)
    {
        DPRINT("NDIS6: Ndis6iProgramMsixTable: Failed to map MSI-X BAR\n");
        BusInterface.InterfaceDereference(BusInterface.Context);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /* Compute the target APIC ID from affinity */
    {
        KAFFINITY tmp = Affinity ? Affinity : KeQueryActiveProcessors();
        ApicId = 0;
        while (tmp && !(tmp & 1))
        {
            ApicId++;
            tmp >>= 1;
        }
    }

    /* Compute MSI message address: 0xFEE00000 | (ApicId << 12) */
    MessageAddress = 0xFEE00000ULL | ((ULONGLONG)ApicId << 12);

    DPRINT("NDIS6: Programming MSI-X Entry[0]: Addr=0x%I64x Data=0x%x (Vector=%u, ApicId=%u)\n",
             MessageAddress, Vector & 0xFF, Vector, ApicId);

    /*
     * Per PCI-SIG MSI-X spec, the recommended programming sequence is:
     * 1. Ensure MSI-X is enabled (so writes to BAR go to QEMU's MSI-X handler)
     * 2. Mask individual vectors via VectorControl
     * 3. Program address and data
     * 4. Unmask individual vectors
     *
     * Some emulators (including QEMU) may only properly handle MSI-X table
     * writes when MSI-X is enabled.
     */

    /* Get pointer to MSI-X table */
    MsixTable = (PNDIS_MSIX_TABLE_ENTRY)((PUCHAR)MsixBarVirtual + TableOffset);

    /* Step 1: Ensure MSI-X is enabled but with function mask set */
    MsixControl = (MsixControl | 0x8000 | 0x4000);  /* Enable + Function Mask */
    BusInterface.SetBusData(BusInterface.Context, PCI_WHICHSPACE_CONFIG,
                            &MsixControl, CapOffset + 2, sizeof(MsixControl));
    DPRINT("NDIS6: MSI-X enabled with function mask (Control=0x%04x)\n", MsixControl);

    /* Step 2: Mask the individual vector */
    MsixTable[0].VectorControl = 1;  /* Mask the vector */
    KeMemoryBarrier();

    /* Step 3: Program the address and data */
    MsixTable[0].MessageAddressLow = (ULONG)(MessageAddress & 0xFFFFFFFF);
    MsixTable[0].MessageAddressHigh = (ULONG)(MessageAddress >> 32);
    MsixTable[0].MessageData = Vector & 0xFF;
    KeMemoryBarrier();

    /* Step 4: Unmask the individual vector */
    MsixTable[0].VectorControl = 0;  /* Unmask the vector */
    KeMemoryBarrier();

    /* Verify the MSI-X table programming */
    DPRINT("NDIS6: MSI-X Table readback: AddrLow=0x%08x AddrHigh=0x%08x Data=0x%08x VectorControl=0x%08x\n",
             MsixTable[0].MessageAddressLow, MsixTable[0].MessageAddressHigh,
             MsixTable[0].MessageData, MsixTable[0].VectorControl);

    /* Step 5: Clear function mask to allow interrupts */
    MsixControl = (MsixControl | 0x8000) & ~0x4000;  /* Enable, no Function Mask */
    BusInterface.SetBusData(BusInterface.Context, PCI_WHICHSPACE_CONFIG,
                            &MsixControl, CapOffset + 2, sizeof(MsixControl));

    /* Verify the write by reading back */
    {
        USHORT ReadbackControl = 0;
        BusInterface.GetBusData(BusInterface.Context, PCI_WHICHSPACE_CONFIG,
                                &ReadbackControl, CapOffset + 2, sizeof(ReadbackControl));
        DPRINT("NDIS6: MSI-X fully enabled (wrote=0x%04x, readback=0x%04x)\n",
                 MsixControl, ReadbackControl);
    }

    /* No need to check if already enabled - we unconditionally re-enable */
    if (0)
    {
        MsixControl |= 0x8000;  /* Set MSI-X Enable bit */
        MsixControl &= ~0x4000; /* Clear Function Mask */
        BusInterface.SetBusData(BusInterface.Context, PCI_WHICHSPACE_CONFIG,
                                &MsixControl, CapOffset + 2, sizeof(MsixControl));
        DPRINT("NDIS6: Enabled MSI-X (Control=0x%04x)\n", MsixControl);
    }

    /*
     * Disable legacy INTx when using MSI-X.
     * PCI Command Register (offset 0x04) bit 10 (0x0400) is the Interrupt Disable bit.
     * Setting this bit disables legacy INTx interrupts, which is required for
     * MSI/MSI-X to work properly on some platforms (including QEMU).
     */
    {
        USHORT PciCommand;
        ULONG BytesWritten;

        BusInterface.GetBusData(BusInterface.Context, PCI_WHICHSPACE_CONFIG,
                                &PciCommand, 0x04, sizeof(PciCommand));
        DPRINT1("NDIS6: PCI Command before INTx disable: 0x%04x (check: %d)\n",
                 PciCommand, !(PciCommand & 0x0400));

        if (!(PciCommand & 0x0400))
        {
            PciCommand |= 0x0400;  /* Set Interrupt Disable bit */
            BytesWritten = BusInterface.SetBusData(BusInterface.Context, PCI_WHICHSPACE_CONFIG,
                                                   &PciCommand, 0x04, sizeof(PciCommand));
            DPRINT1("NDIS6: Disabled INTx (PCI Command=0x%04x, BytesWritten=%u)\n",
                     PciCommand, BytesWritten);

            /* Verify the write */
            BusInterface.GetBusData(BusInterface.Context, PCI_WHICHSPACE_CONFIG,
                                    &PciCommand, 0x04, sizeof(PciCommand));
            DPRINT1("NDIS6: PCI Command after INTx disable: 0x%04x\n", PciCommand);
        }
    }

    /* Unmap and cleanup */
    MmUnmapIoSpace(MsixBarVirtual, MsixBarLength);
    BusInterface.InterfaceDereference(BusInterface.Context);

    DPRINT("NDIS6: Successfully programmed MSI-X table\n");
    return STATUS_SUCCESS;
}

/*
 * Internal helper for NDIS 6.x line-based interrupt connection
 */
static NDIS_STATUS
Ndis6iConnectLineBasedNdis6(
    _Inout_ PNDIS6_INTERRUPT_BLOCK IntBlock,
    _In_ PCM_RESOURCE_LIST ResourceList,
    _In_ PDEVICE_OBJECT DeviceObject,
    _In_ BOOLEAN MsiSupportedByDriver)
{
    NTSTATUS Status;
    ULONG MappedIrq;
    KIRQL Dirql;
    KAFFINITY Affinity;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR InterruptDesc = NULL;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR LegacyInterruptDesc = NULL;
    ULONG i;
    BOOLEAN IsMessageInterrupt = FALSE;
    KINTERRUPT_MODE InterruptMode;
    BOOLEAN ShareVector;

    DPRINT("Ndis6iConnectLineBasedNdis6 called, ResourceList=%p, MsiSupported=%d\n",
                              ResourceList, MsiSupportedByDriver);

    /* Search for an interrupt resource */
    PCM_PARTIAL_RESOURCE_DESCRIPTOR MsiInterruptDesc = NULL;
    for (i = 0; i < ResourceList->List[0].PartialResourceList.Count; i++)
    {
        PCM_PARTIAL_RESOURCE_DESCRIPTOR Desc =
            &ResourceList->List[0].PartialResourceList.PartialDescriptors[i];

        if (Desc->Type == CmResourceTypeInterrupt)
        {
            /* Check if this is MSI or legacy */
            if (Desc->Flags & CM_RESOURCE_INTERRUPT_MESSAGE)
            {
                /* MSI interrupt resource - always capture it */
                if (MsiInterruptDesc == NULL)
                {
                    MsiInterruptDesc = Desc;
                }
                if (InterruptDesc == NULL && MsiSupportedByDriver)
                {
                    InterruptDesc = Desc;
                }
            }
            else
            {
                /* Legacy interrupt resource */
                if (LegacyInterruptDesc == NULL)
                {
                    LegacyInterruptDesc = Desc;
                }
                if (InterruptDesc == NULL)
                {
                    InterruptDesc = Desc;
                }
            }
        }
    }

    /*
     * If driver doesn't want MSI, prefer legacy interrupt if available.
     * This handles the case where PnP allocated both MSI and legacy resources.
     */
    if (!MsiSupportedByDriver && LegacyInterruptDesc != NULL)
    {
        DPRINT("NDIS6: Driver disabled MSI, using legacy interrupt resource\n");
        InterruptDesc = LegacyInterruptDesc;
    }
    else if (!MsiSupportedByDriver && MsiInterruptDesc != NULL && InterruptDesc == NULL)
    {
        /*
         * Driver disabled MSI but there's no legacy interrupt and only MSI is available.
         * Use MSI resource but treat it as legacy (the code below handles this case).
         */
        DPRINT("NDIS6: Driver disabled MSI but only MSI resource available. Using MSI as fallback.\n");
        InterruptDesc = MsiInterruptDesc;
    }

    if (InterruptDesc == NULL)
    {
        DPRINT("No interrupt resource found\n");
        return NDIS_STATUS_RESOURCE_CONFLICT;
    }

    /*
     * Check if this is an MSI/MSI-X interrupt resource.
     * CM_RESOURCE_INTERRUPT_MESSAGE (0x20) indicates MSI/MSI-X.
     * The resource format differs:
     *   Legacy: u.Interrupt.Level, u.Interrupt.Vector, u.Interrupt.Affinity
     *   MSI:    u.MessageInterrupt.Translated.Level/Vector/Affinity
     */
    IsMessageInterrupt = (InterruptDesc->Flags & CM_RESOURCE_INTERRUPT_MESSAGE) != 0;

    DPRINT("NDIS6: Interrupt resource Flags=0x%04x, IsMessage=%d, MsiSupportedByDriver=%d\n",
             InterruptDesc->Flags, IsMessageInterrupt, MsiSupportedByDriver);

    /*
     * If driver explicitly disabled MSI but we only have MSI resources,
     * we need to treat it as legacy. This is a workaround for platforms
     * where MSI is not properly enabled in hardware.
     */
    if (IsMessageInterrupt && !MsiSupportedByDriver)
    {
        DPRINT("NDIS6: WARNING - MSI resource but driver disabled MSI. Treating as legacy.\n");
        IsMessageInterrupt = FALSE;
    }

    /*
     * Determine if resource is actually a message resource (for reading the right union).
     * This differs from IsMessageInterrupt which controls how we treat it.
     */
    BOOLEAN IsActuallyMsiResource = (InterruptDesc->Flags & CM_RESOURCE_INTERRUPT_MESSAGE) != 0;

    if (IsMessageInterrupt)
    {
        /* MSI/MSI-X interrupt - use MessageInterrupt union member */
        MappedIrq = InterruptDesc->u.MessageInterrupt.Translated.Vector;
        Dirql = (KIRQL)InterruptDesc->u.MessageInterrupt.Translated.Level;
        Affinity = InterruptDesc->u.MessageInterrupt.Translated.Affinity;

        /*
         * MSI interrupts are edge-triggered (latched).
         * MSI interrupts are typically NOT shared (exclusive to the device).
         */
        InterruptMode = Latched;
        ShareVector = FALSE;

        DPRINT("NDIS6: MESSAGE interrupt - Vector=%u, Level=%u, Affinity=0x%Ix\n",
                 MappedIrq, Dirql, (ULONG_PTR)Affinity);
        DPRINT1("NDIS6:   Raw MessageCount=%u\n",
                 InterruptDesc->u.MessageInterrupt.Raw.MessageCount);

        /* Update interrupt type to indicate message-based */
        IntBlock->InterruptType = NDIS_CONNECT_MESSAGE_BASED;
        IntBlock->MessageCount = 1;  /* Single MSI vector in this case */
    }
    else if (IsActuallyMsiResource)
    {
        /*
         * This is an MSI resource but driver disabled MSI.
         * Still need to read from MessageInterrupt union, but treat as legacy.
         * Use edge-triggered, non-shared mode for the vector.
         */
        MappedIrq = InterruptDesc->u.MessageInterrupt.Translated.Vector;
        Dirql = (KIRQL)InterruptDesc->u.MessageInterrupt.Translated.Level;
        Affinity = InterruptDesc->u.MessageInterrupt.Translated.Affinity;

        /* Use edge-triggered for MSI vectors even if treated as legacy */
        InterruptMode = Latched;
        ShareVector = FALSE;

        DPRINT("NDIS6: MSI resource treated as legacy - Vector=%u, Level=%u, Affinity=0x%Ix\n",
                 MappedIrq, Dirql, (ULONG_PTR)Affinity);

        IntBlock->InterruptType = NDIS_CONNECT_LINE_BASED;
    }
    else
    {
        /* Legacy line-based interrupt - use Interrupt union member */
        MappedIrq = InterruptDesc->u.Interrupt.Vector;
        Dirql = (KIRQL)InterruptDesc->u.Interrupt.Level;
        Affinity = InterruptDesc->u.Interrupt.Affinity;

        InterruptMode = (InterruptDesc->Flags & CM_RESOURCE_INTERRUPT_LATCHED) ?
                        Latched : LevelSensitive;
        ShareVector = (InterruptDesc->ShareDisposition == CmResourceShareShared);

        DPRINT("NDIS6: Legacy interrupt - Vector=%u, Level=%u, Affinity=0x%Ix, Shared=%d\n",
                 MappedIrq, Dirql, (ULONG_PTR)Affinity, ShareVector);

        IntBlock->InterruptType = NDIS_CONNECT_LINE_BASED;
    }

    DPRINT("NDIS6 Connecting to interrupt Vector=0x%x, Level=%d, Affinity=0x%lx, Mode=%s, Shared=%d\n",
        MappedIrq, Dirql, (ULONG)Affinity,
        (InterruptMode == Latched) ? "Latched" : "Level", ShareVector);

    /* Initialize the DPC */
    KeInitializeDpc(&IntBlock->InterruptDpc, Ndis6iInterruptDpc, IntBlock);

    /* Connect the interrupt */
    Status = IoConnectInterrupt(
        &IntBlock->InterruptObject,
        (PKSERVICE_ROUTINE)Ndis6iInterruptIsr,
        IntBlock,
        &IntBlock->Lock,
        MappedIrq,
        Dirql,
        Dirql,
        InterruptMode,
        ShareVector,
        Affinity,
        FALSE);

    if (!NT_SUCCESS(Status))
    {
        DPRINT("IoConnectInterrupt failed: 0x%x\n", Status);
        DPRINT("NDIS6: IoConnectInterrupt FAILED: Status=0x%08x\n", Status);

        if (Status == STATUS_INSUFFICIENT_RESOURCES)
        {
            return NDIS_STATUS_RESOURCE_CONFLICT;
        }
        return NDIS_STATUS_FAILURE;
    }

    IntBlock->Connected = TRUE;

    DPRINT("NDIS6: Interrupt connected successfully - Type=%s\n",
             (IntBlock->InterruptType == NDIS_CONNECT_MESSAGE_BASED) ? "MESSAGE" : "LINE");

    /*
     * If this is a message-based interrupt (MSI/MSI-X), program the MSI-X table.
     * The device needs the MSI-X table programmed with the message address and data
     * before it can actually generate interrupts.
     */
    if (IntBlock->InterruptType == NDIS_CONNECT_MESSAGE_BASED || IsMessageInterrupt)
    {
        PDEVICE_OBJECT Pdo = NULL;

        /*
         * For NDIS 6.x, the PDO is stored in DeviceExtension[1] by Ndis6AddDevice.
         * DeviceExtension layout:
         *   [0] = NextDeviceObject (device below in stack)
         *   [1] = PDO (physical device object)
         *   [2] = AdapterContext
         *   [3-4] = MajorFunction backups
         *   [5] = ResourceList copy
         */
        if (DeviceObject != NULL && DeviceObject->DeviceExtension != NULL)
        {
            PVOID *ExtensionArray = (PVOID *)DeviceObject->DeviceExtension;
            Pdo = (PDEVICE_OBJECT)ExtensionArray[1];
        }

        if (Pdo != NULL)
        {
            NTSTATUS MsixStatus;

            DPRINT("NDIS6: Programming MSI-X table for message interrupt (PDO=%p)\n", Pdo);
            MsixStatus = Ndis6iProgramMsixTable(Pdo, MappedIrq, Dirql, Affinity, ResourceList);
            if (!NT_SUCCESS(MsixStatus))
            {
                DPRINT("NDIS6: MSI-X table programming failed: 0x%x (interrupts may not work)\n", MsixStatus);
            }
        }
        else
        {
            DPRINT("NDIS6: Could not find PDO for MSI-X programming\n");
        }
    }

    DPRINT("NDIS6 interrupt connected successfully, Type=%d\n",
        IntBlock->InterruptType);

    return NDIS_STATUS_SUCCESS;
}

/*
 * @implemented
 */
NDIS_STATUS
EXPORT
NdisMRegisterInterruptEx(
    _In_ NDIS_HANDLE MiniportAdapterHandle,
    _In_ NDIS_HANDLE MiniportInterruptContext,
    _In_ PNDIS_MINIPORT_INTERRUPT_CHARACTERISTICS MiniportInterruptCharacteristics,
    _Out_ PNDIS_HANDLE NdisInterruptHandle)
{
    PNDIS6_INTERRUPT_BLOCK IntBlock;
    PLOGICAL_ADAPTER Adapter;
    PCM_RESOURCE_LIST Ndis6Resources;
    NDIS_STATUS Status;

    DPRINT("NdisMRegisterInterruptEx called\n");

    /* Validate parameters */
    if (MiniportAdapterHandle == NULL ||
        MiniportInterruptCharacteristics == NULL ||
        NdisInterruptHandle == NULL)
    {
        DPRINT1("Invalid parameter\n");
        return NDIS_STATUS_INVALID_PARAMETER;
    }

    *NdisInterruptHandle = NULL;

    /* Validate characteristics header */
    if (MiniportInterruptCharacteristics->Header.Type != NDIS_OBJECT_TYPE_MINIPORT_INTERRUPT)
    {
        /* Be lenient - some drivers may not set the type correctly */
        DPRINT1("Warning: Unexpected object type 0x%x\n",
            MiniportInterruptCharacteristics->Header.Type);
    }

    /* Validate required handlers */
    if (MiniportInterruptCharacteristics->InterruptHandler == NULL ||
        MiniportInterruptCharacteristics->InterruptDpcHandler == NULL)
    {
        DPRINT("Required interrupt handlers not provided\n");
        return NDIS_STATUS_INVALID_PARAMETER;
    }

    /* Allocate interrupt block */
    IntBlock = ExAllocatePoolWithTag(NonPagedPool,
                                     sizeof(NDIS6_INTERRUPT_BLOCK),
                                     NDIS6_INT_TAG);
    if (IntBlock == NULL)
    {
        DPRINT("Failed to allocate interrupt block\n");
        return NDIS_STATUS_RESOURCES;
    }

    RtlZeroMemory(IntBlock, sizeof(NDIS6_INTERRUPT_BLOCK));

    /* Initialize interrupt block */
    IntBlock->Header.Type = NDIS_OBJECT_TYPE_MINIPORT_INTERRUPT;
    IntBlock->Header.Revision = NDIS_MINIPORT_INTERRUPT_REVISION_1;
    IntBlock->Header.Size = sizeof(NDIS6_INTERRUPT_BLOCK);

    IntBlock->MiniportHandle = MiniportAdapterHandle;
    IntBlock->MiniportInterruptContext = MiniportInterruptContext;

    /* Copy characteristics */
    RtlCopyMemory(&IntBlock->Characteristics,
                  MiniportInterruptCharacteristics,
                  sizeof(NDIS_MINIPORT_INTERRUPT_CHARACTERISTICS));

    KeInitializeSpinLock(&IntBlock->Lock);
    KeInitializeEvent(&IntBlock->DisconnectEvent, NotificationEvent, TRUE);
    IntBlock->DpcPending = 0;
    IntBlock->Connected = FALSE;

    /* Check if this is an NDIS 6.x handle */
    Ndis6Resources = Ndis6iGetResourceList(MiniportAdapterHandle);
    if (Ndis6Resources != NULL)
    {
        PDEVICE_OBJECT DeviceObject = (PDEVICE_OBJECT)MiniportAdapterHandle;

        /*
         * NDIS 6.x path - use resources from device extension.
         * The Ndis6iConnectLineBasedNdis6 function will automatically detect
         * if the resource has CM_RESOURCE_INTERRUPT_MESSAGE flag set and
         * configure for MSI/MSI-X accordingly.
         */
        DPRINT("NDIS 6.x interrupt registration, Resources=%p\n", Ndis6Resources);

        DPRINT("NDIS6: NdisMRegisterInterruptEx - MsiSupported=%d, MsgHandler=%p, MsgDpcHandler=%p\n",
                 MiniportInterruptCharacteristics->MsiSupported,
                 MiniportInterruptCharacteristics->MessageInterruptHandler,
                 MiniportInterruptCharacteristics->MessageInterruptDpcHandler);

        /*
         * Connect the interrupt using resources.
         * Pass MsiSupported flag to allow the connection function to skip MSI
         * resources if the driver explicitly disabled MSI support.
         * This handles the case where PnP allocated MSI resources but the driver
         * doesn't want to use MSI (e.g., due to MSI enablement issues).
         */
        Status = Ndis6iConnectLineBasedNdis6(IntBlock, Ndis6Resources, DeviceObject,
                                              MiniportInterruptCharacteristics->MsiSupported);

        if (Status != NDIS_STATUS_SUCCESS)
        {
            ExFreePoolWithTag(IntBlock, NDIS6_INT_TAG);
            DPRINT("Failed to connect NDIS 6.x interrupt: 0x%x\n", Status);
            DPRINT("NDIS6: NdisMRegisterInterruptEx FAILED: Status=0x%08x\n", Status);
            return Status;
        }

        /* Update the characteristics with the actual interrupt type */
        MiniportInterruptCharacteristics->InterruptType = IntBlock->InterruptType;

        *NdisInterruptHandle = (NDIS_HANDLE)IntBlock;

        DPRINT("NDIS6: NdisMRegisterInterruptEx completed - Type=%s\n",
                 (IntBlock->InterruptType == NDIS_CONNECT_MESSAGE_BASED) ? "MESSAGE" : "LINE");

        DPRINT("NDIS 6.x NdisMRegisterInterruptEx completed successfully, Type=%d\n",
            IntBlock->InterruptType);

        return NDIS_STATUS_SUCCESS;
    }

    /* NDIS 5.x path - use LOGICAL_ADAPTER */
    Adapter = (PLOGICAL_ADAPTER)MiniportAdapterHandle;

    /*
     * Try to connect based on MSI support flag
     * If MSI is requested and supported, try message-based first
     */
    if (MiniportInterruptCharacteristics->MsiSupported)
    {
        Status = Ndis6iConnectMessageBased(IntBlock, &Adapter->NdisMiniportBlock);

        if (Status == NDIS_STATUS_NOT_SUPPORTED)
        {
            /* Fall back to line-based */
            DPRINT("MSI not available, falling back to line-based\n");
            Status = Ndis6iConnectLineBased(IntBlock, &Adapter->NdisMiniportBlock);
        }
    }
    else
    {
        /* Use line-based interrupt */
        Status = Ndis6iConnectLineBased(IntBlock, &Adapter->NdisMiniportBlock);
    }

    if (Status != NDIS_STATUS_SUCCESS)
    {
        ExFreePoolWithTag(IntBlock, NDIS6_INT_TAG);
        DPRINT("Failed to connect interrupt: 0x%x\n", Status);
        return Status;
    }

    /* Update the characteristics with the actual interrupt type */
    MiniportInterruptCharacteristics->InterruptType = IntBlock->InterruptType;

    /* Store the interrupt block handle */
    Adapter->NdisMiniportBlock.RegisteredInterrupts++;

    *NdisInterruptHandle = (NDIS_HANDLE)IntBlock;

    DPRINT("NdisMRegisterInterruptEx completed successfully, Type=%d\n",
        IntBlock->InterruptType);

    return NDIS_STATUS_SUCCESS;
}

/*
 * @implemented
 */
VOID
EXPORT
NdisMDeregisterInterruptEx(
    _In_ NDIS_HANDLE NdisInterruptHandle)
{
    PNDIS6_INTERRUPT_BLOCK IntBlock;
    PLOGICAL_ADAPTER Adapter;

    DPRINT("NdisMDeregisterInterruptEx called\n");

    if (NdisInterruptHandle == NULL)
    {
        DPRINT("NULL interrupt handle\n");
        return;
    }

    IntBlock = (PNDIS6_INTERRUPT_BLOCK)NdisInterruptHandle;

    /* Disconnect the interrupt */
    Ndis6iDisconnectInterrupt(IntBlock);

    /* Update adapter's registered interrupt count */
    Adapter = (PLOGICAL_ADAPTER)IntBlock->MiniportHandle;
    if (Adapter != NULL)
    {
        Adapter->NdisMiniportBlock.RegisteredInterrupts--;
    }

    /* Free the interrupt block */
    ExFreePoolWithTag(IntBlock, NDIS6_INT_TAG);

    DPRINT("NdisMDeregisterInterruptEx completed\n");
}

/*
 * @implemented
 */
BOOLEAN
EXPORT
NdisMSynchronizeWithInterruptEx(
    _In_ NDIS_HANDLE NdisInterruptHandle,
    _In_ ULONG MessageId,
    _In_ MINIPORT_SYNCHRONIZE_INTERRUPT_HANDLER SynchronizeFunction,
    _In_opt_ PVOID SynchronizeContext)
{
    PNDIS6_INTERRUPT_BLOCK IntBlock;
    PKINTERRUPT InterruptObject;
    BOOLEAN Result;

    DPRINT("NdisMSynchronizeWithInterruptEx called, MessageId=%lu\n", MessageId);

    if (NdisInterruptHandle == NULL || SynchronizeFunction == NULL)
    {
        DPRINT1("Invalid parameter\n");
        return FALSE;
    }

    IntBlock = (PNDIS6_INTERRUPT_BLOCK)NdisInterruptHandle;

    if (!IntBlock->Connected)
    {
        DPRINT("Interrupt not connected\n");
        return FALSE;
    }

    /* Get the appropriate interrupt object */
    if (IntBlock->InterruptType == NDIS_CONNECT_MESSAGE_BASED)
    {
        if (IntBlock->MessageInterruptObjects == NULL ||
            MessageId >= IntBlock->MessageCount)
        {
            DPRINT1("Invalid MessageId %lu (max %lu)\n",
                MessageId, IntBlock->MessageCount);
            return FALSE;
        }
        InterruptObject = IntBlock->MessageInterruptObjects[MessageId];
    }
    else
    {
        /* Line-based interrupt - ignore MessageId */
        InterruptObject = IntBlock->InterruptObject;
    }

    if (InterruptObject == NULL)
    {
        DPRINT("No interrupt object for MessageId %lu\n", MessageId);
        return FALSE;
    }

    /* Synchronize with the interrupt */
    Result = KeSynchronizeExecution(InterruptObject,
                                    (PKSYNCHRONIZE_ROUTINE)SynchronizeFunction,
                                    SynchronizeContext);

    return Result;
}

/*
 * @implemented
 */
ULONG
EXPORT
NdisMQueueDpc(
    _In_ NDIS_HANDLE NdisInterruptHandle,
    _In_ ULONG MessageId,
    _In_ ULONG TargetProcessors,
    _In_opt_ PVOID MiniportDpcContext)
{
    PNDIS6_INTERRUPT_BLOCK IntBlock;
    ULONG QueuedCount = 0;
    ULONG ProcessorIndex;
    ULONG ProcessorMask;

    DPRINT("NdisMQueueDpc called, MessageId=%lu, TargetProcessors=0x%x\n",
        MessageId, TargetProcessors);

    if (NdisInterruptHandle == NULL)
    {
        return 0;
    }

    IntBlock = (PNDIS6_INTERRUPT_BLOCK)NdisInterruptHandle;

    if (!IntBlock->Connected)
    {
        return 0;
    }

    /* Store the DPC context */
    IntBlock->DpcContext = MiniportDpcContext;

    /*
     * Queue DPC to specified processors
     * Note: On single-processor systems, TargetProcessors is typically 1
     */
    ProcessorMask = TargetProcessors;

    for (ProcessorIndex = 0; ProcessorIndex < 32 && ProcessorMask != 0; ProcessorIndex++)
    {
        if (ProcessorMask & 1)
        {
            PKDPC Dpc;
            BOOLEAN Queued;

            /* Get the appropriate DPC */
            if (IntBlock->InterruptType == NDIS_CONNECT_MESSAGE_BASED &&
                IntBlock->MessageDpcs != NULL &&
                MessageId < IntBlock->MessageCount)
            {
                Dpc = &IntBlock->MessageDpcs[MessageId];
            }
            else
            {
                Dpc = &IntBlock->InterruptDpc;
            }

            /* Set target processor if not current processor */
            if (ProcessorIndex != KeGetCurrentProcessorNumber())
            {
                KeSetTargetProcessorDpc(Dpc, (CCHAR)ProcessorIndex);
            }

            /* Queue the DPC */
            InterlockedIncrement(&IntBlock->DpcPending);
            Queued = KeInsertQueueDpc(Dpc, MiniportDpcContext, UlongToPtr(MessageId));

            if (Queued)
            {
                QueuedCount++;
            }
            else
            {
                InterlockedDecrement(&IntBlock->DpcPending);
            }
        }
        ProcessorMask >>= 1;
    }

    return QueuedCount;
}

/*
 * @implemented
 */
ULONG
EXPORT
NdisMQueueDpcEx(
    _In_ NDIS_HANDLE NdisInterruptHandle,
    _In_ ULONG MessageId,
    _In_ PGROUP_AFFINITY TargetProcessors,
    _In_opt_ PVOID MiniportDpcContext)
{
    PNDIS6_INTERRUPT_BLOCK IntBlock;

    DPRINT("NdisMQueueDpcEx called, MessageId=%lu\n", MessageId);

    if (NdisInterruptHandle == NULL || TargetProcessors == NULL)
    {
        return 0;
    }

    IntBlock = (PNDIS6_INTERRUPT_BLOCK)NdisInterruptHandle;

    if (!IntBlock->Connected)
    {
        return 0;
    }

    /*
     * For now, convert GROUP_AFFINITY to simple processor mask for group 0
     * Full processor group support would require KeSetTargetProcessorDpcEx
     * which may not be available on all ReactOS builds.
     */
    if (TargetProcessors->Group == 0)
    {
        /* Use the low 32 bits of the affinity mask */
        ULONG SimpleMask = (ULONG)(TargetProcessors->Mask & 0xFFFFFFFF);
        return NdisMQueueDpc(NdisInterruptHandle, MessageId, SimpleMask, MiniportDpcContext);
    }
    else
    {
        /*
         * For processor groups > 0, we would need KeSetTargetProcessorDpcEx
         * For now, queue to the current processor as a fallback
         */
        DPRINT1("Processor group %u not fully supported, using current processor\n",
            TargetProcessors->Group);

        return NdisMQueueDpc(NdisInterruptHandle, MessageId,
                            (1 << KeGetCurrentProcessorNumber()),
                            MiniportDpcContext);
    }
}

#endif /* NDIS_SUPPORT_NDIS6 */

/* EOF */
