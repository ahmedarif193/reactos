/*
 * PROJECT:     ReactOS DirectX GPU Memory Manager and Scheduler
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     DriverEntry and global state for dxgmms1.sys
 * COPYRIGHT:   Copyright 2024 ReactOS Team
 *
 * dxgmms1.sys is the GPU memory manager and scheduler helper that is
 * loaded explicitly by dxgkrnl.sys, NOT by the PnP manager.  It does
 * NOT create device objects and does NOT have IRP dispatch routines.
 *
 * The sole public interface is the pair of exported functions:
 *   DxgkMmsRegister()    — called by dxgkrnl to obtain the dispatch table
 *   DxgkMmsUnregister()  — called by dxgkrnl when the interface is torn down
 */

#include "dxgmms1_private.h"

/* -------------------------------------------------------------------------
 * Global state
 * ------------------------------------------------------------------------- */

/*
 * Spinlock protecting the global adapter list.
 */
KSPIN_LOCK  DxgMmsAdapterListLock;

/*
 * Global doubly-linked list of all DXGMMS_ADAPTER_CONTEXT objects.
 * Protected by DxgMmsAdapterListLock.
 */
LIST_ENTRY  DxgMmsAdapterListHead;

/* -------------------------------------------------------------------------
 * DxgkMmsUnload
 * ------------------------------------------------------------------------- */
VOID
NTAPI
DxgkMmsUnload(
    _In_ PDRIVER_OBJECT DriverObject)
{
    UNREFERENCED_PARAMETER(DriverObject);

    DXGMMS_TRACE("DxgkMmsUnload: unloading dxgmms1\n");

    DxgMmsAssert(IsListEmpty(&DxgMmsAdapterListHead),
                 DXGMMS1_BUGCHECK_DOUBLE_REMOVE);

    DXGMMS_TRACE("DxgkMmsUnload: unload complete\n");
}

/* -------------------------------------------------------------------------
 * DriverEntry
 * ------------------------------------------------------------------------- */
NTSTATUS
NTAPI
DriverEntry(
    _In_ PDRIVER_OBJECT  DriverObject,
    _In_ PUNICODE_STRING RegistryPath)
{
    UNREFERENCED_PARAMETER(RegistryPath);

    DXGMMS_TRACE("DriverEntry: DirectX GPU Memory Manager and Scheduler loading\n");

    KeInitializeSpinLock(&DxgMmsAdapterListLock);
    InitializeListHead(&DxgMmsAdapterListHead);

    DriverObject->DriverUnload = DxgkMmsUnload;

    DXGMMS_TRACE("DriverEntry: dxgmms1 loaded successfully\n");
    return STATUS_SUCCESS;
}
