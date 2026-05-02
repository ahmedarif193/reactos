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
    USHORT IndexTable[MaximumType + 1] = {0};

    UNREFERENCED_PARAMETER(LoaderBlock);

    DPRINT("ARM64: Initializing machine-dependent configuration\n");

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

            /*
             * Build ARM64 processor identifier string
             * Format: "ARM64 Family <arch> Model <impl> Stepping <variant>"
             *
             * For ARM64, we use:
             * - Family: Architecture version (8 for ARMv8)
             * - Model: Implementer/PartNum from MIDR_EL1
             * - Stepping: Variant/Revision from MIDR_EL1
             */
            sprintf(Buffer,
                    CmpFullCpuID,
                    "ARM64",
                    8,  /* ARMv8 architecture */
                    (ULONG)Prcb->CpuType,
                    (ULONG)(Prcb->CpuStep >> 8));

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
                const WCHAR *VendorName;
                UNICODE_STRING ValueName;
                ULONG Length;

                /*
                 * Common ARM64 implementers:
                 * 0x41 = ARM Limited
                 * 0x42 = Broadcom
                 * 0x43 = Cavium
                 * 0x44 = DEC
                 * 0x4E = NVIDIA
                 * 0x50 = APM
                 * 0x51 = Qualcomm
                 * 0x56 = Marvell
                 * For now, we'll use a generic "ARM64" vendor
                 */
                VendorName = L"ARM Limited";

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

            /* Set a generic processor name string for ARM64 */
            {
                WCHAR ProcessorName[64];
                UNICODE_STRING ValueName;
                ULONG Length;
                swprintf(ProcessorName, L"ARMv8 Processor");

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
