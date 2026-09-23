/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     SMP kernel sequenced lists for baseline RV64GC
 */

#include <ntoskrnl.h>

C_ASSERT(sizeof(SLIST_HEADER) == 16);
C_ASSERT(__alignof(SLIST_HEADER) == 16);
C_ASSERT(FIELD_OFFSET(SLIST_HEADER, Region) == 8);

/* Headers and linked entries must be resident, normally cached kernel RAM.
 * No trap, non-maskable callback or firmware handler may reenter these lists.
 * The reserved header bit serializes the two 64-bit words without requiring
 * optional 128-bit atomics. Interrupt masking prevents local reentrancy. */
static
BOOLEAN
RtlpRiscvEnterSList(_In_ const SLIST_HEADER *SListHead)
{
    BOOLEAN WereEnabled = KeDisableInterrupts();

    if ((SListHead == NULL) || ((ULONG_PTR)SListHead & 15))
        KeBugCheckEx(KMODE_EXCEPTION_NOT_HANDLED, STATUS_DATATYPE_MISALIGNMENT, (ULONG_PTR)SListHead, 0x5256534C, 0);

    while (__atomic_fetch_or((PULONG64)&SListHead->Region, 4, __ATOMIC_ACQUIRE) & 4)
        YieldProcessor();
    return WereEnabled;
}

static
VOID
RtlpRiscvLeaveSList(_In_ const SLIST_HEADER *SListHead, _In_ BOOLEAN WereEnabled)
{
    __atomic_fetch_and((PULONG64)&SListHead->Region, ~4ULL, __ATOMIC_RELEASE);
    KeRestoreInterrupts(WereEnabled);
}

static
PSLIST_ENTRY
RtlpRiscvFirstSListEntry(_In_ const SLIST_HEADER *SListHead)
{
    ULONG64 Region = __atomic_load_n(&SListHead->Region, __ATOMIC_RELAXED) & ~4ULL;

    /* A zeroed header (the SDK InitializeSListHead form) is an empty list.
     * Otherwise only the full-width Header16 form is supported, never Header8. */
    if (Region == 0)
        return NULL;
    if (!(Region & 1))
        KeBugCheckEx(KMODE_EXCEPTION_NOT_HANDLED, STATUS_INVALID_PARAMETER, (ULONG_PTR)SListHead, 0x5256534C, Region);
    return (PSLIST_ENTRY)(ULONG_PTR)(Region & ~15ULL);
}

static
VOID
RtlpRiscvWriteSList(
    _Inout_ PSLIST_HEADER SListHead,
    _In_opt_ PSLIST_ENTRY FirstEntry,
    _In_ USHORT Depth)
{
    SLIST_HEADER NewHeader;

    NewHeader.Alignment = __atomic_load_n(&SListHead->Alignment, __ATOMIC_RELAXED);
    NewHeader.Region = 0;
    NewHeader.Header16.Depth = Depth;
    NewHeader.Header16.Sequence++;
    NewHeader.Header16.HeaderType = 1;
    NewHeader.Header16.Init = 1;
    NewHeader.Header16.NextEntry = (ULONG_PTR)FirstEntry >> 4;
    __atomic_store_n(&SListHead->Region, NewHeader.Region | 4, __ATOMIC_RELAXED);
    __atomic_store_n(&SListHead->Alignment, NewHeader.Alignment, __ATOMIC_RELAXED);
}

VOID
NTAPI
RtlInitializeSListHead(_Out_ PSLIST_HEADER SListHead)
{
    /* A new header need not contain initialized lock bits. The caller owns it. */
    ASSERT(SListHead && !((ULONG_PTR)SListHead & 15));
    __atomic_store_n(&SListHead->Alignment, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&SListHead->Region, 1, __ATOMIC_RELEASE);
}

PSLIST_ENTRY
NTAPI
RtlFirstEntrySList(_In_ const SLIST_HEADER *SListHead)
{
    BOOLEAN WereEnabled = RtlpRiscvEnterSList(SListHead);
    PSLIST_ENTRY Entry = RtlpRiscvFirstSListEntry(SListHead);

    RtlpRiscvLeaveSList(SListHead, WereEnabled);
    return Entry;
}

