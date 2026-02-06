/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            ntoskrnl/wmi/smbios.c
 * PURPOSE:         I/O Windows Management Instrumentation (WMI) Support
 * PROGRAMMERS:     Alex Ionescu (alex.ionescu@reactos.org)
 */

/* INCLUDES *****************************************************************/

#include <ntoskrnl.h>
#include <wmiguid.h>
#include <wmidata.h>
#include <wmistr.h>

#include "wmip.h"

/* SMBIOS data structure shared with bootloader */
#include <reactos/arc/loaderblk.h>

#define NDEBUG
#include <debug.h>

/* FUNCTIONS *****************************************************************/

/* SMBIOS entry point structures are defined in loaderblk.h */

static
BOOLEAN
GetEntryPointData(
    _In_ const UCHAR *EntryPointAddress,
    _Out_ PULONG64 TableAddress,
    _Out_ PULONG TableSize,
    _Out_ PMSSmBios_RawSMBiosTables BiosTablesHeader)
{
    PSMBIOS_ENTRY_POINT EntryPoint21;
    PSMBIOS3_ENTRY_POINT EntryPoint30;
    UCHAR Checksum;
    ULONG i;

    /* Check for SMBIOS 2.x entry point */
    EntryPoint21 = (PSMBIOS_ENTRY_POINT)EntryPointAddress;
    if (RtlEqualMemory(EntryPoint21->Anchor, "_SM_", 4))
    {
        if (EntryPoint21->Length > 32)
            return FALSE;

        /* Calculate the checksum */
        Checksum = 0;
        for (i = 0; i < EntryPoint21->Length; i++)
        {
            Checksum += EntryPointAddress[i];
        }

        if (Checksum != 0)
            return FALSE;

        *TableAddress = EntryPoint21->TableAddress;
        *TableSize = EntryPoint21->TableLength;
        BiosTablesHeader->Used20CallingMethod = 0;
        BiosTablesHeader->SmbiosMajorVersion = EntryPoint21->MajorVersion;
        BiosTablesHeader->SmbiosMinorVersion = EntryPoint21->MinorVersion;
        BiosTablesHeader->DmiRevision = 2;
        BiosTablesHeader->Size = EntryPoint21->TableLength;
        return TRUE;
    }

    /* Check for SMBIOS 3.0+ entry point */
    EntryPoint30 = (PSMBIOS3_ENTRY_POINT)EntryPointAddress;
    if (RtlEqualMemory(EntryPoint30->Anchor, "_SM3_", 5))
    {
        if (EntryPoint30->Length > 32)
            return FALSE;

        /* Calculate the checksum */
        Checksum = 0;
        for (i = 0; i < EntryPoint30->Length; i++)
        {
            Checksum += EntryPointAddress[i];
        }

        if (Checksum != 0)
            return FALSE;

        *TableAddress = EntryPoint30->TableAddress;
        *TableSize = EntryPoint30->MaxStructureSize;
        BiosTablesHeader->Used20CallingMethod = 0;
        BiosTablesHeader->SmbiosMajorVersion = EntryPoint30->MajorVersion;
        BiosTablesHeader->SmbiosMinorVersion = EntryPoint30->MinorVersion;
        BiosTablesHeader->DmiRevision = 3;
        BiosTablesHeader->Size = EntryPoint30->MaxStructureSize;
        return TRUE;
    }

    return FALSE;
}

/*
 * Try to get SMBIOS data from registry (populated by bootloader for UEFI systems).
 * Searches HARDWARE\DESCRIPTION\System\MultifunctionAdapter\* for "SMBIOS" identifier.
 */
