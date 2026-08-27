/*
 * PROJECT:     ReactOS DirectX Graphics Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Transaction state for POST display ownership handoff rollback
 * COPYRIGHT:   Copyright 2026 Ahmed Arif <arif.ing@outlook.com>
 */

#ifndef _DXGK_POSTDISPLAY_CORE_H_
#define _DXGK_POSTDISPLAY_CORE_H_

#include <ntddk.h>

typedef enum _DXGK_POST_DISPLAY_COMPLETION_ACTION
{
    DxgkPostDisplayCompletionNone = 0,
    DxgkPostDisplayCompletionCommit,
    DxgkPostDisplayCompletionRollback
} DXGK_POST_DISPLAY_COMPLETION_ACTION;

typedef struct _DXGK_POST_DISPLAY_HANDOFF_CORE
{
    BOOLEAN FallbackStopped;
} DXGK_POST_DISPLAY_HANDOFF_CORE, *PDXGK_POST_DISPLAY_HANDOFF_CORE;

FORCEINLINE
BOOLEAN
DxgkPostDisplayCoreArm(
    _Inout_ PDXGK_POST_DISPLAY_HANDOFF_CORE Core)
{
    if (Core->FallbackStopped)
        return FALSE;
    Core->FallbackStopped = TRUE;
    return TRUE;
}

FORCEINLINE
DXGK_POST_DISPLAY_COMPLETION_ACTION
DxgkPostDisplayCoreComplete(
    _Inout_ PDXGK_POST_DISPLAY_HANDOFF_CORE Core,
    _In_ NTSTATUS StartStatus,
    _In_ BOOLEAN Restartable)
{
    if (!Core->FallbackStopped)
        return DxgkPostDisplayCompletionNone;
    Core->FallbackStopped = FALSE;
    if (!NT_SUCCESS(StartStatus) && Restartable)
        return DxgkPostDisplayCompletionRollback;
    return DxgkPostDisplayCompletionCommit;
}

#endif /* _DXGK_POSTDISPLAY_CORE_H_ */
