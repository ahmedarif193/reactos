/*
 * PROJECT:     ReactOS WDDM 2.x Graphics Memory Manager and Scheduler
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Module initialization for dxgmms2.sys
 */

#include "dxgmms2_private.h"

KMUTEX Dxgmms2GlobalMutex;
volatile LONG Dxgmms2InitializationState = 0;
volatile LONGLONG Dxgmms2NextPublicHandle = 0;
PDXGMMS2_REGISTRATION_CONTEXT Dxgmms2ActiveRegistration = NULL;

VOID
Dxgmms2AcquireMutex(_Inout_ PKMUTEX Mutex)
{
    NTSTATUS Status;

    PAGED_CODE();
    Status = KeWaitForSingleObject(Mutex, Executive, KernelMode, FALSE, NULL);
    ASSERT(Status == STATUS_SUCCESS);
}

VOID
Dxgmms2ReleaseMutex(_Inout_ PKMUTEX Mutex)
{
    PAGED_CODE();
    KeReleaseMutex(Mutex, FALSE);
}

NTSTATUS
Dxgmms2EnsureInitialized(VOID)
{
    LARGE_INTEGER Delay;
    LONG State;

    PAGED_CODE();
    State = InterlockedCompareExchange(&Dxgmms2InitializationState, 1, 0);
    if (State == 0)
    {
        KeInitializeMutex(&Dxgmms2GlobalMutex, 0);
        KeMemoryBarrier();
        InterlockedExchange(&Dxgmms2InitializationState, 2);
        return STATUS_SUCCESS;
    }

    Delay.QuadPart = -(LONGLONG)(10 * 1000);
    while ((State = InterlockedCompareExchange(&Dxgmms2InitializationState, 0, 0)) == 1)
        KeDelayExecutionThread(KernelMode, FALSE, &Delay);
    return State == 2 ? STATUS_SUCCESS : STATUS_DEVICE_NOT_READY;
}

static VOID
NTAPI
Dxgmms2DriverUnload(_In_ PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);

    PAGED_CODE();
    if (InterlockedCompareExchange(&Dxgmms2InitializationState, 0, 0) != 2)
        return;
    Dxgmms2AcquireMutex(&Dxgmms2GlobalMutex);
    ASSERT(Dxgmms2ActiveRegistration == NULL);
    Dxgmms2ReleaseMutex(&Dxgmms2GlobalMutex);
}

NTSTATUS
NTAPI
DriverEntry(_In_ PDRIVER_OBJECT DriverObject, _In_ PUNICODE_STRING RegistryPath)
{
    NTSTATUS Status;

    UNREFERENCED_PARAMETER(RegistryPath);
    Status = Dxgmms2EnsureInitialized();
    if (!NT_SUCCESS(Status))
        return Status;
    DriverObject->DriverUnload = Dxgmms2DriverUnload;
    return STATUS_SUCCESS;
}
