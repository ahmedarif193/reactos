/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Single-hart kernel sequenced lists for RV64GC
 */

#include <ntoskrnl.h>

C_ASSERT(sizeof(SLIST_HEADER) == 16);
C_ASSERT(__alignof(SLIST_HEADER) == 16);
C_ASSERT(FIELD_OFFSET(SLIST_HEADER, Region) == 8);

/* Headers and linked entries must be resident, normally cached kernel RAM.
 * No trap, non-maskable callback or firmware handler may reenter these lists.
 * This is kernel UP serialization, not a general 128-bit atomic primitive. */
static
BOOLEAN
RtlpRiscvEnterSList(_In_ const SLIST_HEADER *SListHead)
{
    BOOLEAN WereEnabled = KeDisableInterrupts();

    if (KeNumberProcessors > 1)
        KeBugCheckEx(MULTIPROCESSOR_CONFIGURATION_NOT_SUPPORTED, 0x5256534C, KeNumberProcessors, 0, 0);
    if ((SListHead == NULL) || ((ULONG_PTR)SListHead & 15))
        KeBugCheckEx(KMODE_EXCEPTION_NOT_HANDLED, STATUS_DATATYPE_MISALIGNMENT, (ULONG_PTR)SListHead, 0x5256534C, 0);

    KeMemoryBarrier();
    return WereEnabled;
}

static
VOID
RtlpRiscvLeaveSList(_In_ BOOLEAN WereEnabled)
{
    KeMemoryBarrier();
    KeRestoreInterrupts(WereEnabled);
}

static
PSLIST_ENTRY
RtlpRiscvFirstSListEntry(_In_ const SLIST_HEADER *SListHead)
{
    ULONG64 Region = __atomic_load_n(&SListHead->Region, __ATOMIC_RELAXED);

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
    __atomic_store_n(&SListHead->Region, NewHeader.Region, __ATOMIC_RELAXED);
    __atomic_store_n(&SListHead->Alignment, NewHeader.Alignment, __ATOMIC_RELAXED);
}

VOID
NTAPI
RtlInitializeSListHead(_Out_ PSLIST_HEADER SListHead)
{
    BOOLEAN WereEnabled = RtlpRiscvEnterSList(SListHead);

    /* Initialization requires exclusive ownership before publication. */
    __atomic_store_n(&SListHead->Alignment, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&SListHead->Region, 1, __ATOMIC_RELAXED);
    RtlpRiscvLeaveSList(WereEnabled);
}

PSLIST_ENTRY
NTAPI
RtlFirstEntrySList(_In_ const SLIST_HEADER *SListHead)
{
    BOOLEAN WereEnabled = RtlpRiscvEnterSList(SListHead);
    PSLIST_ENTRY Entry = RtlpRiscvFirstSListEntry(SListHead);

    RtlpRiscvLeaveSList(WereEnabled);
    return Entry;
}

USHORT
NTAPI
RtlQueryDepthSList(_In_ PSLIST_HEADER SListHead)
{
    BOOLEAN WereEnabled = RtlpRiscvEnterSList(SListHead);
    USHORT Depth = (USHORT)__atomic_load_n(&SListHead->Alignment, __ATOMIC_RELAXED);

    RtlpRiscvLeaveSList(WereEnabled);
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
    RtlpRiscvLeaveSList(WereEnabled);
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

    RtlpRiscvLeaveSList(WereEnabled);
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
    RtlpRiscvLeaveSList(WereEnabled);
    return FirstEntry;
}

/* ELF intermediates do not yet have the PE export-alias generator. */
PSLIST_ENTRY
ExpInterlockedPushEntrySList(PSLIST_HEADER SListHead, PSLIST_ENTRY SListEntry)
{
    return RtlInterlockedPushEntrySList(SListHead, SListEntry);
}

PSLIST_ENTRY
ExpInterlockedPopEntrySList(PSLIST_HEADER SListHead)
{
    return RtlInterlockedPopEntrySList(SListHead);
}

PSLIST_ENTRY
ExpInterlockedFlushSList(PSLIST_HEADER SListHead)
{
    return RtlInterlockedFlushSList(SListHead);
}
