/*
 * PROJECT:     ReactOS NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/include/mivad.h
 * PURPOSE:     Virtual address descriptor structures and interfaces
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once

typedef struct _MI_VAD_NODE
{
    struct _MI_VAD_NODE *Left;
    struct _MI_VAD_NODE *Right;
    struct _MI_VAD_NODE *Parent;
    ULONG64 StartingVpn;
    ULONG64 EndingVpn;
    ULONG64 SubtreeFirstVpn;
    ULONG64 SubtreeLastVpn;
    ULONG64 MaxGap;
    ULONG Height;
} MI_VAD_NODE, *PMI_VAD_NODE;

typedef struct _MI_VAD_ROOT
{
    PMI_VAD_NODE Root;
    ULONG NodeCount;
    ULONG64 LowestVpn;
    ULONG64 HighestVpn;
} MI_VAD_ROOT, *PMI_VAD_ROOT;

VOID MiVadRootInitialize(_Out_ PMI_VAD_ROOT Tree, _In_ ULONG64 LowestVpn, _In_ ULONG64 HighestVpn);
BOOLEAN MiVadInsert(_Inout_ PMI_VAD_ROOT Tree, _Inout_ PMI_VAD_NODE Node);
VOID MiVadRemove(_Inout_ PMI_VAD_ROOT Tree, _Inout_ PMI_VAD_NODE Node);
PMI_VAD_NODE MiVadFind(_In_ PMI_VAD_ROOT Tree, _In_ ULONG64 Vpn);
PMI_VAD_NODE MiVadFindOverlap(_In_ PMI_VAD_ROOT Tree, _In_ ULONG64 StartingVpn, _In_ ULONG64 EndingVpn);
PMI_VAD_NODE MiVadFirst(_In_ PMI_VAD_ROOT Tree);
PMI_VAD_NODE MiVadNext(_In_ PMI_VAD_NODE Node);
BOOLEAN MiVadFindEmptyRangeEx(_In_ PMI_VAD_ROOT Tree, _In_ ULONG64 PageCount, _In_ ULONG64 Alignment,
                              _In_ ULONG64 LowestVpn, _In_ ULONG64 HighestVpn, _In_ BOOLEAN TopDown,
                              _Out_ PULONG64 StartingVpn);
BOOLEAN MiVadFindEmptyRange(_In_ PMI_VAD_ROOT Tree, _In_ ULONG64 PageCount,
                            _In_ ULONG64 Alignment, _Out_ PULONG64 StartingVpn);
BOOLEAN MiVadFindEmptyRangeTopDown(_In_ PMI_VAD_ROOT Tree, _In_ ULONG64 PageCount,
                                   _In_ ULONG64 Alignment, _Out_ PULONG64 StartingVpn);
ULONG MiVadCheck(_In_ PMI_VAD_ROOT Tree);
