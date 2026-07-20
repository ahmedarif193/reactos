/*
 * PROJECT:     ReactOS WDDM 2.x Graphics Memory Manager and Scheduler
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Administrative adapter lifecycle for the v1 module boundary
 */

#include "dxgmms2_private.h"

static BOOLEAN
Dxgmms2IsStopReasonValid(_In_ ULONG Reason)
{
    return Reason >= Dxgmms2StopReasonPnpStop && Reason <= Dxgmms2StopReasonStartRollback;
}

PDXGMMS2_ADAPTER_CONTEXT
Dxgmms2ReferenceAdapterContext(_In_ DXGMMS2_ADAPTER_HANDLE Adapter)
{
    PDXGMMS2_REGISTRATION_CONTEXT RegistrationContext;
    PDXGMMS2_ADAPTER_CONTEXT Context;
    PDXGMMS2_ADAPTER_CONTEXT ReferencedContext;
    PLIST_ENTRY Entry;

    if (Adapter == NULL)
        return NULL;
    ReferencedContext = NULL;
    Dxgmms2AcquireMutex(&Dxgmms2GlobalMutex);
    RegistrationContext = Dxgmms2ActiveRegistration;
    if (RegistrationContext == NULL || RegistrationContext->Signature != DXGMMS2_REGISTRATION_SIGNATURE)
    {
        Dxgmms2ReleaseMutex(&Dxgmms2GlobalMutex);
        return NULL;
    }
    Dxgmms2AcquireMutex(&RegistrationContext->AdapterListMutex);
    for (Entry = RegistrationContext->AdapterListHead.Flink; Entry != &RegistrationContext->AdapterListHead; Entry = Entry->Flink)
    {
        Context = CONTAINING_RECORD(Entry, DXGMMS2_ADAPTER_CONTEXT, RegistrationEntry);
        if (Context->PublicHandle != Adapter)
            continue;
        if (InterlockedCompareExchange(&Context->RundownStarted, 0, 0) == 0 && ExAcquireRundownProtection(&Context->RundownRef))
        {
            if (Context->Signature == DXGMMS2_ADAPTER_SIGNATURE && InterlockedCompareExchange(&Context->RundownStarted, 0, 0) == 0)
                ReferencedContext = Context;
            else
                ExReleaseRundownProtection(&Context->RundownRef);
        }
        break;
    }
    Dxgmms2ReleaseMutex(&RegistrationContext->AdapterListMutex);
    Dxgmms2ReleaseMutex(&Dxgmms2GlobalMutex);
    return ReferencedContext;
}

VOID
Dxgmms2DereferenceAdapterContext(_In_ PDXGMMS2_ADAPTER_CONTEXT Context)
{
    ExReleaseRundownProtection(&Context->RundownRef);
}

