/*
 * PROJECT:     ReactOS DirectX Graphics Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     GPU node execution accounting
 *
 * A GPU node is busy for exactly as long as the miniport holds one of its
 * packets.  The dispatch that hands a packet over opens the node's charge and
 * the retirement that takes it back closes it, so the busy clock never counts
 * time the hardware was not asked to do anything and never stops while it
 * still is.
 *
 * Getting this wrong is not a cosmetic matter: a charge left open makes a
 * node look permanently saturated, a charge closed twice makes a busy node
 * look idle, and a clock that steps backwards makes a running total go
 * negative.  The rules live here, with no dxgkrnl or miniport types, so they
 * can be exercised on their own.
 */

#ifndef _DXGK_NODE_STATS_CORE_H_
#define _DXGK_NODE_STATS_CORE_H_

#include <ntddk.h>

/*
 * The four DMA packet types WDDM distinguishes, in the order
 * D3DKMT_QUERYSTATISTICS_DMA_PACKET_TYPE numbers them: a client's rendering,
 * a client's paging, the system's paging, and preemption.
 */
#define DXGK_NODE_STATS_PACKET_TYPES 4

typedef struct _DXGK_NODE_STATS
{
    /* Busy time charged to this node, in performance-counter ticks. */
    volatile LONG64 RunningTicks;

    /* Counter reading taken when the node went busy; zero while idle. */
    volatile LONG64 BusySince;

    /* Packets the miniport currently holds for this node. */
    volatile LONG   ActiveCount;

    /* Dispatches, the retirements that answered them, and the packets a
     * preemption took back, each split by packet type. */
    volatile LONG64 PacketsDispatched[DXGK_NODE_STATS_PACKET_TYPES];
    volatile LONG64 PacketsRetired[DXGK_NODE_STATS_PACKET_TYPES];
    volatile LONG64 PacketsPreempted[DXGK_NODE_STATS_PACKET_TYPES];

    /* Every dispatch regardless of type: the node's context switch count. */
    volatile LONG64 ContextSwitches;

    /* Preemption requests the miniport accepted, and the reports back. */
    volatile LONG64 PreemptionsRequested;
    volatile LONG64 PreemptionsCompleted;
} DXGK_NODE_STATS, *PDXGK_NODE_STATS;

/*
 * Open a charge for one dispatched packet.  Only the first concurrent packet
 * starts the clock: a node executing two packets is busy once, not twice.
 */
VOID
DxgkNodeStatsCoreOpen(
    _Inout_ PDXGK_NODE_STATS Stats,
    _In_ ULONG PacketType,
    _In_ LONG64 Now);

/*
 * Close the charge for one retired packet.  The elapsed interval is added to
 * the running total only when the last packet leaves, and only when the clock
 * moved forward: an interval measured across a counter that went backwards is
 * dropped rather than added, because a running total that can decrease is
 * useless to every reader of it.
 */
VOID
DxgkNodeStatsCoreClose(
    _Inout_ PDXGK_NODE_STATS Stats,
    _In_ ULONG PacketType,
    _In_ LONG64 Now);

/* A preemption the miniport accepted, and the report that it happened. */
VOID DxgkNodeStatsCoreRequestPreemption(_Inout_ PDXGK_NODE_STATS Stats);
VOID DxgkNodeStatsCoreCompletePreemption(_Inout_ PDXGK_NODE_STATS Stats, _In_ ULONG PacketType, _In_ BOOLEAN PacketWasActive);

/*
 * Busy ticks as of Now, including any interval that is still open.  Leaving
 * the open interval out would make a saturated node report a running time
 * that stalls; including it here rather than committing it keeps the counter
 * itself monotonic.
 */
LONG64
DxgkNodeStatsCoreRunningTicks(
    _In_ const DXGK_NODE_STATS *Stats,
    _In_ LONG64 Now);

/*
 * Convert performance-counter ticks to 100ns units.  The division is split so
 * a fast counter does not lose the whole measurement to truncation and the
 * scaling cannot overflow on a machine that has been up for weeks.
 */
LONG64
DxgkNodeStatsCoreTicksTo100ns(
    _In_ LONG64 Ticks,
    _In_ LONG64 Frequency);

#endif /* _DXGK_NODE_STATS_CORE_H_ */
