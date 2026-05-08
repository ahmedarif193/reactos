/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            ntoskrnl/arch/arm64/config/cmhardwr.c
 * PURPOSE:         Machine-dependent configuration for ARM64
 * COPYRIGHT:       Copyright 2025 Ahmed Arif (arif.ing@outlook.com)
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* Global configuration strings */
PCHAR CmpFullCpuID = "%s Family %u Model %u Stepping %u";

static
ULONGLONG
CmpArm64ReadMidr(VOID)
{
    ULONGLONG Midr;

    __asm__ __volatile__("mrs %0, midr_el1" : "=r"(Midr));
    return Midr;
}

static
const WCHAR*
CmpArm64GetVendorName(
    _In_ ULONG Implementer)
{
    switch (Implementer)
    {
        case 0x41: return L"ARM Limited";
        case 0x42: return L"Broadcom";
        case 0x43: return L"Cavium";
        case 0x44: return L"Digital Equipment Corporation";
        case 0x46: return L"Fujitsu";
        case 0x48: return L"HiSilicon";
        case 0x4E: return L"NVIDIA";
        case 0x50: return L"AppliedMicro";
        case 0x51: return L"Qualcomm";
        case 0x53: return L"Samsung";
        case 0x56: return L"Marvell";
        case 0x61: return L"Apple";
        case 0x69: return L"Intel";
        default: return L"ARM64";
    }
}

static
const WCHAR*
CmpArm64GetArmPartName(
    _In_ ULONG PartNumber)
{
    switch (PartNumber)
    {
        case 0xD03: return L"ARM Cortex-A53";
        case 0xD04: return L"ARM Cortex-A35";
        case 0xD05: return L"ARM Cortex-A55";
        case 0xD07: return L"ARM Cortex-A57";
        case 0xD08: return L"ARM Cortex-A72";
        case 0xD09: return L"ARM Cortex-A73";
        case 0xD0A: return L"ARM Cortex-A75";
        case 0xD0B: return L"ARM Cortex-A76";
        case 0xD0C: return L"ARM Neoverse N1";
        case 0xD0D: return L"ARM Cortex-A77";
        case 0xD0E: return L"ARM Cortex-A76AE";
        case 0xD40: return L"ARM Neoverse V1";
        case 0xD41: return L"ARM Cortex-A78";
        case 0xD44: return L"ARM Cortex-X1";
        case 0xD46: return L"ARM Cortex-A510";
        case 0xD47: return L"ARM Cortex-A710";
        case 0xD48: return L"ARM Cortex-X2";
        case 0xD49: return L"ARM Neoverse N2";
        case 0xD4D: return L"ARM Cortex-A715";
        case 0xD4E: return L"ARM Cortex-X3";
        case 0xD4F: return L"ARM Neoverse V2";
        case 0xD80: return L"ARM Cortex-A520";
        case 0xD81: return L"ARM Cortex-A720";
        case 0xD82: return L"ARM Cortex-X4";
        default: return NULL;
    }
}

static
VOID
CmpArm64BuildProcessorName(
    _Out_writes_bytes_(BufferSize) PWCHAR Buffer,
    _In_ SIZE_T BufferSize,
    _In_ ULONG Implementer,
    _In_ ULONG Architecture,
    _In_ ULONG PartNumber)
{
    const WCHAR *PartName = NULL;

    if (Implementer == 0x41)
        PartName = CmpArm64GetArmPartName(PartNumber);

    if (PartName)
    {
        RtlStringCbCopyW(Buffer, BufferSize, PartName);
        return;
    }

    if (Architecture == 0xF)
    {
        RtlStringCbPrintfW(Buffer,
                           BufferSize,
                           L"%s ARM64 Processor (Part 0x%03lx)",
                           CmpArm64GetVendorName(Implementer),
                           PartNumber);
    }
    else
    {
        RtlStringCbPrintfW(Buffer,
                           BufferSize,
                           L"%s ARMv%lu Processor (Part 0x%03lx)",
                           CmpArm64GetVendorName(Implementer),
                           Architecture,
                           PartNumber);
    }
}

