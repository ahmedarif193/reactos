/*
 * PROJECT:     ReactOS DirectX Graphics Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Generic per-owner accepted-work ledger
 */

#ifndef _DXGK_DEVICE_WORK_CORE_H_
#define _DXGK_DEVICE_WORK_CORE_H_

#include <ntddk.h>

/* Called with Ledger->Lock held at DISPATCH_LEVEL; must be nonblocking,
 * nonpaged, non-reentrant, and must not acquire locks above the ledger. */
typedef NTSTATUS (NTAPI *PDXGK_DEVICE_WORK_TERMINAL_QUERY)(_In_opt_ PVOID Context);

typedef struct _DXGK_DEVICE_WORK_TERMINAL_STATE
{
    volatile LONG *Destroying;
    volatile LONG *ExecutionState;
    LONG ActiveExecutionState;
    NTSTATUS TerminalStatus;
    PDXGK_DEVICE_WORK_TERMINAL_QUERY Query;
    PVOID QueryContext;
} DXGK_DEVICE_WORK_TERMINAL_STATE, *PDXGK_DEVICE_WORK_TERMINAL_STATE;

typedef struct _DXGK_DEVICE_WORK_LEDGER
{
    KSPIN_LOCK Lock;
    LIST_ENTRY OutstandingList;
    ULONGLONG LastAssignedSequence;
    KEVENT ProgressEvent;
    DXGK_DEVICE_WORK_TERMINAL_STATE Terminal;
} DXGK_DEVICE_WORK_LEDGER, *PDXGK_DEVICE_WORK_LEDGER;

typedef enum _DXGK_DEVICE_WORK_ITEM_STATE
{
    DxgkDeviceWorkItemDormant = 0,
    DxgkDeviceWorkItemActive = 1,
    DxgkDeviceWorkItemCompleted = 2
} DXGK_DEVICE_WORK_ITEM_STATE;

typedef struct _DXGK_DEVICE_WORK_ITEM
{
    LIST_ENTRY ListEntry;
    PDXGK_DEVICE_WORK_LEDGER Ledger;
    ULONGLONG Sequence;
    volatile LONG State;
} DXGK_DEVICE_WORK_ITEM, *PDXGK_DEVICE_WORK_ITEM;

typedef struct _DXGK_DEVICE_WORK_SNAPSHOT
{
    PDXGK_DEVICE_WORK_LEDGER Ledger;
    ULONGLONG TargetSequence;
} DXGK_DEVICE_WORK_SNAPSHOT, *PDXGK_DEVICE_WORK_SNAPSHOT;

VOID DxgkDeviceWorkCoreInitializeLedger(_Out_ PDXGK_DEVICE_WORK_LEDGER Ledger, _In_opt_ const DXGK_DEVICE_WORK_TERMINAL_STATE *Terminal);
VOID DxgkDeviceWorkCoreInitializeItem(_Out_ PDXGK_DEVICE_WORK_ITEM Item, _In_ PDXGK_DEVICE_WORK_LEDGER Ledger);
NTSTATUS DxgkDeviceWorkCoreActivate(_Inout_ PDXGK_DEVICE_WORK_ITEM Item);
VOID DxgkDeviceWorkCoreComplete(_Inout_opt_ PDXGK_DEVICE_WORK_ITEM Item);
VOID DxgkDeviceWorkCoreNotifyStateChange(_Inout_opt_ PDXGK_DEVICE_WORK_LEDGER Ledger);
VOID DxgkDeviceWorkCoreTransitionTerminal(_Inout_opt_ PDXGK_DEVICE_WORK_LEDGER Ledger, _Inout_opt_ volatile LONG *State, _In_ LONG Value);
BOOLEAN DxgkDeviceWorkCoreTryTransitionTerminal(_Inout_opt_ PDXGK_DEVICE_WORK_LEDGER Ledger, _Inout_opt_ volatile LONG *State, _In_ LONG ExpectedValue, _In_ LONG NewValue);
NTSTATUS DxgkDeviceWorkCoreCaptureSnapshot(_Inout_ PDXGK_DEVICE_WORK_LEDGER Ledger, _Out_ PDXGK_DEVICE_WORK_SNAPSHOT Snapshot);
NTSTATUS DxgkDeviceWorkCoreWaitForSnapshot(_Inout_ PDXGK_DEVICE_WORK_LEDGER Ledger, _In_ const DXGK_DEVICE_WORK_SNAPSHOT *Snapshot, _Inout_opt_ PKEVENT ArmedEvent);
NTSTATUS DxgkDeviceWorkCoreWaitForIdle(_Inout_ PDXGK_DEVICE_WORK_LEDGER Ledger, _Inout_opt_ PKEVENT ArmedEvent);
BOOLEAN DxgkDeviceWorkCoreIsEmpty(_Inout_opt_ PDXGK_DEVICE_WORK_LEDGER Ledger);

#endif /* _DXGK_DEVICE_WORK_CORE_H_ */
