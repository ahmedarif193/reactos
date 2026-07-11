/*
 * PROJECT:         ReactOS Kernel
 * COPYRIGHT:       GPL - See COPYING in the top level directory
 * FILE:            ntoskrnl/io/pnpmgr/pnpres.c
 * PURPOSE:         Resource handling code
 * PROGRAMMERS:     Cameron Gutman (cameron.gutman@reactos.org)
 *                  ReactOS Portable Systems Group
 */

#include <ntoskrnl.h>

#define NDEBUG
#include <debug.h>

#if defined(_M_AMD64) || defined(_M_ARM64)
NTHALAPI
NTSTATUS
NTAPI
HalpGetMessageRoutingInfo(
    _Inout_ PHAL_MESSAGE_ROUTING_INFO RoutingInfo);
#endif

/* Strict MSI/legacy vector-range separation on Intel APIC HAL.
 *
 * Legacy line IRQs map through HalpIrqToVector(irq) = irq +
 * PRIMARY_VECTOR_BASE. The IOAPIC exposes APIC_MAX_IRQ (= 24) GSIs,
 * so the only valid legacy window is [0x30 .. 0x47]. Anything above
 * that overflows into the HAL's MSI pool at 0x50-0xCF and breaks the
 * "MSI and line vectors are disjoint" invariant.
 *
 * To keep the two classes strictly disjoint we mirror the HAL-side
 * constants here and
 *   - clamp the generic arbiter's legacy fallback loop
 *     (IopFindInterruptResource) to bus interrupt levels < 24; and
 *   - strip any line (non-MESSAGE) interrupt descriptor planted by
 *     boot firmware whose vector already lives in the MSI range.
 *
 * These values MUST stay in sync with hal/halx86/apic/apicp.h
 * (APIC_MAX_IRQ) and hal/halx86/apic/msip.h (MSI_VECTOR_MIN).
 *
 * On the i386 PIC HAL the MSI pool does not exist; the clamp below
 * is a harmless superset. ARM64 uses the GIC (SPI/PPI/LPI) with a
 * completely different vector model and must provide its own
 * equivalent constants when MSI support lands there — do NOT reuse
 * these Intel numbers on arm64. */
#if defined(_M_IX86) || defined(_M_AMD64)
#define IOP_APIC_MAX_IRQ          24
#define IOP_APIC_MSI_VECTOR_MIN   0x50
#endif

/* Fixed GSIs in [IOP_APIC_MAX_IRQ, IOP_APIC_MAX_GSI) pass through to the
 * HAL spillover allocator; flexible ranges stay below IOP_APIC_MAX_IRQ. */
#if defined(_M_AMD64)
#include <reactos/hal/acpi_pci.h>
#define IOP_APIC_MAX_GSI          HAL_ACPI_MAX_GSI_PINS
#endif

static
BOOLEAN
IopCheckResourceDescriptor(IN PCM_PARTIAL_RESOURCE_DESCRIPTOR ResDesc, IN PCM_RESOURCE_LIST ResourceList, IN BOOLEAN Silent, OUT OPTIONAL PCM_PARTIAL_RESOURCE_DESCRIPTOR ConflictingDescriptor);

FORCEINLINE
PIO_RESOURCE_LIST
IopGetNextResourceList(
    _In_ const IO_RESOURCE_LIST *ResourceList)
{
    ASSERT((ResourceList->Count > 0) && (ResourceList->Count < 1000));
    return (PIO_RESOURCE_LIST)(
        &ResourceList->Descriptors[ResourceList->Count]);
}

#if defined(_M_IX86) || defined(_M_AMD64) || defined(_M_ARM64)
/* Consolidate interrupt descriptors in a device's CM_RESOURCE_LIST
 * so that only the interrupt class the device will actually use
 * survives. This runs after the PnP arbiter has picked an
 * alternative and before the list is handed to the driver.
 *
 * Two classes of garbage are removed:
 *
 *   1. On x86, ghost line IRQs planted by firmware with a Vector
 *      already inside the MSI pool (Vector >= IOP_APIC_MSI_VECTOR_MIN).
 *      A legal legacy IRQ must never land there; such descriptors
 *      are leftover artifacts from boot routing tables and confuse
 *      drivers that iterate the list.
 *
 *   2. Legacy line descriptors that coexist with an active MSI or
 *      MSI-X descriptor in the same partial list. When the arbiter
 *      grants MSI, the PCI bus driver sets PCI_COMMAND_INTX_DISABLE
 *      and the line IRQ is electrically disconnected from the
 *      interrupt controller. Leaving the legacy descriptor in the
 *      resource list makes Device Manager display the obsolete INTx line instead
 *      of the MSI vector, breaks any driver that iterates the list
 *      assuming only one interrupt per class, and produces resource
 *      ownership claims on an IRQ the device no longer uses.
 *      Windows removes the legacy entry in the same place; we
 *      match that behaviour by dropping it here after arbitration. */
static VOID
IopConsolidateInterruptDescriptors(
    _Inout_ PCM_RESOURCE_LIST ResourceList)
{
    ULONG FullIdx;
    PCM_FULL_RESOURCE_DESCRIPTOR FullDesc;

    if (ResourceList == NULL || ResourceList->Count == 0)
        return;

    FullDesc = &ResourceList->List[0];
    for (FullIdx = 0; FullIdx < ResourceList->Count; FullIdx++)
    {
        PCM_PARTIAL_RESOURCE_LIST Partial = &FullDesc->PartialResourceList;
        ULONG Src, Dst;
        BOOLEAN HasMessageInterrupt = FALSE;

        /* First pass: does this partial list contain an MSI / MSI-X
         * interrupt descriptor? */
        for (Src = 0; Src < Partial->Count; Src++)
        {
            PCM_PARTIAL_RESOURCE_DESCRIPTOR Desc = &Partial->PartialDescriptors[Src];

            if (Desc->Type == CmResourceTypeInterrupt &&
                (Desc->Flags & CM_RESOURCE_INTERRUPT_MESSAGE))
            {
                HasMessageInterrupt = TRUE;
                break;
            }
        }

        /* Second pass: compact out dead legacy descriptors. */
        Dst = 0;
        for (Src = 0; Src < Partial->Count; Src++)
        {
            PCM_PARTIAL_RESOURCE_DESCRIPTOR Desc = &Partial->PartialDescriptors[Src];
            BOOLEAN Drop = FALSE;

            if (Desc->Type == CmResourceTypeInterrupt &&
                !(Desc->Flags & CM_RESOURCE_INTERRUPT_MESSAGE))
            {
#if defined(_M_IX86) || defined(_M_AMD64)
                if (Desc->u.Interrupt.Vector >= IOP_APIC_MSI_VECTOR_MIN)
                {
                    DPRINT1("IopConsolidateInterruptDescriptors: dropping ghost "
                            "legacy interrupt vec=0x%x level=%lu "
                            "(firmware planted in MSI pool)\n",
                            Desc->u.Interrupt.Vector, Desc->u.Interrupt.Level);
                    Drop = TRUE;
                }
#endif
                if (!Drop && HasMessageInterrupt)
                {
                    DPRINT1("IopConsolidateInterruptDescriptors: dropping obsolete "
                            "legacy interrupt vec=0x%x level=%lu "
                            "(MSI/MSI-X is active, INTx disconnected)\n",
                            Desc->u.Interrupt.Vector, Desc->u.Interrupt.Level);
                    Drop = TRUE;
                }
            }

            if (!Drop)
            {
                if (Dst != Src)
                    Partial->PartialDescriptors[Dst] = *Desc;
                Dst++;
            }
        }
        Partial->Count = Dst;

        FullDesc = CmiGetNextResourceDescriptor(FullDesc);
    }
}
#endif

static
BOOLEAN
IopCheckDescriptorForConflict(PCM_PARTIAL_RESOURCE_DESCRIPTOR CmDesc, OPTIONAL PCM_RESOURCE_LIST PendingList, OPTIONAL PCM_PARTIAL_RESOURCE_DESCRIPTOR ConflictingDescriptor, _In_opt_ PDEVICE_NODE DeviceNode)
{
    /* Resource assignment must account for boot resources even when the
     * corresponding device has no driver and therefore never receives an
     * assigned-resource list. The owner-aware database tracks both kinds. */
    IopResDbEnsureSeeded();
    if (IopResDbCheckDescriptor(CmDesc, DeviceNode, ConflictingDescriptor))
        return TRUE;

    /* Also protect earlier descriptors in this allocation transaction. */
    if (PendingList && IopCheckResourceDescriptor(CmDesc, PendingList, TRUE, ConflictingDescriptor))
        return TRUE;

    return FALSE;
}

static
BOOLEAN
IopFindBusNumberResource(IN PIO_RESOURCE_DESCRIPTOR IoDesc, IN OPTIONAL PCM_RESOURCE_LIST PendingList, OUT PCM_PARTIAL_RESOURCE_DESCRIPTOR CmDesc, _In_opt_ PDEVICE_NODE DeviceNode)
{
    ULONG Start;
    CM_PARTIAL_RESOURCE_DESCRIPTOR ConflictingDesc;

    ASSERT(IoDesc->Type == CmDesc->Type);
    ASSERT(IoDesc->Type == CmResourceTypeBusNumber);

    if (IoDesc->u.BusNumber.Length == 0 ||
        IoDesc->u.BusNumber.MinBusNumber > IoDesc->u.BusNumber.MaxBusNumber ||
        IoDesc->u.BusNumber.Length - 1 >
            IoDesc->u.BusNumber.MaxBusNumber - IoDesc->u.BusNumber.MinBusNumber)
    {
        return FALSE;
    }

    for (Start = IoDesc->u.BusNumber.MinBusNumber;
         Start <= IoDesc->u.BusNumber.MaxBusNumber - IoDesc->u.BusNumber.Length + 1;
         Start++)
    {
        CmDesc->u.BusNumber.Length = IoDesc->u.BusNumber.Length;
        CmDesc->u.BusNumber.Start = Start;

        if (IopCheckDescriptorForConflict(CmDesc, PendingList, &ConflictingDesc, DeviceNode))
        {
            ULONG NextStart = ConflictingDesc.u.BusNumber.Start +
                              ConflictingDesc.u.BusNumber.Length;

            if (NextStart <= Start)
                return FALSE;

            /* The loop increment advances from NextStart - 1 to NextStart. */
            Start = NextStart - 1;
        }
        else
        {
            DPRINT1("Satisfying bus number requirement with 0x%x (length: 0x%x)\n", Start, CmDesc->u.BusNumber.Length);
            return TRUE;
        }
    }

    return FALSE;
}

