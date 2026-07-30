/*
 * PROJECT:     ReactOS WDDM Software GPU Miniport
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     VSync phase and notification policy
 */

#ifndef _SOFTGPU_VSYNC_POLICY_CORE_H_
#define _SOFTGPU_VSYNC_POLICY_CORE_H_

#include <ntddk.h>

typedef enum _SOFTGPU_VSYNC_POLICY_STATE
{
    SoftGpuVsyncEnable = 0,
    SoftGpuVsyncDisableKeepPhase = 1,
    SoftGpuVsyncDisableNoPhase = 2
} SOFTGPU_VSYNC_POLICY_STATE;

typedef struct _SOFTGPU_VSYNC_POLICY
{
    BOOLEAN PhaseEnabled;
    BOOLEAN NotificationEnabled;
    BOOLEAN CancelTimer;
} SOFTGPU_VSYNC_POLICY, *PSOFTGPU_VSYNC_POLICY;

FORCEINLINE
BOOLEAN
SoftGpuVsyncEvaluatePolicy(
    _In_ ULONG State,
    _Out_ PSOFTGPU_VSYNC_POLICY Policy)
{
    Policy->PhaseEnabled = FALSE;
    Policy->NotificationEnabled = FALSE;
    Policy->CancelTimer = FALSE;

    switch (State)
    {
        case SoftGpuVsyncEnable:
            Policy->PhaseEnabled = TRUE;
            Policy->NotificationEnabled = TRUE;
            return TRUE;

        case SoftGpuVsyncDisableKeepPhase:
            Policy->PhaseEnabled = TRUE;
            return TRUE;

        case SoftGpuVsyncDisableNoPhase:
            Policy->CancelTimer = TRUE;
            return TRUE;

        default:
            return FALSE;
    }
}

#endif /* _SOFTGPU_VSYNC_POLICY_CORE_H_ */
