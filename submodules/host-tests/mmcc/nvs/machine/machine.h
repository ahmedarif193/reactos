/*
 * PROJECT:     ReactOS host-native tests
 * FILE:        submodules/host-tests/mmcc/nvs/machine/machine.h
 * PURPOSE:     Host-native memory manager machine model interfaces
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once

#include <nvs/include/mienv.h>

#define MACHINE_MAX_CPUS     8
#define MACHINE_TLB_ENTRIES  64

typedef enum _MACHINE_ACCESS
{
    MachineRead = 0,
    MachineWrite,
    MachineExecute
} MACHINE_ACCESS;

typedef NTSTATUS (*MACHINE_FAULT_ROUTINE)(_In_opt_ PVOID Context, _In_ ULONG Cpu, _In_ ULONG64 VirtualAddress,
                                          _In_ MACHINE_ACCESS Access, _In_ BOOLEAN UserMode);

typedef struct _MACHINE_TLB_ENTRY
{
    BOOLEAN Valid;
    BOOLEAN Writable;
    BOOLEAN User;
    BOOLEAN Executable;
    ULONG64 Root;
    ULONG64 Vpn;
    ULONG64 Frame;
} MACHINE_TLB_ENTRY;

typedef struct _MACHINE_CPU
{
    ULONG64 UserRoot;
    volatile int Busy;
    MACHINE_TLB_ENTRY Tlb[MACHINE_TLB_ENTRIES];
} MACHINE_CPU;

typedef struct _MACHINE
{
    PUCHAR Ram;
    ULONG64 FrameCount;
    ULONG64 SystemRoot;
    ULONG CpuCount;
    MACHINE_CPU Cpu[MACHINE_MAX_CPUS];
    BOOLEAN HardwareAccessDirty;
    BOOLEAN StrictTlb;
    BOOLEAN TlbDisabled;
    MACHINE_FAULT_ROUTINE Fault;
    PVOID FaultContext;

    volatile LONG64 Faults;
    volatile LONG64 TlbHits;
    volatile LONG64 TlbMisses;
    volatile LONG64 TlbInvalidations;
    volatile LONG StaleTlbUses;
    volatile LONG BreakBeforeMakeViolations;
    volatile LONG BadFrameAccesses;
} MACHINE, *PMACHINE;

extern PMACHINE MachineCurrent;
extern _Thread_local ULONG MachineCpu;

void MachineCreate(PMACHINE Machine, ULONG64 FrameCount, ULONG CpuCount);
void MachineDestroy(PMACHINE Machine);
PUCHAR MachineFrame(PMACHINE Machine, ULONG64 Frame);
void MachineSetUserRoot(PMACHINE Machine, ULONG Cpu, ULONG64 RootFrame);
NTSTATUS MachineAccessMemory(PMACHINE Machine, ULONG Cpu, ULONG64 VirtualAddress, PVOID Buffer, SIZE_T Length,
                             MACHINE_ACCESS Access, BOOLEAN UserMode);
NTSTATUS MachineTouch(PMACHINE Machine, ULONG Cpu, ULONG64 VirtualAddress, MACHINE_ACCESS Access, BOOLEAN UserMode);
BOOLEAN MachineProbe(PMACHINE Machine, ULONG Cpu, ULONG64 VirtualAddress, PULONG64 Frame, PULONG64 LeafPte);
