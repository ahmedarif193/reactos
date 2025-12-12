/*
 * PROJECT:         ReactOS ACPI Bus Driver
 * LICENSE:         GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:         Enumerate ACPI-defined PCI/PCIe root bridges
 */

#include <precomp.h>
#include <debug.h>

#ifdef CONFIG_ACPI_PCI


#define ACPI_PCI_ROOT_HANDLE_TAG 'rPcA'

typedef struct _ACPI_PCI_ROOT_TRACK_ENTRY
{
    LIST_ENTRY ListEntry;
    ACPI_HANDLE Handle;
    ULONG Segment;
    ULONG BusStart;
    ULONG BusEnd;
} ACPI_PCI_ROOT_TRACK_ENTRY, *PACPI_PCI_ROOT_TRACK_ENTRY;

typedef struct _ACPI_PCI_ROOT_ENUM_CONTEXT
{
    ULONG RootCount;
    LIST_ENTRY SeenRoots;
} ACPI_PCI_ROOT_ENUM_CONTEXT, *PACPI_PCI_ROOT_ENUM_CONTEXT;

#define OSC_FIRMWARE_FAILURE          0x02
#define OSC_UNRECOGNIZED_UUID         0x04
#define OSC_UNRECOGNIZED_REVISION     0x08
#define OSC_CAPABILITIES_MASKED       0x10

#define PCI_ROOT_BUS_OSC_METHOD_CAPABILITY_REVISION 0x01

#define OSC_SUPPORT_EXTENDED_CONFIG_REGIONS   (1u << 0)
#define OSC_SUPPORT_SEGMENT_GROUPS            (1u << 3)
#define OSC_CONTROL_EXPRESS_CAP_STRUCTURE     (1u << 4)

static
VOID
AcpiPciRootInitWindow(
    _Out_ PHAL_ACPI_PCI_WINDOW Window)
{
    Window->Present = FALSE;
    Window->HasTranslation = FALSE;
    Window->TranslationType = 0;
    Window->Reserved = 0;
    Window->Base = 0;
    Window->Limit = 0;
    Window->Translation = 0;
}

static
VOID
AcpiPciRootAccumulateWindow(
    _Inout_ PHAL_ACPI_PCI_WINDOW Window,
    _In_ ULONGLONG Minimum,
    _In_ ULONGLONG Maximum)
{
    if (Maximum < Minimum)
    {
        return;
    }

    if (!Window->Present)
    {
        Window->Present = TRUE;
        Window->Base = Minimum;
        Window->Limit = Maximum;
    }
    else
    {
        if (Minimum < Window->Base) Window->Base = Minimum;
        if (Maximum > Window->Limit) Window->Limit = Maximum;
    }
}

static
VOID
AcpiPciRootAccumulateAddress(
    _Inout_ PHAL_ACPI_PCI_WINDOW Window,
    _In_ ULONGLONG Minimum,
    _In_ ULONGLONG Maximum,
    _In_ ULONGLONG Length)
{
    if (!Length)
    {
        return;
    }

    if ((Maximum < Minimum) ||
        ((Maximum - Minimum + 1) < Length))
    {
        Maximum = Minimum + Length - 1;
    }

    AcpiPciRootAccumulateWindow(Window, Minimum, Maximum);
}

static
VOID
AcpiPciRootAccumulateBusRange(
    _Inout_ PHAL_ACPI_PCI_ROOT_INFO RootInfo,
    _In_ ULONGLONG Minimum,
    _In_ ULONGLONG Maximum,
    _In_ ULONGLONG Length)
{
    if (!Length)
    {
        return;
    }

    if ((Maximum < Minimum) ||
        ((Maximum - Minimum + 1) < Length))
    {
        Maximum = Minimum + Length - 1;
    }

    if (!RootInfo->BusRangePresent)
    {
        RootInfo->BusRangePresent = TRUE;
        RootInfo->BusStart = (ULONG)Minimum;
        RootInfo->BusEnd = (ULONG)Maximum;
    }
    else
    {
        if (Minimum < RootInfo->BusStart) RootInfo->BusStart = (ULONG)Minimum;
        if (Maximum > RootInfo->BusEnd) RootInfo->BusEnd = (ULONG)Maximum;
    }
}

