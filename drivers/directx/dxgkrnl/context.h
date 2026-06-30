/*
 * PROJECT:     ReactOS DirectX Graphics Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     WDDM device/context lifecycle — private declarations
 * COPYRIGHT:   Copyright 2024 ReactOS Team
 */

#pragma once

/*
 * DxgkContextInit
 *
 * Called from DriverEntry.  Seeds the handle obfuscation cookie and
 * registers DxgkProcessCleanup with PsSetCreateProcessNotifyRoutineEx.
 *
 * IRQL: PASSIVE_LEVEL
 */
NTSTATUS
DxgkContextInit(VOID);

/*
 * DxgkContextUninit
 *
 * Called from DriverUnload.  Deregisters the process-exit notification.
 *
 * IRQL: PASSIVE_LEVEL
 */
VOID
DxgkContextUninit(VOID);

/*
 * DxgkProcessCleanup
 *
 * PCREATE_PROCESS_NOTIFY_ROUTINE_EX callback invoked on process create/exit.
 * Destroys any DXGKRNL_DEVICE objects owned by the exiting process.
 * CreateInfo != NULL means process creation (ignored); NULL means process exit.
 *
 * IRQL: PASSIVE_LEVEL
 */
VOID
NTAPI
DxgkProcessCleanup(
    _Inout_  PEPROCESS              Process,
    _In_     HANDLE                 ProcessId,
    _In_opt_ PPS_CREATE_NOTIFY_INFO CreateInfo);
