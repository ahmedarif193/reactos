/*
 * PROJECT:         ReactOS Operating System
 * LICENSE:         GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:         Driver Infrastructure Framework provider dispatch
 * COPYRIGHT:       Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>

#define NDEBUG
#include <debug.h>

/* GLOBALS *******************************************************************/

static const IOP_DIF_PROVIDER *volatile IopDifProvider;

/* INTERNAL FUNCTIONS ********************************************************/

NTSTATUS
NTAPI
IopRegisterDifProvider(
    _In_ const IOP_DIF_PROVIDER *Provider)
{
    PAGED_CODE();

    if ((Provider == NULL) ||
        (Provider->RegisterClassDriverPlugin == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }

    /* The DIF implementation is a permanent kernel component. */
    if (InterlockedCompareExchangePointer((PVOID volatile *)&IopDifProvider,
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
DifRegisterClassDriverPlugin(
    _In_ ULONG Version,
    _In_opt_ PVOID Plugin,
    _In_ ULONG Flags,
    _In_opt_ PDRIVER_OBJECT DriverObject)
{
    const IOP_DIF_PROVIDER *Provider;

    PAGED_CODE();

    Provider = InterlockedCompareExchangePointer(
        (PVOID volatile *)&IopDifProvider,
        NULL,
        NULL);
    if (Provider == NULL)
        return STATUS_DIF_DRIVER_PLUGIN_MISMATCH;

    return Provider->RegisterClassDriverPlugin(Version,
                                                Plugin,
                                                Flags,
                                                DriverObject);
}