static
VOID
AcpiPciRootSetTranslation(
    _Inout_ PHAL_ACPI_PCI_WINDOW Window,
    _In_ ULONGLONG Translation,
    _In_ UINT8 TranslationType)
{
    Window->HasTranslation = TRUE;
    Window->Translation = Translation;
    Window->TranslationType = TranslationType;
}

static
VOID
AcpiPciRootInitContext(
    _Out_ PACPI_PCI_ROOT_ENUM_CONTEXT Context)
{
    RtlZeroMemory(Context, sizeof(*Context));
    InitializeListHead(&Context->SeenRoots);
}

static
VOID
AcpiPciRootCleanupContext(
    _Inout_ PACPI_PCI_ROOT_ENUM_CONTEXT Context)
{
    PLIST_ENTRY Entry;

    while (!IsListEmpty(&Context->SeenRoots))
    {
        Entry = RemoveHeadList(&Context->SeenRoots);
        ExFreePoolWithTag(CONTAINING_RECORD(Entry,
                                            ACPI_PCI_ROOT_TRACK_ENTRY,
                                            ListEntry),
                          ACPI_PCI_ROOT_HANDLE_TAG);
    }

    InitializeListHead(&Context->SeenRoots);
    Context->RootCount = 0;
}

static
BOOLEAN
AcpiPciRootRememberHandle(
    _Inout_opt_ PACPI_PCI_ROOT_ENUM_CONTEXT Context,
    _In_ ACPI_HANDLE Handle,
    _In_ ULONG Segment,
    _In_ ULONG BusStart,
    _In_ ULONG BusEnd)
{
    PACPI_PCI_ROOT_TRACK_ENTRY Entry;

    if (!Context)
    {
        return TRUE;
    }

    for (Entry = CONTAINING_RECORD(Context->SeenRoots.Flink,
                                   ACPI_PCI_ROOT_TRACK_ENTRY,
                                   ListEntry);
         &Entry->ListEntry != &Context->SeenRoots;
         Entry = CONTAINING_RECORD(Entry->ListEntry.Flink,
                                   ACPI_PCI_ROOT_TRACK_ENTRY,
                                   ListEntry))
    {
        if (Entry->Handle == Handle)
        {
            return FALSE;
        }
    }

    Entry = ExAllocatePoolWithTag(NonPagedPool,
                                  sizeof(*Entry),
                                  ACPI_PCI_ROOT_HANDLE_TAG);
    if (!Entry)
    {
        DPRINT1("ACPI: Failed to track PCI root handle %p (Seg %lu Bus %lu-%lu)\n",
                Handle,
                Segment,
                BusStart,
                BusEnd);
        return TRUE;
    }

    Entry->Handle = Handle;
    Entry->Segment = Segment;
    Entry->BusStart = BusStart;
    Entry->BusEnd = BusEnd;
    InsertTailList(&Context->SeenRoots, &Entry->ListEntry);
    return TRUE;
}

