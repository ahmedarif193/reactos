/*
 * PROJECT:         ReactOS ACPI bus driver
 * FILE:            acpi/ospm/acpienum.c
 * PURPOSE:         ACPI namespace enumerator
 * PROGRAMMERS:     Casper S. Hornstrup (chorns@users.sourceforge.net)
 * UPDATE HISTORY:
 *      01-05-2001  CSH  Created
 */

#include "precomp.h"

#include <acpixf.h>

#define NDEBUG
#include <debug.h>

#define ACPI_PCIROOT_TRACE(...) \
    DPRINT1("ACPI-PCIROOT: " __VA_ARGS__)

#define HAS_CHILDREN(d)		((d)->children.next != &((d)->children))
#define HAS_SIBLINGS(d)		(((d)->parent) && ((d)->node.next != &(d)->parent->children))
#define NODE_TO_DEVICE(n)	(list_entry(n, struct acpi_device, node))

extern struct acpi_device		*acpi_root;

#define BUSP_MADT_GICC_ONLINE_CAPABLE (1UL << 3)

typedef struct _BUSP_PROCESSOR_UID_SET
{
    BOOLEAN Valid;
    ULONG Count;
    ULONG Uids[MAXIMUM_PROCESSORS];
} BUSP_PROCESSOR_UID_SET, *PBUSP_PROCESSOR_UID_SET;

static
BOOLEAN
BuspAddProcessorUid(
    _Inout_ PBUSP_PROCESSOR_UID_SET ProcessorUids,
    _In_ ULONG Uid)
{
    ULONG Index;

    for (Index = 0; Index < ProcessorUids->Count; Index++)
    {
        if (ProcessorUids->Uids[Index] == Uid)
            return TRUE;
    }

    if (ProcessorUids->Count == RTL_NUMBER_OF(ProcessorUids->Uids))
        return FALSE;

    ProcessorUids->Uids[ProcessorUids->Count++] = Uid;
    return TRUE;
}

static
VOID
BuspBuildProcessorUidSet(
    _Out_ PBUSP_PROCESSOR_UID_SET ProcessorUids)
{
    ACPI_TABLE_HEADER *Table;
    ACPI_TABLE_MADT *Madt;
    ACPI_SUBTABLE_HEADER *Entry;
    PUCHAR TableEnd;
    ACPI_STATUS Status;
    BOOLEAN FoundProcessorEntry = FALSE;
    BOOLEAN ValidTable = TRUE;

    RtlZeroMemory(ProcessorUids, sizeof(*ProcessorUids));
    Status = AcpiGetTable(ACPI_SIG_MADT, 0, &Table);
    if (ACPI_FAILURE(Status) || !Table)
        return;

    Madt = (ACPI_TABLE_MADT *)Table;
    if (Table->Length < sizeof(*Madt))
    {
        ValidTable = FALSE;
        goto Exit;
    }

    Entry = (ACPI_SUBTABLE_HEADER *)((PUCHAR)Madt + sizeof(*Madt));
    TableEnd = (PUCHAR)Madt + Table->Length;
    while ((PUCHAR)Entry + sizeof(*Entry) <= TableEnd)
    {
        ULONG Uid = 0;
        BOOLEAN ProcessorEntry = FALSE;
        BOOLEAN Usable = FALSE;

        if (Entry->Length < sizeof(*Entry) || (PUCHAR)Entry + Entry->Length > TableEnd)
        {
            ValidTable = FALSE;
            break;
        }

        switch (Entry->Type)
        {
            case ACPI_MADT_TYPE_LOCAL_APIC:
                FoundProcessorEntry = TRUE;
                if (Entry->Length >= sizeof(ACPI_MADT_LOCAL_APIC))
                {
                    ACPI_MADT_LOCAL_APIC *LocalApic = (ACPI_MADT_LOCAL_APIC *)Entry;

                    ProcessorEntry = TRUE;
                    Uid = LocalApic->ProcessorId;
                    Usable = (LocalApic->LapicFlags & (ACPI_MADT_ENABLED | ACPI_MADT_ONLINE_CAPABLE)) != 0;
                }
                else
                    ValidTable = FALSE;
                break;

            case ACPI_MADT_TYPE_LOCAL_X2APIC:
                FoundProcessorEntry = TRUE;
                if (Entry->Length >= sizeof(ACPI_MADT_LOCAL_X2APIC))
                {
                    ACPI_MADT_LOCAL_X2APIC *LocalX2Apic = (ACPI_MADT_LOCAL_X2APIC *)Entry;

                    ProcessorEntry = TRUE;
                    Uid = LocalX2Apic->Uid;
                    Usable = (LocalX2Apic->LapicFlags & (ACPI_MADT_ENABLED | ACPI_MADT_ONLINE_CAPABLE)) != 0;
                }
                else
                    ValidTable = FALSE;
                break;

            case ACPI_MADT_TYPE_GENERIC_INTERRUPT:
                FoundProcessorEntry = TRUE;
                if (Entry->Length >= FIELD_OFFSET(ACPI_MADT_GENERIC_INTERRUPT, Flags) + sizeof(ULONG))
                {
                    ACPI_MADT_GENERIC_INTERRUPT *Gicc = (ACPI_MADT_GENERIC_INTERRUPT *)Entry;

                    ProcessorEntry = TRUE;
                    Uid = Gicc->Uid;
                    Usable = (Gicc->Flags & (ACPI_MADT_ENABLED | BUSP_MADT_GICC_ONLINE_CAPABLE)) != 0;
                }
                else
                    ValidTable = FALSE;
                break;

            default:
                break;
        }

        if (!ValidTable)
            break;

        if (ProcessorEntry && Usable)
        {
            if (!BuspAddProcessorUid(ProcessorUids, Uid))
            {
                ValidTable = FALSE;
                break;
            }
        }

        Entry = (ACPI_SUBTABLE_HEADER *)((PUCHAR)Entry + Entry->Length);
    }

    if ((PUCHAR)Entry != TableEnd)
        ValidTable = FALSE;

    ProcessorUids->Valid = ValidTable && FoundProcessorEntry && ProcessorUids->Count != 0;

Exit:
    AcpiPutTable(Table);
    if (!ProcessorUids->Valid)
        ProcessorUids->Count = 0;
}

