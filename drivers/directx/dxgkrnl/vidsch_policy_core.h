/*
 * PROJECT:     ReactOS WDDM DirectX Graphics Kernel
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Testable video-scheduler policy helpers
 */

#ifndef _VIDSCH_POLICY_CORE_H_
#define _VIDSCH_POLICY_CORE_H_

#include <ntddk.h>

/*
 * The current scheduler has one public WDDM engine (ordinal zero) per node.
 * Its dxgmms2 queue array is flattened by node, so accepting another public
 * engine ordinal would alias two WDDM engines onto one queue.
 */
FORCEINLINE
BOOLEAN
VidSchPolicyNodeEngineSupported(
    _In_ ULONG NodeOrdinal,
    _In_ ULONG EngineOrdinal,
    _In_ ULONG NodeCount)
{
    return NodeOrdinal < NodeCount && EngineOrdinal == 0;
}

/*
 * Context-owned work is cancelled as one ordered stream.  Contextless paging
 * and tracked packets still need a nonzero, stable owner, supplied by their
 * referenced device.
 */
FORCEINLINE
PVOID
VidSchPolicyOwnerCookie(
    _In_opt_ PVOID Context,
    _In_opt_ PVOID Device)
{
    return Context != NULL ? Context : Device;
}

/* A packet completing after its device entered terminal page-fault state must
 * retire as failed; publishing it as success would signal work after the
 * faulting packet. */
FORCEINLINE
BOOLEAN
VidSchPolicyCompletionMustFail(
    _In_ LONG CurrentExecutionState,
    _In_ LONG PageFaultExecutionState)
{
    return CurrentExecutionState == PageFaultExecutionState;
}

#endif /* _VIDSCH_POLICY_CORE_H_ */