static
VOID
AcpiPciRootEvaluateOsc(
    _In_ ACPI_HANDLE Handle,
    _Inout_ PHAL_ACPI_PCI_ROOT_INFO RootInfo)
{
    static const UINT8 PciExpressUuid[16] = { 0x33, 0xDB, 0x4D, 0x5B, 0x1F, 0xF7, 0x40, 0x1C, 0x96, 0x57, 0x74, 0x41, 0xC0, 0x3D, 0xD7, 0x66 };
    ULONG SupportValue = 0;
    ULONG ControlValue = 0;
    ULONG CapBuffer[2];
    ACPI_OBJECT Parameters[4];
    ACPI_OBJECT_LIST ArgumentList = { 4, Parameters };
    ACPI_BUFFER ReturnBuffer = { ACPI_ALLOCATE_BUFFER, NULL };
    ACPI_STATUS Status;

    SupportValue |= OSC_SUPPORT_EXTENDED_CONFIG_REGIONS;
    SupportValue |= OSC_SUPPORT_SEGMENT_GROUPS;
    SupportValue |= HAL_ACPI_OSC_SUPPORT_MSI;

    ControlValue |= OSC_CONTROL_EXPRESS_CAP_STRUCTURE;

    CapBuffer[0] = SupportValue;
    CapBuffer[1] = ControlValue;

    Parameters[0].Type = ACPI_TYPE_BUFFER;
    Parameters[0].Buffer.Length = sizeof(PciExpressUuid);
    Parameters[0].Buffer.Pointer = (UINT8 *)PciExpressUuid;

    Parameters[1].Type = ACPI_TYPE_INTEGER;
    Parameters[1].Integer.Value = PCI_ROOT_BUS_OSC_METHOD_CAPABILITY_REVISION;

    Parameters[2].Type = ACPI_TYPE_INTEGER;
    Parameters[2].Integer.Value = ARRAYSIZE(CapBuffer);

    Parameters[3].Type = ACPI_TYPE_BUFFER;
    Parameters[3].Buffer.Length = sizeof(CapBuffer);
    Parameters[3].Buffer.Pointer = (UINT8 *)CapBuffer;

    RootInfo->Osc.Evaluated = TRUE;
    RootInfo->Osc.SupportSet = SupportValue;
    RootInfo->Osc.ControlRequest = ControlValue;
    RootInfo->Osc.StatusFlags = 0;
    RootInfo->Osc.ControlGranted = 0;
    RootInfo->Osc.Failed = TRUE;

    Status = AcpiEvaluateObject(Handle, "_OSC", &ArgumentList, &ReturnBuffer);
    if (ACPI_FAILURE(Status))
    {
        DPRINT1("ACPI: _OSC evaluation failed (Status 0x%X)\n", Status);
        goto Cleanup;
    }

    if (!ReturnBuffer.Pointer)
    {
        DPRINT1("ACPI: _OSC returned empty buffer\n");
        goto Cleanup;
    }

    ACPI_OBJECT *Result = ReturnBuffer.Pointer;
    if (Result->Type != ACPI_TYPE_BUFFER || Result->Buffer.Length < sizeof(ULONG))
    {
        DPRINT1("ACPI: _OSC returned unexpected object type %u length %u\n",
                Result->Type,
                Result->Type == ACPI_TYPE_BUFFER ? Result->Buffer.Length : 0);
        goto Cleanup;
    }

    const ULONG *Data = (const ULONG *)Result->Buffer.Pointer;
    ULONG StatusFlags = Data[0];

    RootInfo->Osc.StatusFlags = StatusFlags;
    RootInfo->Osc.Failed = FALSE;

    if (Result->Buffer.Length >= (2 * sizeof(ULONG)))
    {
        RootInfo->Osc.ControlGranted = Data[1];
    }

    if (StatusFlags & (OSC_FIRMWARE_FAILURE |
                       OSC_UNRECOGNIZED_UUID |
                       OSC_UNRECOGNIZED_REVISION))
    {
        RootInfo->Osc.Failed = TRUE;
    }

    if (StatusFlags & OSC_CAPABILITIES_MASKED)
    {
        RootInfo->Osc.Failed = TRUE;
    }

    if (RootInfo->Osc.Failed)
    {
        DPRINT1("ACPI: _OSC returned status 0x%lx (control 0x%lx)\n",
                StatusFlags,
                RootInfo->Osc.ControlGranted);
    }
    else
    {
        DPRINT1("ACPI: _OSC granted control 0x%lx (status 0x%lx)\n",
                RootInfo->Osc.ControlGranted,
                StatusFlags);
    }

Cleanup:
    if (ReturnBuffer.Pointer)
    {
        ACPI_FREE(ReturnBuffer.Pointer);
    }
}

