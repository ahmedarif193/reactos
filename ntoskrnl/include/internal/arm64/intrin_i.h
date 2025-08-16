/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     ARM64 Internal Intrinsics
 * COPYRIGHT:   Copyright 2025 ReactOS Team
 */

#pragma once

//
// ARM64 Memory Barriers
//
#define KeMemoryBarrier()       __dmb(_ARM64_BARRIER_SY)
#define KeMemoryBarrierWithoutFence() __dmb(_ARM64_BARRIER_SY)
#define KeLoadFence()           __dmb(_ARM64_BARRIER_LD)
#define KeStoreFence()          __dmb(_ARM64_BARRIER_ST)

//
// Spin wait
//
#define KeYieldProcessor()      __yield()
#define YieldProcessor()        __yield()

//
// IRQL management
//
FORCEINLINE
KIRQL
KfRaiseIrql(IN KIRQL NewIrql)
{
    KIRQL OldIrql;
    /* Simplified for initial implementation */
    OldIrql = KeGetCurrentIrql();
    /* Set new IRQL in PCR */
    KeGetPcr()->CurrentIrql = NewIrql;
    return OldIrql;
}

FORCEINLINE
VOID
KfLowerIrql(IN KIRQL OldIrql)
{
    /* Set IRQL in PCR */
    KeGetPcr()->CurrentIrql = OldIrql;
}

#define KeRaiseIrql(NewIrql, OldIrql) *(OldIrql) = KfRaiseIrql(NewIrql)
#define KeLowerIrql(OldIrql) KfLowerIrql(OldIrql)
#define KeRaiseIrqlToDpcLevel() KfRaiseIrql(DISPATCH_LEVEL)
#define KeRaiseIrqlToSynchLevel() KfRaiseIrql(SYNCH_LEVEL)

//
// Interrupt control
//
FORCEINLINE
BOOLEAN
KeDisableInterrupts(VOID)
{
    ULONG64 daif;
    __asm__ __volatile__("mrs %0, daif" : "=r" (daif));
    __asm__ __volatile__("msr daifset, #2");
    return (daif & 0x80) == 0;
}

FORCEINLINE
VOID
KeEnableInterrupts(VOID)
{
    __asm__ __volatile__("msr daifclr, #2");
}

FORCEINLINE
VOID
KeRestoreInterrupts(BOOLEAN Enable)
{
    if (Enable)
        KeEnableInterrupts();
}

//
// Cache operations
//
FORCEINLINE
VOID
KiFlushCache(VOID)
{
    __asm__ __volatile__(
        "dsb sy\n\t"
        "ic iallu\n\t"
        "dsb sy\n\t"
        "isb"
        ::: "memory"
    );
}

//
// TLB operations
//
FORCEINLINE
VOID
KiFlushTlb(VOID)
{
    __asm__ __volatile__(
        "dsb sy\n\t"
        "tlbi vmalle1\n\t"
        "dsb sy\n\t"
        "isb"
        ::: "memory"
    );
}

//
// Read system registers
//
FORCEINLINE
ULONG64
__readmsr(ULONG reg)
{
    // ARM64 doesn't have MSRs like x86
    // This is a stub for compatibility
    return 0;
}

FORCEINLINE
VOID
__writemsr(ULONG reg, ULONG64 value)
{
    // ARM64 doesn't have MSRs like x86
    // This is a stub for compatibility
}

//
// CPU ID
//
FORCEINLINE
VOID
__cpuid(INT CPUInfo[4], INT InfoType)
{
    // ARM64 doesn't have CPUID like x86
    // Read MIDR_EL1 for basic CPU info
    ULONG64 midr;
    __asm__ __volatile__("mrs %0, midr_el1" : "=r" (midr));
    
    CPUInfo[0] = (INT)(midr & 0xFFFFFFFF);
    CPUInfo[1] = (INT)(midr >> 32);
    CPUInfo[2] = 0;
    CPUInfo[3] = 0;
}

//
// Breakpoint
//
#define DbgBreakPoint() __asm__ __volatile__("brk #0")

//
// Debug print
//
#define KiDebugPrint(x) DbgPrint x

/* EOF */