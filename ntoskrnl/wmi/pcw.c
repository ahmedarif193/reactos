/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Performance Counter for Windows provider compatibility
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* TYPES *********************************************************************/

typedef struct _PCW_REGISTRATION
{
    ULONG Reserved;
} PCW_REGISTRATION, *PPCW_REGISTRATION;

/* FUNCTIONS *****************************************************************/

NTSTATUS
NTAPI
PcwRegister(
    _Outptr_ PPCW_REGISTRATION *Registration,
    _In_ PVOID RegistrationInformation)
{
    PPCW_REGISTRATION NewRegistration;

    if ((Registration == NULL) || (RegistrationInformation == NULL))
        return STATUS_INVALID_PARAMETER;

    NewRegistration = ExAllocatePoolWithTag(NonPagedPool,
                                            sizeof(*NewRegistration),
                                            'wcPP');
    if (NewRegistration == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;

    NewRegistration->Reserved = 0;
    *Registration = NewRegistration;
    return STATUS_SUCCESS;
}

VOID
NTAPI
PcwUnregister(
    _In_opt_ PPCW_REGISTRATION Registration)
{
    if (Registration != NULL)
        ExFreePoolWithTag(Registration, 'wcPP');
}

NTSTATUS
NTAPI
PcwAddInstance(
    _In_ PVOID Buffer,
    _In_ PCUNICODE_STRING Name,
    _In_ ULONG Id,
    _In_ ULONG Count,
    _In_reads_opt_(Count) PVOID Data)
{
    UNREFERENCED_PARAMETER(Buffer);
    UNREFERENCED_PARAMETER(Name);
    UNREFERENCED_PARAMETER(Id);
    UNREFERENCED_PARAMETER(Count);
    UNREFERENCED_PARAMETER(Data);

    /*
     * No PCW consumer exists yet, so there is no system buffer to populate.
     * Returning success keeps optional provider registration non-fatal.
     */
    return STATUS_SUCCESS;
}
