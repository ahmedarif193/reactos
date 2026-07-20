/*
 * PROJECT:     ReactOS DirectX Graphics Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     VidMm destroy-worker admission and drain transitions
 */

#include "vidmm_worker_drain_core.h"

BOOLEAN DxgkVidMmWorkerDrainCoreTryAdmitLocked(_Inout_ volatile LONG *ActiveWorkers, _In_ BOOLEAN AdmissionBlocked, _Out_ PBOOLEAN ResetDrainedEvent)
{
    if (ActiveWorkers == NULL || ResetDrainedEvent == NULL)
        return FALSE;
    *ResetDrainedEvent = FALSE;
    if (AdmissionBlocked || *ActiveWorkers < 0 || *ActiveWorkers == MAXLONG)
        return FALSE;
    *ResetDrainedEvent = *ActiveWorkers == 0;
    (*ActiveWorkers)++;
    return TRUE;
}

BOOLEAN DxgkVidMmWorkerDrainCoreRetireLocked(_Inout_ volatile LONG *ActiveWorkers, _Out_ PBOOLEAN SetDrainedEvent)
{
    if (ActiveWorkers == NULL || SetDrainedEvent == NULL)
        return FALSE;
    *SetDrainedEvent = FALSE;
    if (*ActiveWorkers <= 0)
        return FALSE;
    (*ActiveWorkers)--;
    *SetDrainedEvent = *ActiveWorkers == 0;
    return TRUE;
}

BOOLEAN DxgkVidMmWorkerDrainCoreIsDrainedLocked(_In_ volatile const LONG *ActiveWorkers)
{
    return ActiveWorkers != NULL && *ActiveWorkers == 0;
}
