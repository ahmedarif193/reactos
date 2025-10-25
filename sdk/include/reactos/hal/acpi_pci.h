#pragma once

#include <ntdef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _HAL_ACPI_PCI_WINDOW
{
    BOOLEAN Present;
    ULONGLONG Base;
    ULONGLONG Limit;
} HAL_ACPI_PCI_WINDOW, *PHAL_ACPI_PCI_WINDOW;

typedef struct _HAL_ACPI_PCI_ROOT_INFO
{
    ULONG Segment;
    ULONG Bus;
    HAL_ACPI_PCI_WINDOW IoWindow;
    HAL_ACPI_PCI_WINDOW MemoryWindow;
    HAL_ACPI_PCI_WINDOW PrefetchWindow;
} HAL_ACPI_PCI_ROOT_INFO, *PHAL_ACPI_PCI_ROOT_INFO;

#define HAL_ACPI_POLARITY_HIGH   0
#define HAL_ACPI_POLARITY_LOW    1
#define HAL_ACPI_POLARITY_BOTH   3

#define HAL_ACPI_TRIGGER_EDGE    0
#define HAL_ACPI_TRIGGER_LEVEL   1

typedef
BOOLEAN
(NTAPI *PHAL_ACPI_PCI_ROUTE_QUERY)(
    _In_ ULONG Segment,
    _In_ UCHAR Bus,
    _In_ UCHAR Device,
    _In_ UCHAR Function,
    _In_ UCHAR Pin,
    _Out_ PULONG Gsi,
    _Out_opt_ PUCHAR Polarity,
    _Out_opt_ PUCHAR TriggerMode
    );

NTSYSAPI
VOID
NTAPI
HalpConfigurePciRootBridge(
    _In_ const HAL_ACPI_PCI_ROOT_INFO *Info
    );

NTSYSAPI
VOID
NTAPI
HalpRegisterPciRouteQuery(
    _In_opt_ PHAL_ACPI_PCI_ROUTE_QUERY Provider
    );

#ifdef __cplusplus
}
#endif

