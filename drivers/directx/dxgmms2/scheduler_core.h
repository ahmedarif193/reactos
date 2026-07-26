/*
 * PROJECT:     ReactOS WDDM 2.x Graphics Memory Manager and Scheduler
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Scheduler queue ownership core
 *
 * dxgmms2 owns the per-engine run queues and their admission order.  This
 * core is deliberately free of dxgkrnl and miniport types: a queued packet is
 * an opaque cookie, so the ordering, dispatch, and retirement rules can be
 * exercised without a graphics stack behind them.
 */

#pragma once

#include <ntddk.h>
#include <reactos/drivers/directx/dxgmms2.h>

#define DXGMMS2_SCHED_MAX_PACKETS   512

typedef struct _DXGMMS2_SCHED_PACKET
{
    LIST_ENTRY Entry;
    ULONGLONG  PacketCookie;
    ULONGLONG  OwnerCookie;
    ULONGLONG  ClaimToken;
    ULONG      SubmissionFenceId;
    ULONG      Flags;
    LONG       Priority;
    BOOLEAN    Dispatched;      /* handed to the miniport at least once */
    BOOLEAN    Claimed;         /* a dispatch claim is outstanding */
} DXGMMS2_SCHED_PACKET, *PDXGMMS2_SCHED_PACKET;

typedef struct _DXGMMS2_SCHED_ENGINE
{
    LIST_ENTRY RunQueue;
    ULONG      PendingPacketCount;
    ULONG      State;
    ULONG      LastSubmittedFenceId;
    ULONG      LastCompletedFenceId;
    ULONGLONG  NextClaimToken;
} DXGMMS2_SCHED_ENGINE, *PDXGMMS2_SCHED_ENGINE;

typedef struct _DXGMMS2_SCHED_CORE
{
    DXGMMS2_SCHED_ENGINE Engines[DXGMMS2_SCHEDULER_MAX_ENGINES];
    ULONG      EngineCount;
    LIST_ENTRY RetirementList;
    ULONG      RetirementCount;
    LIST_ENTRY FreeList;
    ULONG      TotalPackets;
    volatile LONG NextFenceId;
    BOOLEAN    AdmissionOpen;
    BOOLEAN    Started;
} DXGMMS2_SCHED_CORE, *PDXGMMS2_SCHED_CORE;

typedef struct _DXGMMS2_SCHED_RETIREMENT
{
    LIST_ENTRY Entry;
    DXGMMS2_SCHEDULER_RETIREMENT_V1 Record;
} DXGMMS2_SCHED_RETIREMENT, *PDXGMMS2_SCHED_RETIREMENT;

/* All of these run under one caller-owned lock. */
VOID Dxgmms2SchedCoreInitialize(_Out_ PDXGMMS2_SCHED_CORE Core);
NTSTATUS Dxgmms2SchedCoreStart(_Inout_ PDXGMMS2_SCHED_CORE Core, _In_ ULONG EngineCount);
VOID Dxgmms2SchedCoreSetAdmission(_Inout_ PDXGMMS2_SCHED_CORE Core, _In_ BOOLEAN Open);
NTSTATUS Dxgmms2SchedCoreAdmit(_Inout_ PDXGMMS2_SCHED_CORE Core, _In_ const DXGMMS2_SCHEDULER_ADMIT_INFO_V1 *Info, _Inout_ PDXGMMS2_SCHED_PACKET Packet, _Out_ PULONG OutFenceId);
BOOLEAN Dxgmms2SchedCoreClaim(_Inout_ PDXGMMS2_SCHED_CORE Core, _In_ ULONG EngineOrdinal, _Out_ DXGMMS2_SCHEDULER_CLAIM_V1 *Claim);
NTSTATUS Dxgmms2SchedCorePublishDispatch(_Inout_ PDXGMMS2_SCHED_CORE Core, _In_ ULONG EngineOrdinal, _In_ ULONGLONG ClaimToken);
NTSTATUS Dxgmms2SchedCoreCompleteDispatch(_Inout_ PDXGMMS2_SCHED_CORE Core, _In_ ULONG EngineOrdinal, _In_ ULONGLONG ClaimToken, _In_ NTSTATUS DispatchStatus, _Outptr_result_maybenull_ PDXGMMS2_SCHED_PACKET *OutFailed);
ULONG Dxgmms2SchedCoreNotifyCompletion(_Inout_ PDXGMMS2_SCHED_CORE Core, _In_ ULONG EngineOrdinal, _In_ ULONG CompletedFenceId, _Out_writes_to_(Capacity, return) PDXGMMS2_SCHED_PACKET *Retired, _In_ ULONG Capacity);
ULONG Dxgmms2SchedCoreCancelOwner(_Inout_ PDXGMMS2_SCHED_CORE Core, _In_ ULONGLONG OwnerCookie, _Out_writes_to_(Capacity, return) PDXGMMS2_SCHED_PACKET *Cancelled, _In_ ULONG Capacity);
ULONG Dxgmms2SchedCoreAbortAll(_Inout_ PDXGMMS2_SCHED_CORE Core, _Out_writes_to_(Capacity, return) PDXGMMS2_SCHED_PACKET *Aborted, _In_ ULONG Capacity);
BOOLEAN Dxgmms2SchedCoreIsIdle(_In_ PDXGMMS2_SCHED_CORE Core);
NTSTATUS Dxgmms2SchedCoreQueryEngine(_In_ PDXGMMS2_SCHED_CORE Core, _In_ ULONG EngineOrdinal, _Inout_ DXGMMS2_SCHEDULER_ENGINE_STATUS_V1 *Status);
NTSTATUS Dxgmms2SchedCoreSetEngineState(_Inout_ PDXGMMS2_SCHED_CORE Core, _In_ ULONG EngineOrdinal, _In_ ULONG NewState);

/* EOF */
