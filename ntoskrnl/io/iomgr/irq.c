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

    HeaderSize = sizeof(IO_MESSAGE_CONNECT_CONTEXT) +
                 (MessageInfo->MessageCount - 1) * sizeof(IO_INTERRUPT_MESSAGE_INFO_ENTRY);

    return (PIO_INTERRUPT_MESSAGE_ENTRY)((PUCHAR)MessageInfo + HeaderSize);
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

            Mode = (Descriptor->Flags & CM_RESOURCE_INTERRUPT_LATCHED) ?
                   Latched : LevelSensitive;
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

                InfoEntry->MessageAddress.QuadPart = 0xFEE00000 | ((ULONGLONG)ApicId << 12);
                InfoEntry->MessageData = Vector & 0xFF;
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
            PULONG ReleaseVectors = NULL;

            MessageInfo = Parameters->ConnectionContext.InterruptMessageTable;
            if (!MessageInfo)
                return;

            ReleaseVectors = ExAllocatePoolWithTag(PagedPool,
                                                   sizeof(ULONG) * MessageInfo->MessageCount,
                                                   TAG_IO_INTERRUPT);

            MessageEntries = IopGetMessageEntriesFromInfo(MessageInfo);
            for (i = 0; i < MessageInfo->MessageCount; i++)
            {
                if (ReleaseVectors)
                    ReleaseVectors[i] = MessageInfo->MessageInfo[i].Vector;

                if (MessageEntries[i].Interrupt.ServiceRoutine)
                    KeDisconnectInterrupt(&MessageEntries[i].Interrupt);
            }

            if (ReleaseVectors)
            {
                IopReleaseIrqVectors(ReleaseVectors, MessageInfo->MessageCount);
                ExFreePoolWithTag(ReleaseVectors, TAG_IO_INTERRUPT);
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
