/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Machine-dependent registry configuration for RISC-V64
 *
 * Publishes HARDWARE\DESCRIPTION\System\CentralProcessor\<n> from the
 * device-tree/SBI processor record (ABI-128). RISC-V has no CPUID/MIDR:
 * identity comes from firmware, so the values below are firmware strings.
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

static
VOID
CmpRiscvSetString(
    _In_ HANDLE KeyHandle,
    _In_ PCWSTR Name,
    _In_ PCWSTR Value)
{
    UNICODE_STRING ValueName;
    NTSTATUS Status;

    RtlInitUnicodeString(&ValueName, Name);
    Status = NtSetValueKey(KeyHandle, &ValueName, 0, REG_SZ, (PVOID)Value, (ULONG)((wcslen(Value) + 1) * sizeof(WCHAR)));
    if (!NT_SUCCESS(Status))
        DPRINT1("RISC-V: failed to set %S: 0x%lx\n", Name, Status);
}

static
VOID
CmpRiscvSetDword(
    _In_ HANDLE KeyHandle,
    _In_ PCWSTR Name,
    _In_ ULONG Value)
{
    UNICODE_STRING ValueName;

    RtlInitUnicodeString(&ValueName, Name);
    NtSetValueKey(KeyHandle, &ValueName, 0, REG_DWORD, &Value, sizeof(Value));
}

/* SBI implementation IDs from the SBI specification, chapter "Base Extension". */
static
PCWSTR
CmpRiscvSbiImplementationName(
    _In_ ULONG_PTR ImplementationId)
{
    switch (ImplementationId)
    {
        case 0: return L"Berkeley Boot Loader";
        case 1: return L"OpenSBI";
        case 2: return L"Xvisor";
        case 3: return L"KVM";
        case 4: return L"RustSBI";
        case 5: return L"Diosix";
        case 6: return L"Coffer";
        case 7: return L"Xen Project";
        case 8: return L"PolarFire Hart Software Services";
        default: return L"RISC-V SBI";
    }
}

NTSTATUS
NTAPI
CmpInitializeMachineDependentConfiguration(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    const KI_RISCV_PROCESSOR_FEATURES *Features = &KiRiscvProcessorFeatures;
    USHORT IndexTable[MaximumType + 1] = {0};
    CONFIGURATION_COMPONENT_DATA ConfigData;
    OBJECT_ATTRIBUTES ObjectAttributes;
    UNICODE_STRING KeyName;
    HANDLE SystemHandle, KeyHandle;
    CHAR Identifier[64];
    WCHAR Wide[KI_RISCV_EXTENSION_LIST_SIZE];
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
        RtlStringCbPrintfA(Identifier, sizeof(Identifier), "RISC-V %s Hart %I64u",
                           Features->Valid ? Features->IsaBase : "rv64", Features->HartId);
        ConfigData.ComponentEntry.Identifier = Identifier;
        ConfigData.ComponentEntry.IdentifierLength = (ULONG)strlen(Identifier) + 1;

        Status = CmpInitializeRegistryNode(&ConfigData, SystemHandle, &KeyHandle, InterfaceTypeUndefined, 0xFFFFFFFF, IndexTable);
        if (!NT_SUCCESS(Status))
        {
            ExFreePoolWithTag(CmpConfigurationData, TAG_CM);
            NtClose(SystemHandle);
            return Status;
        }

        CmpRiscvSetString(KeyHandle, L"VendorIdentifier", CmpRiscvSbiImplementationName(Features->SbiImplId));
        RtlStringCbPrintfW(Wide, sizeof(Wide), L"RISC-V %S processor (%S)",
                           Features->Valid ? Features->IsaBase : "rv64", Features->MmuType[0] ? Features->MmuType : "sv39");
        CmpRiscvSetString(KeyHandle, L"ProcessorNameString", Wide);
        RtlStringCbPrintfW(Wide, sizeof(Wide), L"%S", Features->Extensions);
        CmpRiscvSetString(KeyHandle, L"Extensions", Wide);
        CmpRiscvSetDword(KeyHandle, L"FeatureSet", Features->Flags);
        CmpRiscvSetDword(KeyHandle, L"SbiSpecVersion", Features->SbiSpecVersion);
        if (KiProcessorBlock[Processor]->MHz)
            CmpRiscvSetDword(KeyHandle, L"~MHz", KiProcessorBlock[Processor]->MHz);
        NtClose(KeyHandle);
    }

    ExFreePoolWithTag(CmpConfigurationData, TAG_CM);
    NtClose(SystemHandle);
    return STATUS_SUCCESS;
}
