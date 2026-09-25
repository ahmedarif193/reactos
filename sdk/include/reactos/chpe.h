/*
 * PROJECT:     ReactOS
 * PURPOSE:     Shared CHPE CPU-area declarations
 */

#pragma once

typedef struct _CHPE_V2_CPU_AREA_INFO
{
    BOOLEAN InSimulation;
    BOOLEAN InSyscallCallback;
    BOOLEAN CriticalLockHeld;
    BOOLEAN AvoidUpcallToKernel32;
    UCHAR Reserved0[4];
    ULONG64 EmulatorStackBase;
    ULONG64 EmulatorStackLimit;
    PVOID ContextAmd64;
    PULONG SuspendDoorbell;
    ULONG64 LoadingModuleModflag;
    PVOID EmulatorData[4];
    ULONG64 EmulatorDataInline;
} CHPE_V2_CPU_AREA_INFO, *PCHPE_V2_CPU_AREA_INFO;

C_ASSERT(FIELD_OFFSET(CHPE_V2_CPU_AREA_INFO, CriticalLockHeld) == 0x2);
C_ASSERT(FIELD_OFFSET(CHPE_V2_CPU_AREA_INFO, SuspendDoorbell) == 0x20);

#define CHPE_TEB_CPU_AREA_OFFSET 0x1788
#define CHPE_BRIDGE_BASE 0x7ffeb8000000ULL
