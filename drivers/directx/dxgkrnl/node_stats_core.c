/*
 * PROJECT:     ReactOS DirectX Graphics Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     GPU node execution accounting
 */

#include "node_stats_core.h"

VOID
DxgkNodeStatsCoreOpen(
    _Inout_ PDXGK_NODE_STATS Stats,
    _In_ ULONG PacketType,
    _In_ LONG64 Now)
{
    if (Stats == NULL || PacketType >= DXGK_NODE_STATS_PACKET_TYPES)
        return;

    if (InterlockedIncrement(&Stats->ActiveCount) == 1)
        InterlockedExchange64(&Stats->BusySince, Now);
    InterlockedIncrement64(&Stats->PacketsDispatched[PacketType]);
    InterlockedIncrement64(&Stats->ContextSwitches);
}

VOID
DxgkNodeStatsCoreClose(
    _Inout_ PDXGK_NODE_STATS Stats,
    _In_ ULONG PacketType,
    _In_ LONG64 Now)
{
    LONG64 Start;

    if (Stats == NULL || PacketType >= DXGK_NODE_STATS_PACKET_TYPES)
        return;

    InterlockedIncrement64(&Stats->PacketsRetired[PacketType]);
    if (InterlockedDecrement(&Stats->ActiveCount) != 0)
        return;

    Start = InterlockedExchange64(&Stats->BusySince, 0);
    if (Start != 0 && Now > Start)
        InterlockedExchangeAdd64(&Stats->RunningTicks, Now - Start);
}

VOID
DxgkNodeStatsCoreRequestPreemption(
    _Inout_ PDXGK_NODE_STATS Stats)
{
    if (Stats == NULL)
        return;
    InterlockedIncrement64(&Stats->PreemptionsRequested);
}

VOID
DxgkNodeStatsCoreCompletePreemption(
    _Inout_ PDXGK_NODE_STATS Stats,
    _In_ ULONG PacketType,
    _In_ BOOLEAN PacketWasActive)
{
    if (Stats == NULL)
        return;
    InterlockedIncrement64(&Stats->PreemptionsCompleted);
    /* A preemption that caught no packet took nothing back; counting one
     * would claim work was interrupted that had already finished. */
    if (PacketWasActive && PacketType < DXGK_NODE_STATS_PACKET_TYPES)
        InterlockedIncrement64(&Stats->PacketsPreempted[PacketType]);
}

LONG64
DxgkNodeStatsCoreRunningTicks(
    _In_ const DXGK_NODE_STATS *Stats,
    _In_ LONG64 Now)
{
    LONG64 Ticks;
    LONG64 BusySince;

    if (Stats == NULL)
        return 0;

    Ticks = InterlockedCompareExchange64((volatile LONG64 *)&Stats->RunningTicks, 0, 0);
    BusySince = InterlockedCompareExchange64((volatile LONG64 *)&Stats->BusySince, 0, 0);
    if (BusySince != 0 && Now > BusySince)
        Ticks += Now - BusySince;
    return Ticks;
}

LONG64
DxgkNodeStatsCoreTicksTo100ns(
    _In_ LONG64 Ticks,
    _In_ LONG64 Frequency)
{
    if (Frequency <= 0 || Ticks <= 0)
        return 0;

    return (Ticks / Frequency) * 10000000LL +
           ((Ticks % Frequency) * 10000000LL) / Frequency;
}

/* EOF */