static
VOID
AcpiPciRootProcessResource(
    _Inout_ PHAL_ACPI_PCI_ROOT_INFO RootInfo,
    _In_ const ACPI_RESOURCE *Resource)
{
    switch (Resource->Type)
    {
        case ACPI_RESOURCE_TYPE_ADDRESS16:
        {
            const ACPI_RESOURCE_ADDRESS16 *Addr = &Resource->Data.Address16;
            switch (Addr->ResourceType)
            {
                case ACPI_BUS_NUMBER_RANGE:
                    AcpiPciRootAccumulateBusRange(RootInfo,
                                                  Addr->Address.Minimum,
                                                  Addr->Address.Maximum,
                                                  Addr->Address.AddressLength);
                    break;

                case ACPI_MEMORY_RANGE:
                    if (Addr->Info.Mem.Caching == ACPI_PREFETCHABLE_MEMORY)
                    {
                        AcpiPciRootAccumulateAddress(&RootInfo->PrefetchWindow,
                                                     Addr->Address.Minimum,
                                                     Addr->Address.Maximum,
                                                     Addr->Address.AddressLength);
                        AcpiPciRootSetTranslation(&RootInfo->PrefetchWindow,
                                                  Addr->Address.TranslationOffset,
                                                  0);
                    }
                    else
                    {
                        AcpiPciRootAccumulateAddress(&RootInfo->MemoryWindow,
                                                     Addr->Address.Minimum,
                                                     Addr->Address.Maximum,
                                                     Addr->Address.AddressLength);
                        AcpiPciRootSetTranslation(&RootInfo->MemoryWindow,
                                                  Addr->Address.TranslationOffset,
                                                  0);
                    }
                    break;

                case ACPI_IO_RANGE:
                    AcpiPciRootAccumulateAddress(&RootInfo->IoWindow,
                                                 Addr->Address.Minimum,
                                                 Addr->Address.Maximum,
                                                 Addr->Address.AddressLength);
                    AcpiPciRootSetTranslation(&RootInfo->IoWindow,
                                              Addr->Address.TranslationOffset,
                                              Addr->Info.Io.TranslationType);
                    break;

                default:
                    break;
            }
            break;
        }

        case ACPI_RESOURCE_TYPE_ADDRESS32:
        {
            const ACPI_RESOURCE_ADDRESS32 *Addr = &Resource->Data.Address32;
            switch (Addr->ResourceType)
            {
                case ACPI_BUS_NUMBER_RANGE:
                    AcpiPciRootAccumulateBusRange(RootInfo,
                                                  Addr->Address.Minimum,
                                                  Addr->Address.Maximum,
                                                  Addr->Address.AddressLength);
                    break;

                case ACPI_MEMORY_RANGE:
                    if (Addr->Info.Mem.Caching == ACPI_PREFETCHABLE_MEMORY)
                    {
                        AcpiPciRootAccumulateAddress(&RootInfo->PrefetchWindow,
                                                     Addr->Address.Minimum,
                                                     Addr->Address.Maximum,
                                                     Addr->Address.AddressLength);
                        AcpiPciRootSetTranslation(&RootInfo->PrefetchWindow,
                                                  Addr->Address.TranslationOffset,
                                                  0);
                    }
                    else
                    {
                        AcpiPciRootAccumulateAddress(&RootInfo->MemoryWindow,
                                                     Addr->Address.Minimum,
                                                     Addr->Address.Maximum,
                                                     Addr->Address.AddressLength);
                        AcpiPciRootSetTranslation(&RootInfo->MemoryWindow,
                                                  Addr->Address.TranslationOffset,
                                                  0);
                    }
                    break;

                case ACPI_IO_RANGE:
                    AcpiPciRootAccumulateAddress(&RootInfo->IoWindow,
                                                 Addr->Address.Minimum,
                                                 Addr->Address.Maximum,
                                                 Addr->Address.AddressLength);
                    AcpiPciRootSetTranslation(&RootInfo->IoWindow,
                                              Addr->Address.TranslationOffset,
                                              Addr->Info.Io.TranslationType);
                    break;

                default:
                    break;
            }
            break;
        }

        case ACPI_RESOURCE_TYPE_ADDRESS64:
        {
            const ACPI_RESOURCE_ADDRESS64 *Addr = &Resource->Data.Address64;
            switch (Addr->ResourceType)
            {
                case ACPI_BUS_NUMBER_RANGE:
                    AcpiPciRootAccumulateBusRange(RootInfo,
                                                  Addr->Address.Minimum,
                                                  Addr->Address.Maximum,
                                                  Addr->Address.AddressLength);
                    break;

                case ACPI_MEMORY_RANGE:
                    if (Addr->Info.Mem.Caching == ACPI_PREFETCHABLE_MEMORY)
                    {
                        AcpiPciRootAccumulateAddress(&RootInfo->PrefetchWindow,
                                                     Addr->Address.Minimum,
                                                     Addr->Address.Maximum,
                                                     Addr->Address.AddressLength);
                        AcpiPciRootSetTranslation(&RootInfo->PrefetchWindow,
                                                  Addr->Address.TranslationOffset,
                                                  0);
                    }
                    else
                    {
                        AcpiPciRootAccumulateAddress(&RootInfo->MemoryWindow,
                                                     Addr->Address.Minimum,
                                                     Addr->Address.Maximum,
                                                     Addr->Address.AddressLength);
                        AcpiPciRootSetTranslation(&RootInfo->MemoryWindow,
                                                  Addr->Address.TranslationOffset,
                                                  0);
                    }
                    break;

                case ACPI_IO_RANGE:
                    AcpiPciRootAccumulateAddress(&RootInfo->IoWindow,
                                                 Addr->Address.Minimum,
                                                 Addr->Address.Maximum,
                                                 Addr->Address.AddressLength);
                    AcpiPciRootSetTranslation(&RootInfo->IoWindow,
                                              Addr->Address.TranslationOffset,
                                              Addr->Info.Io.TranslationType);
                    break;

                default:
                    break;
            }
            break;
        }

        case ACPI_RESOURCE_TYPE_EXTENDED_ADDRESS64:
        {
            const ACPI_RESOURCE_EXTENDED_ADDRESS64 *Addr = &Resource->Data.ExtAddress64;
            switch (Addr->ResourceType)
            {
                case ACPI_BUS_NUMBER_RANGE:
                    AcpiPciRootAccumulateBusRange(RootInfo,
                                                  Addr->Address.Minimum,
                                                  Addr->Address.Maximum,
                                                  Addr->Address.AddressLength);
                    break;

                case ACPI_MEMORY_RANGE:
                    if (Addr->Info.Mem.Caching == ACPI_PREFETCHABLE_MEMORY)
                    {
                        AcpiPciRootAccumulateAddress(&RootInfo->PrefetchWindow,
                                                     Addr->Address.Minimum,
                                                     Addr->Address.Maximum,
                                                     Addr->Address.AddressLength);
                        AcpiPciRootSetTranslation(&RootInfo->PrefetchWindow,
                                                  Addr->Address.TranslationOffset,
                                                  0);
                    }
                    else
                    {
                        AcpiPciRootAccumulateAddress(&RootInfo->MemoryWindow,
                                                     Addr->Address.Minimum,
                                                     Addr->Address.Maximum,
                                                     Addr->Address.AddressLength);
                        AcpiPciRootSetTranslation(&RootInfo->MemoryWindow,
                                                  Addr->Address.TranslationOffset,
                                                  0);
                    }
                    break;

                case ACPI_IO_RANGE:
                    AcpiPciRootAccumulateAddress(&RootInfo->IoWindow,
                                                 Addr->Address.Minimum,
                                                 Addr->Address.Maximum,
                                                 Addr->Address.AddressLength);
                    AcpiPciRootSetTranslation(&RootInfo->IoWindow,
                                              Addr->Address.TranslationOffset,
                                              Addr->Info.Io.TranslationType);
                    break;

                default:
                    break;
            }
            break;
        }

        case ACPI_RESOURCE_TYPE_MEMORY24:
        case ACPI_RESOURCE_TYPE_MEMORY32:
        case ACPI_RESOURCE_TYPE_FIXED_MEMORY32:
        {
            const ACPI_RESOURCE_MEMORY32 *Mem = &Resource->Data.Memory32;
            AcpiPciRootAccumulateAddress(&RootInfo->MemoryWindow,
                                         Mem->Minimum,
                                         Mem->Maximum,
                                         Mem->AddressLength);
            break;
        }

        case ACPI_RESOURCE_TYPE_IO:
        case ACPI_RESOURCE_TYPE_FIXED_IO:
        {
            const ACPI_RESOURCE_IO *Io = &Resource->Data.Io;
            AcpiPciRootAccumulateAddress(&RootInfo->IoWindow,
                                         Io->Minimum,
                                         Io->Maximum,
                                         Io->AddressLength);
            break;
        }

        case ACPI_RESOURCE_TYPE_EXTENDED_IRQ:
        {
            const ACPI_RESOURCE_EXTENDED_IRQ *Irq = &Resource->Data.ExtendedIrq;
            for (UINT32 i = 0; i < Irq->InterruptCount; ++i)
            {
                if (Irq->Interrupts[i] > RootInfo->MaxGsi)
                    RootInfo->MaxGsi = (ULONG)Irq->Interrupts[i];
            }
            break;
        }

        case ACPI_RESOURCE_TYPE_IRQ:
        {
            const ACPI_RESOURCE_IRQ *Irq = &Resource->Data.Irq;
            for (UINT32 i = 0; i < Irq->InterruptCount; ++i)
            {
                if (Irq->Interrupts[i] > RootInfo->MaxGsi)
                    RootInfo->MaxGsi = (ULONG)Irq->Interrupts[i];
            }
            break;
        }

        default:
            break;
    }
}

