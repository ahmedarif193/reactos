/*
 * PROJECT:     ReactOS DirectX Graphics Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Handle tables, capability staircase, node affinity and TDR policy
 *
 * The bookkeeping around the GPU path.  A handle table that hands a recycled
 * index back without a generation bump lets a stale handle address somebody
 * else's object, which is the classic graphics-driver escape.  No dxgkrnl
 * or miniport types.
 */

#ifndef _DXGK_OBJECT_CORE_H_
#define _DXGK_OBJECT_CORE_H_

#include <ntddk.h>
#include "caps_core.h"

/* --- handle table ----------------------------------------------------- */

#define DXGK_HANDLE_CORE_MAX_ENTRIES  64

typedef enum _DXGK_HANDLE_TYPE
{
    DxgkHandleTypeNone       = 0,
    DxgkHandleTypeDevice     = 1,
    DxgkHandleTypeContext    = 2,
    DxgkHandleTypeAllocation = 3,
    DxgkHandleTypeResource   = 4,
    DxgkHandleTypeSync       = 5,
    DxgkHandleTypeMax
} DXGK_HANDLE_TYPE;

typedef struct _DXGK_HANDLE_ENTRY
{
    PVOID Object;
    DXGK_HANDLE_TYPE Type;
    ULONG Generation;
    BOOLEAN InUse;
} DXGK_HANDLE_ENTRY, *PDXGK_HANDLE_ENTRY;

typedef struct _DXGK_HANDLE_TABLE
{
    DXGK_HANDLE_ENTRY Entries[DXGK_HANDLE_CORE_MAX_ENTRIES];
    ULONG LiveCount;
} DXGK_HANDLE_TABLE, *PDXGK_HANDLE_TABLE;

VOID DxgkHandleCoreInitialize(_Out_ PDXGK_HANDLE_TABLE Table);
NTSTATUS DxgkHandleCoreAllocate(_Inout_ PDXGK_HANDLE_TABLE Table, _In_ PVOID Object, _In_ DXGK_HANDLE_TYPE Type, _Out_ PULONG Handle);
/* Resolving checks both the type and the generation, so a handle to a freed
 * object cannot address whatever now occupies the slot. */
NTSTATUS DxgkHandleCoreResolve(_In_ const DXGK_HANDLE_TABLE *Table, _In_ ULONG Handle, _In_ DXGK_HANDLE_TYPE ExpectedType, _Outptr_ PVOID *Object);
NTSTATUS DxgkHandleCoreFree(_Inout_ PDXGK_HANDLE_TABLE Table, _In_ ULONG Handle, _In_ DXGK_HANDLE_TYPE ExpectedType);
ULONG DxgkHandleCoreLiveCount(_In_ const DXGK_HANDLE_TABLE *Table);

/* --- node / engine affinity ------------------------------------------- */

#define DXGK_NODE_CORE_MAX_NODES  8

NTSTATUS DxgkNodeCoreValidateAffinity(_In_ ULONG AffinityMask, _In_ ULONG NodeCount);
/* Lowest set node in the mask; the ordinal a submission is steered to. */
BOOLEAN DxgkNodeCoreFirstNode(_In_ ULONG AffinityMask, _In_ ULONG NodeCount, _Out_ PULONG NodeOrdinal);
ULONG DxgkNodeCoreCountNodes(_In_ ULONG AffinityMask, _In_ ULONG NodeCount);

/* --- TDR policy -------------------------------------------------------- */

typedef enum _DXGK_TDR_ACTION
{
    DxgkTdrActionNone          = 0,
    DxgkTdrActionPreempt       = 1,
    DxgkTdrActionResetEngine   = 2,
    DxgkTdrActionResetAdapter  = 3,
    DxgkTdrActionRemoveAdapter = 4
} DXGK_TDR_ACTION;

typedef struct _DXGK_TDR_STATE
{
    ULONG TimeoutMs;
    ULONG ElapsedMs;
    ULONG ConsecutiveResets;
    ULONG MaxConsecutiveResets;
    BOOLEAN PacketOutstanding;
} DXGK_TDR_STATE, *PDXGK_TDR_STATE;

NTSTATUS DxgkTdrCoreInitialize(_Out_ PDXGK_TDR_STATE State, _In_ ULONG TimeoutMs, _In_ ULONG MaxConsecutiveResets);
VOID DxgkTdrCoreBeginPacket(_Inout_ PDXGK_TDR_STATE State);
VOID DxgkTdrCoreCompletePacket(_Inout_ PDXGK_TDR_STATE State);
/* Advances the watchdog and reports the escalation this tick calls for. */
DXGK_TDR_ACTION DxgkTdrCoreTick(_Inout_ PDXGK_TDR_STATE State, _In_ ULONG DeltaMs);
VOID DxgkTdrCoreNoteResetOutcome(_Inout_ PDXGK_TDR_STATE State, _In_ BOOLEAN Succeeded);

#endif /* _DXGK_OBJECT_CORE_H_ */
