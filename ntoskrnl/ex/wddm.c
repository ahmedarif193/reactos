/*
 * PROJECT:         ReactOS Operating System
 * LICENSE:         GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:         Executive device address-space provider dispatch
 * COPYRIGHT:       Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>

#define NDEBUG
#include <debug.h>

/* GLOBALS *******************************************************************/

static const EXP_DEVICE_ADDRESS_SPACE_PROVIDER *volatile
    ExpDeviceAddressSpaceProvider;

/* INTERNAL FUNCTIONS ********************************************************/

NTSTATUS
NTAPI
ExpRegisterDeviceAddressSpaceProvider(
    _In_ const EXP_DEVICE_ADDRESS_SPACE_PROVIDER *Provider)
{
    PAGED_CODE();

    if ((Provider == NULL) || (Provider->ShareAddressSpace == NULL))
        return STATUS_INVALID_PARAMETER;

    /* The provider is a permanent kernel component and cannot be replaced. */
    if (InterlockedCompareExchangePointer(
            (PVOID volatile *)&ExpDeviceAddressSpaceProvider,
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
ExShareAddressSpaceWithDevice(
    _In_ PDEVICE_OBJECT PhysicalDeviceObject,
    _Out_ PULONG ReturnedAsid)
{
    const EXP_DEVICE_ADDRESS_SPACE_PROVIDER *Provider;

    PAGED_CODE();

    if (PhysicalDeviceObject == NULL)
        return STATUS_INVALID_PARAMETER_1;
    if (ReturnedAsid == NULL)
        return STATUS_INVALID_PARAMETER_2;

    Provider = InterlockedCompareExchangePointer(
        (PVOID volatile *)&ExpDeviceAddressSpaceProvider,
        NULL,
        NULL);
    if (Provider == NULL)
        return STATUS_NOT_SUPPORTED;

    return Provider->ShareAddressSpace(PhysicalDeviceObject, ReturnedAsid);
}