static
NTSTATUS
WmipGetSMBiosFromRegistry(
    _Out_ PULONG64 TableAddress,
    _Out_ PULONG TableSize,
    _Out_ PMSSmBios_RawSMBiosTables BiosTablesHeader)
{
    UNICODE_STRING MultiKeyPath = RTL_CONSTANT_STRING(
        L"\\Registry\\Machine\\HARDWARE\\DESCRIPTION\\System\\MultifunctionAdapter");
    UNICODE_STRING IdentifierName = RTL_CONSTANT_STRING(L"Identifier");
    UNICODE_STRING ConfigDataName = RTL_CONSTANT_STRING(L"Configuration Data");
    OBJECT_ATTRIBUTES ObjectAttributes;
    HANDLE MultiKey = NULL, SubKey = NULL;
    NTSTATUS Status;
    ULONG Index;
    WCHAR SubKeyName[16];
    UNICODE_STRING SubKeyString;
    PKEY_VALUE_PARTIAL_INFORMATION ValueInfo = NULL;
    ULONG ValueSize;
    PSMBIOS_BIOS_DATA SmbiosData;
    PCM_FULL_RESOURCE_DESCRIPTOR FullDesc;
    PCM_PARTIAL_RESOURCE_DESCRIPTOR PartialDesc;

    *TableAddress = 0;
    *TableSize = 0;

    InitializeObjectAttributes(&ObjectAttributes,
                               &MultiKeyPath,
                               OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                               NULL,
                               NULL);

    Status = ZwOpenKey(&MultiKey, KEY_READ, &ObjectAttributes);
    if (!NT_SUCCESS(Status))
    {
        DPRINT("Failed to open MultifunctionAdapter key: 0x%08lx\n", Status);
        return Status;
    }

    /* Enumerate subkeys (0, 1, 2, ...) looking for SMBIOS */
    for (Index = 0; Index < 10; Index++)
    {
        swprintf(SubKeyName, L"%lu", Index);
        RtlInitUnicodeString(&SubKeyString, SubKeyName);

        InitializeObjectAttributes(&ObjectAttributes,
                                   &SubKeyString,
                                   OBJ_CASE_INSENSITIVE | OBJ_KERNEL_HANDLE,
                                   MultiKey,
                                   NULL);

        Status = ZwOpenKey(&SubKey, KEY_READ, &ObjectAttributes);
        if (!NT_SUCCESS(Status))
        {
            continue;
        }

        /* Check Identifier value */
        Status = ZwQueryValueKey(SubKey,
                                 &IdentifierName,
                                 KeyValuePartialInformation,
                                 NULL,
                                 0,
                                 &ValueSize);

        if (Status != STATUS_BUFFER_TOO_SMALL)
        {
            ZwClose(SubKey);
            continue;
        }

        ValueInfo = ExAllocatePoolWithTag(PagedPool, ValueSize, 'BTMS');
        if (ValueInfo == NULL)
        {
            ZwClose(SubKey);
            continue;
        }

        Status = ZwQueryValueKey(SubKey,
                                 &IdentifierName,
                                 KeyValuePartialInformation,
                                 ValueInfo,
                                 ValueSize,
                                 &ValueSize);

        if (!NT_SUCCESS(Status) || ValueInfo->Type != REG_SZ)
        {
            ExFreePoolWithTag(ValueInfo, 'BTMS');
            ZwClose(SubKey);
            continue;
        }

        /* Check if this is the SMBIOS entry */
        if (_wcsicmp((PWCHAR)ValueInfo->Data, L"SMBIOS") != 0)
        {
            ExFreePoolWithTag(ValueInfo, 'BTMS');
            ZwClose(SubKey);
            continue;
        }

        ExFreePoolWithTag(ValueInfo, 'BTMS');
        ValueInfo = NULL;

        /* Found SMBIOS - get Configuration Data */
        Status = ZwQueryValueKey(SubKey,
                                 &ConfigDataName,
                                 KeyValuePartialInformation,
                                 NULL,
                                 0,
                                 &ValueSize);

        if (Status != STATUS_BUFFER_TOO_SMALL)
        {
            DPRINT1("SMBIOS Configuration Data not found\n");
            ZwClose(SubKey);
            ZwClose(MultiKey);
            return STATUS_NOT_FOUND;
        }

        ValueInfo = ExAllocatePoolWithTag(PagedPool, ValueSize, 'BTMS');
        if (ValueInfo == NULL)
        {
            ZwClose(SubKey);
            ZwClose(MultiKey);
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        Status = ZwQueryValueKey(SubKey,
                                 &ConfigDataName,
                                 KeyValuePartialInformation,
                                 ValueInfo,
                                 ValueSize,
                                 &ValueSize);

        ZwClose(SubKey);
        ZwClose(MultiKey);

        if (!NT_SUCCESS(Status))
        {
            ExFreePoolWithTag(ValueInfo, 'BTMS');
            return Status;
        }

        /* Configuration Data should be REG_FULL_RESOURCE_DESCRIPTOR */
        if (ValueInfo->Type != REG_FULL_RESOURCE_DESCRIPTOR)
        {
            ExFreePoolWithTag(ValueInfo, 'BTMS');
            return STATUS_NOT_FOUND;
        }

        /* Parse CM_FULL_RESOURCE_DESCRIPTOR to get SMBIOS_BIOS_DATA */
        FullDesc = (PCM_FULL_RESOURCE_DESCRIPTOR)ValueInfo->Data;
        if (FullDesc->PartialResourceList.Count < 1)
        {
            ExFreePoolWithTag(ValueInfo, 'BTMS');
            return STATUS_NOT_FOUND;
        }

        PartialDesc = &FullDesc->PartialResourceList.PartialDescriptors[0];
        if (PartialDesc->Type != CmResourceTypeDeviceSpecific ||
            PartialDesc->u.DeviceSpecificData.DataSize < sizeof(SMBIOS_BIOS_DATA))
        {
            ExFreePoolWithTag(ValueInfo, 'BTMS');
            return STATUS_NOT_FOUND;
        }

        /* Get SMBIOS data from after the descriptor */
        SmbiosData = (PSMBIOS_BIOS_DATA)(PartialDesc + 1);

        *TableAddress = SmbiosData->TableAddress.QuadPart;
        *TableSize = SmbiosData->TableSize;
        BiosTablesHeader->Used20CallingMethod = 0;
        BiosTablesHeader->SmbiosMajorVersion = SmbiosData->MajorVersion;
        BiosTablesHeader->SmbiosMinorVersion = SmbiosData->MinorVersion;
        BiosTablesHeader->DmiRevision = SmbiosData->DmiRevision;
        BiosTablesHeader->Size = SmbiosData->TableSize;

        DPRINT("Got SMBIOS from registry: table at 0x%llx, size %lu\n",
               *TableAddress, *TableSize);

        ExFreePoolWithTag(ValueInfo, 'BTMS');
        return STATUS_SUCCESS;
    }

    ZwClose(MultiKey);
    return STATUS_NOT_FOUND;
}

_At_(*OutTableData, __drv_allocatesMem(Mem))
NTSTATUS
NTAPI
WmipGetRawSMBiosTableData(
    _Outptr_opt_result_buffer_(*OutDataSize) PVOID *OutTableData,
    _Out_ PULONG OutDataSize)
{
    static const SIZE_T SearchSize = 0x10000;
    static const ULONG HeaderSize = FIELD_OFFSET(MSSmBios_RawSMBiosTables, SMBiosData);
    PHYSICAL_ADDRESS PhysicalAddress;
    PUCHAR EntryPointMapping;
    MSSmBios_RawSMBiosTables BiosTablesHeader;
    PVOID BiosTables, TableMapping;
    ULONG Offset, TableSize;
    ULONG64 TableAddress = 0;
    NTSTATUS Status;

    /*
     * First try to get SMBIOS from registry (for UEFI systems).
     * The bootloader passes SMBIOS info via the "SMBIOS" MultiFunctionAdapter.
     */
    Status = WmipGetSMBiosFromRegistry(&TableAddress, &TableSize, &BiosTablesHeader);
    if (NT_SUCCESS(Status) && TableAddress != 0)
    {
        DPRINT("Using SMBIOS from bootloader (UEFI path)\n");
        goto CopyTable;
    }

    /*
     * Fallback: scan legacy BIOS memory range 0xF0000-0xFFFFF.
     * This is where the range for the entry point starts on BIOS systems.
     */
    PhysicalAddress.QuadPart = 0xF0000;

    /* Map the range into the system address space */
    EntryPointMapping = MmMapIoSpace(PhysicalAddress, SearchSize, MmCached);
    if (EntryPointMapping == NULL)
    {
        DPRINT1("Failed to map range for SMBIOS entry point\n");
        return STATUS_UNSUCCESSFUL;
    }

    /* Loop the table memory in 16 byte steps */
    for (Offset = 0; Offset <= (0x10000 - 32); Offset += 16)
    {
        /* Check if we have an entry point here and get it's data */
        if (GetEntryPointData(EntryPointMapping + Offset,
                              &TableAddress,
                              &TableSize,
                              &BiosTablesHeader))
        {
            break;
        }
    }

    /* Unmap the entry point */
    MmUnmapIoSpace(EntryPointMapping, SearchSize);

    /* Did we find anything */
    if (TableAddress == 0)
    {
        DPRINT1("Could not find the SMBIOS entry point\n");
        return STATUS_NOT_FOUND;
    }

    DPRINT("Found SMBIOS via legacy scan at 0x%llx, size %lu\n",
           TableAddress, TableSize);

CopyTable:
    /* Check if the caller asked for the buffer */
    if (OutTableData != NULL)
    {
        /* Allocate a buffer for the result */
        BiosTables = ExAllocatePoolWithTag(PagedPool,
                                           HeaderSize + TableSize,
                                           'BTMS');
        if (BiosTables == NULL)
        {
            DPRINT1("Failed to allocate %lu bytes for the SMBIOS table\n",
                    HeaderSize + TableSize);
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        /* Copy the header */
        RtlCopyMemory(BiosTables, &BiosTablesHeader, HeaderSize);

        /* This is where the table is */
        PhysicalAddress.QuadPart = TableAddress;

        /* Map the table into the system address space */
        TableMapping = MmMapIoSpace(PhysicalAddress, TableSize, MmCached);
        if (TableMapping == NULL)
        {
            ExFreePoolWithTag(BiosTables, 'BTMS');
            return STATUS_UNSUCCESSFUL;
        }

        /* Copy the table */
        RtlCopyMemory((PUCHAR)BiosTables + HeaderSize, TableMapping, TableSize);

        /* Unmap the table */
        MmUnmapIoSpace(TableMapping, TableSize);

        *OutTableData = BiosTables;
    }

    *OutDataSize = HeaderSize + TableSize;
    return STATUS_SUCCESS;
}


NTSTATUS
NTAPI
WmipQueryRawSMBiosTables(
    _Inout_ ULONG *InOutBufferSize,
    _Out_opt_ PVOID OutBuffer)
{
    NTSTATUS Status;
    PVOID TableData = NULL;
    ULONG TableSize, ResultSize;
    PWNODE_ALL_DATA AllData;

    /* Get the table data */
    Status = WmipGetRawSMBiosTableData(OutBuffer ? &TableData : NULL, &TableSize);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("WmipGetRawSMBiosTableData failed: 0x%08lx\n", Status);
        return Status;
    }

    ResultSize = sizeof(WNODE_ALL_DATA) + TableSize;

    /* Check if the caller provided a buffer */
    if ((OutBuffer != NULL) && (*InOutBufferSize != 0))
    {
        /* Check if the buffer is large enough */
        if (*InOutBufferSize < ResultSize)
        {
            DPRINT1("Buffer too small. Got %lu, need %lu\n",
                    *InOutBufferSize, ResultSize);
            return STATUS_BUFFER_TOO_SMALL;
        }

        /// FIXME: most of this is fubar
        AllData = OutBuffer;
        AllData->WnodeHeader.BufferSize = ResultSize;
        AllData->WnodeHeader.ProviderId = 0;
        AllData->WnodeHeader.Version = 0;
        AllData->WnodeHeader.Linkage = 0; // last entry
        //AllData->WnodeHeader.CountLost;
        AllData->WnodeHeader.KernelHandle = NULL;
        //AllData->WnodeHeader.TimeStamp;
        AllData->WnodeHeader.Guid = MSSmBios_RawSMBiosTables_GUID;
        //AllData->WnodeHeader.ClientContext;
        AllData->WnodeHeader.Flags = WNODE_FLAG_FIXED_INSTANCE_SIZE;
        AllData->DataBlockOffset = sizeof(WNODE_ALL_DATA);
        AllData->InstanceCount = 1;
        //AllData->OffsetInstanceNameOffsets;
        AllData->FixedInstanceSize = TableSize;

        RtlCopyMemory(AllData + 1, TableData, TableSize);
    }

    /* Set the size */
    *InOutBufferSize = ResultSize;

    /* Free the table buffer */
    if (TableData != NULL)
    {
        ExFreePoolWithTag(TableData, 'BTMS');
    }

    return STATUS_SUCCESS;
}

