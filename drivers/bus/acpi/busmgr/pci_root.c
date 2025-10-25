/*
 * PROJECT:         ReactOS ACPI Bus Driver
 * LICENSE:         GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:         Enumerate ACPI-defined PCI/PCIe root bridges
 */

#include <precomp.h>
#include <debug.h>

#ifdef CONFIG_ACPI_PCI

typedef struct _ACPI_PCI_ROOT_ENUM_CONTEXT
{
    ULONG RootCount;
} ACPI_PCI_ROOT_ENUM_CONTEXT, *PACPI_PCI_ROOT_ENUM_CONTEXT;

static
VOID
AcpiPciRootInitWindow(
    _Out_ PHAL_ACPI_PCI_WINDOW Window)
{
    Window->Present = FALSE;
    Window->Base = 0;
    Window->Limit = 0;
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
                case ACPI_MEMORY_RANGE:
                    if (Addr->Info.Mem.Caching == ACPI_PREFETCHABLE_MEMORY)
                        AcpiPciRootAccumulateAddress(&RootInfo->PrefetchWindow,
                                                     Addr->Address.Minimum,
                                                     Addr->Address.Maximum,
                                                     Addr->Address.AddressLength);
                    else
                        AcpiPciRootAccumulateAddress(&RootInfo->MemoryWindow,
                                                     Addr->Address.Minimum,
                                                     Addr->Address.Maximum,
                                                     Addr->Address.AddressLength);
                    break;

                case ACPI_IO_RANGE:
                    AcpiPciRootAccumulateAddress(&RootInfo->IoWindow,
                                                 Addr->Address.Minimum,
                                                 Addr->Address.Maximum,
                                                 Addr->Address.AddressLength);
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
                case ACPI_MEMORY_RANGE:
                    if (Addr->Info.Mem.Caching == ACPI_PREFETCHABLE_MEMORY)
                        AcpiPciRootAccumulateAddress(&RootInfo->PrefetchWindow,
                                                     Addr->Address.Minimum,
                                                     Addr->Address.Maximum,
                                                     Addr->Address.AddressLength);
                    else
                        AcpiPciRootAccumulateAddress(&RootInfo->MemoryWindow,
                                                     Addr->Address.Minimum,
                                                     Addr->Address.Maximum,
                                                     Addr->Address.AddressLength);
                    break;

                case ACPI_IO_RANGE:
                    AcpiPciRootAccumulateAddress(&RootInfo->IoWindow,
                                                 Addr->Address.Minimum,
                                                 Addr->Address.Maximum,
                                                 Addr->Address.AddressLength);
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
                case ACPI_MEMORY_RANGE:
                    if (Addr->Info.Mem.Caching == ACPI_PREFETCHABLE_MEMORY)
                        AcpiPciRootAccumulateAddress(&RootInfo->PrefetchWindow,
                                                     Addr->Address.Minimum,
                                                     Addr->Address.Maximum,
                                                     Addr->Address.AddressLength);
                    else
                        AcpiPciRootAccumulateAddress(&RootInfo->MemoryWindow,
                                                     Addr->Address.Minimum,
                                                     Addr->Address.Maximum,
                                                     Addr->Address.AddressLength);
                    break;

                case ACPI_IO_RANGE:
                    AcpiPciRootAccumulateAddress(&RootInfo->IoWindow,
                                                 Addr->Address.Minimum,
                                                 Addr->Address.Maximum,
                                                 Addr->Address.AddressLength);
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
                case ACPI_MEMORY_RANGE:
                    if (Addr->Info.Mem.Caching == ACPI_PREFETCHABLE_MEMORY)
                        AcpiPciRootAccumulateAddress(&RootInfo->PrefetchWindow,
                                                     Addr->Address.Minimum,
                                                     Addr->Address.Maximum,
                                                     Addr->Address.AddressLength);
                    else
                        AcpiPciRootAccumulateAddress(&RootInfo->MemoryWindow,
                                                     Addr->Address.Minimum,
                                                     Addr->Address.Maximum,
                                                     Addr->Address.AddressLength);
                    break;

                case ACPI_IO_RANGE:
                    AcpiPciRootAccumulateAddress(&RootInfo->IoWindow,
                                                 Addr->Address.Minimum,
                                                 Addr->Address.Maximum,
                                                 Addr->Address.AddressLength);
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

        DPRINT1("ACPI: PCI Root %lu: HID=%s UID=%s SEG=%lu BUS=%lu\n",
                Context ? (Context->RootCount + 1) : 0,
                (Info->Valid & ACPI_VALID_HID) ? Info->HardwareId.String : "<none>",
                (Info->Valid & ACPI_VALID_UID) ? Info->UniqueId.String : "<none>",
                RootInfo.Segment,
                RootInfo.Bus);

        if (RootInfo.IoWindow.Present)
        {
            DPRINT1("    IO window   : [%I64x - %I64x]\n",
                    RootInfo.IoWindow.Base,
                    RootInfo.IoWindow.Limit);
        }
        if (RootInfo.MemoryWindow.Present)
        {
            DPRINT1("    Memory window: [%I64x - %I64x]\n",
                    RootInfo.MemoryWindow.Base,
                    RootInfo.MemoryWindow.Limit);
        }
        if (RootInfo.PrefetchWindow.Present)
        {
            DPRINT1("    Prefetch window: [%I64x - %I64x]\n",
                    RootInfo.PrefetchWindow.Base,
                    RootInfo.PrefetchWindow.Limit);
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
    ACPI_PCI_ROOT_ENUM_CONTEXT Context = { 0 };

    DPRINT1("ACPI: Enumerating PCI root bridges (ACPI 1.0/2.0+/PCIe)\n");

    AcpiPciRootEnumerateByHid("PNP0A03", &Context);
    AcpiPciRootEnumerateByHid("PNP0A08", &Context);

    if (!Context.RootCount)
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
