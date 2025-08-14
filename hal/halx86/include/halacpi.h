#pragma once

//
// Internal HAL structure
//
typedef struct _ACPI_CACHED_TABLE
{
    LIST_ENTRY Links;
    DESCRIPTION_HEADER Header;
    // table follows
    // ...
} ACPI_CACHED_TABLE, *PACPI_CACHED_TABLE;

NTSTATUS
NTAPI
HalpAcpiTableCacheInit(
    IN PLOADER_PARAMETER_BLOCK LoaderBlock
);

PVOID
NTAPI
HalpAcpiGetTable(
    IN PLOADER_PARAMETER_BLOCK LoaderBlock,
    IN ULONG Signature
);

CODE_SEG("INIT")
NTSTATUS
NTAPI
HalpSetupAcpiPhase0(
    IN PLOADER_PARAMETER_BLOCK LoaderBlock
);

PVOID
NTAPI
HalAcpiGetTable(
    IN PLOADER_PARAMETER_BLOCK LoaderBlock,
    IN ULONG Signature
);

//
// ACPI PCI Interrupt Routing Functions
//
NTSTATUS
NTAPI
HalpAcpiParsePrtTable(VOID);

ULONG
NTAPI
HalpAcpiGetPciInterrupt(
    IN UCHAR Bus,
    IN UCHAR Device, 
    IN UCHAR Function,
    IN UCHAR Pin
);

//
// ACPI Power Management Functions
//
NTSTATUS
NTAPI
HalpAcpiInitializePowerManagement(VOID);

NTSTATUS
NTAPI
HalpAcpiSetDevicePowerState(
    IN UCHAR Bus,
    IN UCHAR Device,
    IN UCHAR Function,
    IN ACPI_POWER_STATE PowerState
);

ACPI_POWER_STATE
NTAPI
HalpAcpiGetDevicePowerState(
    IN UCHAR Bus,
    IN UCHAR Device,
    IN UCHAR Function
);

BOOLEAN
NTAPI
HalpAcpiCanDeviceWakeSystem(
    IN UCHAR Bus,
    IN UCHAR Device,
    IN UCHAR Function,
    IN ACPI_POWER_STATE FromState
);

//
// ACPI Resource Management Functions (_CRS/_PRS/_SRS)
//
NTSTATUS
NTAPI
HalpAcpiInitializeResourceManagement(VOID);

NTSTATUS
NTAPI
HalpAcpiGetCurrentResources(
    IN UCHAR Bus,
    IN UCHAR Device,
    IN UCHAR Function,
    OUT PACPI_RESOURCE_LIST ResourceList
);

NTSTATUS
NTAPI
HalpAcpiGetPossibleResources(
    IN UCHAR Bus,
    IN UCHAR Device,
    IN UCHAR Function,
    OUT PACPI_RESOURCE_LIST ResourceList
);

NTSTATUS
NTAPI
HalpAcpiSetResources(
    IN UCHAR Bus,
    IN UCHAR Device,
    IN UCHAR Function,
    IN PACPI_RESOURCE_LIST ResourceList
);

BOOLEAN
NTAPI
HalpAcpiAreResourcesAssigned(
    IN UCHAR Bus,
    IN UCHAR Device,
    IN UCHAR Function
);

NTSTATUS
NTAPI
HalpAcpiRebalanceResources(
    IN UCHAR StartBus,
    IN UCHAR EndBus
);

/* EOF */