USHORT
NTAPI
RtlQueryDepthSList(_In_ PSLIST_HEADER SListHead)
{
    BOOLEAN WereEnabled = RtlpRiscvEnterSList(SListHead);
    USHORT Depth = (USHORT)__atomic_load_n(&SListHead->Alignment, __ATOMIC_RELAXED);

    RtlpRiscvLeaveSList(SListHead, WereEnabled);
    return Depth;
}

PSLIST_ENTRY
FASTCALL
RtlInterlockedPushListSList(
    _Inout_ PSLIST_HEADER SListHead,
    _Inout_ __drv_aliasesMem PSLIST_ENTRY List,
    _Inout_ PSLIST_ENTRY ListEnd,
    _In_ ULONG Count)
{
    BOOLEAN WereEnabled = RtlpRiscvEnterSList(SListHead);
    PSLIST_ENTRY FirstEntry;
    USHORT Depth;

    if (!List || !ListEnd || !Count || (((ULONG_PTR)List | (ULONG_PTR)ListEnd) & 15))
        KeBugCheckEx(KMODE_EXCEPTION_NOT_HANDLED, STATUS_INVALID_PARAMETER, (ULONG_PTR)List, (ULONG_PTR)ListEnd, Count);

    /* The caller owns the supplied chain and its count through ListEnd. */
    FirstEntry = RtlpRiscvFirstSListEntry(SListHead);
    Depth = (USHORT)__atomic_load_n(&SListHead->Alignment, __ATOMIC_RELAXED);
    ListEnd->Next = FirstEntry;
    RtlpRiscvWriteSList(SListHead, List, (USHORT)(Depth + Count));
    RtlpRiscvLeaveSList(SListHead, WereEnabled);
    return FirstEntry;
}

PSLIST_ENTRY
NTAPI
RtlInterlockedPushEntrySList(
    _Inout_ PSLIST_HEADER SListHead,
    _Inout_ __drv_aliasesMem PSLIST_ENTRY SListEntry)
{
    return RtlInterlockedPushListSList(SListHead, SListEntry, SListEntry, 1);
}

PSLIST_ENTRY
NTAPI
RtlInterlockedPopEntrySList(_Inout_ PSLIST_HEADER SListHead)
{
    BOOLEAN WereEnabled = RtlpRiscvEnterSList(SListHead);
    PSLIST_ENTRY FirstEntry = RtlpRiscvFirstSListEntry(SListHead);

    if (FirstEntry != NULL)
    {
        USHORT Depth = (USHORT)__atomic_load_n(&SListHead->Alignment, __ATOMIC_RELAXED);
        PSLIST_ENTRY NextEntry = FirstEntry->Next;

        if ((ULONG_PTR)NextEntry & 15)
            KeBugCheckEx(KMODE_EXCEPTION_NOT_HANDLED, STATUS_DATATYPE_MISALIGNMENT, (ULONG_PTR)NextEntry, 0x5256534C, 0);
        RtlpRiscvWriteSList(SListHead, NextEntry, (USHORT)(Depth - 1));
    }

    RtlpRiscvLeaveSList(SListHead, WereEnabled);
    return FirstEntry;
}

PSLIST_ENTRY
NTAPI
RtlInterlockedFlushSList(_Inout_ PSLIST_HEADER SListHead)
{
    BOOLEAN WereEnabled = RtlpRiscvEnterSList(SListHead);
    PSLIST_ENTRY FirstEntry = RtlpRiscvFirstSListEntry(SListHead);

    if (FirstEntry != NULL)
        RtlpRiscvWriteSList(SListHead, NULL, 0);
    RtlpRiscvLeaveSList(SListHead, WereEnabled);
    return FirstEntry;
}

/* The SDK uses Exp names inside the kernel as well as in driver imports.
 * Export aliases alone do not define symbols for those internal references. */
__typeof__(RtlInterlockedPushEntrySList) ExpInterlockedPushEntrySList
    __attribute__((alias("RtlInterlockedPushEntrySList")));
__typeof__(RtlInterlockedPopEntrySList) ExpInterlockedPopEntrySList
    __attribute__((alias("RtlInterlockedPopEntrySList")));
__typeof__(RtlInterlockedFlushSList) ExpInterlockedFlushSList
    __attribute__((alias("RtlInterlockedFlushSList")));
