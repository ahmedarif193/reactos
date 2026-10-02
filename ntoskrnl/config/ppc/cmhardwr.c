/*
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Windows NT PowerPC hardware description in the registry
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

static
VOID
CmpPpcSetString(_In_ HANDLE KeyHandle, _In_ PCWSTR Name, _In_ PCWSTR Value)
{
    UNICODE_STRING ValueName;

    RtlInitUnicodeString(&ValueName, Name);
    NtSetValueKey(KeyHandle, &ValueName, 0, REG_SZ, (PVOID)Value, (ULONG)(wcslen(Value) + 1) * sizeof(WCHAR));
}

static
VOID
CmpPpcSetDword(_In_ HANDLE KeyHandle, _In_ PCWSTR Name, _In_ ULONG Value)
{
    UNICODE_STRING ValueName;

    RtlInitUnicodeString(&ValueName, Name);
    NtSetValueKey(KeyHandle, &ValueName, 0, REG_DWORD, &Value, sizeof(Value));
}

static
PCSTR
CmpPpcProcessorName(_In_ USHORT Level)
{
    switch (Level)
    {
        case 1: return "601";
        case 3: return "603";
        case 4: return "604";
        case 6: return "603e";
        case 7: return "603ev";
        case 8: return "750";
        case 9: return "604e";
        case 12: return "7400";
        default: return "PowerPC";
    }
}

NTSTATUS
NTAPI
CmpInitializeMachineDependentConfiguration(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    USHORT IndexTable[MaximumType + 1] = {0};
    CONFIGURATION_COMPONENT_DATA ConfigData;
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING KeyName;
    HANDLE SystemHandle, KeyHandle;
    CHAR Identifier[64];
    WCHAR Wide[64];
    ULONG Processor, Disposition;
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(LoaderBlock);

    RtlInitUnicodeString(&KeyName, L"\\Registry\\Machine\\Hardware\\Description\\System");
    InitializeObjectAttributes(&ObjectAttributes, &KeyName, OBJ_CASE_INSENSITIVE, NULL, NULL);
    Status = NtOpenKey(&SystemHandle, KEY_READ | KEY_WRITE, &ObjectAttributes);
    if (!NT_SUCCESS(Status))
        return Status;

    RtlInitUnicodeString(&KeyName, L"CentralProcessor");
    InitializeObjectAttributes(&ObjectAttributes, &KeyName, OBJ_CASE_INSENSITIVE, SystemHandle, NULL);
    Status = NtCreateKey(&KeyHandle, KEY_READ | KEY_WRITE, &ObjectAttributes, 0, NULL, 0, &Disposition);
    if (!NT_SUCCESS(Status))
    {
        NtClose(SystemHandle);
        return Status;
    }
    NtClose(KeyHandle);
    if (Disposition != REG_CREATED_NEW_KEY)
    {
        NtClose(SystemHandle);
        return STATUS_SUCCESS;
    }

    CmpConfigurationData = ExAllocatePoolWithTag(PagedPool, CmpConfigurationAreaSize, TAG_CM);
    if (!CmpConfigurationData)
    {
        NtClose(SystemHandle);
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    for (Processor = 0; Processor < (ULONG)KeNumberProcessors; Processor++)
    {
        RtlZeroMemory(&ConfigData, sizeof(ConfigData));
        ConfigData.ComponentEntry.Class = ProcessorClass;
        ConfigData.ComponentEntry.Type = CentralProcessor;
        ConfigData.ComponentEntry.Key = Processor;
        ConfigData.ComponentEntry.AffinityMask = AFFINITY_MASK(Processor);
        RtlStringCbPrintfA(Identifier, sizeof(Identifier), "PowerPC %s", CmpPpcProcessorName(KeProcessorLevel));
        ConfigData.ComponentEntry.Identifier = Identifier;
        ConfigData.ComponentEntry.IdentifierLength = (ULONG)strlen(Identifier) + 1;

        Status = CmpInitializeRegistryNode(&ConfigData, SystemHandle, &KeyHandle, InterfaceTypeUndefined, 0xFFFFFFFF, IndexTable);
        if (!NT_SUCCESS(Status))
        {
            ExFreePoolWithTag(CmpConfigurationData, TAG_CM);
            NtClose(SystemHandle);
            return Status;
        }

        CmpPpcSetString(KeyHandle, L"VendorIdentifier", L"PowerPC");
        RtlStringCbPrintfW(Wide, sizeof(Wide), L"PowerPC %S processor", CmpPpcProcessorName(KeProcessorLevel));
        CmpPpcSetString(KeyHandle, L"ProcessorNameString", Wide);
        if (KiProcessorBlock[Processor]->MHz)
            CmpPpcSetDword(KeyHandle, L"~MHz", KiProcessorBlock[Processor]->MHz);
        NtClose(KeyHandle);
    }

    ExFreePoolWithTag(CmpConfigurationData, TAG_CM);
    NtClose(SystemHandle);
    return STATUS_SUCCESS;
}
