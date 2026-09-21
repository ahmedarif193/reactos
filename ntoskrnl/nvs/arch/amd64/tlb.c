/*
 * PROJECT:     ReactOS NT Virtual Memory Subsystem
 * FILE:        ntoskrnl/nvs/arch/amd64/tlb.c
 * PURPOSE:     AMD64 translation lookaside buffer maintenance
 *
 * SPDX-FileCopyrightText: 2026 Ahmed ARIF
 * SPDX-License-Identifier: GPL-3.0-only
 */

#ifdef MM_HOST_TEST
#include <mmcc/nvs/amd64/tlbshim.h>
#else
#include <nvs/nt/mint.h>
#endif
#include "hardware.h"
#include "tlb.h"
#include "tlbops.h"

typedef struct MI_CACHE_ALIGNED _MI_AMD64_TLB_MESSAGE
{
    ULONG64 Address;
    ULONG64 Pages;
    ULONG64 Published;
    ULONG64 Completed;
} MI_AMD64_TLB_MESSAGE;

static MI_AMD64_TLB_MESSAGE MiAmd64TlbMessages[MAXIMUM_PROCESSORS][MAXIMUM_PROCESSORS];
static ULONG MiAmd64TlbFeatures[MAXIMUM_PROCESSORS];
volatile LONG KiTbFlushTimeStamp;

ULONG
MiAmd64TlbCapabilities(VOID)
{
    BOOLEAN Enabled = MiAmd64TlbDisableInterrupts();
    ULONG Cpu = MiAmd64TlbCpu();
    ULONG Features;
    int Registers[4];

    MI_ASSERT(Cpu < MAXIMUM_PROCESSORS);
    Features = __atomic_load_n(&MiAmd64TlbFeatures[Cpu], __ATOMIC_ACQUIRE);
    if (Features == 0)
    {
        Features = 1;
        MiAmd64TlbCpuid(Registers, 0);
        if ((ULONG)Registers[0] >= 7)
        {
            MiAmd64TlbCpuid(Registers, 7);
            if (Registers[1] & (1 << 10))
                Features |= MI_AMD64_TLB_INVPCID;
        }
        __atomic_store_n(&MiAmd64TlbFeatures[Cpu], Features, __ATOMIC_RELEASE);
    }
    MiAmd64TlbRestoreInterrupts(Enabled);
    return Features & ~1u;
}

static
VOID
MiAmd64FlushLocal(ULONG64 Address, ULONG64 Pages)
{
    BOOLEAN Enabled = MiAmd64TlbDisableInterrupts();

    if (Pages != 0)
    {
        do
        {
            MiAmd64TlbPage(Address);
            Address += PAGE_SIZE;
        } while (--Pages != 0);
    }
    else if (MiAmd64TlbCapabilities() & MI_AMD64_TLB_INVPCID)
    {
        MiAmd64TlbInvpcid();
    }
    else
    {
        ULONG64 Cr4 = MiAmd64TlbReadCr4();
        if (Cr4 & ((1ULL << 7) | (1ULL << 17)))
        {
            MiAmd64TlbWriteCr4(Cr4 ^ (1ULL << 7));
            MiAmd64TlbWriteCr4(Cr4);
        }
        else
        {
            MiAmd64TlbWriteCr3(MiAmd64TlbReadCr3() & ~(1ULL << 63));
        }
    }
    MiAmd64TlbRestoreInterrupts(Enabled);
}

VOID
MiAmd64ProcessTlbRequest(ULONG Source)
{
    BOOLEAN Enabled = MiAmd64TlbDisableInterrupts();
    ULONG Cpu = MiAmd64TlbCpu();
    MI_AMD64_TLB_MESSAGE *Message;
    ULONG64 Published;

    MI_ASSERT(Cpu < MAXIMUM_PROCESSORS && Source < MAXIMUM_PROCESSORS);
    Message = &MiAmd64TlbMessages[Cpu][Source];
    Published = __atomic_load_n(&Message->Published, __ATOMIC_ACQUIRE);
    if (Published != __atomic_load_n(&Message->Completed, __ATOMIC_RELAXED))
    {
        MiAmd64FlushLocal(Message->Address, Message->Pages);
        __atomic_store_n(&Message->Completed, Published, __ATOMIC_RELEASE);
    }
    MiAmd64TlbRestoreInterrupts(Enabled);
}

