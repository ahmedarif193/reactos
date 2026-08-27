/*
 * PROJECT:         ReactOS Operating System
 * LICENSE:         GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:         Kernel soft-restart persistent-memory contract dispatch
 * COPYRIGHT:       Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>

#define NDEBUG
#include <debug.h>

/* GLOBALS *******************************************************************/

static PVOID volatile IopKsrProvider;

static
const IOP_KSR_PROVIDER *
IopGetKsrProvider(VOID)
{
    return (const IOP_KSR_PROVIDER *)ReadPointerAcquire(&IopKsrProvider);
}

/* KSR API-SET ROUTINES *******************************************************/

NTSTATUS
NTAPI
KsrClaimPersistedMemory(
    _In_ const GUID *MemoryId,
    _In_ ULONGLONG MemoryLength,
    _Out_writes_opt_(MemoryRunCount) PULONGLONG MemoryRuns,
    _In_ ULONG MemoryRunCount,
    _In_ BOOLEAN MapMemory,
    _Out_ PULONG ClaimedRunCount)
{
    const IOP_KSR_PROVIDER *Provider = IopGetKsrProvider();

    if ((Provider == NULL) || (Provider->ClaimPersistedMemory == NULL))
        return STATUS_NOT_SUPPORTED;

    return Provider->ClaimPersistedMemory(MemoryId,
                                          MemoryLength,
                                          MemoryRuns,
                                          MemoryRunCount,
                                          MapMemory,
                                          ClaimedRunCount);
}

NTSTATUS
NTAPI
KsrQueryMetadata(
    _In_ const GUID *MemoryId,
    _In_ ULONGLONG MemoryLength,
    _Out_writes_bytes_opt_(MetadataLength) PVOID Metadata,
    _In_ ULONG MetadataLength,
    _Out_ PULONG RequiredLength)
{
    const IOP_KSR_PROVIDER *Provider = IopGetKsrProvider();

    if ((Provider == NULL) || (Provider->QueryMetadata == NULL))
        return STATUS_NOT_SUPPORTED;

    return Provider->QueryMetadata(MemoryId,
                                   MemoryLength,
                                   Metadata,
                                   MetadataLength,
                                   RequiredLength);
}

VOID
NTAPI
KsrFreePersistedMemory(
    _In_ const GUID *MemoryId,
    _In_ BOOLEAN ReleaseMemory)
{
    const IOP_KSR_PROVIDER *Provider = IopGetKsrProvider();

    if ((Provider != NULL) && (Provider->FreePersistedMemory != NULL))
        Provider->FreePersistedMemory(MemoryId, ReleaseMemory);
}

NTSTATUS
NTAPI
KsrPersistMemoryWithMetadata(
    _In_ const GUID *MemoryId,
    _In_reads_(MemoryRunCount) PULONGLONG MemoryRuns,
    _In_ ULONG MemoryRunCount,
    _In_reads_bytes_opt_(MetadataLength) PVOID Metadata,
    _In_ ULONG MetadataLength,
    _Out_ PULONGLONG PersistedMemoryLength)
{
    const IOP_KSR_PROVIDER *Provider = IopGetKsrProvider();

    if ((Provider == NULL) || (Provider->PersistMemoryWithMetadata == NULL))
        return STATUS_NOT_SUPPORTED;

    return Provider->PersistMemoryWithMetadata(MemoryId,
                                               MemoryRuns,
                                               MemoryRunCount,
                                               Metadata,
                                               MetadataLength,
                                               PersistedMemoryLength);
}

NTSTATUS
NTAPI
KsrGetFirmwareInformation(
    _Out_ PCIOP_KSR_FIRMWARE_INFORMATION *FirmwareInformation)
{
    const IOP_KSR_PROVIDER *Provider = IopGetKsrProvider();

    if ((Provider == NULL) || (Provider->GetFirmwareInformation == NULL))
        return STATUS_NOT_SUPPORTED;

    return Provider->GetFirmwareInformation(FirmwareInformation);
}

NTSTATUS
NTAPI
KsrEnumeratePersistedMemory(
    _In_ const GUID *MemoryType,
    _In_ PIOP_KSR_ENUMERATE_CALLBACK Callback,
    _In_opt_ PVOID Context)
{
    const IOP_KSR_PROVIDER *Provider = IopGetKsrProvider();

    if ((Provider == NULL) || (Provider->EnumeratePersistedMemory == NULL))
        return STATUS_NOT_SUPPORTED;

    return Provider->EnumeratePersistedMemory(MemoryType, Callback, Context);
}

NTSTATUS
NTAPI
KsrMdlToMemoryRuns(
    _In_ PMDL Mdl,
    _Out_writes_(MemoryRunCapacity) PULONGLONG MemoryRuns,
    _In_ ULONG MemoryRunCapacity,
    _Out_ PULONG MemoryRunCount)
{
    const IOP_KSR_PROVIDER *Provider = IopGetKsrProvider();

    if ((Provider == NULL) || (Provider->MdlToMemoryRuns == NULL))
        return STATUS_NOT_SUPPORTED;

    return Provider->MdlToMemoryRuns(Mdl,
                                     MemoryRuns,
                                     MemoryRunCapacity,
                                     MemoryRunCount);
}

/* INTERNAL FUNCTIONS ********************************************************/

NTSTATUS
NTAPI
IopRegisterKsrProvider(
    _In_ const IOP_KSR_PROVIDER *Provider)
{
    if ((Provider == NULL) ||
        (Provider->Size < sizeof(*Provider)) ||
        (Provider->Version != IOP_KSR_PROVIDER_VERSION))
    {
        return STATUS_INVALID_PARAMETER;
    }

    if (InterlockedCompareExchangePointer(&IopKsrProvider,
                                          (PVOID)Provider,
                                          NULL) != NULL)
    {
        return STATUS_OBJECT_NAME_COLLISION;
    }

    return STATUS_SUCCESS;
}

PVOID
NTAPI
IopResolveKsrApiSetRoutine(
    _In_ PCSTR RoutineName)
{
    if (!strcmp(RoutineName, "KsrClaimPersistedMemory"))
        return KsrClaimPersistedMemory;
    if (!strcmp(RoutineName, "KsrQueryMetadata"))
        return KsrQueryMetadata;
    if (!strcmp(RoutineName, "KsrFreePersistedMemory"))
        return KsrFreePersistedMemory;
    if (!strcmp(RoutineName, "KsrPersistMemoryWithMetadata"))
        return KsrPersistMemoryWithMetadata;
    if (!strcmp(RoutineName, "KsrGetFirmwareInformation"))
        return KsrGetFirmwareInformation;
    if (!strcmp(RoutineName, "KsrEnumeratePersistedMemory"))
        return KsrEnumeratePersistedMemory;
    if (!strcmp(RoutineName, "KsrMdlToMemoryRuns"))
        return KsrMdlToMemoryRuns;

    return NULL;
}