NTSTATUS
NTAPI
CmpInitializeMachineDependentConfiguration(_In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    UNICODE_STRING KeyName;
    OBJECT_ATTRIBUTES ObjectAttributes;
    ULONG i, Disposition;
    NTSTATUS Status;
    HANDLE KeyHandle, SystemHandle;
    CONFIGURATION_COMPONENT_DATA ConfigData;
    CHAR Buffer[128];
    PKPRCB Prcb;
    ULONGLONG Midr;
    ULONG Implementer, Architecture, Variant, PartNumber, Revision;
    const WCHAR *VendorName;
    USHORT IndexTable[MaximumType + 1] = {0};

    UNREFERENCED_PARAMETER(LoaderBlock);

    DPRINT("ARM64: Initializing machine-dependent configuration\n");

    Midr = CmpArm64ReadMidr();
    Implementer = (ULONG)((Midr >> 24) & 0xFF);
    Variant = (ULONG)((Midr >> 20) & 0xF);
    Architecture = (ULONG)((Midr >> 16) & 0xF);
    PartNumber = (ULONG)((Midr >> 4) & 0xFFF);
    Revision = (ULONG)(Midr & 0xF);
    VendorName = CmpArm64GetVendorName(Implementer);

    /* Open the hardware description key */
    RtlInitUnicodeString(&KeyName,
                         L"\\Registry\\Machine\\Hardware\\Description\\System");
    InitializeObjectAttributes(&ObjectAttributes,
                               &KeyName,
                               OBJ_CASE_INSENSITIVE,
                               NULL,
                               NULL);
    Status = NtOpenKey(&SystemHandle, KEY_READ | KEY_WRITE, &ObjectAttributes);
    if (!NT_SUCCESS(Status))
    {
        DPRINT1("ARM64: Failed to open Hardware\\Description\\System key: 0x%lx\n", Status);
        return Status;
    }

    /* Create the CentralProcessor key */
    RtlInitUnicodeString(&KeyName, L"CentralProcessor");
    InitializeObjectAttributes(&ObjectAttributes,
                               &KeyName,
                               OBJ_CASE_INSENSITIVE,
                               SystemHandle,
                               NULL);
    Status = NtCreateKey(&KeyHandle,
                         KEY_READ | KEY_WRITE,
                         &ObjectAttributes,
                         0,
                         NULL,
                         0,
                         &Disposition);
    NtClose(KeyHandle);

    /* Check if the key was newly created */
    if (Disposition == REG_CREATED_NEW_KEY)
    {
        DPRINT("ARM64: Creating processor registry entries for %u processor(s)\n",
               KeNumberProcessors);

        /* Allocate configuration data buffer */
        CmpConfigurationData = ExAllocatePoolWithTag(PagedPool,
                                                     CmpConfigurationAreaSize,
                                                     TAG_CM);
        if (!CmpConfigurationData)
        {
            NtClose(SystemHandle);
            return STATUS_INSUFFICIENT_RESOURCES;
        }

        /* Loop through all processors */
        for (i = 0; i < KeNumberProcessors; i++)
        {
            /* Get the PRCB for this processor */
            Prcb = KiProcessorBlock[i];

            /* Setup the configuration entry for the processor */
            RtlZeroMemory(&ConfigData, sizeof(ConfigData));
            ConfigData.ComponentEntry.Class = ProcessorClass;
            ConfigData.ComponentEntry.Type = CentralProcessor;
            ConfigData.ComponentEntry.Key = i;
            ConfigData.ComponentEntry.AffinityMask = AFFINITY_MASK(i);
            ConfigData.ComponentEntry.Identifier = Buffer;

            RtlStringCbPrintfA(Buffer,
                               sizeof(Buffer),
                               CmpFullCpuID,
                               (Architecture == 0xF) ? "ARM64" : "ARM",
                               (Architecture == 0xF) ? 8 : Architecture,
                               PartNumber,
                               (Variant << 4) | Revision);

            /* Save the identifier string length */
            ConfigData.ComponentEntry.IdentifierLength = (ULONG)strlen(Buffer) + 1;

            DPRINT("ARM64: CPU %u Identifier: %s\n", i, Buffer);

            /* Initialize the registry node for this processor */
            Status = CmpInitializeRegistryNode(&ConfigData,
                                               SystemHandle,
                                               &KeyHandle,
                                               InterfaceTypeUndefined,
                                               0xFFFFFFFF,
                                               IndexTable);
            if (!NT_SUCCESS(Status))
            {
                DPRINT1("ARM64: Failed to create registry node for CPU %u: 0x%lx\n", i, Status);
                ExFreePoolWithTag(CmpConfigurationData, TAG_CM);
                NtClose(SystemHandle);
                return Status;
            }

            /*
             * Add ARM64-specific registry values
             */

            /* Set VendorIdentifier based on MIDR_EL1 implementer field */
            {
                UNICODE_STRING ValueName;
                ULONG Length;

                RtlInitUnicodeString(&ValueName, L"VendorIdentifier");
                Length = (ULONG)((wcslen(VendorName) + 1) * sizeof(WCHAR));
                Status = NtSetValueKey(KeyHandle,
                                       &ValueName,
                                       0,
                                       REG_SZ,
                                       (PVOID)VendorName,
                                       Length);

                DPRINT("ARM64: Set VendorIdentifier = %S (status: 0x%lx)\n", VendorName, Status);
            }

            /* Set processor speed if available */
            if (Prcb->MHz)
            {
                UNICODE_STRING ValueName;
                RtlInitUnicodeString(&ValueName, L"~MHz");
                Status = NtSetValueKey(KeyHandle,
                                       &ValueName,
                                       0,
                                       REG_DWORD,
                                       &Prcb->MHz,
                                       sizeof(Prcb->MHz));

                DPRINT("ARM64: Set MHz = %u (status: 0x%lx)\n", Prcb->MHz, Status);
            }

            /* Set the processor name string from MIDR_EL1 */
            {
                WCHAR ProcessorName[64];
                UNICODE_STRING ValueName;
                ULONG Length;

                CmpArm64BuildProcessorName(ProcessorName,
                                           sizeof(ProcessorName),
                                           Implementer,
                                           Architecture,
                                           PartNumber);

                RtlInitUnicodeString(&ValueName, L"ProcessorNameString");
                Length = (ULONG)((wcslen(ProcessorName) + 1) * sizeof(WCHAR));
                Status = NtSetValueKey(KeyHandle,
                                       &ValueName,
                                       0,
                                       REG_SZ,
                                       ProcessorName,
                                       Length);

                DPRINT("ARM64: Set ProcessorNameString = %S (status: 0x%lx)\n",
                       ProcessorName, Status);
            }

            /* Close the processor key handle */
            NtClose(KeyHandle);
        }

        /* Free the configuration data buffer */
        ExFreePoolWithTag(CmpConfigurationData, TAG_CM);
    }
    else
    {
        DPRINT("ARM64: CentralProcessor key already exists\n");
    }

    /* Close the System handle */
    NtClose(SystemHandle);

    DPRINT("ARM64: Machine-dependent configuration completed successfully\n");

    return STATUS_SUCCESS;
}
