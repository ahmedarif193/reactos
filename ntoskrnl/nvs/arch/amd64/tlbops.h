/*
 * PROJECT:     ReactOS NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/arch/amd64/tlbops.h
 * PURPOSE:     AMD64 translation lookaside buffer operation definitions
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#pragma once

#ifndef MM_HOST_TEST
FORCEINLINE ULONG MiAmd64TlbCpu(VOID) { return KeGetCurrentProcessorNumber(); }
FORCEINLINE ULONG64 MiAmd64TlbActiveCpus(VOID) { return (ULONG64)KeActiveProcessors; }
FORCEINLINE ULONG64 MiAmd64TlbReadCr3(VOID) { return __readcr3(); }
FORCEINLINE ULONG64 MiAmd64TlbReadCr4(VOID) { return __readcr4(); }
FORCEINLINE VOID MiAmd64TlbWriteCr3(ULONG64 Value) { __writecr3(Value); }
FORCEINLINE VOID MiAmd64TlbWriteCr4(ULONG64 Value) { __writecr4(Value); }
FORCEINLINE VOID MiAmd64TlbPage(ULONG64 Address) { __invlpg((PVOID)(ULONG_PTR)Address); }
FORCEINLINE VOID MiAmd64TlbCpuid(int Registers[4], int Leaf) { __cpuidex(Registers, Leaf, 0); }
FORCEINLINE VOID MiAmd64TlbPause(VOID) { YieldProcessor(); }

FORCEINLINE
BOOLEAN
MiAmd64TlbDisableInterrupts(VOID)
{
    BOOLEAN Enabled = (__readeflags() & (1ULL << 9)) != 0;
    _disable();
    return Enabled;
}

FORCEINLINE
VOID
MiAmd64TlbRestoreInterrupts(BOOLEAN Enabled)
{
    if (Enabled)
        _enable();
}

FORCEINLINE
KIRQL
MiAmd64TlbRaiseIrql(VOID)
{
    KIRQL Previous = KeGetCurrentIrql();
    if (Previous < SYNCH_LEVEL)
        KeRaiseIrql(SYNCH_LEVEL, &Previous);
    return Previous;
}

FORCEINLINE VOID MiAmd64TlbLowerIrql(KIRQL Previous) { KeLowerIrql(Previous); }

FORCEINLINE
VOID
MiAmd64TlbPoll(VOID)
{
    BOOLEAN Enabled = MiAmd64TlbDisableInterrupts();
    KiIpiProcessRequests();
    MiAmd64TlbRestoreInterrupts(Enabled);
}

FORCEINLINE
VOID
MiAmd64TlbInvpcid(VOID)
{
    volatile struct { ULONG64 Pcid, Address; } Descriptor;
    ULONG64 Type = 2;
    Descriptor.Pcid = 0;
    Descriptor.Address = 0;
    __asm__ __volatile__("invpcid %0, %1" : : "m"(Descriptor), "r"(Type) : "memory");
}
#endif
