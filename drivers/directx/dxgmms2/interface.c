/*
 * PROJECT:     ReactOS WDDM 2.x Graphics Memory Manager and Scheduler
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Typed registration contract exported to dxgkrnl.sys
 */

#include "dxgmms2_private.h"

#ifdef _WIN64
C_ASSERT(sizeof(DXGMMS2_DXGKRNL_INTERFACE_V1) == 32);
C_ASSERT(sizeof(DXGMMS2_CREATE_ADAPTER_INFO_V1) == 24);
C_ASSERT(sizeof(DXGMMS2_START_ADAPTER_INFO_V1) == 32);
C_ASSERT(sizeof(DXGMMS2_START_ADAPTER_RESULT_V1) == 24);
C_ASSERT(sizeof(DXGMMS2_STOP_ADAPTER_INFO_V1) == 16);
C_ASSERT(sizeof(DXGMMS2_PROVIDER_INTERFACE_V1) == 64);
C_ASSERT(sizeof(DXGMMS2_PROVIDER_INTERFACE_V2) == 72);
C_ASSERT(sizeof(DXGMMS2_PROVIDER_INTERFACE_V3) == 80);
C_ASSERT(FIELD_OFFSET(DXGMMS2_PROVIDER_INTERFACE_V1, RegistrationHandle) == 16);
C_ASSERT(FIELD_OFFSET(DXGMMS2_PROVIDER_INTERFACE_V1, DestroyAdapter) == 56);
C_ASSERT(FIELD_OFFSET(DXGMMS2_PROVIDER_INTERFACE_V2, QuerySchedulerTimelineInterface) == 64);
C_ASSERT(FIELD_OFFSET(DXGMMS2_PROVIDER_INTERFACE_V3, QueryContextStreamInterface) == 72);
C_ASSERT(sizeof(DXGMMS2_PROVIDER_INTERFACE_V4) == 88);
C_ASSERT(FIELD_OFFSET(DXGMMS2_PROVIDER_INTERFACE_V4, QuerySchedulerInterface) == 80);
#endif