NTSTATUS
NTAPI
Dxgmms2CreateAdapter(_In_ DXGMMS2_REGISTRATION_HANDLE Registration, _In_ const DXGMMS2_CREATE_ADAPTER_INFO_V1 *Info, _Out_ DXGMMS2_ADAPTER_HANDLE *Adapter)
{
    PDXGMMS2_REGISTRATION_CONTEXT RegistrationContext;
    PDXGMMS2_ADAPTER_CONTEXT Context;
    PLIST_ENTRY Entry;

    PAGED_CODE();
    if (Adapter == NULL)
        return STATUS_INVALID_PARAMETER;
    *Adapter = NULL;
    if (Registration == NULL || Info == NULL || Info->AdapterCookie == NULL)
        return STATUS_INVALID_PARAMETER;
    if (Info->Size < (FIELD_OFFSET(DXGMMS2_CREATE_ADAPTER_INFO_V1, Version) + sizeof(Info->Version)))
        return STATUS_INFO_LENGTH_MISMATCH;
    if (Info->Version != DXGMMS2_ABI_VERSION_1)
        return STATUS_REVISION_MISMATCH;
    if (Info->Size < DXGMMS2_CREATE_ADAPTER_INFO_V1_SIZE)
        return STATUS_INFO_LENGTH_MISMATCH;
    if ((Info->AdapterFlags & ~DXGMMS2_ADAPTER_FLAG_VALID_MASK) != 0 || Info->Reserved != 0)
        return STATUS_INVALID_PARAMETER;

    Context = ExAllocatePoolWithTag(NonPagedPool, sizeof(*Context), DXGMMS2_ADAPTER_TAG);
    if (Context == NULL)
        return STATUS_INSUFFICIENT_RESOURCES;
    RtlZeroMemory(Context, sizeof(*Context));
    Context->Signature = DXGMMS2_ADAPTER_SIGNATURE;
    do
    {
        Context->PublicHandle = (DXGMMS2_ADAPTER_HANDLE)(ULONG_PTR)InterlockedIncrement64(&Dxgmms2NextPublicHandle);
    } while (Context->PublicHandle == NULL);
    Context->AdapterCookie = Info->AdapterCookie;
    Context->AdapterFlags = Info->AdapterFlags;
    Context->State = Dxgmms2AdapterCreated;
    InitializeListHead(&Context->RegistrationEntry);
    KeInitializeMutex(&Context->StateMutex, 0);
    ExInitializeRundownProtection(&Context->RundownRef);
    Dxgmms2TimelineInitialize(&Context->Timeline);
    Dxgmms2ContextStreamManagerInitialize(&Context->ContextStreamManager);

    Dxgmms2AcquireMutex(&Dxgmms2GlobalMutex);
    RegistrationContext = Dxgmms2ActiveRegistration;
    if (RegistrationContext == NULL || RegistrationContext->PublicHandle != Registration || RegistrationContext->Signature != DXGMMS2_REGISTRATION_SIGNATURE || InterlockedCompareExchange(&RegistrationContext->Unregistering, 0, 0) != 0)
    {
        Dxgmms2ReleaseMutex(&Dxgmms2GlobalMutex);
        Context->Signature = 0;
        ExFreePoolWithTag(Context, DXGMMS2_ADAPTER_TAG);
        return STATUS_INVALID_HANDLE;
    }
    Context->Registration = RegistrationContext;

    Dxgmms2AcquireMutex(&RegistrationContext->AdapterListMutex);
    for (Entry = RegistrationContext->AdapterListHead.Flink; Entry != &RegistrationContext->AdapterListHead; Entry = Entry->Flink)
    {
        PDXGMMS2_ADAPTER_CONTEXT Existing;

        Existing = CONTAINING_RECORD(Entry, DXGMMS2_ADAPTER_CONTEXT, RegistrationEntry);
        if (Existing->AdapterCookie == Info->AdapterCookie)
        {
            Dxgmms2ReleaseMutex(&RegistrationContext->AdapterListMutex);
            Dxgmms2ReleaseMutex(&Dxgmms2GlobalMutex);
            Context->Signature = 0;
            ExFreePoolWithTag(Context, DXGMMS2_ADAPTER_TAG);
            return STATUS_OBJECT_NAME_COLLISION;
        }
    }
    InsertTailList(&RegistrationContext->AdapterListHead, &Context->RegistrationEntry);
    Context->RegistrationLinked = TRUE;
    RegistrationContext->AdapterCount++;
    Dxgmms2ReleaseMutex(&RegistrationContext->AdapterListMutex);
    Dxgmms2ReleaseMutex(&Dxgmms2GlobalMutex);
    *Adapter = Context->PublicHandle;
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
Dxgmms2StartAdapter(_In_ DXGMMS2_ADAPTER_HANDLE Adapter, _In_ const DXGMMS2_START_ADAPTER_INFO_V1 *Info, _Inout_ DXGMMS2_START_ADAPTER_RESULT_V1 *Result)
{
    PDXGMMS2_ADAPTER_CONTEXT Context;
    ULONG ResultCapacity;
    LONG State;
    NTSTATUS ManagerStatus;
    NTSTATUS TimelineStatus;

    PAGED_CODE();
    if (Info == NULL || Result == NULL)
        return STATUS_INVALID_PARAMETER;
    if (Info->Size < (FIELD_OFFSET(DXGMMS2_START_ADAPTER_INFO_V1, Version) + sizeof(Info->Version)))
        return STATUS_INFO_LENGTH_MISMATCH;
    if (Info->Version != DXGMMS2_ABI_VERSION_1)
        return STATUS_REVISION_MISMATCH;
    if (Info->Size < DXGMMS2_START_ADAPTER_INFO_V1_SIZE)
        return STATUS_INFO_LENGTH_MISMATCH;
    if ((Info->AdapterFlags & ~DXGMMS2_ADAPTER_FLAG_VALID_MASK) != 0)
        return STATUS_INVALID_PARAMETER;

    ResultCapacity = Result->Size;
    if (ResultCapacity < (FIELD_OFFSET(DXGMMS2_START_ADAPTER_RESULT_V1, Version) + sizeof(Result->Version)))
    {
        if (ResultCapacity >= sizeof(Result->Size))
            Result->Size = DXGMMS2_START_ADAPTER_RESULT_V1_SIZE;
        return STATUS_BUFFER_TOO_SMALL;
    }
    if (Result->Version != DXGMMS2_ABI_VERSION_1)
        return STATUS_REVISION_MISMATCH;
    if (ResultCapacity < DXGMMS2_START_ADAPTER_RESULT_V1_SIZE)
    {
        Result->Size = DXGMMS2_START_ADAPTER_RESULT_V1_SIZE;
        return STATUS_BUFFER_TOO_SMALL;
    }

    Context = Dxgmms2ReferenceAdapterContext(Adapter);
    if (Context == NULL)
        return STATUS_INVALID_HANDLE;
    Dxgmms2AcquireMutex(&Context->StateMutex);
    State = InterlockedCompareExchange(&Context->State, 0, 0);
    if (State != Dxgmms2AdapterCreated && State != Dxgmms2AdapterStopped)
    {
        Dxgmms2ReleaseMutex(&Context->StateMutex);
        Dxgmms2DereferenceAdapterContext(Context);
        return STATUS_INVALID_DEVICE_STATE;
    }
    if ((Info->AdapterFlags & Context->AdapterFlags) != Context->AdapterFlags)
    {
        Dxgmms2ReleaseMutex(&Context->StateMutex);
        Dxgmms2DereferenceAdapterContext(Context);
        return STATUS_INVALID_PARAMETER;
    }

    ManagerStatus = Dxgmms2ContextStreamManagerStart(&Context->ContextStreamManager, Info->NodeCount);
    if (!NT_SUCCESS(ManagerStatus))
    {
        Dxgmms2ReleaseMutex(&Context->StateMutex);
        Dxgmms2DereferenceAdapterContext(Context);
        return ManagerStatus;
    }
    TimelineStatus = Dxgmms2TimelineStart(&Context->Timeline, Info->NodeCount);
    if (!NT_SUCCESS(TimelineStatus))
    {
        ManagerStatus = Dxgmms2ContextStreamManagerBeginStop(&Context->ContextStreamManager, Dxgmms2StopReasonStartRollback);
        ASSERT(NT_SUCCESS(ManagerStatus));
        ManagerStatus = Dxgmms2ContextStreamManagerCompleteStop(&Context->ContextStreamManager, TRUE);
        ASSERT(NT_SUCCESS(ManagerStatus));
        Dxgmms2ReleaseMutex(&Context->StateMutex);
        Dxgmms2DereferenceAdapterContext(Context);
        return TimelineStatus;
    }

    Context->MiniportDdiVersion = Info->MiniportDdiVersion;
    Context->RequestedWddmVersion = Info->RequestedWddmVersion;
    Context->NodeCount = Info->NodeCount;
    Context->SegmentCount = Info->SegmentCount;
    Context->AdapterFlags = Info->AdapterFlags;
    Context->SchedulingCaps = Info->SchedulingCaps;
    Context->EnabledSubsystems = 0;
    Context->HighestCompleteWddmVersion = 0;
    Context->StopReason = 0;
    InterlockedExchange(&Context->State, Dxgmms2AdapterStarted);

    RtlZeroMemory(Result, DXGMMS2_START_ADAPTER_RESULT_V1_SIZE);
    Result->Size = DXGMMS2_START_ADAPTER_RESULT_V1_SIZE;
    Result->Version = DXGMMS2_ABI_VERSION_1;
    Result->EnabledSubsystems = 0;
    Result->HighestCompleteWddmVersion = 0;
    Result->Reserved = 0;
    Dxgmms2ReleaseMutex(&Context->StateMutex);
    Dxgmms2DereferenceAdapterContext(Context);
    return STATUS_SUCCESS;
}

NTSTATUS
NTAPI
Dxgmms2BeginStopAdapter(_In_ DXGMMS2_ADAPTER_HANDLE Adapter, _In_ const DXGMMS2_STOP_ADAPTER_INFO_V1 *Info)
{
    PDXGMMS2_ADAPTER_CONTEXT Context;
    LONG State;
    NTSTATUS Status;

    PAGED_CODE();
    if (Info == NULL)
        return STATUS_INVALID_PARAMETER;
    if (Info->Size < (FIELD_OFFSET(DXGMMS2_STOP_ADAPTER_INFO_V1, Version) + sizeof(Info->Version)))
        return STATUS_INFO_LENGTH_MISMATCH;
    if (Info->Version != DXGMMS2_ABI_VERSION_1)
        return STATUS_REVISION_MISMATCH;
    if (Info->Size < DXGMMS2_STOP_ADAPTER_INFO_V1_SIZE)
        return STATUS_INFO_LENGTH_MISMATCH;
    if (!Dxgmms2IsStopReasonValid(Info->Reason) || Info->Flags != 0)
        return STATUS_INVALID_PARAMETER;

    Context = Dxgmms2ReferenceAdapterContext(Adapter);
    if (Context == NULL)
        return STATUS_INVALID_HANDLE;
    Status = STATUS_SUCCESS;
    Dxgmms2AcquireMutex(&Context->StateMutex);
    State = InterlockedCompareExchange(&Context->State, 0, 0);
    if (State == Dxgmms2AdapterStarted)
    {
        Status = Dxgmms2TimelineBeginStop(&Context->Timeline);
        if (!NT_SUCCESS(Status))
        {
            Dxgmms2ReleaseMutex(&Context->StateMutex);
            Dxgmms2DereferenceAdapterContext(Context);
            return Status;
        }
        Status = Dxgmms2ContextStreamManagerBeginStop(&Context->ContextStreamManager, Info->Reason);
        if (!NT_SUCCESS(Status))
        {
            Dxgmms2ReleaseMutex(&Context->StateMutex);
            Dxgmms2DereferenceAdapterContext(Context);
            return Status;
        }
        Context->StopReason = Info->Reason;
        InterlockedExchange(&Context->State, Dxgmms2AdapterStopping);
    }
    else if (State != Dxgmms2AdapterStopping || Context->StopReason != Info->Reason)
    {
        Status = STATUS_INVALID_DEVICE_STATE;
    }
    Dxgmms2ReleaseMutex(&Context->StateMutex);
    Dxgmms2DereferenceAdapterContext(Context);
    return Status;
}

NTSTATUS
NTAPI
Dxgmms2CompleteStopAdapter(_In_ DXGMMS2_ADAPTER_HANDLE Adapter, _In_ const DXGMMS2_STOP_ADAPTER_INFO_V1 *Info)
{
    PDXGMMS2_ADAPTER_CONTEXT Context;
    LONG State;
    NTSTATUS Status;

    PAGED_CODE();
    if (Info == NULL)
        return STATUS_INVALID_PARAMETER;
    if (Info->Size < (FIELD_OFFSET(DXGMMS2_STOP_ADAPTER_INFO_V1, Version) + sizeof(Info->Version)))
        return STATUS_INFO_LENGTH_MISMATCH;
    if (Info->Version != DXGMMS2_ABI_VERSION_1)
        return STATUS_REVISION_MISMATCH;
    if (Info->Size < DXGMMS2_STOP_ADAPTER_INFO_V1_SIZE)
        return STATUS_INFO_LENGTH_MISMATCH;
    if (!Dxgmms2IsStopReasonValid(Info->Reason) || (Info->Flags & ~DXGMMS2_COMPLETE_STOP_VALID_MASK) != 0 || (Info->Flags & DXGMMS2_COMPLETE_STOP_HARDWARE_RETIRED) == 0)
        return STATUS_INVALID_PARAMETER;

    Context = Dxgmms2ReferenceAdapterContext(Adapter);
    if (Context == NULL)
        return STATUS_INVALID_HANDLE;
    Status = STATUS_SUCCESS;
    Dxgmms2AcquireMutex(&Context->StateMutex);
    State = InterlockedCompareExchange(&Context->State, 0, 0);
    if (State == Dxgmms2AdapterStopping && Context->StopReason == Info->Reason)
    {
        Status = Dxgmms2ContextStreamManagerCompleteStop(&Context->ContextStreamManager, TRUE);
        if (!NT_SUCCESS(Status))
        {
            Dxgmms2ReleaseMutex(&Context->StateMutex);
            Dxgmms2DereferenceAdapterContext(Context);
            return Status;
        }
        Status = Dxgmms2TimelineCompleteStop(&Context->Timeline);
        if (!NT_SUCCESS(Status))
        {
            Dxgmms2ReleaseMutex(&Context->StateMutex);
            Dxgmms2DereferenceAdapterContext(Context);
            return Status;
        }
        InterlockedExchange(&Context->State, Dxgmms2AdapterStopped);
    }
    else if (State != Dxgmms2AdapterStopped || Context->StopReason != Info->Reason)
    {
        Status = STATUS_INVALID_DEVICE_STATE;
    }
    Dxgmms2ReleaseMutex(&Context->StateMutex);
    Dxgmms2DereferenceAdapterContext(Context);
    return Status;
}

NTSTATUS
NTAPI
Dxgmms2DestroyAdapter(_In_ DXGMMS2_ADAPTER_HANDLE Adapter)
{
    PDXGMMS2_ADAPTER_CONTEXT Context;
    PDXGMMS2_REGISTRATION_CONTEXT RegistrationContext;
    LONG State;

    PAGED_CODE();
    Context = Dxgmms2ReferenceAdapterContext(Adapter);
    if (Context == NULL)
        return STATUS_INVALID_HANDLE;
    if (InterlockedCompareExchange(&Context->DestroyClaimed, 1, 0) != 0)
    {
        Dxgmms2DereferenceAdapterContext(Context);
        return STATUS_DEVICE_BUSY;
    }

    Dxgmms2AcquireMutex(&Context->StateMutex);
    State = InterlockedCompareExchange(&Context->State, 0, 0);
    if (State != Dxgmms2AdapterCreated && State != Dxgmms2AdapterStopped)
    {
        InterlockedExchange(&Context->DestroyClaimed, 0);
        Dxgmms2ReleaseMutex(&Context->StateMutex);
        Dxgmms2DereferenceAdapterContext(Context);
        return STATUS_INVALID_DEVICE_STATE;
    }
    {
        NTSTATUS ContextStreamStatus = Dxgmms2ContextStreamManagerCanDestroy(&Context->ContextStreamManager);

        if (!NT_SUCCESS(ContextStreamStatus))
        {
            InterlockedExchange(&Context->DestroyClaimed, 0);
            Dxgmms2ReleaseMutex(&Context->StateMutex);
            Dxgmms2DereferenceAdapterContext(Context);
            return ContextStreamStatus;
        }
    }
    {
        NTSTATUS TimelineStatus = Dxgmms2TimelinePrepareDestroy(&Context->Timeline);

        if (!NT_SUCCESS(TimelineStatus))
        {
            InterlockedExchange(&Context->DestroyClaimed, 0);
            Dxgmms2ReleaseMutex(&Context->StateMutex);
            Dxgmms2DereferenceAdapterContext(Context);
            return TimelineStatus;
        }
    }
    {
        NTSTATUS ContextStreamStatus = Dxgmms2ContextStreamManagerPrepareDestroy(&Context->ContextStreamManager);

        ASSERT(NT_SUCCESS(ContextStreamStatus));
        if (!NT_SUCCESS(ContextStreamStatus))
        {
            InterlockedExchange(&Context->DestroyClaimed, 0);
            Dxgmms2ReleaseMutex(&Context->StateMutex);
            Dxgmms2DereferenceAdapterContext(Context);
            return ContextStreamStatus;
        }
    }
    InterlockedExchange(&Context->State, Dxgmms2AdapterDestroying);
    Dxgmms2ReleaseMutex(&Context->StateMutex);

    Dxgmms2DereferenceAdapterContext(Context);
    if (InterlockedCompareExchange(&Context->RundownStarted, 1, 0) == 0)
        ExWaitForRundownProtectionRelease(&Context->RundownRef);

    RegistrationContext = Context->Registration;
    Dxgmms2AcquireMutex(&RegistrationContext->AdapterListMutex);
    if (Context->RegistrationLinked)
    {
        RemoveEntryList(&Context->RegistrationEntry);
        InitializeListHead(&Context->RegistrationEntry);
        Context->RegistrationLinked = FALSE;
        ASSERT(RegistrationContext->AdapterCount != 0);
        RegistrationContext->AdapterCount--;
    }
    Dxgmms2ReleaseMutex(&RegistrationContext->AdapterListMutex);
    Context->Signature = 0;
    Context->Registration = NULL;
    Context->PublicHandle = NULL;
    Context->AdapterCookie = NULL;
    ExFreePoolWithTag(Context, DXGMMS2_ADAPTER_TAG);
    return STATUS_SUCCESS;
}
