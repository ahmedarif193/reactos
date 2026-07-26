/*
 * PROJECT:     ReactOS DirectX Graphics Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Keyed mutex state machine
 * COPYRIGHT:   Copyright 2026 ReactOS Team
 */

#include "keyedmutex_core.h"

VOID
DxgkKeyedMutexCoreInitialize(
    _Out_ PDXGK_KEYED_MUTEX_STATE State,
    _In_ UINT64 InitialKey)
{
    RtlZeroMemory(State, sizeof(*State));
    State->CurrentKey = InitialKey;
}

BOOLEAN
DxgkKeyedMutexCoreCanAcquire(
    _In_ CONST DXGK_KEYED_MUTEX_STATE *State,
    _In_ UINT64 Key)
{
    return (!State->Owned && State->CurrentKey == Key);
}

BOOLEAN
DxgkKeyedMutexCoreAcquire(
    _Inout_ PDXGK_KEYED_MUTEX_STATE State,
    _In_ UINT64 Key)
{
    if (!DxgkKeyedMutexCoreCanAcquire(State, Key))
        return FALSE;
    State->Owned = TRUE;
    return TRUE;
}

BOOLEAN
DxgkKeyedMutexCoreRelease(
    _Inout_ PDXGK_KEYED_MUTEX_STATE State,
    _In_ UINT64 Key,
    _In_ UINT64 FenceValue)
{
    if (!State->Owned)
        return FALSE;
    State->CurrentKey = Key;
    State->FenceValue = FenceValue;
    State->Owned = FALSE;
    return TRUE;
}

VOID
DxgkKeyedMutexCoreAddWaiter(
    _Inout_ PDXGK_KEYED_MUTEX_STATE State)
{
    State->WaiterCount++;
}

VOID
DxgkKeyedMutexCoreRemoveWaiter(
    _Inout_ PDXGK_KEYED_MUTEX_STATE State)
{
    if (State->WaiterCount != 0)
        State->WaiterCount--;
}

BOOLEAN
DxgkKeyedMutexCorePrivateDataSizeValid(
    _In_ ULONG Size,
    _In_ BOOLEAN HasBuffer)
{
    if (Size == 0)
        return TRUE;
    if (!HasBuffer)
        return FALSE;
    return (Size <= DXGK_KEYED_MUTEX_MAX_PRIVATE_DATA);
}

/* EOF */
