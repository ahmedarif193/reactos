/*
 * PROJECT:     ReactOS WDDM DirectX Graphics Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Retained scheduler-ordered synchronization batches
 */

#pragma once

#include "context_sync_core.h"

typedef struct _DXGKRNL_CONTEXT_SYNC_CAPTURE
{
    KSPIN_LOCK Lock;
    DXGK_CONTEXT_SYNC_CORE_RETENTION Retention;
    DXGK_CONTEXT_SYNC_OPERATION Operation;
    volatile LONG Executed;
    PDXGKRNL_DEVICE Device;
    PEPROCESS OwnerProcess;
    ULONG ObjectCount;
    ULONG SignalFlags;
    UINT64 PayloadValue;
    PKEVENT EnqueueEvent;
    PDXGKRNL_SYNC_OBJECT Objects[D3DDDI_MAX_OBJECT_SIGNALED];
    DXGK_CONTEXT_SYNC_CORE_OBJECT CoreObjects[D3DDDI_MAX_OBJECT_SIGNALED];
} DXGKRNL_CONTEXT_SYNC_CAPTURE, *PDXGKRNL_CONTEXT_SYNC_CAPTURE;

/* Capture and release run at PASSIVE_LEVEL.  Execute is nonpaged and may run
 * at any IRQL through DISPATCH_LEVEL while a scheduler marker is claimed. */

NTSTATUS
DxgkContextSyncCapture(
    _In_ PDXGKRNL_DEVICE Device,
    _In_ PEPROCESS OwnerProcess,
    _In_ KPROCESSOR_MODE AccessMode,
    _In_ DXGK_CONTEXT_SYNC_OPERATION Operation,
    _In_reads_opt_(ObjectCount) CONST D3DKMT_HANDLE *ObjectHandles,
    _In_ ULONG ObjectCount,
    _In_ ULONG SignalFlags,
    _In_ UINT64 PayloadValue,
    _Out_ PDXGKRNL_CONTEXT_SYNC_CAPTURE Capture);

NTSTATUS
DxgkContextSyncExecute(
    _Inout_ PDXGKRNL_CONTEXT_SYNC_CAPTURE Capture);

VOID
DxgkContextSyncRelease(
    _Inout_ PDXGKRNL_CONTEXT_SYNC_CAPTURE Capture);