static
BOOLEAN
BuspShouldEnumerateDevice(
    _In_ struct acpi_device *Device,
    _In_ PBUSP_PROCESSOR_UID_SET ProcessorUids)
{
    unsigned long long Uid;
    ACPI_STATUS Status;
    ULONG Index;

    if (!ProcessorUids->Valid || !Device->pnp.hardware_id || strcmp(Device->pnp.hardware_id, "ACPI0007") != 0)
        return TRUE;

    Status = acpi_evaluate_integer(Device->handle, "_UID", NULL, &Uid);
    if (ACPI_FAILURE(Status) || Uid > MAXULONG)
        return TRUE;

    for (Index = 0; Index < ProcessorUids->Count; Index++)
    {
        if (ProcessorUids->Uids[Index] == (ULONG)Uid)
            return TRUE;
    }

    return FALSE;
}

static
BOOLEAN
BuspAppendHardwareId(
    _Inout_updates_(BufferLength) PWCHAR Buffer,
    _Inout_ PULONG Index,
    _In_ ULONG BufferLength,
    _In_ PCWSTR Prefix,
    _In_ PCSTR Id)
{
    int written;

    if (!Id || !Id[0] || *Index >= BufferLength)
        return FALSE;

    written = _snwprintf(Buffer + *Index,
                         BufferLength - *Index,
                         L"%s%hs",
                         Prefix,
                         Id);
    if (written < 0 || (ULONG)written >= BufferLength - *Index)
        return FALSE;

    *Index += (ULONG)written;
    if (*Index >= BufferLength)
        return FALSE;

    Buffer[(*Index)++] = UNICODE_NULL;
    return TRUE;
}

static BOOLEAN BuspCheckedForMmConfig;
static BOOLEAN BuspHasMmConfig;

static
VOID
BuspLogPciRootResources(
    _In_ struct acpi_device *Device);

static
VOID
BuspEnsureMmConfigState(VOID)
{
    ACPI_STATUS Status;
    ACPI_TABLE_HEADER *Table;

    if (BuspCheckedForMmConfig)
        return;

    BuspCheckedForMmConfig = TRUE;

    Status = AcpiGetTable(ACPI_SIG_MCFG, 0, &Table);
    if (ACPI_SUCCESS(Status) && Table)
    {
        BuspHasMmConfig = TRUE;
        AcpiPutTable(Table);
    }
}

