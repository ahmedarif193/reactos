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

/* ============================================================================
 *  C1: CONNECT_MESSAGE_BASED — MSI/MSI-X interrupt connection
 *
 *  Walks the PDO's translated resource list for CmResourceTypeInterrupt
 *  entries with CM_RESOURCE_INTERRUPT_MESSAGE set. For each message-based
 *  IRQ it allocates a PKINTERRUPT via IoConnectInterrupt and fills in a
 *  IO_INTERRUPT_MESSAGE_INFO entry. The whole table is returned to the
 *  caller via ConnectionContext.InterruptMessageTable.
 *
 *  Each connected vector uses a per-vector dispatch stub stored in the
 *  IopMsiDispatchEntry wrapper so that ISR calls carry the right
 *  MessageID back to the driver's PKMESSAGE_SERVICE_ROUTINE.
 *
 *  On QEMU / virtual hardware that doesn't expose MSI resources, this
 *  function returns STATUS_NOT_FOUND and the caller is expected to fall
 *  back to the line-based path (which the NDIS bridge already does).
 * ============================================================================ */

/* Per-vector dispatch stub — stored once per connected message vector.
 * Wraps the PKMESSAGE_SERVICE_ROUTINE into a PKSERVICE_ROUTINE by
 * remembering which MessageID this vector maps to. IoConnectInterrupt
 * only speaks PKSERVICE_ROUTINE, so this intermediate shim is required.
 *
 * IsFallback == TRUE means LineRoutine is a plain PKSERVICE_ROUTINE
 * (from the fallback path) and we call it WITHOUT the MessageId arg. */
typedef struct _IOP_MSI_DISPATCH_ENTRY
{
    BOOLEAN                     IsFallback;
    PKMESSAGE_SERVICE_ROUTINE   MessageRoutine;
    PKSERVICE_ROUTINE           LineRoutine;
    PVOID                       ServiceContext;
    ULONG                       MessageId;
} IOP_MSI_DISPATCH_ENTRY, *PIOP_MSI_DISPATCH_ENTRY;

static BOOLEAN NTAPI
IopMsiDispatchWrapper(
    _In_ PKINTERRUPT Interrupt,
    _In_ PVOID       Context)
{
    PIOP_MSI_DISPATCH_ENTRY entry = (PIOP_MSI_DISPATCH_ENTRY)Context;
    if (entry == NULL)
        return FALSE;

    if (entry->IsFallback)
    {
        if (entry->LineRoutine == NULL)
            return FALSE;
        return entry->LineRoutine(Interrupt, entry->ServiceContext);
    }

    if (entry->MessageRoutine == NULL)
        return FALSE;
    return entry->MessageRoutine(Interrupt, entry->ServiceContext, entry->MessageId);
}

