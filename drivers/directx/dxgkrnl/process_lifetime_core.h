/*
 * PROJECT:     ReactOS DirectX Graphics Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Per-process/per-adapter creation and destruction state machine
 */

#ifndef _DXGKRNL_PROCESS_LIFETIME_CORE_H_
#define _DXGKRNL_PROCESS_LIFETIME_CORE_H_

/*
 * Every operation below is serialized by the caller's process-list lock.
 * References acquired while Creating or Destroying are lifetime pins for a
 * waiter.  A Creating pin becomes an ordinary owner reference when creation
 * succeeds; a Destroying pin is released after the destroy-completion event.
 */
typedef enum _DXGK_PROCESS_LIFETIME_STATE
{
    DxgkProcessLifetimeCreating = 0,
    DxgkProcessLifetimeReady,
    DxgkProcessLifetimeDestroying,
    DxgkProcessLifetimeFailed
} DXGK_PROCESS_LIFETIME_STATE;

typedef enum _DXGK_PROCESS_LIFETIME_ACQUIRE
{
    DxgkProcessLifetimeAcquireReady = 0,
    DxgkProcessLifetimeWaitForCreate,
    DxgkProcessLifetimeWaitForDestroy,
    DxgkProcessLifetimeRejectReentrant,
    DxgkProcessLifetimeRetry
} DXGK_PROCESS_LIFETIME_ACQUIRE;

typedef enum _DXGK_PROCESS_LIFETIME_RELEASE
{
    DxgkProcessLifetimeReleaseNone = 0,
    DxgkProcessLifetimeBeginDestroy,
    DxgkProcessLifetimeFree
} DXGK_PROCESS_LIFETIME_RELEASE;

typedef struct _DXGK_PROCESS_LIFETIME_CORE
{
    LONG ReferenceCount;
    DXGK_PROCESS_LIFETIME_STATE State;
    NTSTATUS FailureStatus;
    PVOID CallbackOwner;
} DXGK_PROCESS_LIFETIME_CORE, *PDXGK_PROCESS_LIFETIME_CORE;

FORCEINLINE
VOID
DxgkProcessLifetimeInitialize(
    _Out_ PDXGK_PROCESS_LIFETIME_CORE Core,
    _In_ PVOID CallbackOwner)
{
    ASSERT(CallbackOwner != NULL);
    Core->ReferenceCount = 1;
    Core->State = DxgkProcessLifetimeCreating;
    Core->FailureStatus = STATUS_PENDING;
    Core->CallbackOwner = CallbackOwner;
}

FORCEINLINE
DXGK_PROCESS_LIFETIME_ACQUIRE
DxgkProcessLifetimeAcquire(
    _Inout_ PDXGK_PROCESS_LIFETIME_CORE Core,
    _In_ PVOID Caller)
{
    switch (Core->State)
    {
        case DxgkProcessLifetimeReady:
            Core->ReferenceCount++;
            return DxgkProcessLifetimeAcquireReady;

        case DxgkProcessLifetimeCreating:
            if (Caller != NULL && Core->CallbackOwner == Caller)
                return DxgkProcessLifetimeRejectReentrant;
            Core->ReferenceCount++;
            return DxgkProcessLifetimeWaitForCreate;

        case DxgkProcessLifetimeDestroying:
            if (Caller != NULL && Core->CallbackOwner == Caller)
                return DxgkProcessLifetimeRejectReentrant;
            Core->ReferenceCount++;
            return DxgkProcessLifetimeWaitForDestroy;

        case DxgkProcessLifetimeFailed:
        default:
            return DxgkProcessLifetimeRetry;
    }
}

FORCEINLINE
VOID
DxgkProcessLifetimeCompleteCreate(
    _Inout_ PDXGK_PROCESS_LIFETIME_CORE Core,
    _In_ PVOID CallbackOwner,
    _In_ NTSTATUS Status)
{
    ASSERT(Core->State == DxgkProcessLifetimeCreating);
    ASSERT(Core->CallbackOwner == CallbackOwner);
    Core->FailureStatus = Status;
    Core->CallbackOwner = NULL;
    Core->State = NT_SUCCESS(Status) ?
                      DxgkProcessLifetimeReady :
                      DxgkProcessLifetimeFailed;
}

FORCEINLINE
DXGK_PROCESS_LIFETIME_RELEASE
DxgkProcessLifetimeRelease(
    _Inout_ PDXGK_PROCESS_LIFETIME_CORE Core,
    _In_opt_ PVOID DestroyCallbackOwner)
{
    ASSERT(Core->ReferenceCount > 0);
    if (--Core->ReferenceCount != 0)
        return DxgkProcessLifetimeReleaseNone;

    switch (Core->State)
    {
        case DxgkProcessLifetimeReady:
            ASSERT(DestroyCallbackOwner != NULL);
            Core->State = DxgkProcessLifetimeDestroying;
            Core->CallbackOwner = DestroyCallbackOwner;
            return DxgkProcessLifetimeBeginDestroy;

        case DxgkProcessLifetimeDestroying:
        case DxgkProcessLifetimeFailed:
            return DxgkProcessLifetimeFree;

        case DxgkProcessLifetimeCreating:
        default:
            ASSERT(FALSE);
            return DxgkProcessLifetimeReleaseNone;
    }
}

FORCEINLINE
BOOLEAN
DxgkProcessLifetimeCompleteDestroy(
    _Inout_ PDXGK_PROCESS_LIFETIME_CORE Core,
    _In_ PVOID CallbackOwner)
{
    ASSERT(Core->State == DxgkProcessLifetimeDestroying);
    ASSERT(Core->CallbackOwner == CallbackOwner);
    Core->CallbackOwner = NULL;
    return Core->ReferenceCount == 0;
}

FORCEINLINE
BOOLEAN
DxgkProcessLifetimeStorageUnpinned(
    _In_ const DXGK_PROCESS_LIFETIME_CORE *Core)
{
    ASSERT(Core->State == DxgkProcessLifetimeDestroying);
    return Core->ReferenceCount == 0;
}

#endif /* _DXGKRNL_PROCESS_LIFETIME_CORE_H_ */
