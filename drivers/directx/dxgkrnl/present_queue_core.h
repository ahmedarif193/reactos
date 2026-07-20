/*
 * PROJECT:     ReactOS DirectX Graphics Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Stable circular present-queue removal core
 */

#ifndef _DXGK_PRESENT_QUEUE_CORE_H_
#define _DXGK_PRESENT_QUEUE_CORE_H_

#include <ntddk.h>

#define DXGKRNL_DEFAULT_QUEUED_PRESENT_LIMIT 3
#define DXGKRNL_DEFAULT_PENDING_FLIP_LIMIT 1

typedef BOOLEAN (NTAPI *PDXGK_PRESENT_QUEUE_MATCH)(_In_ const VOID *Entry, _In_opt_ PVOID Context);

typedef struct _DXGK_PRESENT_LIMIT_CORE
{
    KSPIN_LOCK Lock;
    ULONG Limit;
    ULONG Reserved;
} DXGK_PRESENT_LIMIT_CORE, *PDXGK_PRESENT_LIMIT_CORE;

VOID DxgkPresentLimitCoreInitialize(_Out_ PDXGK_PRESENT_LIMIT_CORE State, _In_ ULONG DefaultLimit);
NTSTATUS DxgkPresentLimitCoreSet(_Inout_ PDXGK_PRESENT_LIMIT_CORE State, _In_ ULONG RequestedLimit, _In_ ULONG DefaultLimit, _In_ ULONG MaximumLimit);
BOOLEAN DxgkPresentLimitCoreTryReserve(_Inout_ PDXGK_PRESENT_LIMIT_CORE State);
VOID DxgkPresentLimitCoreRelease(_Inout_ PDXGK_PRESENT_LIMIT_CORE State);
BOOLEAN DxgkPresentLimitCoreIsReached(_Inout_ PDXGK_PRESENT_LIMIT_CORE State);
ULONG DxgkPresentLimitCoreGetLimit(_Inout_ PDXGK_PRESENT_LIMIT_CORE State);
ULONG DxgkPresentLimitCoreGetReserved(_Inout_ PDXGK_PRESENT_LIMIT_CORE State);

/* Match executes under Lock at DISPATCH_LEVEL. The removed entry is returned
 * after Lock has been released so its owner can release resources safely. */
BOOLEAN
DxgkPresentQueueCoreRemove(
    _Inout_ PKSPIN_LOCK Lock,
    _Inout_ PVOID Entries,
    _In_ SIZE_T EntrySize,
    _In_ ULONG Capacity,
    _Inout_ PULONG Head,
    _Inout_ PULONG Tail,
    _Inout_ PULONG Count,
    _In_ PDXGK_PRESENT_QUEUE_MATCH Match,
    _In_opt_ PVOID MatchContext,
    _Out_ PVOID RemovedEntry);

#endif /* _DXGK_PRESENT_QUEUE_CORE_H_ */