NTSTATUS
NTAPI
DxgkMms2Register(_In_ const DXGMMS2_DXGKRNL_INTERFACE_V1 *DxgkrnlInterface, _Inout_ DXGMMS2_PROVIDER_INTERFACE_V1 *ProviderInterface)
{
    PDXGMMS2_REGISTRATION_CONTEXT Registration;
    ULONG ProviderCapacity;
    ULONG ProviderRequiredSize;
    ULONG ProviderVersion;
    NTSTATUS Status;

    PAGED_CODE();
    if (DxgkrnlInterface == NULL || ProviderInterface == NULL)
        return STATUS_INVALID_PARAMETER;
    if (DxgkrnlInterface->Size < (FIELD_OFFSET(DXGMMS2_DXGKRNL_INTERFACE_V1, Version) + sizeof(DxgkrnlInterface->Version)))
        return STATUS_INFO_LENGTH_MISMATCH;
    if (DxgkrnlInterface->Version != DXGMMS2_ABI_VERSION_1)
        return STATUS_REVISION_MISMATCH;
    if (DxgkrnlInterface->Size < DXGMMS2_DXGKRNL_INTERFACE_V1_SIZE)
        return STATUS_INFO_LENGTH_MISMATCH;
    if (DxgkrnlInterface->ReferenceAdapter == NULL || DxgkrnlInterface->DereferenceAdapter == NULL)
        return STATUS_INVALID_PARAMETER;

    ProviderCapacity = ProviderInterface->Size;
    if (ProviderCapacity < (FIELD_OFFSET(DXGMMS2_PROVIDER_INTERFACE_V1, Version) + sizeof(ProviderInterface->Version)))
    {
        if (ProviderCapacity >= sizeof(ProviderInterface->Size))
            ProviderInterface->Size = DXGMMS2_PROVIDER_INTERFACE_V1_SIZE;
        return STATUS_BUFFER_TOO_SMALL;
    }
    ProviderVersion = ProviderInterface->Version;
    if (ProviderVersion == DXGMMS2_ABI_VERSION_1)
        ProviderRequiredSize = DXGMMS2_PROVIDER_INTERFACE_V1_SIZE;
    else if (ProviderVersion == DXGMMS2_ABI_VERSION_2)
        ProviderRequiredSize = DXGMMS2_PROVIDER_INTERFACE_V2_SIZE;
    else if (ProviderVersion == DXGMMS2_ABI_VERSION_3)
        ProviderRequiredSize = DXGMMS2_PROVIDER_INTERFACE_V3_SIZE;
    else if (ProviderVersion == DXGMMS2_ABI_VERSION_4)
        ProviderRequiredSize = DXGMMS2_PROVIDER_INTERFACE_V4_SIZE;
    else
        return STATUS_REVISION_MISMATCH;
    if (ProviderCapacity < ProviderRequiredSize)
    {
        ProviderInterface->Size = ProviderRequiredSize;
        return STATUS_BUFFER_TOO_SMALL;
    }

    Status = Dxgmms2EnsureInitialized();
    if (!NT_SUCCESS(Status))
        return Status;
    Registration = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Registration), DXGMMS2_REGISTRATION_TAG);
    if (Registration == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(Registration, sizeof(*Registration));
    Registration->Signature = DXGMMS2_REGISTRATION_SIGNATURE;
    do
    {
        Registration->PublicHandle = (DXGMMS2_REGISTRATION_HANDLE)(ULONG_PTR)InterlockedIncrement64(&Dxgmms2NextPublicHandle);
    } while (Registration->PublicHandle == NULL);
    RtlCopyMemory(&Registration->ClientInterface, DxgkrnlInterface, sizeof(Registration->ClientInterface));
    KeInitializeMutex(&Registration->AdapterListMutex, 0);
    InitializeListHead(&Registration->AdapterListHead);

    Dxgmms2AcquireMutex(&Dxgmms2GlobalMutex);
    if (Dxgmms2ActiveRegistration != NULL)
    {
        Dxgmms2ReleaseMutex(&Dxgmms2GlobalMutex);
        Registration->Signature = 0;
        ExFreePoolWithTag(Registration, DXGMMS2_REGISTRATION_TAG);
        return STATUS_DEVICE_BUSY;
    }

    RtlZeroMemory(ProviderInterface, ProviderRequiredSize);
    ProviderInterface->Size = ProviderRequiredSize;
    ProviderInterface->Version = ProviderVersion;
    ProviderInterface->ProviderFlags = 0;
    ProviderInterface->Reserved = 0;
    ProviderInterface->RegistrationHandle = Registration->PublicHandle;
    ProviderInterface->CreateAdapter = Dxgmms2CreateAdapter;
    ProviderInterface->StartAdapter = Dxgmms2StartAdapter;
    ProviderInterface->BeginStopAdapter = Dxgmms2BeginStopAdapter;
    ProviderInterface->CompleteStopAdapter = Dxgmms2CompleteStopAdapter;
    ProviderInterface->DestroyAdapter = Dxgmms2DestroyAdapter;
    if (ProviderVersion == DXGMMS2_ABI_VERSION_2 || ProviderVersion == DXGMMS2_ABI_VERSION_3 || ProviderVersion == DXGMMS2_ABI_VERSION_4)
        ((PDXGMMS2_PROVIDER_INTERFACE_V2)ProviderInterface)->QuerySchedulerTimelineInterface = Dxgmms2QuerySchedulerTimelineInterface;
    if (ProviderVersion == DXGMMS2_ABI_VERSION_3 || ProviderVersion == DXGMMS2_ABI_VERSION_4)
        ((PDXGMMS2_PROVIDER_INTERFACE_V3)ProviderInterface)->QueryContextStreamInterface = Dxgmms2QueryContextStreamInterface;
    if (ProviderVersion == DXGMMS2_ABI_VERSION_4)
        ((PDXGMMS2_PROVIDER_INTERFACE_V4)ProviderInterface)->QuerySchedulerInterface = Dxgmms2QuerySchedulerInterface;
    KeMemoryBarrier();
    Dxgmms2ActiveRegistration = Registration;
    Dxgmms2ReleaseMutex(&Dxgmms2GlobalMutex);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
DxgkMms2Unregister(_In_ DXGMMS2_REGISTRATION_HANDLE Registration)
{
    PDXGMMS2_REGISTRATION_CONTEXT RegistrationContext;
    NTSTATUS Status;

    PAGED_CODE();
    if (Registration == NULL)
        return STATUS_INVALID_PARAMETER;
    Status = Dxgmms2EnsureInitialized();
    if (!NT_SUCCESS(Status))
        return Status;

    Dxgmms2AcquireMutex(&Dxgmms2GlobalMutex);
    RegistrationContext = Dxgmms2ActiveRegistration;
    if (RegistrationContext == NULL || RegistrationContext->PublicHandle != Registration)
    {
        Dxgmms2ReleaseMutex(&Dxgmms2GlobalMutex);
        return STATUS_INVALID_HANDLE;
    }
    if (RegistrationContext->Signature != DXGMMS2_REGISTRATION_SIGNATURE || InterlockedCompareExchange(&RegistrationContext->Unregistering, 1, 0) != 0)
    {
        Dxgmms2ReleaseMutex(&Dxgmms2GlobalMutex);
        return STATUS_DEVICE_BUSY;
    }

    Dxgmms2AcquireMutex(&RegistrationContext->AdapterListMutex);
    if (RegistrationContext->AdapterCount != 0 || !IsListEmpty(&RegistrationContext->AdapterListHead))
    {
        InterlockedExchange(&RegistrationContext->Unregistering, 0);
        Dxgmms2ReleaseMutex(&RegistrationContext->AdapterListMutex);
        Dxgmms2ReleaseMutex(&Dxgmms2GlobalMutex);
        return STATUS_DEVICE_BUSY;
    }
    Dxgmms2ActiveRegistration = NULL;
    RegistrationContext->Signature = 0;
    RegistrationContext->PublicHandle = NULL;
    Dxgmms2ReleaseMutex(&RegistrationContext->AdapterListMutex);
    Dxgmms2ReleaseMutex(&Dxgmms2GlobalMutex);
    RtlZeroMemory(&RegistrationContext->ClientInterface, sizeof(RegistrationContext->ClientInterface));
    ExFreePoolWithTag(RegistrationContext, DXGMMS2_REGISTRATION_TAG);
    return STATUS_SUCCESS;
}