static
BOOLEAN
IopFindMemoryResource(IN PIO_RESOURCE_DESCRIPTOR IoDesc, IN OPTIONAL PCM_RESOURCE_LIST PendingList, OUT PCM_PARTIAL_RESOURCE_DESCRIPTOR CmDesc, _In_opt_ PDEVICE_NODE DeviceNode)
{
    ULONGLONG Start;
    CM_PARTIAL_RESOURCE_DESCRIPTOR ConflictingDesc;

    ASSERT(IoDesc->Type == CmDesc->Type);
    ASSERT(IoDesc->Type == CmResourceTypeMemory);

    /* HACK */
    if (IoDesc->u.Memory.Alignment == 0)
        IoDesc->u.Memory.Alignment = 1;

    if (IoDesc->u.Memory.Length == 0 ||
        (ULONGLONG)IoDesc->u.Memory.MinimumAddress.QuadPart >
            (ULONGLONG)IoDesc->u.Memory.MaximumAddress.QuadPart ||
        IoDesc->u.Memory.Length - 1 >
            (ULONGLONG)IoDesc->u.Memory.MaximumAddress.QuadPart -
            (ULONGLONG)IoDesc->u.Memory.MinimumAddress.QuadPart)
    {
        return FALSE;
    }

    for (Start = (ULONGLONG)IoDesc->u.Memory.MinimumAddress.QuadPart;
         Start <= (ULONGLONG)IoDesc->u.Memory.MaximumAddress.QuadPart - IoDesc->u.Memory.Length + 1;
         Start += IoDesc->u.Memory.Alignment)
    {
        CmDesc->u.Memory.Length = IoDesc->u.Memory.Length;
        CmDesc->u.Memory.Start.QuadPart = (LONGLONG)Start;

        if (IopCheckDescriptorForConflict(CmDesc, PendingList, &ConflictingDesc, DeviceNode))
        {
            ULONGLONG Alignment = IoDesc->u.Memory.Alignment;
            ULONGLONG ConflictEnd =
                (ULONGLONG)ConflictingDesc.u.Memory.Start.QuadPart +
                ConflictingDesc.u.Memory.Length;
            ULONGLONG NextStart;

            if (ConflictEnd > MAXULONGLONG - (Alignment - 1))
                return FALSE;

            NextStart = ((ConflictEnd + Alignment - 1) / Alignment) * Alignment;
            if (NextStart <= Start || NextStart < Alignment)
                return FALSE;

            /* The loop increment advances from here to NextStart. */
            Start = NextStart - Alignment;
        }
        else
        {
            DPRINT1("Satisfying memory requirement with 0x%I64x (length: 0x%x)\n", Start, CmDesc->u.Memory.Length);
            return TRUE;
        }
    }

    return FALSE;
}

static
BOOLEAN
IopFindPortResource(IN PIO_RESOURCE_DESCRIPTOR IoDesc, IN OPTIONAL PCM_RESOURCE_LIST PendingList, OUT PCM_PARTIAL_RESOURCE_DESCRIPTOR CmDesc, _In_opt_ PDEVICE_NODE DeviceNode)
{
    ULONGLONG Start;
    CM_PARTIAL_RESOURCE_DESCRIPTOR ConflictingDesc;

    ASSERT(IoDesc->Type == CmDesc->Type);
    ASSERT(IoDesc->Type == CmResourceTypePort);

    /* HACK */
    if (IoDesc->u.Port.Alignment == 0)
        IoDesc->u.Port.Alignment = 1;

    if (IoDesc->u.Port.Length == 0 ||
        (ULONGLONG)IoDesc->u.Port.MinimumAddress.QuadPart >
            (ULONGLONG)IoDesc->u.Port.MaximumAddress.QuadPart ||
        IoDesc->u.Port.Length - 1 >
            (ULONGLONG)IoDesc->u.Port.MaximumAddress.QuadPart -
            (ULONGLONG)IoDesc->u.Port.MinimumAddress.QuadPart)
    {
        return FALSE;
    }

    for (Start = (ULONGLONG)IoDesc->u.Port.MinimumAddress.QuadPart;
         Start <= (ULONGLONG)IoDesc->u.Port.MaximumAddress.QuadPart - IoDesc->u.Port.Length + 1;
         Start += IoDesc->u.Port.Alignment)
    {
        CmDesc->u.Port.Length = IoDesc->u.Port.Length;
        CmDesc->u.Port.Start.QuadPart = (LONGLONG)Start;

        if (IopCheckDescriptorForConflict(CmDesc, PendingList, &ConflictingDesc, DeviceNode))
        {
            ULONGLONG Alignment = IoDesc->u.Port.Alignment;
            ULONGLONG ConflictEnd =
                (ULONGLONG)ConflictingDesc.u.Port.Start.QuadPart +
                ConflictingDesc.u.Port.Length;
            ULONGLONG NextStart;

            if (ConflictEnd > MAXULONGLONG - (Alignment - 1))
                return FALSE;

            NextStart = ((ConflictEnd + Alignment - 1) / Alignment) * Alignment;
            if (NextStart <= Start || NextStart < Alignment)
                return FALSE;

            /* The loop increment advances from here to NextStart. */
            Start = NextStart - Alignment;
        }
        else
        {
            DPRINT("Satisfying port requirement with 0x%I64x (length: 0x%x)\n", Start, CmDesc->u.Port.Length);
            return TRUE;
        }
    }

    DPRINT1("IopFindPortResource failed!\n");
    return FALSE;
}

static
BOOLEAN
IopFindDmaResource(IN PIO_RESOURCE_DESCRIPTOR IoDesc, IN OPTIONAL PCM_RESOURCE_LIST PendingList, OUT PCM_PARTIAL_RESOURCE_DESCRIPTOR CmDesc, _In_opt_ PDEVICE_NODE DeviceNode)
{
    ULONG Channel;

    ASSERT(IoDesc->Type == CmDesc->Type);
    ASSERT(IoDesc->Type == CmResourceTypeDma);

    for (Channel = IoDesc->u.Dma.MinimumChannel;
         Channel <= IoDesc->u.Dma.MaximumChannel;
         Channel++)
    {
        CmDesc->u.Dma.Channel = Channel;
        CmDesc->u.Dma.Port = 0;

        if (!IopCheckDescriptorForConflict(CmDesc, PendingList, NULL, DeviceNode))
        {
            DPRINT1("Satisfying DMA requirement with channel 0x%x\n", Channel);
            return TRUE;
        }
    }

    return FALSE;
}

