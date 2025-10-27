#pragma once

#include <ntdef.h>

#ifdef __cplusplus
extern "C" {
#endif

#define HAL_ACPI_OSC_SUPPORT_EXTENDED_CONFIG   (1u << 0)
#define HAL_ACPI_OSC_SUPPORT_ASPM               (1u << 1)
#define HAL_ACPI_OSC_SUPPORT_CLOCK_POWER        (1u << 2)
#define HAL_ACPI_OSC_SUPPORT_SEGMENT_GROUPS     (1u << 3)
#define HAL_ACPI_OSC_SUPPORT_MSI                (1u << 4)
#define HAL_ACPI_OSC_SUPPORT_AER                (1u << 5)

#define HAL_ACPI_OSC_CONTROL_NATIVE_HOTPLUG     (1u << 0)
#define HAL_ACPI_OSC_CONTROL_SHPC_HOTPLUG       (1u << 1)
#define HAL_ACPI_OSC_CONTROL_NATIVE_PME         (1u << 2)
#define HAL_ACPI_OSC_CONTROL_NATIVE_AER         (1u << 3)
#define HAL_ACPI_OSC_CONTROL_EXPRESS_CAP        (1u << 4)

typedef struct _HAL_ACPI_PCI_WINDOW
{
    BOOLEAN Present;
    BOOLEAN HasTranslation;
    UCHAR TranslationType;
    UCHAR Reserved;
    ULONGLONG Base;
    ULONGLONG Limit;
    ULONGLONG Translation;
} HAL_ACPI_PCI_WINDOW, *PHAL_ACPI_PCI_WINDOW;

typedef struct _HAL_ACPI_PCI_OSC_INFO
{
    BOOLEAN Evaluated;
    BOOLEAN Failed;
    USHORT Reserved;
    ULONG StatusFlags;
    ULONG SupportSet;
    ULONG ControlRequest;
    ULONG ControlGranted;
} HAL_ACPI_PCI_OSC_INFO, *PHAL_ACPI_PCI_OSC_INFO;

typedef struct _HAL_ACPI_PCI_ROOT_INFO
{
    ULONG Segment;
    ULONG Bus;
    BOOLEAN BusRangePresent;
    ULONG BusStart;
    ULONG BusEnd;
    HAL_ACPI_PCI_WINDOW IoWindow;
    HAL_ACPI_PCI_WINDOW MemoryWindow;
    HAL_ACPI_PCI_WINDOW PrefetchWindow;
    HAL_ACPI_PCI_OSC_INFO Osc;
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

typedef struct _HAL_ACPI_PCI_ROUTE_ENTRY
{
    ULONG Segment;
    UCHAR Bus;
    UCHAR Device;
    UCHAR Pin;
    ULONG Gsi;
    UCHAR Polarity;
    UCHAR TriggerMode;
} HAL_ACPI_PCI_ROUTE_ENTRY, *PHAL_ACPI_PCI_ROUTE_ENTRY;

NTHALAPI
VOID
NTAPI
HalpConfigurePciRootBridge(
    _In_ const HAL_ACPI_PCI_ROOT_INFO *Info
    );

NTHALAPI
VOID
NTAPI
HalpRegisterPciRouteQuery(
    _In_opt_ PHAL_ACPI_PCI_ROUTE_QUERY Provider
    );

NTHALAPI
VOID
NTAPI
HalpSetPciRoutingMap(
    _In_reads_opt_(EntryCount) const HAL_ACPI_PCI_ROUTE_ENTRY *Entries,
    _In_ ULONG EntryCount
    );

#ifdef __cplusplus
}
#endif
