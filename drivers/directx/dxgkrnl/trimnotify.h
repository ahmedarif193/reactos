/*
 * PROJECT:     ReactOS DirectX Graphics Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Trim notification registration
 * COPYRIGHT:   Copyright 2026 ReactOS Team
 */

#pragma once

NTSTATUS DxgkTrimNotificationInitialize(VOID);
VOID DxgkTrimNotificationUninitialize(VOID);
NTSTATUS DxgkRegisterTrimNotification(_Inout_ D3DKMT_REGISTERTRIMNOTIFICATION *pData);
NTSTATUS DxgkUnregisterTrimNotification(_In_ CONST D3DKMT_UNREGISTERTRIMNOTIFICATION *pData);
VOID DxgkTrimNotificationProcessCleanup(_In_ PEPROCESS Process);
BOOLEAN DxgkTrimNotificationPending(_In_ D3DKMT_HANDLE hDevice);