VOID
MiAmd64FlushTargets(ULONG64 Targets, ULONG64 Address, ULONG64 Pages)
{
    KIRQL Previous = MiAmd64TlbRaiseIrql();
    ULONG Cpu = MiAmd64TlbCpu(), Target;
    ULONG64 Remaining, Remote;

    MI_ASSERT(Cpu < MAXIMUM_PROCESSORS);
    Targets &= MiAmd64TlbActiveCpus() | (1ULL << Cpu);
    Address &= ~((ULONG64)PAGE_SIZE - 1);
    if (Pages > MI_AMD64_TLB_RANGE_LIMIT ||
        !MiAmd64CanonicalAddress(Address) ||
        (Pages != 0 && (Pages - 1 > (~Address >> PAGE_SHIFT) ||
                       !MiAmd64CanonicalAddress(Address + ((Pages - 1) << PAGE_SHIFT)))))
        Pages = 0;
    Remote = Targets & ~(1ULL << Cpu);
    for (Remaining = Remote; Remaining != 0; Remaining &= Remaining - 1)
    {
        MI_AMD64_TLB_MESSAGE *Message;
        ULONG64 Published;

        Target = (ULONG)__builtin_ctzll(Remaining);
        MI_ASSERT(Target < MAXIMUM_PROCESSORS);
        Message = &MiAmd64TlbMessages[Target][Cpu];
        Published = __atomic_load_n(&Message->Published, __ATOMIC_RELAXED);
        MI_ASSERT(Published == __atomic_load_n(&Message->Completed, __ATOMIC_ACQUIRE));
        Message->Address = Address;
        Message->Pages = Pages;
        __atomic_store_n(&Message->Published, Published + 1, __ATOMIC_RELEASE);
    }
    if (Remote != 0)
        KiSendMemoryIpi(Remote);
    if (Targets & (1ULL << Cpu))
        MiAmd64FlushLocal(Address, Pages);
    Remaining = Remote;
    while (Remaining != 0)
    {
        for (Remote = Remaining; Remote != 0; Remote &= Remote - 1)
        {
            MI_AMD64_TLB_MESSAGE *Message;
            Target = (ULONG)__builtin_ctzll(Remote);
            Message = &MiAmd64TlbMessages[Target][Cpu];
            if (__atomic_load_n(&Message->Completed, __ATOMIC_ACQUIRE) ==
                __atomic_load_n(&Message->Published, __ATOMIC_RELAXED))
                Remaining &= ~(1ULL << Target);
        }
        if (Remaining != 0)
        {
            MiAmd64TlbPoll();
            MiAmd64TlbPause();
        }
    }
    MiAmd64TlbLowerIrql(Previous);
}

VOID
MiArchInvalidateTlbRange(PVOID BaseAddress, SIZE_T Size, MI_TLB_SCOPE Scope)
{
    ULONG64 Address = (ULONG_PTR)BaseAddress;
    ULONG64 Pages, Targets;
    KIRQL Previous;

    if (Size == 0)
        return;
    Pages = ((Size - 1) >> PAGE_SHIFT) + 1;
    Pages += (((Size - 1) & (PAGE_SIZE - 1)) + (Address & (PAGE_SIZE - 1))) >> PAGE_SHIFT;
    Previous = MiAmd64TlbRaiseIrql();
    Targets = 1ULL << MiAmd64TlbCpu();
    if (Scope == MiTlbAllProcessors)
        Targets |= MiAmd64TlbActiveCpus();
    MiAmd64FlushTargets(Targets, Address, Pages);
    MiAmd64TlbLowerIrql(Previous);
}

VOID
MiArchInvalidateTlbSingle(PVOID Address, MI_TLB_SCOPE Scope)
{
    MiArchInvalidateTlbRange(Address, 1, Scope);
}

VOID
MiArchInvalidateTlbAll(MI_TLB_SCOPE Scope)
{
    KIRQL Previous = MiAmd64TlbRaiseIrql();
    ULONG64 Targets = 1ULL << MiAmd64TlbCpu();
    if (Scope == MiTlbAllProcessors)
        Targets |= MiAmd64TlbActiveCpus();
    MiAmd64FlushTargets(Targets, 0, 0);
    MiAmd64TlbLowerIrql(Previous);
}

VOID
NTAPI
KeFlushCurrentTb(VOID)
{
    MiAmd64FlushLocal(0, 0);
}

VOID
NTAPI
KeFlushEntireTb(BOOLEAN Invalid, BOOLEAN AllProcessors)
{
    UNREFERENCED_PARAMETER(Invalid);
    MiArchInvalidateTlbAll(AllProcessors ? MiTlbAllProcessors : MiTlbLocal);
    __atomic_fetch_add(&KiTbFlushTimeStamp, 1, __ATOMIC_RELAXED);
}

VOID
MiArchTlbInvalidate(ULONG64 VirtualAddress, ULONG64 PageCount, BOOLEAN AllProcessors)
{
    MI_TLB_SCOPE Scope = AllProcessors ? MiTlbAllProcessors : MiTlbLocal;
    if (PageCount > ((SIZE_T)-1 >> PAGE_SHIFT))
        MiArchInvalidateTlbAll(Scope);
    else
        MiArchInvalidateTlbRange((PVOID)(ULONG_PTR)VirtualAddress,
                                 (SIZE_T)(PageCount << PAGE_SHIFT), Scope);
}

VOID
MiArchTlbInvalidateAll(BOOLEAN AllProcessors)
{
    MiArchInvalidateTlbAll(AllProcessors ? MiTlbAllProcessors : MiTlbLocal);
}
