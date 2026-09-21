/*
 * PROJECT:     ReactOS NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/include/miproc.h
 * PURPOSE:     Process memory manager structures and interfaces
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once

#include <nvs/include/misys.h>

#define MI_SHARED_USER_DATA_VA    0x7FFE0000ULL
#define MI_DEFAULT_WS_MINIMUM     50
#define MI_DEFAULT_WS_MAXIMUM     345

typedef struct _MI_PROCESS
{
    LIST_ENTRY SystemLink;
    LIST_ENTRY SessionLink;
    PEPROCESS SessionProcess;
    MI_ADDRESS_SPACE Space;
    ULONG64 WorkingSetMinimum;
    ULONG64 WorkingSetMaximum;
    volatile LONG64 PeakWorkingSet;
    volatile LONG64 PeakCommit;
    ULONG64 Peb;
    BOOLEAN HardWorkingSetMaximum;
    BOOLEAN SessionLeader;
} MI_PROCESS, *PMI_PROCESS;

typedef struct _MI_PROCESS_COUNTERS
{
    ULONG64 PageFaultCount;
    ULONG64 WorkingSetSize;
    ULONG64 PeakWorkingSetSize;
    ULONG64 PagefileUsage;
    ULONG64 PeakPagefileUsage;
    ULONG64 PageTablePages;
} MI_PROCESS_COUNTERS, *PMI_PROCESS_COUNTERS;

typedef struct _MI_PROCESS_MANAGER
{
    MI_MUTEX Lock;
    LIST_ENTRY ProcessList;
    ULONG ProcessCount;
    PMI_SEGMENT UserSharedSegment;
    ULONG64 UserSharedSystemVa;
    PMI_MDL UserSharedMdl;
    BOOLEAN UserSharedAdopted;
    ULONG64 AvailableLow;
    ULONG64 AvailableHigh;
    PLIST_ENTRY TrimCursor;
    volatile LONG64 BalancePasses;
    volatile LONG64 PagesTrimmed;
} MI_PROCESS_MANAGER, *PMI_PROCESS_MANAGER;

NTSTATUS MiProcessManagerInitialize(_Inout_ PMI_SYSTEM System, _Out_ PMI_PROCESS_MANAGER Manager);
NTSTATUS MiProcessManagerInitializeEx(_Inout_ PMI_SYSTEM System, _Out_ PMI_PROCESS_MANAGER Manager,
                                      _In_ ULONG AdoptedFrame, _In_ ULONG64 AdoptedSystemVa);
VOID MiProcessManagerUninitialize(_Inout_ PMI_SYSTEM System, _Inout_ PMI_PROCESS_MANAGER Manager);

NTSTATUS MiProcessCreate(_Inout_ PMI_SYSTEM System, _Inout_ PMI_PROCESS_MANAGER Manager, _Out_ PMI_PROCESS Process);
NTSTATUS MiProcessAdopt(_Inout_ PMI_SYSTEM System, _Inout_ PMI_PROCESS_MANAGER Manager, _Out_ PMI_PROCESS Process,
                        _In_ ULONG RootFrame);
VOID MiProcessDelete(_Inout_ PMI_PROCESS_MANAGER Manager, _Inout_ PMI_PROCESS Process);
NTSTATUS MiProcessCreatePeb(_Inout_ PMI_PROCESS Process, _In_ ULONG64 Size, _Out_ PULONG64 Peb);
NTSTATUS MiProcessCreateTeb(_Inout_ PMI_PROCESS Process, _In_ ULONG64 Size, _Out_ PULONG64 Teb);
NTSTATUS MiProcessDeleteTeb(_Inout_ PMI_PROCESS Process, _In_ ULONG64 Teb);
NTSTATUS MiProcessSetWorkingSetLimits(_Inout_ PMI_PROCESS Process, _In_ ULONG64 Minimum, _In_ ULONG64 Maximum,
                                      _In_ BOOLEAN HardMaximum);
VOID MiProcessQueryCounters(_Inout_ PMI_PROCESS Process, _Out_ PMI_PROCESS_COUNTERS Counters);
ULONG MiProcessEmptyWorkingSet(_Inout_ PMI_PROCESS Process);

ULONG MiBalanceMemory(_Inout_ PMI_SYSTEM System, _Inout_ PMI_PROCESS_MANAGER Manager);

NTSTATUS MiCopyVirtualMemory(_Inout_ PMI_ADDRESS_SPACE SourceSpace, _In_ ULONG64 SourceAddress,
                             _Inout_ PMI_ADDRESS_SPACE TargetSpace, _In_ ULONG64 TargetAddress, _In_ ULONG64 Size,
                             _In_ BOOLEAN UserMode, _Out_ PULONG64 BytesCopied);