NTSTATUS
Bus_PlugInDevice (
    struct acpi_device *Device,
    PFDO_DEVICE_DATA            FdoData
    )
{
    PDEVICE_OBJECT      pdo;
    PPDO_DEVICE_DATA    pdoData;
    NTSTATUS            status;
    ULONG               index;
	WCHAR               temp[512];
    PLIST_ENTRY         entry;
    BOOLEAN             multiSzBuilt = TRUE;
    BOOLEAN             hasPciExpressCid = FALSE;
    BOOLEAN             isPciRoot;

    PAGED_CODE ();

    //Don't enumerate the root device
    if (Device->handle == ACPI_ROOT_OBJECT)
        return STATUS_SUCCESS;

    /* Check we didnt add this already */
    for (entry = FdoData->ListOfPDOs.Flink;
        entry != &FdoData->ListOfPDOs; entry = entry->Flink)
    {
        struct acpi_device *CurrentDevice;

        pdoData = CONTAINING_RECORD (entry, PDO_DEVICE_DATA, Link);

        //dont duplicate devices
        if (pdoData->AcpiHandle == Device->handle)
            return STATUS_SUCCESS;

        //check everything but fixed feature devices
        if (pdoData->AcpiHandle)
            acpi_bus_get_device(pdoData->AcpiHandle, &CurrentDevice);
        else
            continue;

        //check if the HID matches
        if (!strcmp(Device->pnp.hardware_id, CurrentDevice->pnp.hardware_id))
        {
            //check if UID exists for both and matches
            if (Device->flags.unique_id && CurrentDevice->flags.unique_id &&
                !strcmp(Device->pnp.unique_id, CurrentDevice->pnp.unique_id))
            {
                /* We have a UID on both but they're the same so we have to ignore it */
                DPRINT1("Detected duplicate device: %hs %hs\n", Device->pnp.hardware_id, Device->pnp.unique_id);
                return STATUS_SUCCESS;
            }
            else if (!Device->flags.unique_id && !CurrentDevice->flags.unique_id)
            {
                /* No UID so we can only legally have 1 of these devices */
                DPRINT1("Detected duplicate device: %hs\n", Device->pnp.hardware_id);
                return STATUS_SUCCESS;
            }
        }
    }

    isPciRoot = (Device->pnp.hardware_id &&
                 strncmp(Device->pnp.hardware_id, "PNP0A", 5) == 0);

    if (isPciRoot)
    {
        BuspEnsureMmConfigState();

        const char *UidString = (Device->flags.unique_id && Device->pnp.unique_id[0]) ?
                                Device->pnp.unique_id : "<none>";
        ULONGLONG AdrValue = Device->flags.bus_address ? (ULONGLONG)Device->pnp.bus_address : (ULONGLONG)-1;

        ACPI_PCIROOT_TRACE("Enumerating PCI root candidate HID=%hs UID=%s ADR=0x%I64x handle=%p\n",
                           Device->pnp.hardware_id,
                           UidString,
                           AdrValue,
                           Device->handle);

        if (Device->pnp.cid_list && Device->pnp.cid_list->Count)
        {
            UINT32 Index;

            for (Index = 0; Index < Device->pnp.cid_list->Count; Index++)
            {
                ACPI_PCIROOT_TRACE("  |_ CID[%u]=%s\n",
                                   Index,
                                   Device->pnp.cid_list->Ids[Index].String);
            }
        }

        BuspLogPciRootResources(Device);
    }

    DPRINT("Exposing PDO\n"
           "======AcpiHandle:   %p\n"
           "======HardwareId:     %s\n",
           Device->handle,
           Device->pnp.hardware_id);


    //
    // Create the PDO
    //

    DPRINT("FdoData->NextLowerDriver = 0x%p\n", FdoData->NextLowerDriver);

    status = IoCreateDevice(FdoData->Common.Self->DriverObject,
                              sizeof(PDO_DEVICE_DATA),
                              NULL,
                              FILE_DEVICE_CONTROLLER,
                              FILE_AUTOGENERATED_DEVICE_NAME,
                              FALSE,
                              &pdo);

    if (!NT_SUCCESS (status)) {
        return status;
    }

    pdoData = (PPDO_DEVICE_DATA) pdo->DeviceExtension;
    RtlZeroMemory(pdoData, sizeof(*pdoData));
    pdoData->AcpiHandle = Device->handle;
    InitializeListHead(&pdoData->NotificationList);
    KeInitializeSpinLock(&pdoData->NotificationLock);
    AcpiThermalInitialize(pdoData);

    if (isPciRoot)
    {
        ULONG Segment, BusStart, BusEnd;

        if (AcpiPciRootQueryInfo(Device->handle, &Segment, &BusStart, &BusEnd))
        {
            pdoData->HasPciRootSegment = TRUE;
            pdoData->PciRootSegment = Segment;

            if (!pdoData->HasPciRootBusRange)
            {
                pdoData->PciRootMinBus = BusStart;
                pdoData->PciRootMaxBus = BusEnd;
                pdoData->HasPciRootBusRange = TRUE;
            }
        }
    }

    //
    // Copy the hardware IDs and merge in compat IDs from _CID
    //
    RtlZeroMemory(temp, sizeof(temp));
    index = 0;

    if (!BuspAppendHardwareId(temp, &index, RTL_NUMBER_OF(temp), L"ACPI\\", Device->pnp.hardware_id) ||
        !BuspAppendHardwareId(temp, &index, RTL_NUMBER_OF(temp), L"*", Device->pnp.hardware_id))
    {
        multiSzBuilt = FALSE;
    }

    if (multiSzBuilt && Device->pnp.cid_list && Device->pnp.cid_list->Count)
    {
        UINT32 CidIndex;

        for (CidIndex = 0; CidIndex < Device->pnp.cid_list->Count; CidIndex++)
        {
            PCSTR Cid = Device->pnp.cid_list->Ids[CidIndex].String;

            if (!Cid || !Cid[0])
                continue;

            if (!BuspAppendHardwareId(temp, &index, RTL_NUMBER_OF(temp), L"ACPI\\", Cid) ||
                !BuspAppendHardwareId(temp, &index, RTL_NUMBER_OF(temp), L"*", Cid))
            {
                multiSzBuilt = FALSE;
                break;
            }

            if (_stricmp(Cid, "PNP0A08") == 0)
            {
                hasPciExpressCid = TRUE;
            }
        }
    }

    if (multiSzBuilt)
    {
        if (index < RTL_NUMBER_OF(temp))
        {
            temp[index++] = UNICODE_NULL;
        }
        else
        {
            multiSzBuilt = FALSE;
        }
    }

    if (!multiSzBuilt)
    {
        index = 0;
        index += swprintf(&temp[index],
                          RTL_NUMBER_OF(temp) - index,
                          L"ACPI\\%hs",
                          Device->pnp.hardware_id);
        temp[index++] = UNICODE_NULL;

        index += swprintf(&temp[index],
                          RTL_NUMBER_OF(temp) - index,
                          L"*%hs",
                          Device->pnp.hardware_id);
        temp[index++] = UNICODE_NULL;
        temp[index++] = UNICODE_NULL;
    }

    /* If firmware omitted PNP0A08 but MCFG exists, advertise PCIe anyway. */
    if (!hasPciExpressCid && BuspHasMmConfig &&
        Device->pnp.hardware_id && !_stricmp(Device->pnp.hardware_id, "PNP0A03"))
    {
        ULONG injectionIndex = (index > 0) ? index - 1 : 0;

        if (BuspAppendHardwareId(temp,
                                 &injectionIndex,
                                 RTL_NUMBER_OF(temp),
                                 L"ACPI\\",
                                 "PNP0A08") &&
            BuspAppendHardwareId(temp,
                                 &injectionIndex,
                                 RTL_NUMBER_OF(temp),
                                 L"*",
                                 "PNP0A08"))
        {
            if (injectionIndex < RTL_NUMBER_OF(temp))
            {
                temp[injectionIndex++] = UNICODE_NULL;
                index = injectionIndex;
                hasPciExpressCid = TRUE;
                ACPI_PCIROOT_TRACE("  |_ Injected PCIe CID PNP0A08 based on MCFG presence\n");
            }
        }
    }

		pdoData->HardwareIDs = ExAllocatePoolWithTag(NonPagedPool, index*sizeof(WCHAR), 'DpcA');


    if (!pdoData->HardwareIDs) {
        IoDeleteDevice(pdo);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

	RtlCopyMemory (pdoData->HardwareIDs, temp, index*sizeof(WCHAR));
	Device->pdo = pdo;

    if (hasPciExpressCid)
    {
        ACPI_PCIROOT_TRACE("  |_ PCI root %hs exposes PCIe compatibility ID\n",
                           Device->pnp.hardware_id);
    }
    Bus_InitializePdo (pdo, FdoData);

    //
    // Device Relation changes if a new pdo is created. So let
    // the PNP system now about that. This forces it to send bunch of pnp
    // queries and cause the function driver to be loaded.
    //

    //IoInvalidateDeviceRelations (FdoData->UnderlyingPDO, BusRelations);

    return status;
}


/* looks alot like acpi_bus_walk doesnt it */
NTSTATUS
ACPIEnumerateDevices(PFDO_DEVICE_DATA DeviceExtension)
{
    ULONG Count = 0;
    ULONG ProcessorCount = 0;
    ULONG SkippedProcessorCount = 0;
    BUSP_PROCESSOR_UID_SET ProcessorUids;
    struct acpi_device *Device = acpi_root;

    BuspBuildProcessorUidSet(&ProcessorUids);

    while(Device)
    {
        if (Device->status.present && Device->status.enabled &&
            Device->flags.hardware_id)
        {
            if (BuspShouldEnumerateDevice(Device, &ProcessorUids))
            {
                Bus_PlugInDevice(Device, DeviceExtension);
                Count++;
                if (!strcmp(Device->pnp.hardware_id, "ACPI0007"))
                    ProcessorCount++;
            }
            else
            {
                SkippedProcessorCount++;
            }
        }

        if (HAS_CHILDREN(Device)) {
            Device = NODE_TO_DEVICE(Device->children.next);
            continue;
        }
        if (HAS_SIBLINGS(Device)) {
            Device = NODE_TO_DEVICE(Device->node.next);
            continue;
        }
        while ((Device = Device->parent)) {
            if (HAS_SIBLINGS(Device)) {
                Device = NODE_TO_DEVICE(Device->node.next);
                break;
            }
        }
    }
    if (ProcessorUids.Valid)
        DPRINT1("ACPI: processor MADT filter exposed %lu ACPI0007 device(s), skipped %lu inactive firmware slot(s)\n", ProcessorCount, SkippedProcessorCount);
    DPRINT("acpi device count: %d\n", Count);
    return STATUS_SUCCESS;
}

/* EOF */
static
VOID
BuspLogPciRootResources(
    _In_ struct acpi_device *Device)
{
    ACPI_BUFFER Buffer;
    ACPI_STATUS Status;
    ACPI_RESOURCE *Resource;
    BOOLEAN FoundBusRange = FALSE;

    Buffer.Length = ACPI_ALLOCATE_BUFFER;
    Buffer.Pointer = NULL;

    Status = AcpiGetCurrentResources(Device->handle, &Buffer);
    if (ACPI_FAILURE(Status))
    {
        ACPI_PCIROOT_TRACE("  |_ Failed to evaluate _CRS (Status=0x%08x)\n", Status);
        return;
    }

    Resource = Buffer.Pointer;
    while (Resource && Resource->Type != ACPI_RESOURCE_TYPE_END_TAG)
    {
        UINT64 Minimum = 0;
        UINT64 Maximum = 0;
        UINT64 Length = 0;
        const CHAR *DescriptorName = NULL;

        switch (Resource->Type)
        {
            case ACPI_RESOURCE_TYPE_ADDRESS16:
            {
                const ACPI_RESOURCE_ADDRESS16 *Address = &Resource->Data.Address16;
                if (Address->ResourceType == ACPI_BUS_NUMBER_RANGE)
                {
                    Minimum = Address->Address.Minimum;
                    Maximum = Address->Address.Maximum;
                    Length = Address->Address.AddressLength;
                    DescriptorName = "Bus";
                    FoundBusRange = TRUE;
                }
                else if (Address->ResourceType == ACPI_MEMORY_RANGE)
                {
                    Minimum = Address->Address.Minimum;
                    Maximum = Address->Address.Maximum;
                    Length = Address->Address.AddressLength;
                    DescriptorName = "Mem16";
                }
                else if (Address->ResourceType == ACPI_IO_RANGE)
                {
                    Minimum = Address->Address.Minimum;
                    Maximum = Address->Address.Maximum;
                    Length = Address->Address.AddressLength;
                    DescriptorName = "Io16";
                }
                break;
            }

            case ACPI_RESOURCE_TYPE_ADDRESS32:
            {
                const ACPI_RESOURCE_ADDRESS32 *Address = &Resource->Data.Address32;
                if (Address->ResourceType == ACPI_BUS_NUMBER_RANGE)
                {
                    Minimum = Address->Address.Minimum;
                    Maximum = Address->Address.Maximum;
                    Length = Address->Address.AddressLength;
                    DescriptorName = "Bus";
                    FoundBusRange = TRUE;
                }
                else if (Address->ResourceType == ACPI_MEMORY_RANGE)
                {
                    Minimum = Address->Address.Minimum;
                    Maximum = Address->Address.Maximum;
                    Length = Address->Address.AddressLength;
                    DescriptorName = "Mem32";
                }
                else if (Address->ResourceType == ACPI_IO_RANGE)
                {
                    Minimum = Address->Address.Minimum;
                    Maximum = Address->Address.Maximum;
                    Length = Address->Address.AddressLength;
                    DescriptorName = "Io32";
                }
                break;
            }

            case ACPI_RESOURCE_TYPE_ADDRESS64:
            {
                const ACPI_RESOURCE_ADDRESS64 *Address = &Resource->Data.Address64;
                if (Address->ResourceType == ACPI_BUS_NUMBER_RANGE)
                {
                    Minimum = Address->Address.Minimum;
                    Maximum = Address->Address.Maximum;
                    Length = Address->Address.AddressLength;
                    DescriptorName = "Bus";
                    FoundBusRange = TRUE;
                }
                else if (Address->ResourceType == ACPI_MEMORY_RANGE)
                {
                    Minimum = Address->Address.Minimum;
                    Maximum = Address->Address.Maximum;
                    Length = Address->Address.AddressLength;
                    DescriptorName = "Mem64";
                }
                else if (Address->ResourceType == ACPI_IO_RANGE)
                {
                    Minimum = Address->Address.Minimum;
                    Maximum = Address->Address.Maximum;
                    Length = Address->Address.AddressLength;
                    DescriptorName = "Io64";
                }
                break;
            }

            case ACPI_RESOURCE_TYPE_EXTENDED_ADDRESS64:
            {
                const ACPI_RESOURCE_EXTENDED_ADDRESS64 *Address = &Resource->Data.ExtAddress64;
                Minimum = Address->Address.Minimum;
                Maximum = Address->Address.Maximum;
                Length = Address->Address.AddressLength;
                DescriptorName = "ExtMem64";
                break;
            }

            case ACPI_RESOURCE_TYPE_FIXED_MEMORY32:
            {
                const ACPI_RESOURCE_FIXED_MEMORY32 *Memory = &Resource->Data.FixedMemory32;
                Minimum = Memory->Address;
                Maximum = (ULONGLONG)Memory->Address + Memory->AddressLength - 1;
                Length = Memory->AddressLength;
                DescriptorName = "FixedMem32";
                break;
            }

            case ACPI_RESOURCE_TYPE_IO:
            {
                const ACPI_RESOURCE_IO *Io = &Resource->Data.Io;
                Minimum = Io->Minimum;
                Maximum = Io->Maximum;
                Length = Io->AddressLength;
                DescriptorName = "Io16";
                break;
            }

            case ACPI_RESOURCE_TYPE_FIXED_IO:
            {
                const ACPI_RESOURCE_FIXED_IO *Io = &Resource->Data.FixedIo;
                Minimum = Io->Address;
                Maximum = Io->Address + Io->AddressLength - 1;
                Length = Io->AddressLength;
                DescriptorName = "FixedIO";
                break;
            }

            default:
                ACPI_PCIROOT_TRACE("  |_ _CRS resource type %u not decoded\n",
                                   Resource->Type);
                break;
        }

        if (DescriptorName)
        {
            ACPI_PCIROOT_TRACE("  |_ _CRS %s 0x%I64x-0x%I64x (len=0x%I64x)\n",
                               DescriptorName,
                               Minimum,
                               Maximum,
                               Length);
        }

        Resource = ACPI_NEXT_RESOURCE(Resource);
    }

    if (!FoundBusRange)
    {
        ACPI_PCIROOT_TRACE("  |_ WARNING: _CRS does not expose a bus-number range\n");
    }

    if (Buffer.Pointer)
    {
        AcpiOsFree(Buffer.Pointer);
    }
}
