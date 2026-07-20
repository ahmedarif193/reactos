/*
 * PROJECT:     ReactOS DirectX Graphics Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     VidMm destroy-worker admission and drain transitions
 */

#ifndef _DXGK_VIDMM_WORKER_DRAIN_CORE_H_
#define _DXGK_VIDMM_WORKER_DRAIN_CORE_H_

#include <ntddk.h>

/* The caller must serialize each transition, the admission-blocked state,
 * and the drained event with the same lock.  Event changes returned by these
 * helpers must be applied before that lock is released. */
BOOLEAN DxgkVidMmWorkerDrainCoreTryAdmitLocked(_Inout_ volatile LONG *ActiveWorkers, _In_ BOOLEAN AdmissionBlocked, _Out_ PBOOLEAN ResetDrainedEvent);
BOOLEAN DxgkVidMmWorkerDrainCoreRetireLocked(_Inout_ volatile LONG *ActiveWorkers, _Out_ PBOOLEAN SetDrainedEvent);
BOOLEAN DxgkVidMmWorkerDrainCoreIsDrainedLocked(_In_ volatile const LONG *ActiveWorkers);

#endif /* _DXGK_VIDMM_WORKER_DRAIN_CORE_H_ */