static
BOOLEAN
IopFindInterruptResource(
    IN PIO_RESOURCE_DESCRIPTOR IoDesc,
    IN OPTIONAL PCM_RESOURCE_LIST PendingList,
    OUT PCM_PARTIAL_RESOURCE_DESCRIPTOR CmDesc,
    _In_opt_ PDEVICE_NODE DeviceNode,
    OUT OPTIONAL PULONG MessageCountOut)
{
    ULONG Vector;

    ASSERT(IoDesc->Type == CmDesc->Type);
    ASSERT(IoDesc->Type == CmResourceTypeInterrupt);

    if (MessageCountOut)
        *MessageCountOut = 1;

    DPRINT("IopFindInterruptResource flags=0x%x min=0x%x max=0x%x\n",
           IoDesc->Flags,
           IoDesc->u.Interrupt.MinimumVector, IoDesc->u.Interrupt.MaximumVector);

#if defined(_M_AMD64)
    /* MSI/MSI-X satisfaction path. Allocate a real message vector
     * via the HAL allocator and synthesize a
     * CM_RESOURCE_INTERRUPT_MESSAGE descriptor. The PCI driver's
     * start path will program the MSI capability registers from
     * the resulting descriptor. */
    {
        if (IoDesc->Flags & CM_RESOURCE_INTERRUPT_MESSAGE)
        {
            HAL_MESSAGE_ROUTING_INFO RoutingInfo;
            NTSTATUS RoutingStatus;

            RtlZeroMemory(&RoutingInfo, sizeof(RoutingInfo));
            RoutingInfo.Version = HAL_MESSAGE_ROUTING_INFO_VERSION;
            RoutingInfo.Flags = HAL_MSI_ROUTING_ALLOCATE_VECTOR;
            RoutingInfo.DesiredIrql = CLOCK_LEVEL - 1;
            RoutingInfo.MessageCount = 1;

            RoutingStatus = HalpGetMessageRoutingInfo(&RoutingInfo);
            if (NT_SUCCESS(RoutingStatus))
            {
                CmDesc->Flags = IoDesc->Flags;
                CmDesc->ShareDisposition = IoDesc->ShareDisposition;
                CmDesc->u.Interrupt.Vector   = RoutingInfo.Vector;
                CmDesc->u.Interrupt.Level    = RoutingInfo.Irql;
                CmDesc->u.Interrupt.Affinity = RoutingInfo.TargetProcessors;
                DPRINT1("MSI: allocated vector 0x%02x at irql %u\n",
                        RoutingInfo.Vector, RoutingInfo.Irql);
                return TRUE;
            }

            DPRINT1("MSI: HalGetMessageRoutingInfo failed 0x%lx, falling back to legacy alternative\n",
                    RoutingStatus);
            return FALSE;
        }
    }
#endif /* defined(_M_AMD64) */

#if defined(_M_ARM64)
    if (IoDesc->Flags & CM_RESOURCE_INTERRUPT_MESSAGE)
    {
        HAL_MESSAGE_ROUTING_INFO RoutingInfo;
        NTSTATUS RoutingStatus;
        ULONG MessageCount;
        ULONG AllocCount;

        /*
         * Message-interrupt requirements encode the requested message count
         * as MinimumVector == MaximumVector == N (see the PCI PDO's
         * PdoQueryResourceRequirements).  Grant the whole group: the HAL
         * allocator hands out a naturally-aligned contiguous power-of-two
         * block of GICv2m SPIs / ITS LPIs, and the caller emits one
         * CM_RESOURCE_INTERRUPT_MESSAGE descriptor per message so
         * IoConnectInterruptEx builds a real per-message ISR for each.
         */
        MessageCount = 1;
        if (IoDesc->u.Interrupt.MinimumVector == IoDesc->u.Interrupt.MaximumVector &&
            IoDesc->u.Interrupt.MaximumVector >= 1 &&
            IoDesc->u.Interrupt.MaximumVector <= 512)
        {
            MessageCount = IoDesc->u.Interrupt.MaximumVector;
        }

        AllocCount = 1;
        while (AllocCount < MessageCount)
            AllocCount <<= 1;

        RtlZeroMemory(&RoutingInfo, sizeof(RoutingInfo));
        RoutingInfo.Version = HAL_MESSAGE_ROUTING_INFO_VERSION;
        RoutingInfo.Flags = HAL_MSI_ROUTING_ALLOCATE_VECTOR;
        RoutingInfo.MessageCount = AllocCount;

        RoutingStatus = HalpGetMessageRoutingInfo(&RoutingInfo);
        if (!NT_SUCCESS(RoutingStatus) && AllocCount > 1)
        {
            /* Pool exhausted/fragmented: one shared vector still delivers
             * (ISR-side demux) — prefer that over failing the device. */
            DPRINT1("MSI: ARM64 %lu-message block failed 0x%lx — retrying single\n",
                    AllocCount, RoutingStatus);
            MessageCount = 1;
            RtlZeroMemory(&RoutingInfo, sizeof(RoutingInfo));
            RoutingInfo.Version = HAL_MESSAGE_ROUTING_INFO_VERSION;
            RoutingInfo.Flags = HAL_MSI_ROUTING_ALLOCATE_VECTOR;
            RoutingInfo.MessageCount = 1;
            RoutingStatus = HalpGetMessageRoutingInfo(&RoutingInfo);
        }
        if (!NT_SUCCESS(RoutingStatus))
        {
            DPRINT1("MSI: ARM64 HalpGetMessageRoutingInfo failed 0x%lx\n",
                    RoutingStatus);
            return FALSE;
        }

        CmDesc->Flags = IoDesc->Flags;
        CmDesc->ShareDisposition = IoDesc->ShareDisposition;
        CmDesc->u.Interrupt.Level = RoutingInfo.Irql;
        CmDesc->u.Interrupt.Vector = RoutingInfo.Vector;
        CmDesc->u.Interrupt.Affinity = RoutingInfo.TargetProcessors;
        if (MessageCountOut)
            *MessageCountOut = MessageCount;
        DPRINT1("MSI: allocated ARM64 message block base 0x%lx count %lu at irql %u affinity 0x%Ix\n",
                RoutingInfo.Vector,
                MessageCount,
                RoutingInfo.Irql,
                RoutingInfo.TargetProcessors);
        return TRUE;
    }
#endif /* defined(_M_ARM64) */

    {
        ULONG LegacyMin = IoDesc->u.Interrupt.MinimumVector;
        ULONG LegacyMax = IoDesc->u.Interrupt.MaximumVector;

#if defined(_M_AMD64)
        /* Fixed requirements (min == max) are firmware-assigned GSIs: let
         * them through to the pin bound and let translation decide.
         * Flexible ranges stay clamped below the fixed line window. */
        if (LegacyMin == LegacyMax)
        {
            if (LegacyMin >= IOP_APIC_MAX_GSI)
            {
                DPRINT1("IopFindInterruptResource: fixed GSI %lu past IOAPIC range\n", LegacyMin);
                return FALSE;
            }
        }
        else
        {
            if (LegacyMax >= IOP_APIC_MAX_IRQ)
                LegacyMax = IOP_APIC_MAX_IRQ - 1;
            if (LegacyMin >= IOP_APIC_MAX_IRQ)
            {
                DPRINT1("IopFindInterruptResource: legacy min %lu past IOAPIC range\n", LegacyMin);
                return FALSE;
            }
        }
#elif defined(_M_IX86)
        /* The i386 APIC HAL keeps the fixed 24-line window */
        if (LegacyMax >= IOP_APIC_MAX_IRQ)
            LegacyMax = IOP_APIC_MAX_IRQ - 1;
        if (LegacyMin >= IOP_APIC_MAX_IRQ)
        {
            DPRINT1("IopFindInterruptResource: legacy min %lu past IOAPIC range\n",
                    LegacyMin);
            return FALSE;
        }
#endif

        for (Vector = LegacyMin; Vector <= LegacyMax; Vector++)
        {
            CmDesc->u.Interrupt.Vector = Vector;
            CmDesc->u.Interrupt.Level = Vector;
            CmDesc->u.Interrupt.Affinity = (KAFFINITY)-1;

            if (!IopCheckDescriptorForConflict(CmDesc, PendingList, NULL, DeviceNode))
            {
                DPRINT1("Satisfying interrupt requirement with IRQ 0x%x\n", Vector);
                return TRUE;
            }
        }
    }

    DPRINT1("Failed to satisfy interrupt requirement with IRQ 0x%x-0x%x\n",
            IoDesc->u.Interrupt.MinimumVector,
            IoDesc->u.Interrupt.MaximumVector);
    return FALSE;
}

