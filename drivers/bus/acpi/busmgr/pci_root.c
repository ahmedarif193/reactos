/*
 * PROJECT:         ReactOS ACPI Bus Driver
 * LICENSE:         GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:         Enumerate ACPI-defined PCI/PCIe root bridges
 */

#include <precomp.h>
#include <debug.h>

#ifdef CONFIG_ACPI_PCI


#define ACPI_PCI_ROOT_HANDLE_TAG 'rPcA'

static FAST_MUTEX AcpiPciRootTrackLock;
static LIST_ENTRY AcpiPciRootTrackList;
static BOOLEAN AcpiPciRootTrackingInitialized = FALSE;

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
} ACPI_PCI_ROOT_ENUM_CONTEXT, *PACPI_PCI_ROOT_ENUM_CONTEXT;

/*
 * _OSC Capabilities Buffer DWORD layout for PCI Host Bridge (per PCI Firmware Spec 3.0+):
 *   DWORD 0 (Status): Query flag on input (bit 0), error bits on return (bits 1-4)
 *   DWORD 1 (Support): OS support capabilities
 *   DWORD 2 (Control): OS control request / granted on return
 */
#define OSC_QUERY_ENABLE              0x01  /* Query mode, no hardware changes */
#define OSC_FIRMWARE_FAILURE          0x02  /* Firmware failed to process request */
#define OSC_UNRECOGNIZED_UUID         0x04  /* UUID not recognized */
#define OSC_UNRECOGNIZED_REVISION     0x08  /* Revision not recognized */
#define OSC_CAPABILITIES_MASKED       0x10  /* Some requested capabilities were masked */

#define PCI_ROOT_BUS_OSC_METHOD_CAPABILITY_REVISION 0x01

/* Support capabilities (DWORD 1) */
#define OSC_SUPPORT_EXTENDED_CONFIG_REGIONS   (1u << 0)
#define OSC_SUPPORT_ASPM                      (1u << 1)
#define OSC_SUPPORT_CLOCK_PM                  (1u << 2)
#define OSC_SUPPORT_SEGMENT_GROUPS            (1u << 3)
#define OSC_SUPPORT_MSI                       (1u << 4)

/* Control capabilities (DWORD 2) */
#define OSC_CONTROL_NATIVE_HOTPLUG            (1u << 0)
#define OSC_CONTROL_SHPC_NATIVE               (1u << 1)
#define OSC_CONTROL_NATIVE_PME                (1u << 2)
#define OSC_CONTROL_NATIVE_AER                (1u << 3)
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
}

static
VOID
AcpiPciRootEnsureTrackingInitialized(VOID)
{
    if (AcpiPciRootTrackingInitialized)
    {
        return;
    }

    ExInitializeFastMutex(&AcpiPciRootTrackLock);
    InitializeListHead(&AcpiPciRootTrackList);
    AcpiPciRootTrackingInitialized = TRUE;
}

