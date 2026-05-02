#pragma once

#include <ntdef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _HAL_ACPI_C_STATE_REGISTER {
    UCHAR AddressSpaceId;
    UCHAR BitWidth;
    UCHAR BitOffset;
    UCHAR AccessSize;
    ULONGLONG Address;
} HAL_ACPI_C_STATE_REGISTER, *PHAL_ACPI_C_STATE_REGISTER;

typedef struct _HAL_ACPI_C_STATE_ENTRY {
    ULONG Type;
    ULONG Latency;
    HAL_ACPI_C_STATE_REGISTER Register;
    BOOLEAN RequiresCacheFlush;
} HAL_ACPI_C_STATE_ENTRY, *PHAL_ACPI_C_STATE_ENTRY;

#define HAL_ACPI_MAX_C_STATES 3

typedef struct _HAL_ACPI_C_STATE_INFO {
    ULONG Count;
    HAL_ACPI_C_STATE_ENTRY States[HAL_ACPI_MAX_C_STATES];
} HAL_ACPI_C_STATE_INFO, *PHAL_ACPI_C_STATE_INFO;

NTSTATUS
NTAPI
HalGetAcpiCStateInformation(
    _Out_ PHAL_ACPI_C_STATE_INFO Info);

BOOLEAN
NTAPI
HalIsAcpiBusMasterActive(VOID);

#ifdef __cplusplus
}
#endif