static
VOID
AcpiPciRootExtractResources(
    _In_ ACPI_HANDLE Handle,
    _Inout_ PHAL_ACPI_PCI_ROOT_INFO RootInfo)
{
    ACPI_BUFFER ResourceBuffer = { ACPI_ALLOCATE_BUFFER, NULL };
    ACPI_STATUS Status;

    AcpiPciRootInitWindow(&RootInfo->IoWindow);
    AcpiPciRootInitWindow(&RootInfo->MemoryWindow);
    AcpiPciRootInitWindow(&RootInfo->PrefetchWindow);
    RootInfo->MaxGsi = 0;

    Status = AcpiGetCurrentResources(Handle, &ResourceBuffer);
    if (ACPI_FAILURE(Status))
    {
        if (Status != AE_NOT_FOUND)
        {
            DPRINT1("ACPI: _CRS query for PCI root %p failed (Status 0x%X)\n",
                    Handle,
                    Status);
        }
        return;
    }

    for (ACPI_RESOURCE *Resource = (ACPI_RESOURCE *)ResourceBuffer.Pointer;
         Resource && Resource->Type != ACPI_RESOURCE_TYPE_END_TAG;
         Resource = ACPI_NEXT_RESOURCE(Resource))
    {
        AcpiPciRootProcessResource(RootInfo, Resource);
    }

    ACPI_FREE(ResourceBuffer.Pointer);
}