NTSTATUS NTAPI
IopFixupResourceListWithRequirements(IN PIO_RESOURCE_REQUIREMENTS_LIST RequirementsList, OUT PCM_RESOURCE_LIST *ResourceList, _In_opt_ PDEVICE_NODE DeviceNode)
{
    ULONG i, OldCount;
    BOOLEAN AlternateRequired = FALSE;
    PIO_RESOURCE_LIST ResList;

    /* Save the initial resource count when we got here so we can restore if an alternate fails */
    if (*ResourceList != NULL)
        OldCount = (*ResourceList)->List[0].PartialResourceList.Count;
    else
        OldCount = 0;

    ResList = &RequirementsList->List[0];
    for (i = 0; i < RequirementsList->AlternativeLists; i++, ResList = IopGetNextResourceList(ResList))
    {
        ULONG ii;

        /* We need to get back to where we were before processing the last alternative list */
        if (OldCount == 0 && *ResourceList != NULL)
        {
            /* Just free it and kill the pointer */
            ExFreePool(*ResourceList);
            *ResourceList = NULL;
        }
        else if (OldCount != 0)
        {
            PCM_RESOURCE_LIST NewList;

            /* Let's resize it */
            (*ResourceList)->List[0].PartialResourceList.Count = OldCount;

            /* Allocate the new smaller list */
            NewList = ExAllocatePool(PagedPool, PnpDetermineResourceListSize(*ResourceList));
            if (!NewList)
                return STATUS_NO_MEMORY;

            /* Copy the old stuff back */
            RtlCopyMemory(NewList, *ResourceList, PnpDetermineResourceListSize(*ResourceList));

            /* Free the old one */
            ExFreePool(*ResourceList);

            /* Store the pointer to the new one */
            *ResourceList = NewList;
        }

        for (ii = 0; ii < ResList->Count; ii++)
        {
            ULONG iii;
            PCM_PARTIAL_RESOURCE_LIST PartialList = (*ResourceList) ? &(*ResourceList)->List[0].PartialResourceList : NULL;
            PIO_RESOURCE_DESCRIPTOR IoDesc = &ResList->Descriptors[ii];
            BOOLEAN Matched = FALSE;

            /* Skip alternates if we don't need one */
            if (!AlternateRequired && (IoDesc->Option & IO_RESOURCE_ALTERNATIVE))
            {
                DPRINT("Skipping unneeded alternate\n");
                continue;
            }

            /* Check if we couldn't satsify a requirement or its alternates */
            if (AlternateRequired && !(IoDesc->Option & IO_RESOURCE_ALTERNATIVE))
            {
                DPRINT1("Unable to satisfy preferred resource or alternates in list %lu\n", i);

                /* Break out of this loop and try the next list */
                break;
            }

            for (iii = 0; PartialList && iii < PartialList->Count && !Matched; iii++)
            {
                /* Partial resource descriptors can be of variable size (CmResourceTypeDeviceSpecific),
                   but only one is allowed and it must be the last one in the list! */
                PCM_PARTIAL_RESOURCE_DESCRIPTOR CmDesc = &PartialList->PartialDescriptors[iii];

                /* First check types */
                if (IoDesc->Type != CmDesc->Type)
                    continue;

                switch (IoDesc->Type)
                {
                    case CmResourceTypeInterrupt:
                        /* A CM_RESOURCE_INTERRUPT_MESSAGE (MSI/MSI-X)
                         * requirement uses MinimumVector and
                         * MaximumVector to express the message count,
                         * not a global vector range. The actual
                         * allocated vector lives in a distinct pool
                         * from line IRQs. Match by the MESSAGE flag
                         * rather than the value range. */
                        if ((IoDesc->Flags & CM_RESOURCE_INTERRUPT_MESSAGE) &&
                            (CmDesc->Flags & CM_RESOURCE_INTERRUPT_MESSAGE))
                        {
                            Matched = TRUE;
                        }
                        else if (!(IoDesc->Flags & CM_RESOURCE_INTERRUPT_MESSAGE) &&
                                 !(CmDesc->Flags & CM_RESOURCE_INTERRUPT_MESSAGE) &&
                                 CmDesc->u.Interrupt.Vector >= IoDesc->u.Interrupt.MinimumVector &&
                                 CmDesc->u.Interrupt.Vector <= IoDesc->u.Interrupt.MaximumVector)
                        {
                            /* Legacy interrupt match — vector in requested range. */
                            Matched = TRUE;
                        }
                        else
                        {
                            DPRINT("Interrupt - Not a match! 0x%x flags=0x%x not inside 0x%x-0x%x flags=0x%x\n",
                                   CmDesc->u.Interrupt.Vector, CmDesc->Flags,
                                   IoDesc->u.Interrupt.MinimumVector,
                                   IoDesc->u.Interrupt.MaximumVector,
                                   IoDesc->Flags);
                        }
                        break;

                    case CmResourceTypeMemory:
                    case CmResourceTypePort:
                        /* Make sure the length matches and it satisfies our address range */
                        if (CmDesc->u.Memory.Length == IoDesc->u.Memory.Length &&
                            (ULONGLONG)CmDesc->u.Memory.Start.QuadPart >= (ULONGLONG)IoDesc->u.Memory.MinimumAddress.QuadPart &&
                            (ULONGLONG)CmDesc->u.Memory.Start.QuadPart + CmDesc->u.Memory.Length - 1 <= (ULONGLONG)IoDesc->u.Memory.MaximumAddress.QuadPart)
                        {
                            /* Found it */
                            Matched = TRUE;
                        }
                        else
                        {
                            DPRINT("Memory/Port - Not a match! 0x%I64x with length 0x%x not inside 0x%I64x to 0x%I64x with length 0x%x\n",
                                   CmDesc->u.Memory.Start.QuadPart,
                                   CmDesc->u.Memory.Length,
                                   IoDesc->u.Memory.MinimumAddress.QuadPart,
                                   IoDesc->u.Memory.MaximumAddress.QuadPart,
                                   IoDesc->u.Memory.Length);
                        }
                        break;

                    case CmResourceTypeBusNumber:
                        /* Make sure the length matches and it satisfies our bus number range */
                        if (CmDesc->u.BusNumber.Length == IoDesc->u.BusNumber.Length &&
                            CmDesc->u.BusNumber.Start >= IoDesc->u.BusNumber.MinBusNumber &&
                            CmDesc->u.BusNumber.Start + CmDesc->u.BusNumber.Length - 1 <= IoDesc->u.BusNumber.MaxBusNumber)
                        {
                            /* Found it */
                            Matched = TRUE;
                        }
                        else
                        {
                            DPRINT("Bus Number - Not a match! 0x%x with length 0x%x not inside 0x%x to 0x%x with length 0x%x\n",
                                   CmDesc->u.BusNumber.Start,
                                   CmDesc->u.BusNumber.Length,
                                   IoDesc->u.BusNumber.MinBusNumber,
                                   IoDesc->u.BusNumber.MaxBusNumber,
                                   IoDesc->u.BusNumber.Length);
                        }
                        break;

                    case CmResourceTypeDma:
                        /* Make sure it fits in our channel range */
                        if (CmDesc->u.Dma.Channel >= IoDesc->u.Dma.MinimumChannel &&
                            CmDesc->u.Dma.Channel <= IoDesc->u.Dma.MaximumChannel)
                        {
                            /* Found it */
                            Matched = TRUE;
                        }
                        else
                        {
                            DPRINT("DMA - Not a match! 0x%x not inside 0x%x to 0x%x\n",
                                   CmDesc->u.Dma.Channel,
                                   IoDesc->u.Dma.MinimumChannel,
                                   IoDesc->u.Dma.MaximumChannel);
                        }
                        break;

                    default:
                        /* Other stuff is fine */
                        Matched = TRUE;
                        break;
                }
            }

            /* Check if we found a matching descriptor */
            if (!Matched)
            {
                PCM_RESOURCE_LIST NewList;
                CM_PARTIAL_RESOURCE_DESCRIPTOR NewDesc;
                PCM_PARTIAL_RESOURCE_DESCRIPTOR DescPtr;
                BOOLEAN FoundResource = TRUE;
                ULONG MessageCount = 1;
                ULONG Message;

                /* Setup the new CM descriptor */
                NewDesc.Type = IoDesc->Type;
                NewDesc.Flags = IoDesc->Flags;
                NewDesc.ShareDisposition = IoDesc->ShareDisposition;

                /* Let'se see if we can find a resource to satisfy this */
                switch (IoDesc->Type)
                {
                    case CmResourceTypeInterrupt:
                        /* Find an available interrupt */
                        if (!IopFindInterruptResource(IoDesc,
                                                      *ResourceList,
                                                      &NewDesc,
                                                      DeviceNode,
                                                      &MessageCount))
                        {
                            DPRINT1("Failed to find an available interrupt resource (0x%x to 0x%x)\n",
                                    IoDesc->u.Interrupt.MinimumVector, IoDesc->u.Interrupt.MaximumVector);

                            FoundResource = FALSE;
                        }
                        break;

                    case CmResourceTypePort:
                        /* Find an available port range */
                        if (!IopFindPortResource(IoDesc, *ResourceList, &NewDesc, DeviceNode))
                        {
                            DPRINT1("Failed to find an available port resource (0x%I64x to 0x%I64x length: 0x%x)\n",
                                    IoDesc->u.Port.MinimumAddress.QuadPart, IoDesc->u.Port.MaximumAddress.QuadPart,
                                    IoDesc->u.Port.Length);

                            FoundResource = FALSE;
                        }
                        break;

                    case CmResourceTypeMemory:
                        /* Find an available memory range */
                        if (!IopFindMemoryResource(IoDesc, *ResourceList, &NewDesc, DeviceNode))
                        {
                            DPRINT1("Failed to find an available memory resource (0x%I64x to 0x%I64x length: 0x%x)\n",
                                    IoDesc->u.Memory.MinimumAddress.QuadPart, IoDesc->u.Memory.MaximumAddress.QuadPart,
                                    IoDesc->u.Memory.Length);

                            FoundResource = FALSE;
                        }
                        break;

                    case CmResourceTypeBusNumber:
                        /* Find an available bus address range */
                        if (!IopFindBusNumberResource(IoDesc, *ResourceList, &NewDesc, DeviceNode))
                        {
                            DPRINT1("Failed to find an available bus number resource (0x%x to 0x%x length: 0x%x)\n",
                                    IoDesc->u.BusNumber.MinBusNumber, IoDesc->u.BusNumber.MaxBusNumber,
                                    IoDesc->u.BusNumber.Length);

                            FoundResource = FALSE;
                        }
                        break;

                    case CmResourceTypeDma:
                        /* Find an available DMA channel */
                        if (!IopFindDmaResource(IoDesc, *ResourceList, &NewDesc, DeviceNode))
                        {
                            DPRINT1("Failed to find an available dma resource (0x%x to 0x%x)\n",
                                    IoDesc->u.Dma.MinimumChannel, IoDesc->u.Dma.MaximumChannel);

                            FoundResource = FALSE;
                        }
                        break;

                    default:
                        DPRINT1("Unsupported resource type: %x\n", IoDesc->Type);
                        FoundResource = FALSE;
                        break;
                }

                /* Check if it's missing and required */
                if (!FoundResource && IoDesc->Option == 0)
                {
                    /* Break out of this loop and try the next list */
                    DPRINT1("Unable to satisfy required resource in list %lu\n", i);
                    break;
                }
                else if (!FoundResource)
                {
                    /* Try an alternate for this preferred descriptor */
                    AlternateRequired = TRUE;
                    continue;
                }
                else
                {
                    /* Move on to the next preferred or required descriptor after this one */
                    AlternateRequired = FALSE;
                }

                /* Append one CM descriptor per granted message (legacy and
                 * single-message grants append exactly one). */
                for (Message = 0; Message < MessageCount; Message++)
                {
                    if (Message != 0)
                    {
                        NewDesc.u.Interrupt.Vector++;
                        PartialList = &(*ResourceList)->List[0].PartialResourceList;
                    }

                    /* Figure out what we need */
                    if (PartialList == NULL)
                    {
                        /* We need a new list */
                        NewList = ExAllocatePool(PagedPool, sizeof(CM_RESOURCE_LIST));
                        if (!NewList)
                            return STATUS_NO_MEMORY;

                        /* Set it up */
                        NewList->Count = 1;
                        NewList->List[0].InterfaceType = RequirementsList->InterfaceType;
                        NewList->List[0].BusNumber = RequirementsList->BusNumber;
                        NewList->List[0].PartialResourceList.Version = 1;
                        NewList->List[0].PartialResourceList.Revision = 1;
                        NewList->List[0].PartialResourceList.Count = 1;

                        /* Set our pointer */
                        DescPtr = &NewList->List[0].PartialResourceList.PartialDescriptors[0];
                    }
                    else
                    {
                        /* Allocate the new larger list */
                        NewList = ExAllocatePool(PagedPool, PnpDetermineResourceListSize(*ResourceList) + sizeof(CM_PARTIAL_RESOURCE_DESCRIPTOR));
                        if (!NewList)
                            return STATUS_NO_MEMORY;

                        /* Copy the old stuff back */
                        RtlCopyMemory(NewList, *ResourceList, PnpDetermineResourceListSize(*ResourceList));

                        /* Set our pointer */
                        DescPtr = &NewList->List[0].PartialResourceList.PartialDescriptors[NewList->List[0].PartialResourceList.Count];

                        /* Increment the descriptor count */
                        NewList->List[0].PartialResourceList.Count++;

                        /* Free the old list */
                        ExFreePool(*ResourceList);
                    }

                    /* Copy the descriptor in */
                    *DescPtr = NewDesc;

                    /* Store the new list */
                    *ResourceList = NewList;
                }
            }
        }

        /* Check if we need an alternate with no resources left */
        if (AlternateRequired)
        {
            DPRINT1("Unable to satisfy preferred resource or alternates in list %lu\n", i);

            /* Try the next alternate list */
            continue;
        }

        /* We're done because we satisfied one of the alternate lists */
        return STATUS_SUCCESS;
    }

    /* We ran out of alternates */
    DPRINT1("Out of alternate lists!\n");

    /* Free the list */
    if (*ResourceList)
    {
        ExFreePool(*ResourceList);
        *ResourceList = NULL;
    }

    /* Fail */
    return STATUS_CONFLICTING_ADDRESSES;
}

