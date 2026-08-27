/*
 * PROJECT:         ReactOS Operating System
 * LICENSE:         GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:         Terminal Topology Manager notification dispatch
 * COPYRIGHT:       Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>

#define NDEBUG
#include <debug.h>

/* PRIVATE TYPES *************************************************************/

typedef struct _POP_TTM_DEVICE_INFORMATION
{
    PVOID Value[4];
} POP_TTM_DEVICE_INFORMATION, *PPOP_TTM_DEVICE_INFORMATION;

typedef struct _POP_TTM_ARRIVAL_INFORMATION
{
    ULONGLONG Reserved;
    PUNICODE_STRING InstanceName;
} POP_TTM_ARRIVAL_INFORMATION, *PPOP_TTM_ARRIVAL_INFORMATION;

/* GLOBALS *******************************************************************/

static const POP_TTM_PROVIDER *volatile PopTtmProvider;

/* PRIVATE FUNCTIONS *********************************************************/

static
const POP_TTM_PROVIDER *
PopGetTtmProvider(VOID)
{
    return InterlockedCompareExchangePointer((PVOID volatile *)&PopTtmProvider,
                                             NULL,
                                             NULL);
}

static
NTSTATUS
PopValidateTtmArrival(
    _In_opt_ PVOID DeviceInformation,
    _In_opt_ PVOID ArrivalInformation)
{
    PPOP_TTM_DEVICE_INFORMATION Information = DeviceInformation;
    PPOP_TTM_ARRIVAL_INFORMATION Arrival = ArrivalInformation;
    PUNICODE_STRING InstanceName;

    if ((Information == NULL) || (Information->Value[0] == NULL) ||
        ((Information->Value[3] != NULL) && (Information->Value[2] == NULL)))
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (Arrival == NULL)
        return STATUS_SUCCESS;

    InstanceName = Arrival->InstanceName;
    if ((InstanceName == NULL) ||
        ((InstanceName->Length & (sizeof(WCHAR) - 1)) != 0) ||
        (InstanceName->Length > InstanceName->MaximumLength) ||
        ((InstanceName->Length != 0) && (InstanceName->Buffer == NULL)))
    {
        return STATUS_INVALID_PARAMETER;
    }

    return STATUS_SUCCESS;
}

/* INTERNAL FUNCTIONS ********************************************************/

NTSTATUS
NTAPI
PopRegisterTtmProvider(
    _In_ const POP_TTM_PROVIDER *Provider)
{
    PAGED_CODE();

    if ((Provider == NULL) ||
        (Provider->NotifyDeviceArrival == NULL) ||
        (Provider->NotifyDeviceDeparture == NULL) ||
        (Provider->NotifyDeviceInput == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (InterlockedCompareExchangePointer((PVOID volatile *)&PopTtmProvider,
                                          (PVOID)Provider,
                                          NULL) != NULL)
    {
        return STATUS_ALREADY_REGISTERED;
    }

    return STATUS_SUCCESS;
}

/* PUBLIC FUNCTIONS **********************************************************/

NTSTATUS
NTAPI
TtmNotifyDeviceArrival(
    _In_ ULONG DeviceType,
    _In_opt_ PVOID DeviceId,
    _In_opt_ PVOID DeviceInformation,
    _In_ ULONG Flags,
    _In_opt_ PVOID ArrivalInformation)
{
    const POP_TTM_PROVIDER *Provider;
    NTSTATUS Status;

    PAGED_CODE();

    Status = PopValidateTtmArrival(DeviceInformation, ArrivalInformation);
    if (!NT_SUCCESS(Status))
        return Status;

    Provider = PopGetTtmProvider();
    if (Provider == NULL)
        return STATUS_NOT_SUPPORTED;

    return Provider->NotifyDeviceArrival(DeviceType,
                                          DeviceId,
                                          DeviceInformation,
                                          Flags,
                                          ArrivalInformation);
}

VOID
NTAPI
TtmNotifyDeviceDeparture(
    _In_ ULONG DeviceType,
    _In_opt_ PVOID DeviceId)
{
    const POP_TTM_PROVIDER *Provider;

    PAGED_CODE();

    Provider = PopGetTtmProvider();
    if (Provider != NULL)
        Provider->NotifyDeviceDeparture(DeviceType, DeviceId);
}

VOID
NTAPI
TtmNotifyDeviceInput(
    _In_ ULONG DeviceType,
    _In_opt_ PVOID DeviceId,
    _In_ ULONG Flags)
{
    const POP_TTM_PROVIDER *Provider;

    PAGED_CODE();

    Provider = PopGetTtmProvider();
    if (Provider != NULL)
        Provider->NotifyDeviceInput(DeviceType, DeviceId, Flags);
}