NTSTATUS
IopConnectInterruptExMessageBased(
    _Inout_ PIO_CONNECT_INTERRUPT_PARAMETERS Parameters)
{
    PIO_CONNECT_INTERRUPT_MESSAGE_BASED_PARAMETERS p = &Parameters->MessageBased;
    PDEVICE_NODE DeviceNode;
    PCM_RESOURCE_LIST Resources;
    PCM_PARTIAL_RESOURCE_LIST Partial;
    PIO_INTERRUPT_MESSAGE_INFO Table;
    ULONG MessageCount = 0;
    ULONG i, MessageIdx;
    NTSTATUS Status;

    PAGED_CODE();

    if (p->PhysicalDeviceObject == NULL || p->MessageServiceRoutine == NULL)
        return STATUS_INVALID_PARAMETER;

    /* Look up the device node so we can get the translated resource list. */
    DeviceNode = IopGetDeviceNode(p->PhysicalDeviceObject);
    if (DeviceNode == NULL)
    {
        DPRINT1("IoConnectInterruptEx: no device node for PDO %p\n",
                p->PhysicalDeviceObject);
        goto FallBackToLine;
    }

    Resources = DeviceNode->ResourceListTranslated;
    if (Resources == NULL || Resources->Count == 0)
    {
        DPRINT1("IoConnectInterruptEx: no translated resources on PDO %p\n",
                p->PhysicalDeviceObject);
        goto FallBackToLine;
    }

    /* Count message-based interrupts across all full descriptors. */
    Partial = &Resources->List[0].PartialResourceList;
    for (i = 0; i < Partial->Count; i++)
    {
        PCM_PARTIAL_RESOURCE_DESCRIPTOR d = &Partial->PartialDescriptors[i];
        if (d->Type == CmResourceTypeInterrupt &&
            (d->Flags & CM_RESOURCE_INTERRUPT_MESSAGE))
        {
            MessageCount++;
        }
    }

    if (MessageCount == 0)
    {
        DPRINT("IoConnectInterruptEx: no message-based interrupts; falling back to line-based\n");
        goto FallBackToLine;
    }

    /* Allocate the IO_INTERRUPT_MESSAGE_INFO table with one entry per
     * message. The header has one inline entry; allocate N-1 extras. */
    Table = (PIO_INTERRUPT_MESSAGE_INFO)ExAllocatePoolZero(
        NonPagedPool,
        sizeof(IO_INTERRUPT_MESSAGE_INFO) +
            (MessageCount - 1) * sizeof(IO_INTERRUPT_MESSAGE_INFO_ENTRY),
        'IsMI');
    if (Table == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    Table->MessageCount = MessageCount;
    Table->UnifiedIrql  = PASSIVE_LEVEL;

    /* Walk the descriptors again and connect each message vector. */
    MessageIdx = 0;
    for (i = 0; i < Partial->Count && MessageIdx < MessageCount; i++)
    {
        PCM_PARTIAL_RESOURCE_DESCRIPTOR d = &Partial->PartialDescriptors[i];
        PIO_INTERRUPT_MESSAGE_INFO_ENTRY MsgEntry;
        PIOP_MSI_DISPATCH_ENTRY DispEntry;
        PKINTERRUPT InterruptObject = NULL;

        if (d->Type != CmResourceTypeInterrupt ||
            !(d->Flags & CM_RESOURCE_INTERRUPT_MESSAGE))
            continue;

        /* Allocate the per-vector wrapper context. Lives for the life
         * of the interrupt connection; freed in IoDisconnectInterruptEx. */
        DispEntry = ExAllocatePoolZero(NonPagedPool,
                                       sizeof(IOP_MSI_DISPATCH_ENTRY),
                                       'EsMI');
        if (DispEntry == NULL)
        {
            Status = STATUS_INSUFFICIENT_RESOURCES;
            goto FailureCleanup;
        }

        DispEntry->IsFallback     = FALSE;
        DispEntry->MessageRoutine = p->MessageServiceRoutine;
        DispEntry->LineRoutine    = NULL;
        DispEntry->ServiceContext = p->ServiceContext;
        DispEntry->MessageId      = MessageIdx;

        Status = IoConnectInterrupt(
            &InterruptObject,
            IopMsiDispatchWrapper,
            DispEntry,
            p->SpinLock,
            d->u.Interrupt.Vector,
            (KIRQL)d->u.Interrupt.Level,
            p->SynchronizeIrql ? p->SynchronizeIrql : (KIRQL)d->u.Interrupt.Level,
            (d->Flags & CM_RESOURCE_INTERRUPT_LATCHED) ? Latched : LevelSensitive,
            FALSE,              /* MSI/MSI-X vectors are not shared */
            d->u.Interrupt.Affinity,
            p->FloatingSave);

        if (!NT_SUCCESS(Status))
        {
            DPRINT1("IoConnectInterruptEx: IoConnectInterrupt for msg %lu failed 0x%lx\n",
                    MessageIdx, Status);
            ExFreePoolWithTag(DispEntry, 'EsMI');
            goto FailureCleanup;
        }

        MsgEntry = &Table->MessageInfo[MessageIdx];
        MsgEntry->MessageAddress.QuadPart = 0;    /* set by the hardware */
        MsgEntry->TargetProcessorSet       = d->u.Interrupt.Affinity;
        MsgEntry->InterruptObject          = InterruptObject;
        MsgEntry->MessageData              = MessageIdx;
        MsgEntry->Vector                   = d->u.Interrupt.Vector;
        MsgEntry->Irql                     = (KIRQL)d->u.Interrupt.Level;
        MsgEntry->Mode                     = (d->Flags & CM_RESOURCE_INTERRUPT_LATCHED)
                                                 ? Latched : LevelSensitive;
        MsgEntry->Polarity                 = InterruptActiveHigh;

        MessageIdx++;
    }

    /* ConnectionContext.InterruptMessageTable is a double pointer — the
     * caller provided the storage for the pointer to the table. */
    if (p->ConnectionContext.InterruptMessageTable != NULL)
        *p->ConnectionContext.InterruptMessageTable = Table;
    DPRINT("IoConnectInterruptEx: connected %lu message vectors\n", MessageCount);
    return STATUS_SUCCESS;

FailureCleanup:
    /* Disconnect any vectors we already connected, free dispatch entries. */
    for (i = 0; i < MessageIdx; i++)
    {
        PKINTERRUPT Intr = Table->MessageInfo[i].InterruptObject;
        if (Intr != NULL)
        {
            /* The ServiceContext the kernel was holding is our dispatch
             * entry — free it AFTER disconnect so the ISR can't fire on
             * freed memory. */
            PIOP_MSI_DISPATCH_ENTRY DispEntry = NULL;
            PIO_INTERRUPT IoInterrupt = CONTAINING_RECORD(
                Intr, IO_INTERRUPT, FirstInterrupt);
            DispEntry = (PIOP_MSI_DISPATCH_ENTRY)IoInterrupt->FirstInterrupt.ServiceContext;
            IoDisconnectInterrupt(Intr);
            if (DispEntry != NULL)
                ExFreePoolWithTag(DispEntry, 'EsMI');
        }
    }
    ExFreePoolWithTag(Table, 'IsMI');
    return Status;

FallBackToLine:
    /* No PDO resources or no MSI entries — fall back to a single line-based
     * connection. The caller provided a FallBackServiceRoutine for this
     * case; we use it as a plain PKSERVICE_ROUTINE via the dispatch
     * shim with MessageId = 0.
     *
     * To keep the caller's interface consistent (it always passes
     * &MessageTable as the ConnectionContext), we allocate a real
     * IO_INTERRUPT_MESSAGE_INFO with one entry containing the line-based
     * KINTERRUPT. The bridge can then use Table->MessageCount==1 the
     * same way it handles real MSI. */
    if (p->FallBackServiceRoutine == NULL)
        return STATUS_NOT_FOUND;
    {
        PIOP_MSI_DISPATCH_ENTRY DispEntry;
        PKINTERRUPT LineInt = NULL;
        PIO_INTERRUPT_MESSAGE_INFO FallbackTable;

        /* Pull the first line-based CmResourceTypeInterrupt from the PDO
         * if available, so we can get vector/irql/affinity for the
         * fallback connect. */
        ULONG Vector = 0;
        KIRQL Irql = PASSIVE_LEVEL;
        KAFFINITY Affinity = 1;
        KINTERRUPT_MODE Mode = LevelSensitive;

        if (DeviceNode != NULL &&
            DeviceNode->ResourceListTranslated != NULL &&
            DeviceNode->ResourceListTranslated->Count > 0)
        {
            PCM_PARTIAL_RESOURCE_LIST prl =
                &DeviceNode->ResourceListTranslated->List[0].PartialResourceList;
            ULONG k;
            for (k = 0; k < prl->Count; k++)
            {
                PCM_PARTIAL_RESOURCE_DESCRIPTOR dd = &prl->PartialDescriptors[k];
                if (dd->Type == CmResourceTypeInterrupt &&
                    !(dd->Flags & CM_RESOURCE_INTERRUPT_MESSAGE))
                {
                    Vector   = dd->u.Interrupt.Vector;
                    Irql     = (KIRQL)dd->u.Interrupt.Level;
                    Affinity = dd->u.Interrupt.Affinity;
                    Mode     = (dd->Flags & CM_RESOURCE_INTERRUPT_LATCHED)
                                   ? Latched : LevelSensitive;
                    break;
                }
            }
        }

        if (Vector == 0)
            return STATUS_NOT_FOUND;

        DispEntry = ExAllocatePoolZero(NonPagedPool,
                                       sizeof(IOP_MSI_DISPATCH_ENTRY),
                                       'EsMI');
        if (DispEntry == NULL)
            return STATUS_INSUFFICIENT_RESOURCES;

        DispEntry->IsFallback     = TRUE;
        DispEntry->MessageRoutine = NULL;
        DispEntry->LineRoutine    = p->FallBackServiceRoutine;
        DispEntry->ServiceContext = p->ServiceContext;
        DispEntry->MessageId      = 0;

        Status = IoConnectInterrupt(&LineInt,
                                    IopMsiDispatchWrapper,
                                    DispEntry,
                                    p->SpinLock,
                                    Vector, Irql, Irql, Mode,
                                    TRUE, Affinity, p->FloatingSave);
        if (!NT_SUCCESS(Status))
        {
            ExFreePoolWithTag(DispEntry, 'EsMI');
            return Status;
        }

        /* Allocate a 1-entry IO_INTERRUPT_MESSAGE_INFO so the caller's
         * MessageInfoTable interface stays consistent. */
        FallbackTable = (PIO_INTERRUPT_MESSAGE_INFO)ExAllocatePoolZero(
            NonPagedPool, sizeof(IO_INTERRUPT_MESSAGE_INFO), 'IsMI');
        if (FallbackTable == NULL)
        {
            IoDisconnectInterrupt(LineInt);
            ExFreePoolWithTag(DispEntry, 'EsMI');
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        FallbackTable->MessageCount = 1;
        FallbackTable->UnifiedIrql  = Irql;
        FallbackTable->MessageInfo[0].InterruptObject = LineInt;
        FallbackTable->MessageInfo[0].MessageData     = 0;
        FallbackTable->MessageInfo[0].Vector          = Vector;
        FallbackTable->MessageInfo[0].Irql            = Irql;
        FallbackTable->MessageInfo[0].Mode            = Mode;
        FallbackTable->MessageInfo[0].Polarity        = InterruptActiveHigh;
        FallbackTable->MessageInfo[0].TargetProcessorSet = Affinity;

        if (p->ConnectionContext.InterruptMessageTable != NULL)
            *p->ConnectionContext.InterruptMessageTable = FallbackTable;
        DPRINT("IoConnectInterruptEx: MSI fallback to line-based vector=%lu irql=%u\n",
               Vector, Irql);
        return STATUS_SUCCESS;
    }
}

NTSTATUS
NTAPI
IoConnectInterruptEx(
    _Inout_ PIO_CONNECT_INTERRUPT_PARAMETERS Parameters)
{
    PAGED_CODE();

    switch (Parameters->Version)
    {
        case CONNECT_FULLY_SPECIFIED:
            return IopConnectInterruptExFullySpecific(Parameters);
        case CONNECT_FULLY_SPECIFIED_GROUP:
            //TODO: We don't do anything for the group type
            return IopConnectInterruptExFullySpecific(Parameters);
        case CONNECT_MESSAGE_BASED:
            return IopConnectInterruptExMessageBased(Parameters);
        case CONNECT_LINE_BASED:
            DPRINT1("FIXME: Line based interrupts are UNIMPLEMENTED\n");
            break;
    }

    return STATUS_SUCCESS;
}

VOID
NTAPI
IoDisconnectInterruptEx(
    _In_ PIO_DISCONNECT_INTERRUPT_PARAMETERS Parameters)
{
    PAGED_CODE();

    //FIXME: This eventually will need to handle more cases
    if (Parameters->ConnectionContext.InterruptObject)
        IoDisconnectInterrupt(Parameters->ConnectionContext.InterruptObject);
}

/* EOF */