static
BOOLEAN
IopCheckResourceDescriptor(IN PCM_PARTIAL_RESOURCE_DESCRIPTOR ResDesc, IN PCM_RESOURCE_LIST ResourceList, IN BOOLEAN Silent, OUT OPTIONAL PCM_PARTIAL_RESOURCE_DESCRIPTOR ConflictingDescriptor)
{
    ULONG i, ii;
    BOOLEAN Result = FALSE;
    PCM_FULL_RESOURCE_DESCRIPTOR FullDescriptor;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR ResDesc2;

    FullDescriptor = &ResourceList->List[0];
    for (i = 0; i < ResourceList->Count; i++)
    {
        PCM_PARTIAL_RESOURCE_LIST ResList = &FullDescriptor->PartialResourceList;
        FullDescriptor = CmiGetNextResourceDescriptor(FullDescriptor);

        ResDesc2 = &ResList->PartialDescriptors[0];
        for (ii = 0; ii < ResList->Count; ii++, ResDesc2 = CmiGetNextPartialDescriptor(ResDesc2))
        {
            /* Only identical descriptor storage is a self-comparison. Two
             * separately stored descriptors with the same range represent
             * two claims and must conflict unless they are shareable. */
            if (ResDesc == ResDesc2)
                continue;

            /* We don't care about shared resources */
            if (ResDesc->ShareDisposition == CmResourceShareShared &&
                ResDesc2->ShareDisposition == CmResourceShareShared)
                continue;

            /* Make sure we're comparing the same types */
            if (ResDesc->Type != ResDesc2->Type)
                continue;

            switch (ResDesc->Type)
            {
                case CmResourceTypeMemory:
                {
                    /* NOTE: ranges are in a form [x1;x2) */
                    UINT64 rStart = (UINT64)ResDesc->u.Memory.Start.QuadPart;
                    UINT64 rEnd = (UINT64)ResDesc->u.Memory.Start.QuadPart
                                  + ResDesc->u.Memory.Length;
                    UINT64 r2Start = (UINT64)ResDesc2->u.Memory.Start.QuadPart;
                    UINT64 r2End = (UINT64)ResDesc2->u.Memory.Start.QuadPart
                                   + ResDesc2->u.Memory.Length;

                    if (rStart < r2End && r2Start < rEnd)
                    {
                        if (((ResDesc->Flags & CM_RESOURCE_MEMORY_WINDOW_DECODE) &&
                             rStart <= r2Start && rEnd >= r2End) ||
                            ((ResDesc2->Flags & CM_RESOURCE_MEMORY_WINDOW_DECODE) &&
                             r2Start <= rStart && r2End >= rEnd))
                        {
                            break;
                        }

                        if (!Silent)
                        {
                            DPRINT1("Resource conflict: Memory (0x%I64x to 0x%I64x vs. 0x%I64x to 0x%I64x)\n",
                                    rStart, rEnd, r2Start, r2End);
                        }

                        Result = TRUE;

                        goto ByeBye;
                    }
                    break;
                }
                case CmResourceTypePort:
                {
                    /* NOTE: ranges are in a form [x1;x2) */
                    UINT64 rStart = (UINT64)ResDesc->u.Port.Start.QuadPart;
                    UINT64 rEnd = (UINT64)ResDesc->u.Port.Start.QuadPart
                                  + ResDesc->u.Port.Length;
                    UINT64 r2Start = (UINT64)ResDesc2->u.Port.Start.QuadPart;
                    UINT64 r2End = (UINT64)ResDesc2->u.Port.Start.QuadPart
                                   + ResDesc2->u.Port.Length;

                    if (rStart < r2End && r2Start < rEnd)
                    {
                        if (((ResDesc->Flags & CM_RESOURCE_PORT_WINDOW_DECODE) &&
                             rStart <= r2Start && rEnd >= r2End) ||
                            ((ResDesc2->Flags & CM_RESOURCE_PORT_WINDOW_DECODE) &&
                             r2Start <= rStart && r2End >= rEnd))
                        {
                            break;
                        }

                        if (!Silent)
                        {
                            DPRINT1("Resource conflict: Port (0x%I64x to 0x%I64x vs. 0x%I64x to 0x%I64x)\n",
                                    rStart, rEnd, r2Start, r2End);
                        }

                        Result = TRUE;

                        goto ByeBye;
                    }
                    break;
                }
                case CmResourceTypeInterrupt:
                {
                    if (ResDesc->u.Interrupt.Vector == ResDesc2->u.Interrupt.Vector)
                    {
                        if (!Silent)
                        {
                            DPRINT1("Resource conflict: IRQ (0x%x 0x%x vs. 0x%x 0x%x)\n",
                                    ResDesc->u.Interrupt.Vector, ResDesc->u.Interrupt.Level,
                                    ResDesc2->u.Interrupt.Vector, ResDesc2->u.Interrupt.Level);
                        }

                        Result = TRUE;

                        goto ByeBye;
                    }
                    break;
                }
                case CmResourceTypeBusNumber:
                {
                    /* NOTE: ranges are in a form [x1;x2) */
                    UINT32 rStart = ResDesc->u.BusNumber.Start;
                    UINT32 rEnd = ResDesc->u.BusNumber.Start + ResDesc->u.BusNumber.Length;
                    UINT32 r2Start = ResDesc2->u.BusNumber.Start;
                    UINT32 r2End = ResDesc2->u.BusNumber.Start + ResDesc2->u.BusNumber.Length;

                    if (rStart < r2End && r2Start < rEnd)
                    {
                        /*
                         * A PCI root bridge owns a bus-number window that
                         * contains the ranges assigned to its downstream
                         * bridges.  Those parent/child ranges are not a
                         * conflict.  Preserve detection of equal or partially
                         * overlapping sibling ranges while accepting strict
                         * containment in either direction.
                         */
                        if ((ResDesc->ShareDisposition == CmResourceShareShared &&
                             rStart < r2Start && rEnd >= r2End) ||
                            (ResDesc2->ShareDisposition == CmResourceShareShared &&
                             r2Start < rStart && r2End >= rEnd))
                        {
                            break;
                        }

                        if (!Silent)
                        {
                            DPRINT1("Resource conflict: Bus number (0x%x to 0x%x vs. 0x%x to 0x%x)\n",
                                    rStart, rEnd, r2Start, r2End);
                        }

                        Result = TRUE;

                        goto ByeBye;
                    }
                    break;
                }
                case CmResourceTypeDma:
                {
                    if (ResDesc->u.Dma.Channel == ResDesc2->u.Dma.Channel)
                    {
                        if (!Silent)
                        {
                            DPRINT1("Resource conflict: Dma (0x%x 0x%x vs. 0x%x 0x%x)\n",
                                    ResDesc->u.Dma.Channel, ResDesc->u.Dma.Port,
                                    ResDesc2->u.Dma.Channel, ResDesc2->u.Dma.Port);
                        }

                        Result = TRUE;

                        goto ByeBye;
                    }
                    break;
                }
            }
        }
    }

ByeBye:

    if (Result && ConflictingDescriptor)
    {
        RtlCopyMemory(ConflictingDescriptor,
                      ResDesc2,
                      sizeof(CM_PARTIAL_RESOURCE_DESCRIPTOR));
    }

    // Hacked, because after fixing resource list parsing
    // we actually detect resource conflicts
    return Silent ? Result : FALSE; // Result;
}

static
NTSTATUS
IopUpdateControlKeyWithResources(
    IN PDEVICE_NODE DeviceNode)
{
    UNICODE_STRING EnumRoot = RTL_CONSTANT_STRING(ENUM_ROOT);
    UNICODE_STRING Control = RTL_CONSTANT_STRING(L"Control");
    UNICODE_STRING ValueName = RTL_CONSTANT_STRING(L"AllocConfig");
    HANDLE EnumKey, InstanceKey, ControlKey;
    NTSTATUS Status;
    OBJECT_ATTRIBUTES ObjectAttributes;

    /* Open the Enum key */
    Status = IopOpenRegistryKeyEx(&EnumKey, NULL, &EnumRoot, KEY_ENUMERATE_SUB_KEYS);
    if (!NT_SUCCESS(Status))
        return Status;

    /* Open the instance key (eg. Root\PNP0A03) */
    Status = IopOpenRegistryKeyEx(&InstanceKey, EnumKey, &DeviceNode->InstancePath, KEY_ENUMERATE_SUB_KEYS);
    ZwClose(EnumKey);

    if (!NT_SUCCESS(Status))
        return Status;

    /* Create/Open the Control key */
    InitializeObjectAttributes(&ObjectAttributes,
                               &Control,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               InstanceKey,
                               NULL);
    Status = ZwCreateKey(&ControlKey,
                         KEY_SET_VALUE,
                         &ObjectAttributes,
                         0,
                         NULL,
                         REG_OPTION_VOLATILE,
                         NULL);
    ZwClose(InstanceKey);

    if (!NT_SUCCESS(Status))
        return Status;

    /* Write the resource list */
    Status = ZwSetValueKey(ControlKey,
                           &ValueName,
                           0,
                           REG_RESOURCE_LIST,
                           DeviceNode->ResourceList,
                           PnpDetermineResourceListSize(DeviceNode->ResourceList));
    ZwClose(ControlKey);

    if (!NT_SUCCESS(Status))
        return Status;

    return STATUS_SUCCESS;
}

static
NTSTATUS
IopFilterResourceRequirements(
    IN PDEVICE_NODE DeviceNode)
{
    IO_STACK_LOCATION Stack;
    IO_STATUS_BLOCK IoStatusBlock;
    NTSTATUS Status;

    DPRINT("Sending IRP_MN_FILTER_RESOURCE_REQUIREMENTS to device stack\n");

    Stack.Parameters.FilterResourceRequirements.IoResourceRequirementList = DeviceNode->ResourceRequirements;
    Status = IopInitiatePnpIrp(DeviceNode->PhysicalDeviceObject,
                               &IoStatusBlock,
                               IRP_MN_FILTER_RESOURCE_REQUIREMENTS,
                               &Stack);
    if (!NT_SUCCESS(Status) && Status != STATUS_NOT_SUPPORTED)
    {
        DPRINT1("IopInitiatePnpIrp(IRP_MN_FILTER_RESOURCE_REQUIREMENTS) failed\n");
        return Status;
    }
    else if (NT_SUCCESS(Status) && IoStatusBlock.Information)
    {
        DeviceNode->ResourceRequirements = (PIO_RESOURCE_REQUIREMENTS_LIST)IoStatusBlock.Information;
    }

    return STATUS_SUCCESS;
}


