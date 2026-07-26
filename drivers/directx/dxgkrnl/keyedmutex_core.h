/*
 * PROJECT:     ReactOS DirectX Graphics Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Keyed mutex state machine
 *
 * A keyed mutex guards a surface shared between processes: a release names the
 * key the next acquirer must ask for, so producer and consumer hand the surface
 * back and forth without either polling.  The dangerous parts are all here --
 * an acquire that succeeds on the wrong key, or a release that loses the
 * wake-up, corrupts the shared surface rather than failing loudly -- so the
 * transitions carry no dxgkrnl or miniport types and can be exercised alone.
 */

#ifndef _DXGK_KEYEDMUTEX_CORE_H_
#define _DXGK_KEYEDMUTEX_CORE_H_

#include <ntddk.h>

/* Private runtime data travels with the mutex so a process that opens it later
 * sees what the creator published.  Windows caps this; the cap is ours. */
#define DXGK_KEYED_MUTEX_MAX_PRIVATE_DATA 1024U

typedef struct _DXGK_KEYED_MUTEX_STATE
{
    UINT64  CurrentKey;
    UINT64  FenceValue;
    BOOLEAN Owned;
    /* Acquirers waiting for a key that no release has named yet.  Tracked so a
     * destroy can tell "nobody is here" from "somebody is parked in a wait". */
    ULONG   WaiterCount;
} DXGK_KEYED_MUTEX_STATE, *PDXGK_KEYED_MUTEX_STATE;

/* Initial state: unowned, holding the key the creator asked for. */
VOID
DxgkKeyedMutexCoreInitialize(
    _Out_ PDXGK_KEYED_MUTEX_STATE State,
    _In_ UINT64 InitialKey);

/*
 * TRUE when an acquire for Key can proceed right now.  Both conditions matter:
 * an unowned mutex holding a different key belongs to a different hand-off, and
 * an owned mutex holding the right key is still in use by its current owner.
 */
BOOLEAN
DxgkKeyedMutexCoreCanAcquire(
    _In_ CONST DXGK_KEYED_MUTEX_STATE *State,
    _In_ UINT64 Key);

/* Takes ownership; only valid when DxgkKeyedMutexCoreCanAcquire said so.
 * Returns FALSE without touching the state otherwise. */
BOOLEAN
DxgkKeyedMutexCoreAcquire(
    _Inout_ PDXGK_KEYED_MUTEX_STATE State,
    _In_ UINT64 Key);

/*
 * Publishes Key and drops ownership.  Refuses on an unowned mutex: a release
 * without a matching acquire would hand a surface to a waiter while the real
 * writer is still drawing into it.
 */
BOOLEAN
DxgkKeyedMutexCoreRelease(
    _Inout_ PDXGK_KEYED_MUTEX_STATE State,
    _In_ UINT64 Key,
    _In_ UINT64 FenceValue);

VOID DxgkKeyedMutexCoreAddWaiter(_Inout_ PDXGK_KEYED_MUTEX_STATE State);
VOID DxgkKeyedMutexCoreRemoveWaiter(_Inout_ PDXGK_KEYED_MUTEX_STATE State);

/* TRUE if the private-data size is one the mutex will carry. */
BOOLEAN
DxgkKeyedMutexCorePrivateDataSizeValid(
    _In_ ULONG Size,
    _In_ BOOLEAN HasBuffer);

#endif /* _DXGK_KEYEDMUTEX_CORE_H_ */