static
ACPI_STATUS
AcpiPciRootEnumerateCallback(
    _In_ ACPI_HANDLE Handle,
    _In_ UINT32 Level,
    _Inout_opt_ PACPI_PCI_ROOT_ENUM_CONTEXT Context,
    _Inout_opt_ void **ReturnValue)
{
    ACPI_DEVICE_INFO *Info = NULL;
    ACPI_STATUS Status;
    ACPI_INTEGER SegValue = 0;
    ACPI_INTEGER BusValue = 0;

    UNREFERENCED_PARAMETER(Level);
    UNREFERENCED_PARAMETER(ReturnValue);

    Status = AcpiGetObjectInfo(Handle, &Info);
    if (ACPI_FAILURE(Status))
    {
        DPRINT1("ACPI: AcpiGetObjectInfo failed for PCI root %p (0x%X)\n",
                Handle,
                Status);
        return AE_OK;
    }

    {
        ACPI_OBJECT Result;
        ACPI_BUFFER Buffer = { sizeof(Result), &Result };

        Status = AcpiEvaluateObjectTyped(Handle,
                                         METHOD_NAME__SEG,
                                         NULL,
                                         &Buffer,
                                         ACPI_TYPE_INTEGER);
        if (ACPI_SUCCESS(Status))
        {
            SegValue = Result.Integer.Value;
        }

        Status = AcpiEvaluateObjectTyped(Handle,
                                         METHOD_NAME__BBN,
                                         NULL,
                                         &Buffer,
                                         ACPI_TYPE_INTEGER);
        if (ACPI_SUCCESS(Status))
        {
            BusValue = Result.Integer.Value;
        }
    }

    {
        HAL_ACPI_PCI_ROOT_INFO RootInfo = {0};

        RootInfo.Segment = (ULONG)SegValue;
        RootInfo.Bus = (ULONG)BusValue;

        AcpiPciRootExtractResources(Handle, &RootInfo);

        if (!RootInfo.BusRangePresent)
        {
            ULONG ClampedBus = (RootInfo.Bus <= 0xFF) ? RootInfo.Bus : 0xFF;

            RootInfo.BusRangePresent = TRUE;
            RootInfo.BusStart = ClampedBus;
            RootInfo.BusEnd = ClampedBus;
        }
        else
        {
            if (RootInfo.BusStart > 0xFF)
                RootInfo.BusStart = 0xFF;
            if (RootInfo.BusEnd > 0xFF)
                RootInfo.BusEnd = 0xFF;
            if (RootInfo.BusEnd < RootInfo.BusStart)
                RootInfo.BusEnd = RootInfo.BusStart;
        }

        if (Context &&
            !AcpiPciRootRememberHandle(Context,
                                       Handle,
                                       RootInfo.Segment,
                                       RootInfo.BusStart,
                                       RootInfo.BusEnd))
        {
            DPRINT1("ACPI: PCI root HID=%s UID=%s already processed (Seg %lu Bus %lu-%lu), skipping duplicate handle %p\n",
                    (Info->Valid & ACPI_VALID_HID) ? Info->HardwareId.String : "<none>",
                    (Info->Valid & ACPI_VALID_UID) ? Info->UniqueId.String : "<none>",
                    RootInfo.Segment,
                    RootInfo.BusStart,
                    RootInfo.BusEnd,
                    Handle);
            ACPI_FREE(Info);
            return AE_OK;
        }

        AcpiPciRootEvaluateOsc(Handle, &RootInfo);

        DPRINT1("ACPI: PCI Root %lu: HID=%s UID=%s SEG=%lu BUS=%lu\n",
                Context ? (Context->RootCount + 1) : 0,
                (Info->Valid & ACPI_VALID_HID) ? Info->HardwareId.String : "<none>",
                (Info->Valid & ACPI_VALID_UID) ? Info->UniqueId.String : "<none>",
                RootInfo.Segment,
                RootInfo.Bus);

        if (RootInfo.BusRangePresent)
        {
            DPRINT1("    Bus range  : [%lu - %lu]\n",
                    RootInfo.BusStart,
                    RootInfo.BusEnd);
        }

        if (RootInfo.IoWindow.Present)
        {
            DPRINT1("    IO window   : [%I64x - %I64x]\n",
                    RootInfo.IoWindow.Base,
                    RootInfo.IoWindow.Limit);
            if (RootInfo.IoWindow.HasTranslation)
            {
                DPRINT1("      translation: +%I64x (type %u)\n",
                        RootInfo.IoWindow.Translation,
                        RootInfo.IoWindow.TranslationType);
            }
        }
        if (RootInfo.MemoryWindow.Present)
        {
            DPRINT1("    Memory window: [%I64x - %I64x]\n",
                    RootInfo.MemoryWindow.Base,
                    RootInfo.MemoryWindow.Limit);
            if (RootInfo.MemoryWindow.HasTranslation)
            {
                DPRINT1("      translation: +%I64x\n",
                        RootInfo.MemoryWindow.Translation);
            }
        }
        if (RootInfo.PrefetchWindow.Present)
        {
            DPRINT1("    Prefetch window: [%I64x - %I64x]\n",
                    RootInfo.PrefetchWindow.Base,
                    RootInfo.PrefetchWindow.Limit);
            if (RootInfo.PrefetchWindow.HasTranslation)
            {
                DPRINT1("      translation: +%I64x\n",
                        RootInfo.PrefetchWindow.Translation);
            }
        }

        if (RootInfo.Osc.Evaluated)
        {
            DPRINT1("    _OSC status 0x%lx request 0x%lx grant 0x%lx%s\n",
                    RootInfo.Osc.StatusFlags,
                    RootInfo.Osc.ControlRequest,
                    RootInfo.Osc.ControlGranted,
                    RootInfo.Osc.Failed ? " (firmware retained control)" : "");
        }

        HalpConfigurePciRootBridge(&RootInfo);
    }

    if (Context)
    {
        Context->RootCount++;
    }

    ACPI_FREE(Info);
    return AE_OK;
}