NTSTATUS
IopUpdateResourceMap(
    IN PDEVICE_NODE DeviceNode,
    PWCHAR Level1Key,
    PWCHAR Level2Key)
{
    NTSTATUS Status;
    ULONG Disposition;
    HANDLE PnpMgrLevel1, PnpMgrLevel2, ResourceMapKey;
    UNICODE_STRING KeyName;
    OBJECT_ATTRIBUTES ObjectAttributes;

    RtlInitUnicodeString(&KeyName,
                         L"\\Registry\\Machine\\HARDWARE\\RESOURCEMAP");
    InitializeObjectAttributes(&ObjectAttributes,
                               &KeyName,
                               OBJ_CASE_INSENSITIVE | OBJ_OPENIF | OBJ_KERNEL_HANDLE,
                               NULL,
                               NULL);
    Status = ZwCreateKey(&ResourceMapKey,
                         KEY_ALL_ACCESS,
                         &ObjectAttributes,
                         0,
                         NULL,
                         REG_OPTION_VOLATILE,
                         &Disposition);
    if (!NT_SUCCESS(Status))
        return Status;

    RtlInitUnicodeString(&KeyName, Level1Key);
    InitializeObjectAttributes(&ObjectAttributes,
                               &KeyName,
                               OBJ_CASE_INSENSITIVE | OBJ_OPENIF | OBJ_KERNEL_HANDLE,
                               ResourceMapKey,
                               NULL);
    Status = ZwCreateKey(&PnpMgrLevel1,
                         KEY_ALL_ACCESS,
                         &ObjectAttributes,
                         0,
                         NULL,
                         REG_OPTION_VOLATILE,
                         &Disposition);
    ZwClose(ResourceMapKey);
    if (!NT_SUCCESS(Status))
        return Status;

    RtlInitUnicodeString(&KeyName, Level2Key);
    InitializeObjectAttributes(&ObjectAttributes,
                               &KeyName,
                               OBJ_CASE_INSENSITIVE | OBJ_OPENIF | OBJ_KERNEL_HANDLE,
                               PnpMgrLevel1,
                               NULL);
    Status = ZwCreateKey(&PnpMgrLevel2,
                         KEY_ALL_ACCESS,
                         &ObjectAttributes,
                         0,
                         NULL,
                         REG_OPTION_VOLATILE,
                         &Disposition);
    ZwClose(PnpMgrLevel1);
    if (!NT_SUCCESS(Status))
        return Status;

    if (DeviceNode->ResourceList)
    {
        UNICODE_STRING NameU;
        UNICODE_STRING RawSuffix, TranslatedSuffix;
        ULONG OldLength = 0;

        ASSERT(DeviceNode->ResourceListTranslated);

        RtlInitUnicodeString(&TranslatedSuffix, L".Translated");
        RtlInitUnicodeString(&RawSuffix, L".Raw");

        Status = IoGetDeviceProperty(DeviceNode->PhysicalDeviceObject,
                                     DevicePropertyPhysicalDeviceObjectName,
                                     0,
                                     NULL,
                                     &OldLength);
        if (Status == STATUS_BUFFER_OVERFLOW || Status == STATUS_BUFFER_TOO_SMALL)
        {
            ASSERT(OldLength);

            NameU.Buffer = ExAllocatePool(PagedPool, OldLength + TranslatedSuffix.Length);
            if (!NameU.Buffer)
            {
                ZwClose(PnpMgrLevel2);
                return STATUS_INSUFFICIENT_RESOURCES;
            }

            NameU.Length = 0;
            NameU.MaximumLength = (USHORT)OldLength + TranslatedSuffix.Length;

            Status = IoGetDeviceProperty(DeviceNode->PhysicalDeviceObject,
                                         DevicePropertyPhysicalDeviceObjectName,
                                         NameU.MaximumLength,
                                         NameU.Buffer,
                                         &OldLength);
            if (!NT_SUCCESS(Status))
            {
                ZwClose(PnpMgrLevel2);
                ExFreePool(NameU.Buffer);
                return Status;
            }
        }
        else if (!NT_SUCCESS(Status))
        {
            /* Some failure */
            ZwClose(PnpMgrLevel2);
            return Status;
        }
        else
        {
            /* This should never happen */
            ASSERT(FALSE);
        }

        NameU.Length = (USHORT)OldLength - sizeof(UNICODE_NULL); /* Remove final NULL */

        RtlAppendUnicodeStringToString(&NameU, &RawSuffix);

        Status = ZwSetValueKey(PnpMgrLevel2,
                               &NameU,
                               0,
                               REG_RESOURCE_LIST,
                               DeviceNode->ResourceList,
                               PnpDetermineResourceListSize(DeviceNode->ResourceList));
        if (!NT_SUCCESS(Status))
        {
            ZwClose(PnpMgrLevel2);
            ExFreePool(NameU.Buffer);
            return Status;
        }

        /* "Remove" the suffix by setting the length back to what it used to be */
        NameU.Length = (USHORT)OldLength - sizeof(UNICODE_NULL); /* Remove final NULL */

        RtlAppendUnicodeStringToString(&NameU, &TranslatedSuffix);

        Status = ZwSetValueKey(PnpMgrLevel2,
                               &NameU,
                               0,
                               REG_RESOURCE_LIST,
                               DeviceNode->ResourceListTranslated,
                               PnpDetermineResourceListSize(DeviceNode->ResourceListTranslated));
        ZwClose(PnpMgrLevel2);
        ExFreePool(NameU.Buffer);

        if (!NT_SUCCESS(Status))
            return Status;
    }
    else
    {
        ZwClose(PnpMgrLevel2);
    }

    return STATUS_SUCCESS;
}

NTSTATUS
IopUpdateResourceMapForPnPDevice(
    IN PDEVICE_NODE DeviceNode)
{
    return IopUpdateResourceMap(DeviceNode, L"PnP Manager", L"PnpManager");
}

static
NTSTATUS
IopTranslateDeviceResources(
   IN PDEVICE_NODE DeviceNode)
{
   PCM_PARTIAL_RESOURCE_LIST pPartialResourceList;
   PCM_PARTIAL_RESOURCE_DESCRIPTOR DescriptorRaw, DescriptorTranslated;
   PCM_FULL_RESOURCE_DESCRIPTOR FullDescriptor;
   ULONG i, j, ListSize;
   NTSTATUS Status;

   if (!DeviceNode->ResourceList)
   {
      DeviceNode->ResourceListTranslated = NULL;
      return STATUS_SUCCESS;
   }

   /* That's easy to translate a resource list. Just copy the
    * untranslated one and change few fields in the copy
    */
   ListSize = PnpDetermineResourceListSize(DeviceNode->ResourceList);

   DeviceNode->ResourceListTranslated = ExAllocatePool(PagedPool, ListSize);
   if (!DeviceNode->ResourceListTranslated)
   {
      Status = STATUS_NO_MEMORY;
      goto cleanup;
   }
   RtlCopyMemory(DeviceNode->ResourceListTranslated, DeviceNode->ResourceList, ListSize);

   FullDescriptor = &DeviceNode->ResourceList->List[0];
   for (i = 0; i < DeviceNode->ResourceList->Count; i++)
   {
      pPartialResourceList = &FullDescriptor->PartialResourceList;
      FullDescriptor = CmiGetNextResourceDescriptor(FullDescriptor);

      for (j = 0; j < pPartialResourceList->Count; j++)
      {
        /* Partial resource descriptors can be of variable size (CmResourceTypeDeviceSpecific),
           but only one is allowed and it must be the last one in the list! */
         DescriptorRaw = &pPartialResourceList->PartialDescriptors[j];

         /* Calculate the location of the translated resource descriptor */
         DescriptorTranslated = (PCM_PARTIAL_RESOURCE_DESCRIPTOR)(
             (PUCHAR)DeviceNode->ResourceListTranslated +
             ((PUCHAR)DescriptorRaw - (PUCHAR)DeviceNode->ResourceList));

         switch (DescriptorRaw->Type)
         {
            case CmResourceTypePort:
            {
               ULONG AddressSpace = 1; /* IO space */
               if (!HalTranslateBusAddress(
                  DeviceNode->ResourceList->List[i].InterfaceType,
                  DeviceNode->ResourceList->List[i].BusNumber,
                  DescriptorRaw->u.Port.Start,
                  &AddressSpace,
                  &DescriptorTranslated->u.Port.Start))
               {
                  Status = STATUS_UNSUCCESSFUL;
                  DPRINT1("Failed to translate port resource (Start: 0x%I64x)\n", DescriptorRaw->u.Port.Start.QuadPart);
                  goto cleanup;
               }

               if (AddressSpace == 0)
               {
                   DPRINT1("Guessed incorrect address space: 1 -> 0\n");

                   /* FIXME: I think all other CM_RESOURCE_PORT_XXX flags are
                    * invalid for this state but I'm not 100% sure */
                   DescriptorRaw->Flags =
                   DescriptorTranslated->Flags = CM_RESOURCE_PORT_MEMORY;
               }
               break;
            }
            case CmResourceTypeInterrupt:
            {
               KIRQL Irql;

               /* CM_RESOURCE_INTERRUPT_MESSAGE descriptors carry an
                * APIC vector / IRQL pair that's already in fully-
                * translated form (allocated by HalpAllocateMsiVector
                * in IopFindInterruptResource). HalGetInterruptVector
                * doesn't know about MSI vectors and would either
                * return 0 or remap into the legacy IOAPIC range,
                * both of which destroy the message vector and break
                * MSI delivery. Skip translation for MSI descriptors;
                * the translated copy already has the final values
                * from the earlier RtlCopyMemory. */
               if (DescriptorRaw->Flags & CM_RESOURCE_INTERRUPT_MESSAGE)
                   break;

               DescriptorTranslated->u.Interrupt.Vector = HalGetInterruptVector(
                  DeviceNode->ResourceList->List[i].InterfaceType,
                  DeviceNode->ResourceList->List[i].BusNumber,
                  DescriptorRaw->u.Interrupt.Level,
                  DescriptorRaw->u.Interrupt.Vector,
                  &Irql,
                  &DescriptorTranslated->u.Interrupt.Affinity);
               DescriptorTranslated->u.Interrupt.Level = Irql;
               if (!DescriptorTranslated->u.Interrupt.Vector)
               {
                   Status = STATUS_UNSUCCESSFUL;
                   DPRINT1("Failed to translate interrupt resource (Vector: 0x%x | Level: 0x%x)\n", DescriptorRaw->u.Interrupt.Vector,
                                                                                                   DescriptorRaw->u.Interrupt.Level);
                   goto cleanup;
               }
               break;
            }
            case CmResourceTypeMemory:
            {
               ULONG AddressSpace = 0; /* Memory space */
               if (!HalTranslateBusAddress(
                  DeviceNode->ResourceList->List[i].InterfaceType,
                  DeviceNode->ResourceList->List[i].BusNumber,
                  DescriptorRaw->u.Memory.Start,
                  &AddressSpace,
                  &DescriptorTranslated->u.Memory.Start))
               {
                  Status = STATUS_UNSUCCESSFUL;
                  DPRINT1("Failed to translate memory resource (Start: 0x%I64x)\n", DescriptorRaw->u.Memory.Start.QuadPart);
                  goto cleanup;
               }

               if (AddressSpace != 0)
               {
                   DPRINT1("Guessed incorrect address space: 0 -> 1\n");

                   /* This should never happen for memory space */
                   ASSERT(FALSE);
               }
            }

            case CmResourceTypeDma:
            case CmResourceTypeBusNumber:
            case CmResourceTypeDevicePrivate:
            case CmResourceTypeDeviceSpecific:
               /* Nothing to do */
               break;
            default:
               DPRINT1("Unknown resource descriptor type 0x%x\n", DescriptorRaw->Type);
               Status = STATUS_NOT_IMPLEMENTED;
               goto cleanup;
         }
      }
   }
   return STATUS_SUCCESS;

cleanup:
   /* Yes! Also delete ResourceList because ResourceList and
    * ResourceListTranslated should be a pair! */
   ExFreePool(DeviceNode->ResourceList);
   DeviceNode->ResourceList = NULL;
   if (DeviceNode->ResourceListTranslated)
   {
      ExFreePool(DeviceNode->ResourceListTranslated);
      DeviceNode->ResourceList = NULL;
   }
   return Status;
}