static
BOOLEAN
AcpiPciRootRememberHandle(
    _In_ ACPI_HANDLE Handle,
    _In_ ULONG Segment,
    _In_ ULONG BusStart,
    _In_ ULONG BusEnd)
{
    PACPI_PCI_ROOT_TRACK_ENTRY Entry;
    PLIST_ENTRY Link;

    AcpiPciRootEnsureTrackingInitialized();

    if (!AcpiPciRootTrackingInitialized)
    {
        return TRUE;
    }

    ExAcquireFastMutex(&AcpiPciRootTrackLock);

    for (Link = AcpiPciRootTrackList.Flink;
         Link != &AcpiPciRootTrackList;
         Link = Link->Flink)
    {
        Entry = CONTAINING_RECORD(Link,
                                  ACPI_PCI_ROOT_TRACK_ENTRY,
                                  ListEntry);
        if (Entry->Handle == Handle)
        {
            ExReleaseFastMutex(&AcpiPciRootTrackLock);
            return FALSE;
        }
    }

    Entry = ExAllocatePoolWithTag(NonPagedPool,
                                  sizeof(*Entry),
                                  ACPI_PCI_ROOT_HANDLE_TAG);
    if (!Entry)
    {
        ExReleaseFastMutex(&AcpiPciRootTrackLock);
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
    Entry->Segment = Segment;
    Entry->BusStart = BusStart;
    Entry->BusEnd = BusEnd;
    InsertTailList(&AcpiPciRootTrackList, &Entry->ListEntry);

    ExReleaseFastMutex(&AcpiPciRootTrackLock);
    return TRUE;
}

static
VOID
AcpiPciRootEvaluateOsc(
    _In_ ACPI_HANDLE Handle,
    _Inout_ PHAL_ACPI_PCI_ROOT_INFO RootInfo)
{
    /*
     * PCI Express Host Bridge _OSC UUID (PCI Firmware Spec 3.0)
     * 33DB4D5B-1FF7-401C-9657-7441C03DD766
     */
    static const UINT8 PciExpressUuid[16] = {
        0x5B, 0x4D, 0xDB, 0x33,  /* Data1: 33DB4D5B (little-endian) */
        0xF7, 0x1F,              /* Data2: 1FF7 (little-endian) */
        0x1C, 0x40,              /* Data3: 401C (little-endian) */
        0x96, 0x57,              /* Data4[0-1]: 9657 (big-endian, as-is) */
        0x74, 0x41, 0xC0, 0x3D, 0xD7, 0x66  /* Data4[2-7]: 7441C03DD766 */
    };
    ULONG SupportValue = 0;
    ULONG ControlValue = 0;
    /*
     * Capabilities buffer layout (3 DWORDs per PCI Firmware Spec):
     *   CapBuffer[0] = Status/Query DWORD (bit 0 = query mode)
     *   CapBuffer[1] = Support DWORD (what OS supports)
     *   CapBuffer[2] = Control DWORD (what OS requests to control)
     */
    ULONG CapBuffer[3];
    ACPI_OBJECT Parameters[4];
    ACPI_OBJECT_LIST ArgumentList = { 4, Parameters };
    ACPI_BUFFER ReturnBuffer = { ACPI_ALLOCATE_BUFFER, NULL };
    ACPI_STATUS Status;

    /* Build Support capabilities (DWORD 1) */
    SupportValue |= OSC_SUPPORT_EXTENDED_CONFIG_REGIONS;
    SupportValue |= OSC_SUPPORT_SEGMENT_GROUPS;
    SupportValue |= OSC_SUPPORT_MSI;

    /* Build Control request (DWORD 2) - request PCIe native control */
    ControlValue |= OSC_CONTROL_EXPRESS_CAP_STRUCTURE;

    /* Populate the 3-DWORD capabilities buffer */
    CapBuffer[0] = 0;            /* Status/Query: 0 = commit mode (not query) */
    CapBuffer[1] = SupportValue; /* Support capabilities */
    CapBuffer[2] = ControlValue; /* Control request */

    /* Arg0: UUID buffer */
    Parameters[0].Type = ACPI_TYPE_BUFFER;
    Parameters[0].Buffer.Length = sizeof(PciExpressUuid);
    Parameters[0].Buffer.Pointer = (UINT8 *)PciExpressUuid;

    /* Arg1: Revision ID */
    Parameters[1].Type = ACPI_TYPE_INTEGER;
    Parameters[1].Integer.Value = PCI_ROOT_BUS_OSC_METHOD_CAPABILITY_REVISION;

    /* Arg2: Count of DWORDs in capabilities buffer */
    Parameters[2].Type = ACPI_TYPE_INTEGER;
    Parameters[2].Integer.Value = ARRAYSIZE(CapBuffer);

    /* Arg3: Capabilities buffer */
    Parameters[3].Type = ACPI_TYPE_BUFFER;
    Parameters[3].Buffer.Length = sizeof(CapBuffer);
    Parameters[3].Buffer.Pointer = (UINT8 *)CapBuffer;

    /* Initialize result tracking */
    RootInfo->Osc.Evaluated = TRUE;
    RootInfo->Osc.SupportSet = SupportValue;
    RootInfo->Osc.ControlRequest = ControlValue;
    RootInfo->Osc.StatusFlags = 0;
    RootInfo->Osc.ControlGranted = 0;
    RootInfo->Osc.Failed = TRUE;

    DPRINT1("ACPI: _OSC calling with Support=0x%lx Control=0x%lx\n",
            SupportValue, ControlValue);

    Status = AcpiEvaluateObject(Handle, "_OSC", &ArgumentList, &ReturnBuffer);
    if (ACPI_FAILURE(Status))
    {
        /*
         * AE_NOT_FOUND means the _OSC method does not exist on this root bridge.
         * This is normal for legacy ACPI 1.0/2.0 systems without PCIe.
         * Only log as error for other failure codes.
         */
        if (Status == AE_NOT_FOUND)
        {
            DPRINT1("ACPI: _OSC method not found (legacy system)\n");
        }
        else
        {
            DPRINT1("ACPI: _OSC evaluation failed (Status 0x%X)\n", Status);
        }
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

    /*
     * Parse return buffer (same layout as input):
     *   Data[0] = Status DWORD (error flags set by firmware)
     *   Data[1] = Support DWORD (echoed back, may be modified)
     *   Data[2] = Control DWORD (what firmware actually granted)
     */
    const ULONG *Data = (const ULONG *)Result->Buffer.Pointer;
    ULONG StatusFlags = Data[0];

    RootInfo->Osc.StatusFlags = StatusFlags;
    RootInfo->Osc.Failed = FALSE;

    /* Control granted is in DWORD 2 (index 2), not DWORD 1 */
    if (Result->Buffer.Length >= (3 * sizeof(ULONG)))
    {
        RootInfo->Osc.ControlGranted = Data[2];
    }
    else if (Result->Buffer.Length >= (2 * sizeof(ULONG)))
    {
        /* Fallback: some firmware may return only 2 DWORDs */
        RootInfo->Osc.ControlGranted = Data[1];
        DPRINT1("ACPI: _OSC returned only %u bytes (expected 12)\n",
                Result->Buffer.Length);
    }

    /* Check for error conditions in Status DWORD */
    if (StatusFlags & OSC_FIRMWARE_FAILURE)
    {
        DPRINT1("ACPI: _OSC firmware failure\n");
        RootInfo->Osc.Failed = TRUE;
    }
    if (StatusFlags & OSC_UNRECOGNIZED_UUID)
    {
        DPRINT1("ACPI: _OSC unrecognized UUID\n");
        RootInfo->Osc.Failed = TRUE;
    }
    if (StatusFlags & OSC_UNRECOGNIZED_REVISION)
    {
        DPRINT1("ACPI: _OSC unrecognized revision\n");
        RootInfo->Osc.Failed = TRUE;
    }
    if (StatusFlags & OSC_CAPABILITIES_MASKED)
    {
        /*
         * Capabilities masked means firmware refused some requested controls.
         * This is not necessarily a failure - we may have partial control.
         * Only fail if we got nothing we wanted.
         */
        DPRINT1("ACPI: _OSC some capabilities were masked (request=0x%lx grant=0x%lx)\n",
                ControlValue, RootInfo->Osc.ControlGranted);
        if (RootInfo->Osc.ControlGranted == 0)
        {
            RootInfo->Osc.Failed = TRUE;
        }
    }

    if (RootInfo->Osc.Failed)
    {
        DPRINT1("ACPI: _OSC failed - status 0x%lx request 0x%lx grant 0x%lx\n",
                StatusFlags,
                ControlValue,
                RootInfo->Osc.ControlGranted);
    }
    else
    {
        DPRINT1("ACPI: _OSC success - granted control 0x%lx (status 0x%lx)\n",
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

        {
            ACPI_OBJECT CbaResult;
            ACPI_BUFFER CbaBuffer = { sizeof(CbaResult), &CbaResult };

            Status = AcpiEvaluateObjectTyped(Handle,
                                             "_CBA",
                                             NULL,
                                             &CbaBuffer,
                                             ACPI_TYPE_INTEGER);
            if (ACPI_SUCCESS(Status))
            {
                RootInfo.ConfigSpaceBasePresent = TRUE;
                RootInfo.ConfigSpaceBase = CbaResult.Integer.Value;
            }
        }

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
            !AcpiPciRootRememberHandle(Handle,
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

BOOLEAN
NTAPI
AcpiPciRootQueryInfo(
    _In_ ACPI_HANDLE Handle,
    _Out_opt_ PULONG Segment,
    _Out_opt_ PULONG BusStart,
    _Out_opt_ PULONG BusEnd)
{
    PACPI_PCI_ROOT_TRACK_ENTRY Entry;
    PLIST_ENTRY Link;
    BOOLEAN Found = FALSE;

    if (!Handle || !AcpiPciRootTrackingInitialized)
    {
        return FALSE;
    }

    ExAcquireFastMutex(&AcpiPciRootTrackLock);

    for (Link = AcpiPciRootTrackList.Flink;
         Link != &AcpiPciRootTrackList;
         Link = Link->Flink)
    {
        Entry = CONTAINING_RECORD(Link,
                                  ACPI_PCI_ROOT_TRACK_ENTRY,
                                  ListEntry);
        if (Entry->Handle == Handle)
        {
            if (Segment) *Segment = Entry->Segment;
            if (BusStart) *BusStart = Entry->BusStart;
            if (BusEnd) *BusEnd = Entry->BusEnd;
            Found = TRUE;
            break;
        }
    }

    ExReleaseFastMutex(&AcpiPciRootTrackLock);
    return Found;
}

int
acpi_pci_root_init(VOID)
{
    ACPI_PCI_ROOT_ENUM_CONTEXT Context;
    ULONG Enumerated;

    AcpiPciRootInitContext(&Context);
    AcpiPciRootEnsureTrackingInitialized();
    DPRINT1("ACPI: Enumerating PCI root bridges (ACPI 1.0/2.0+/PCIe)\n");

    AcpiPciRootEnumerateByHid("PNP0A03", &Context);
    AcpiPciRootEnumerateByHid("PNP0A08", &Context);

    Enumerated = Context.RootCount;

    if (!Enumerated)
    {
        DPRINT1("ACPI: No PCI root bridges reported in namespace\n");
    }

    return 0;
}

void
acpi_pci_root_exit(VOID)
{
    if (!AcpiPciRootTrackingInitialized)
    {
        return;
    }

    for (;;)
    {
        PACPI_PCI_ROOT_TRACK_ENTRY Entry;
        PLIST_ENTRY Link;

        ExAcquireFastMutex(&AcpiPciRootTrackLock);
        if (IsListEmpty(&AcpiPciRootTrackList))
        {
            ExReleaseFastMutex(&AcpiPciRootTrackLock);
            break;
        }

        Link = RemoveHeadList(&AcpiPciRootTrackList);
        ExReleaseFastMutex(&AcpiPciRootTrackLock);

        Entry = CONTAINING_RECORD(Link, ACPI_PCI_ROOT_TRACK_ENTRY, ListEntry);
        ExFreePoolWithTag(Entry, ACPI_PCI_ROOT_HANDLE_TAG);
    }
}

/*
 * Context structure for walking ACPI namespace to find PCI devices
 */
typedef struct _ACPI_PCI_DEVICE_SEARCH_CONTEXT
{
    ULONG Segment;
    ULONG Bus;
    ULONG Device;
    ULONG Function;
    ACPI_HANDLE RootHandle;
    ACPI_HANDLE FoundHandle;
} ACPI_PCI_DEVICE_SEARCH_CONTEXT, *PACPI_PCI_DEVICE_SEARCH_CONTEXT;

/*
 * Callback for AcpiWalkNamespace to find a PCI device by _ADR
 *
 * The _ADR encoding for PCI devices is: (Device << 16) | Function
 */
static
ACPI_STATUS
AcpiFindPciDeviceCallback(
    _In_ ACPI_HANDLE Handle,
    _In_ UINT32 Level,
    _Inout_ void *Context,
    _Inout_opt_ void **ReturnValue)
{
    PACPI_PCI_DEVICE_SEARCH_CONTEXT SearchContext = Context;
    ACPI_STATUS Status;
    ACPI_OBJECT Result;
    ACPI_BUFFER Buffer = { sizeof(Result), &Result };
    ULONGLONG AdrValue;
    ULONG ExpectedAdr;

    UNREFERENCED_PARAMETER(Level);
    UNREFERENCED_PARAMETER(ReturnValue);

    /* Evaluate _ADR method to get PCI address */
    Status = AcpiEvaluateObjectTyped(Handle,
                                     METHOD_NAME__ADR,
                                     NULL,
                                     &Buffer,
                                     ACPI_TYPE_INTEGER);
    if (ACPI_FAILURE(Status))
    {
        /* No _ADR - not a PCI device, continue walking */
        return AE_OK;
    }

    AdrValue = Result.Integer.Value;

    /* _ADR encoding: (Device << 16) | Function */
    ExpectedAdr = (SearchContext->Device << 16) | SearchContext->Function;

    if ((ULONG)AdrValue == ExpectedAdr)
    {
        /* Found it */
        SearchContext->FoundHandle = Handle;
        DPRINT1("ACPI: Found PCI device %lu:%lu:%lu:%lu at ACPI handle %p\n",
                SearchContext->Segment,
                SearchContext->Bus,
                SearchContext->Device,
                SearchContext->Function,
                Handle);
        return AE_CTRL_TERMINATE;
    }

    return AE_OK;
}

/**
 * AcpiFindPciDeviceInNamespace - Find ACPI device for a PCI device
 *
 * @Segment: PCI segment number (typically 0)
 * @Bus: PCI bus number
 * @Device: PCI device number (slot)
 * @Function: PCI function number
 * @OutHandle: Returns ACPI handle if found
 *
 * This function walks the ACPI namespace under PCI root bridges to find
 * a device with _ADR matching the specified PCI device/function.
 *
 * The _ADR value for PCI devices encodes: (Device << 16) | Function
 * For example, Device 20 (0x14), Function 0 = 0x00140000
 *
 * Returns TRUE if a matching ACPI device was found, FALSE otherwise.
 */
BOOLEAN
NTAPI
AcpiFindPciDeviceInNamespace(
    _In_ ULONG Segment,
    _In_ ULONG Bus,
    _In_ ULONG Device,
    _In_ ULONG Function,
    _Out_ ACPI_HANDLE *OutHandle)
{
    PACPI_PCI_ROOT_TRACK_ENTRY Entry;
    PLIST_ENTRY Link;
    ACPI_PCI_DEVICE_SEARCH_CONTEXT SearchContext;

    if (!OutHandle)
    {
        return FALSE;
    }

    *OutHandle = NULL;

    if (!AcpiPciRootTrackingInitialized)
    {
        DPRINT1("ACPI: PCI root tracking not initialized\n");
        return FALSE;
    }

    /* Initialize search context */
    SearchContext.Segment = Segment;
    SearchContext.Bus = Bus;
    SearchContext.Device = Device;
    SearchContext.Function = Function;
    SearchContext.RootHandle = NULL;
    SearchContext.FoundHandle = NULL;

    DPRINT1("ACPI: Searching for PCI device %lu:%lu:%lu:%lu in namespace\n",
            Segment, Bus, Device, Function);

    ExAcquireFastMutex(&AcpiPciRootTrackLock);

    /*
     * Find the PCI root bridge that owns this bus.
     * For now, we iterate through all tracked roots and search each one.
     * A more sophisticated implementation would match Segment and Bus range.
     */
    for (Link = AcpiPciRootTrackList.Flink;
         Link != &AcpiPciRootTrackList;
         Link = Link->Flink)
    {
        Entry = CONTAINING_RECORD(Link,
                                  ACPI_PCI_ROOT_TRACK_ENTRY,
                                  ListEntry);

        /* Check if this root bridge covers our segment and bus */
        if (Entry->Segment != Segment)
        {
            continue;
        }

        if (Bus < Entry->BusStart || Bus > Entry->BusEnd)
        {
            continue;
        }

        SearchContext.RootHandle = Entry->Handle;

        DPRINT1("ACPI: Searching under PCI root %p (Seg %lu Bus %lu-%lu)\n",
                Entry->Handle,
                Entry->Segment,
                Entry->BusStart,
                Entry->BusEnd);

        /*
         * Walk namespace under this root bridge to find the device.
         * We search up to 2 levels deep:
         *   Level 1: Direct children of root (Bus 0 devices)
         *   Level 2: Children of bridges (for devices on secondary buses)
         *
         * For devices directly on the root bus (Bus == BusStart), search
         * direct children. For devices on secondary buses, we would need
         * to walk through PCI-to-PCI bridges (more complex, TODO).
         *
         * For now, we search all descendants up to a reasonable depth.
         */
        (void)AcpiWalkNamespace(ACPI_TYPE_DEVICE,
                                Entry->Handle,
                                4,  /* Max depth - handles bridges */
                                AcpiFindPciDeviceCallback,
                                NULL,
                                &SearchContext,
                                NULL);

        if (SearchContext.FoundHandle != NULL)
        {
            *OutHandle = SearchContext.FoundHandle;
            ExReleaseFastMutex(&AcpiPciRootTrackLock);
            return TRUE;
        }
    }

    ExReleaseFastMutex(&AcpiPciRootTrackLock);

    DPRINT1("ACPI: PCI device %lu:%lu:%lu:%lu not found in namespace\n",
            Segment, Bus, Device, Function);
    return FALSE;
}

/*
 * USB _OSC UUID in ACPI wire format (LE/BE converted)
 * Original UUID: CE2EE385-00E6-48CB-9F05-2EDB927C4899
 */
static const UINT8 UsbOscUuid[16] = {
    0x85, 0xE3, 0x2E, 0xCE,  /* Data1: CE2EE385 -> LE */
    0xE6, 0x00,              /* Data2: 00E6 -> LE */
    0xCB, 0x48,              /* Data3: 48CB -> LE */
    0x9F, 0x05,              /* Data4[0-1]: BE (no swap) */
    0x2E, 0xDB, 0x92, 0x7C, 0x48, 0x99  /* Data4[2-7]: BE (no swap) */
};

/* USB _OSC Status Flags */
#define USB_OSC_STATUS_QUERY            0x0001
#define USB_OSC_STATUS_FAILURE          0x0002
#define USB_OSC_STATUS_UUID_UNKNOWN     0x0004
#define USB_OSC_STATUS_REV_UNKNOWN      0x0008
#define USB_OSC_STATUS_MASKED           0x0010

/**
 * AcpiEvaluatePciDeviceOsc - Evaluate _OSC on a PCI device's ACPI node
 *
 * @Segment: PCI segment number (typically 0)
 * @Bus: PCI bus number
 * @Device: PCI device number (slot)
 * @Function: PCI function number
 * @Uuid: 16-byte UUID buffer (may be NULL to use USB _OSC UUID)
 * @Revision: _OSC revision number (typically 1)
 * @QueryMode: TRUE for Query phase (Bit 0 = 1), FALSE for Commit
 * @SupportCaps: Support capabilities (DWORD 1 input)
 * @ControlCaps: Control capabilities requested (DWORD 2 input)
 * @ReturnStatus: Output: Status DWORD returned by firmware (may be NULL)
 * @ReturnControl: Output: Control DWORD returned by firmware (may be NULL)
 *
 * This function finds the ACPI device node for a PCI device and evaluates
 * the _OSC method on it. It handles the entire ACPI interaction internally.
 *
 * Returns:
 *   STATUS_SUCCESS: _OSC evaluated successfully
 *   STATUS_NOT_FOUND: ACPI device or _OSC method not found
 *   STATUS_NOT_SUPPORTED: UUID or Revision not recognized
 *   STATUS_UNSUCCESSFUL: _OSC returned failure
 */
NTSTATUS
NTAPI
AcpiEvaluatePciDeviceOsc(
    _In_ ULONG Segment,
    _In_ ULONG Bus,
    _In_ ULONG Device,
    _In_ ULONG Function,
    _In_reads_opt_(16) const UCHAR *Uuid,
    _In_ ULONG Revision,
    _In_ BOOLEAN QueryMode,
    _In_ ULONG SupportCaps,
    _In_ ULONG ControlCaps,
    _Out_opt_ PULONG ReturnStatus,
    _Out_opt_ PULONG ReturnControl)
{
    ACPI_HANDLE AcpiHandle = NULL;
    ACPI_OBJECT Parameters[4];
    ACPI_OBJECT_LIST ArgumentList = { 4, Parameters };
    ACPI_BUFFER ReturnBuffer = { ACPI_ALLOCATE_BUFFER, NULL };
    ACPI_STATUS AcpiStatus;
    ULONG CapBuffer[3];
    ULONG StatusValue = 0;
    ULONG ControlValue = 0;
    NTSTATUS Status = STATUS_UNSUCCESSFUL;
    const UINT8 *UuidToUse;

    /* Initialize outputs */
    if (ReturnStatus) *ReturnStatus = 0;
    if (ReturnControl) *ReturnControl = 0;

    /* Use provided UUID or default to USB _OSC UUID */
    UuidToUse = Uuid ? Uuid : UsbOscUuid;

    /* Find the ACPI device node for this PCI device */
    if (!AcpiFindPciDeviceInNamespace(Segment, Bus, Device, Function, &AcpiHandle))
    {
        DPRINT1("ACPI: _OSC: PCI device %lu:%lu:%lu:%lu not found in namespace\n",
                Segment, Bus, Device, Function);
        return STATUS_NOT_FOUND;
    }

    /*
     * Build the 3-DWORD capabilities buffer per ACPI 6.5:
     *   DWORD 0: Status/Query (Bit 0 = Query flag)
     *   DWORD 1: Support Capabilities
     *   DWORD 2: Control Capabilities (request)
     */
    CapBuffer[0] = QueryMode ? USB_OSC_STATUS_QUERY : 0;
    CapBuffer[1] = SupportCaps;
    CapBuffer[2] = ControlCaps;

    /* Arg0: UUID buffer */
    Parameters[0].Type = ACPI_TYPE_BUFFER;
    Parameters[0].Buffer.Length = 16;
    Parameters[0].Buffer.Pointer = (UINT8 *)UuidToUse;

    /* Arg1: Revision ID */
    Parameters[1].Type = ACPI_TYPE_INTEGER;
    Parameters[1].Integer.Value = Revision;

    /* Arg2: Count of DWORDs in capabilities buffer (always 3) */
    Parameters[2].Type = ACPI_TYPE_INTEGER;
    Parameters[2].Integer.Value = 3;

    /* Arg3: Capabilities buffer */
    Parameters[3].Type = ACPI_TYPE_BUFFER;
    Parameters[3].Buffer.Length = sizeof(CapBuffer);
    Parameters[3].Buffer.Pointer = (UINT8 *)CapBuffer;

    DPRINT1("ACPI: _OSC calling PCI %lu:%lu:%lu:%lu with Query=%u Support=0x%lx Control=0x%lx\n",
            Segment, Bus, Device, Function,
            QueryMode ? 1 : 0, SupportCaps, ControlCaps);

    /* Evaluate _OSC method */
    AcpiStatus = AcpiEvaluateObject(AcpiHandle, "_OSC", &ArgumentList, &ReturnBuffer);
    if (ACPI_FAILURE(AcpiStatus))
    {
        if (AcpiStatus == AE_NOT_FOUND)
        {
            DPRINT1("ACPI: _OSC method not found on PCI %lu:%lu:%lu:%lu\n",
                    Segment, Bus, Device, Function);
            Status = STATUS_NOT_FOUND;
        }
        else
        {
            DPRINT1("ACPI: _OSC evaluation failed (ACPI Status 0x%X)\n", AcpiStatus);
            Status = STATUS_UNSUCCESSFUL;
        }
        goto Cleanup;
    }

    /* Validate return buffer */
    if (!ReturnBuffer.Pointer)
    {
        DPRINT1("ACPI: _OSC returned empty buffer\n");
        Status = STATUS_UNSUCCESSFUL;
        goto Cleanup;
    }

    {
        ACPI_OBJECT *Result = (ACPI_OBJECT *)ReturnBuffer.Pointer;

        if (Result->Type != ACPI_TYPE_BUFFER || Result->Buffer.Length < sizeof(ULONG))
        {
            DPRINT1("ACPI: _OSC returned unexpected type %u length %u\n",
                    Result->Type,
                    Result->Type == ACPI_TYPE_BUFFER ? Result->Buffer.Length : 0);
            Status = STATUS_UNSUCCESSFUL;
            goto Cleanup;
        }

        /*
         * Parse return buffer (same layout as input):
         *   Data[0] = Status DWORD (error flags set by firmware)
         *   Data[1] = Support DWORD (echoed back, may be modified)
         *   Data[2] = Control DWORD (what firmware actually granted)
         */
        {
            const ULONG *Data = (const ULONG *)Result->Buffer.Pointer;

            StatusValue = Data[0];

            /* Control granted is in DWORD 2 (index 2) */
            if (Result->Buffer.Length >= (3 * sizeof(ULONG)))
            {
                ControlValue = Data[2];
            }
            else if (Result->Buffer.Length >= (2 * sizeof(ULONG)))
            {
                /* Fallback: some firmware may return only 2 DWORDs */
                ControlValue = Data[1];
                DPRINT1("ACPI: _OSC returned only %u bytes (expected 12)\n",
                        Result->Buffer.Length);
            }

            DPRINT1("ACPI: _OSC result: Status=0x%lX Control=0x%lX\n",
                    StatusValue, ControlValue);
        }
    }

    /* Store outputs */
    if (ReturnStatus) *ReturnStatus = StatusValue;
    if (ReturnControl) *ReturnControl = ControlValue;

    /* Check for error conditions in Status DWORD */
    if (StatusValue & USB_OSC_STATUS_UUID_UNKNOWN)
    {
        DPRINT1("ACPI: _OSC returned UUID Unrecognized\n");
        Status = STATUS_NOT_SUPPORTED;
        goto Cleanup;
    }

    if (StatusValue & USB_OSC_STATUS_REV_UNKNOWN)
    {
        DPRINT1("ACPI: _OSC returned Revision Unrecognized\n");
        Status = STATUS_NOT_SUPPORTED;
        goto Cleanup;
    }

    if (StatusValue & USB_OSC_STATUS_FAILURE)
    {
        DPRINT1("ACPI: _OSC returned General Failure\n");
        Status = STATUS_UNSUCCESSFUL;
        goto Cleanup;
    }

    Status = STATUS_SUCCESS;

Cleanup:
    if (ReturnBuffer.Pointer)
    {
        ACPI_FREE(ReturnBuffer.Pointer);
    }

    return Status;
}

#endif /* CONFIG_ACPI_PCI */