static
VOID
AcpiPciRootEnumerateByHid(
    _In_z_ const CHAR *HardwareId,
    _Inout_ PACPI_PCI_ROOT_ENUM_CONTEXT Context)
{
    ACPI_STATUS Status;

    Status = AcpiGetDevices((char *)HardwareId,
                            (ACPI_WALK_CALLBACK)AcpiPciRootEnumerateCallback,
                            Context,
                            NULL);
    if (ACPI_FAILURE(Status) && Status != AE_NOT_FOUND)
    {
        DPRINT1("ACPI: AcpiGetDevices(%s) failed 0x%X\n", HardwareId, Status);
    }
}

int
acpi_pci_root_init(VOID)
{
    ACPI_PCI_ROOT_ENUM_CONTEXT Context;
    ULONG Enumerated;

    AcpiPciRootInitContext(&Context);
    DPRINT1("ACPI: Enumerating PCI root bridges (ACPI 1.0/2.0+/PCIe)\n");

    AcpiPciRootEnumerateByHid("PNP0A03", &Context);
    AcpiPciRootEnumerateByHid("PNP0A08", &Context);

    Enumerated = Context.RootCount;
    AcpiPciRootCleanupContext(&Context);

    if (!Enumerated)
    {
        DPRINT1("ACPI: No PCI root bridges reported in namespace\n");
    }

    return 0;
}

void
acpi_pci_root_exit(VOID)
{
    /* Nothing to tear down yet. */
}

#endif /* CONFIG_ACPI_PCI */