NTSTATUS
NTAPI
IopAssignDeviceResources(
   IN PDEVICE_NODE DeviceNode)
{
   NTSTATUS Status;
   ULONG ListSize;

   /* Drop this node's boot or prior assignment claim while it is being
    * replaced. Other devices' boot claims remain visible to the allocator. */
   IopResDbEnsureSeeded();
   IopResDbRelease(DeviceNode);

   Status = IopFilterResourceRequirements(DeviceNode);
   if (!NT_SUCCESS(Status))
       goto ByeBye;

   if (!DeviceNode->BootResources && !DeviceNode->ResourceRequirements)
   {
      /* No resource needed for this device */
      DeviceNode->ResourceList = NULL;
      DeviceNode->ResourceListTranslated = NULL;
      PiSetDevNodeState(DeviceNode, DeviceNodeResourcesAssigned);
      PiSetDevNodeFlag(DeviceNode, DNF_NO_RESOURCE_REQUIRED);

      return STATUS_SUCCESS;
   }

   if (DeviceNode->BootResources)
   {
       ListSize = PnpDetermineResourceListSize(DeviceNode->BootResources);

       DeviceNode->ResourceList = ExAllocatePool(PagedPool, ListSize);
       if (!DeviceNode->ResourceList)
       {
           Status = STATUS_NO_MEMORY;
           goto ByeBye;
       }

       RtlCopyMemory(DeviceNode->ResourceList, DeviceNode->BootResources, ListSize);

#if defined(_M_IX86) || defined(_M_AMD64)
       /* First consolidation pass: drop firmware-planted line
        * interrupts whose vector is inside the MSI pool. This runs
        * before arbitration so the boot resources are clean when the
        * arbiter matches requirements against them. */
       IopConsolidateInterruptDescriptors(DeviceNode->ResourceList);
#endif

       /* Conflicts with other owners are checked while missing requirements
        * are allocated below. Existing boot descriptors remain preferred. */
   }
   else
   {
       /* We'll make this from the requirements */
       DeviceNode->ResourceList = NULL;
   }

   /* No resources requirements */
   if (!DeviceNode->ResourceRequirements)
       goto Finish;

   /* Call HAL to fixup our resource requirements list */
   HalAdjustResourceList(&DeviceNode->ResourceRequirements);

   /* Add resource requirements that aren't in the list we already got */
   Status = IopFixupResourceListWithRequirements(DeviceNode->ResourceRequirements, &DeviceNode->ResourceList, DeviceNode);

#if defined(_M_IX86) || defined(_M_AMD64) || defined(_M_ARM64)
   /* Second consolidation pass: now that arbitration has finished
    * and the MSI/MSI-X descriptor (if any) has been added, drop the
    * obsolete legacy line descriptor that came from BootResources.
    * After this pass the resource list presented to the driver and
    * surfaced in Device Manager contains only the interrupt class
    * the device will actually use — matching Windows behaviour. */
   if (NT_SUCCESS(Status) && DeviceNode->ResourceList)
       IopConsolidateInterruptDescriptors(DeviceNode->ResourceList);
#endif
   if (!NT_SUCCESS(Status))
   {
       DPRINT1("Failed to fixup a resource list from supplied resources for %wZ\n", &DeviceNode->InstancePath);
       DeviceNode->Problem = CM_PROB_NORMAL_CONFLICT;
       goto ByeBye;
   }

Finish:
   Status = IopTranslateDeviceResources(DeviceNode);
   if (!NT_SUCCESS(Status))
   {
       DeviceNode->Problem = CM_PROB_TRANSLATION_FAILED;
       DPRINT1("Failed to translate resources for %wZ\n", &DeviceNode->InstancePath);
       goto ByeBye;
   }

   Status = IopUpdateResourceMapForPnPDevice(DeviceNode);
   if (!NT_SUCCESS(Status))
       goto ByeBye;

   Status = IopUpdateControlKeyWithResources(DeviceNode);
   if (!NT_SUCCESS(Status))
       goto ByeBye;

   /* Replace the temporary gap above with the completed grant. */
   if (DeviceNode->ResourceList)
       IopResDbReserve(DeviceNode, DeviceNode->ResourceList, NULL);

   PiSetDevNodeState(DeviceNode, DeviceNodeResourcesAssigned);

   return STATUS_SUCCESS;

ByeBye:
   if (DeviceNode->ResourceList)
   {
      ExFreePool(DeviceNode->ResourceList);
      DeviceNode->ResourceList = NULL;
   }

   DeviceNode->ResourceListTranslated = NULL;

   /* Failed assignment must not forget firmware resources that the device
    * can still decode while it remains present. */
   if (DeviceNode->BootResources)
       IopResDbReserve(DeviceNode, DeviceNode->BootResources, NULL);

   return Status;
}

static
BOOLEAN
IopCheckForResourceConflict(
   IN PCM_RESOURCE_LIST ResourceList1,
   IN PCM_RESOURCE_LIST ResourceList2,
   IN BOOLEAN Silent,
   OUT OPTIONAL PCM_PARTIAL_RESOURCE_DESCRIPTOR ConflictingDescriptor)
{
   ULONG i, ii;
   BOOLEAN Result = FALSE;
   PCM_FULL_RESOURCE_DESCRIPTOR FullDescriptor;
   PCM_PARTIAL_RESOURCE_DESCRIPTOR ResDesc;

   FullDescriptor = &ResourceList1->List[0];
   for (i = 0; i < ResourceList1->Count; i++)
   {
      PCM_PARTIAL_RESOURCE_LIST ResList = &FullDescriptor->PartialResourceList;
      FullDescriptor = CmiGetNextResourceDescriptor(FullDescriptor);

      ResDesc = &ResList->PartialDescriptors[0];
      for (ii = 0; ii < ResList->Count; ii++, ResDesc = CmiGetNextPartialDescriptor(ResDesc))
      {
         Result = IopCheckResourceDescriptor(ResDesc,
                                             ResourceList2,
                                             Silent,
                                             ConflictingDescriptor);
         if (Result) goto ByeBye;
      }
   }

ByeBye:

   return Result;
}

BOOLEAN
IopIsResourceListValid(
   IN PCM_RESOURCE_LIST ResourceList,
   IN ULONG ResourceListLength)
{
   ULONG i, ii;
   ULONG_PTR End;
   PCM_FULL_RESOURCE_DESCRIPTOR FullDescriptor;

   if (ResourceListLength < FIELD_OFFSET(CM_RESOURCE_LIST, List))
      return FALSE;

   if (ResourceList->Count == 0)
      return TRUE;

   End = (ULONG_PTR)ResourceList + ResourceListLength;
   if (End < (ULONG_PTR)ResourceList)
      return FALSE;

   FullDescriptor = &ResourceList->List[0];

   for (i = 0; i < ResourceList->Count; i++)
   {
      ULONG_PTR Current = (ULONG_PTR)FullDescriptor;
      ULONG HeaderSize = FIELD_OFFSET(CM_FULL_RESOURCE_DESCRIPTOR, PartialResourceList.PartialDescriptors);
      PCM_PARTIAL_RESOURCE_DESCRIPTOR PartialDescriptor;

      if (Current > End || End - Current < HeaderSize)
         return FALSE;

      PartialDescriptor = FullDescriptor->PartialResourceList.PartialDescriptors;
      for (ii = 0; ii < FullDescriptor->PartialResourceList.Count; ii++)
      {
         ULONG_PTR Entry = (ULONG_PTR)PartialDescriptor;
         ULONG_PTR EntrySize = sizeof(CM_PARTIAL_RESOURCE_DESCRIPTOR);

         if (Entry > End || End - Entry < EntrySize)
            return FALSE;

         if (PartialDescriptor->Type == CmResourceTypeDeviceSpecific)
         {
            ULONG DataSize = PartialDescriptor->u.DeviceSpecificData.DataSize;

            if (DataSize > End - Entry - EntrySize)
               return FALSE;

            EntrySize += DataSize;
         }

         PartialDescriptor = (PCM_PARTIAL_RESOURCE_DESCRIPTOR)(Entry + EntrySize);
      }

      FullDescriptor = (PCM_FULL_RESOURCE_DESCRIPTOR)PartialDescriptor;
   }

   return TRUE;
}

