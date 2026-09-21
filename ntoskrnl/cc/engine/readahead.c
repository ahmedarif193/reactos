/*
 * PROJECT:     ReactOS Cache Manager
 * FILE:        ntoskrnl/cc/engine/readahead.c
 * PURPOSE:     Cached file read-ahead
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#include "../include/ccengine.h"

VOID
CcReadAheadInitialize(
    _Out_ PCC_READ_AHEAD State)
{
    RtlZeroMemory(State, sizeof(*State));
    State->Granularity = CC_READ_AHEAD_DEFAULT;
}

BOOLEAN
CcReadAheadNote(
    _Inout_ PCC_READ_AHEAD State,
    _In_ ULONG64 FileOffset,
    _In_ ULONG Length,
    _In_ ULONG64 FileSize,
    _Out_ PULONG64 AheadOffset,
    _Out_ PULONG AheadLength)
{
    ULONG64 End = FileOffset + Length;
    ULONG64 Window;
    ULONG64 From;
    ULONG64 To;

    *AheadOffset = 0;
    *AheadLength = 0;

    if (State->Disabled || Length == 0)
        return FALSE;

    if (FileOffset == State->LastEnd)
    {
        State->SequentialHits++;
    }
    else
    {
        State->SequentialHits = 0;
        State->AheadEnd = 0;
    }

    State->LastEnd = End;

    if (State->SequentialHits < CC_SEQUENTIAL_HITS)
        return FALSE;

    Window = (ULONG64)State->Granularity << ((State->SequentialHits > 8) ? 3 : (State->SequentialHits / 3));
    if (Window > CC_READ_AHEAD_MAX)
        Window = CC_READ_AHEAD_MAX;

    if (State->AheadEnd >= End + Window / 2)
        return FALSE;

    From = (State->AheadEnd > End) ? State->AheadEnd : End;
    To = End + Window;
    if (To > FileSize)
        To = FileSize;

    if (From >= To)
        return FALSE;

    State->AheadEnd = To;
    *AheadOffset = From;
    *AheadLength = (ULONG)(To - From);
    return TRUE;
}