NTSTATUS NTAPI
IopDetectResourceConflict(
   IN PCM_RESOURCE_LIST ResourceList,
   IN BOOLEAN Silent,
   OUT OPTIONAL PCM_PARTIAL_RESOURCE_DESCRIPTOR ConflictingDescriptor)
{
   OBJECT_ATTRIBUTES ObjectAttributes;
   UNICODE_STRING KeyName;
   HANDLE ResourceMapKey = NULL, ChildKey2 = NULL, ChildKey3 = NULL;
   ULONG KeyInformationLength, RequiredLength, KeyValueInformationLength, KeyNameInformationLength;
   PKEY_BASIC_INFORMATION KeyInformation;
   PKEY_VALUE_PARTIAL_INFORMATION KeyValueInformation;
   PKEY_VALUE_BASIC_INFORMATION KeyNameInformation;
   ULONG ChildKeyIndex1 = 0, ChildKeyIndex2, ChildKeyIndex3;
   NTSTATUS Status;

   if (!ResourceList || ResourceList->Count == 0)
      return STATUS_SUCCESS;

   RtlInitUnicodeString(&KeyName, L"\\Registry\\Machine\\HARDWARE\\RESOURCEMAP");
   InitializeObjectAttributes(&ObjectAttributes,
                              &KeyName,
                              OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                              NULL,
                              NULL);
   Status = ZwOpenKey(&ResourceMapKey, KEY_ENUMERATE_SUB_KEYS | KEY_QUERY_VALUE, &ObjectAttributes);
   if (!NT_SUCCESS(Status))
   {
      /* The key is missing which means we are the first device */
      return STATUS_SUCCESS;
   }

   while (TRUE)
   {
      Status = ZwEnumerateKey(ResourceMapKey,
                              ChildKeyIndex1,
                              KeyBasicInformation,
                              NULL,
                              0,
                              &RequiredLength);
      if (Status == STATUS_NO_MORE_ENTRIES)
          break;
      else if (Status == STATUS_BUFFER_OVERFLOW || Status == STATUS_BUFFER_TOO_SMALL)
      {
          KeyInformationLength = RequiredLength;
          KeyInformation = ExAllocatePoolWithTag(PagedPool,
                                                 KeyInformationLength,
                                                 TAG_IO);
          if (!KeyInformation)
          {
              Status = STATUS_INSUFFICIENT_RESOURCES;
              goto cleanup;
          }

          Status = ZwEnumerateKey(ResourceMapKey,
                                  ChildKeyIndex1,
                                  KeyBasicInformation,
                                  KeyInformation,
                                  KeyInformationLength,
                                  &RequiredLength);
      }
      else
         goto cleanup;
      ChildKeyIndex1++;
      if (!NT_SUCCESS(Status))
      {
          ExFreePoolWithTag(KeyInformation, TAG_IO);
          goto cleanup;
      }

      KeyName.Buffer = KeyInformation->Name;
      KeyName.MaximumLength = KeyName.Length = (USHORT)KeyInformation->NameLength;
      InitializeObjectAttributes(&ObjectAttributes,
                                 &KeyName,
                                 OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                                 ResourceMapKey,
                                 NULL);
      Status = ZwOpenKey(&ChildKey2,
                         KEY_ENUMERATE_SUB_KEYS | KEY_QUERY_VALUE,
                         &ObjectAttributes);
      ExFreePoolWithTag(KeyInformation, TAG_IO);
      if (!NT_SUCCESS(Status))
          goto cleanup;

      ChildKeyIndex2 = 0;
      while (TRUE)
      {
          Status = ZwEnumerateKey(ChildKey2,
                                  ChildKeyIndex2,
                                  KeyBasicInformation,
                                  NULL,
                                  0,
                                  &RequiredLength);
          if (Status == STATUS_NO_MORE_ENTRIES)
              break;
          else if (Status == STATUS_BUFFER_TOO_SMALL)
          {
              KeyInformationLength = RequiredLength;
              KeyInformation = ExAllocatePoolWithTag(PagedPool,
                                                     KeyInformationLength,
                                                     TAG_IO);
              if (!KeyInformation)
              {
                  Status = STATUS_INSUFFICIENT_RESOURCES;
                  goto cleanup;
              }

              Status = ZwEnumerateKey(ChildKey2,
                                      ChildKeyIndex2,
                                      KeyBasicInformation,
                                      KeyInformation,
                                      KeyInformationLength,
                                      &RequiredLength);
          }
          else
              goto cleanup;
          ChildKeyIndex2++;
          if (!NT_SUCCESS(Status))
          {
              ExFreePoolWithTag(KeyInformation, TAG_IO);
              goto cleanup;
          }

          KeyName.Buffer = KeyInformation->Name;
          KeyName.MaximumLength = KeyName.Length = (USHORT)KeyInformation->NameLength;
          InitializeObjectAttributes(&ObjectAttributes,
                                     &KeyName,
                                     OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                                     ChildKey2,
                                     NULL);
          Status = ZwOpenKey(&ChildKey3, KEY_QUERY_VALUE, &ObjectAttributes);
          ExFreePoolWithTag(KeyInformation, TAG_IO);
          if (!NT_SUCCESS(Status))
              goto cleanup;

          ChildKeyIndex3 = 0;
          while (TRUE)
          {
              Status = ZwEnumerateValueKey(ChildKey3,
                                           ChildKeyIndex3,
                                           KeyValuePartialInformation,
                                           NULL,
                                           0,
                                           &RequiredLength);
              if (Status == STATUS_NO_MORE_ENTRIES)
                  break;
              else if (Status == STATUS_BUFFER_TOO_SMALL)
              {
                  KeyValueInformationLength = RequiredLength;
                  KeyValueInformation = ExAllocatePoolWithTag(PagedPool,
                                                              KeyValueInformationLength,
                                                              TAG_IO);
                  if (!KeyValueInformation)
                  {
                      Status = STATUS_INSUFFICIENT_RESOURCES;
                      goto cleanup;
                  }

                  Status = ZwEnumerateValueKey(ChildKey3,
                                               ChildKeyIndex3,
                                               KeyValuePartialInformation,
                                               KeyValueInformation,
                                               KeyValueInformationLength,
                                               &RequiredLength);
              }
              else
                  goto cleanup;
              if (!NT_SUCCESS(Status))
              {
                  ExFreePoolWithTag(KeyValueInformation, TAG_IO);
                  goto cleanup;
              }

              Status = ZwEnumerateValueKey(ChildKey3,
                                           ChildKeyIndex3,
                                           KeyValueBasicInformation,
                                           NULL,
                                           0,
                                           &RequiredLength);
              if (Status == STATUS_BUFFER_TOO_SMALL)
              {
                  KeyNameInformationLength = RequiredLength;
                  KeyNameInformation = ExAllocatePoolWithTag(PagedPool,
                                                             KeyNameInformationLength + sizeof(WCHAR),
                                                             TAG_IO);
                  if (!KeyNameInformation)
                  {
                      Status = STATUS_INSUFFICIENT_RESOURCES;
                      goto cleanup;
                  }

                  Status = ZwEnumerateValueKey(ChildKey3,
                                               ChildKeyIndex3,
                                               KeyValueBasicInformation,
                                               KeyNameInformation,
                                               KeyNameInformationLength,
                                               &RequiredLength);
              }
              else
                  goto cleanup;
              ChildKeyIndex3++;
              if (!NT_SUCCESS(Status))
              {
                  ExFreePoolWithTag(KeyNameInformation, TAG_IO);
                  goto cleanup;
              }

              KeyNameInformation->Name[KeyNameInformation->NameLength / sizeof(WCHAR)] = UNICODE_NULL;

              /* Skip translated entries */
              if (wcsstr(KeyNameInformation->Name, L".Translated"))
              {
                  ExFreePoolWithTag(KeyNameInformation, TAG_IO);
                  ExFreePoolWithTag(KeyValueInformation, TAG_IO);
                  continue;
              }

              ExFreePoolWithTag(KeyNameInformation, TAG_IO);

              if (KeyValueInformation->Type != REG_RESOURCE_LIST ||
                  !IopIsResourceListValid((PCM_RESOURCE_LIST)KeyValueInformation->Data,
                                          KeyValueInformation->DataLength) ||
                  ((PCM_RESOURCE_LIST)KeyValueInformation->Data)->Count == 0)
              {
                  ExFreePoolWithTag(KeyValueInformation, TAG_IO);
                  continue;
              }

              if (IopCheckForResourceConflict(ResourceList,
                                              (PCM_RESOURCE_LIST)KeyValueInformation->Data,
                                              Silent,
                                              ConflictingDescriptor))
              {
                  ExFreePoolWithTag(KeyValueInformation, TAG_IO);
                  Status = STATUS_CONFLICTING_ADDRESSES;
                  goto cleanup;
              }

              ExFreePoolWithTag(KeyValueInformation, TAG_IO);
          }
      }
   }

cleanup:
   if (ResourceMapKey != NULL)
       ObCloseHandle(ResourceMapKey, KernelMode);
   if (ChildKey2 != NULL)
       ObCloseHandle(ChildKey2, KernelMode);
   if (ChildKey3 != NULL)
       ObCloseHandle(ChildKey3, KernelMode);

   if (Status == STATUS_NO_MORE_ENTRIES)
       Status = STATUS_SUCCESS;

   return Status;
}
