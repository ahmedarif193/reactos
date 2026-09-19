/*
 * PROJECT:     ReactOS Runtime Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Baseline RISC-V64 user-mode sequenced lists
 */

#include <rtl.h>
#include <intrin.h>

#define RISCV_SLIST_HEADER_TYPE 0x1ULL
#define RISCV_SLIST_LOCK_BIT    0x4ULL
#define RISCV_SLIST_RESERVED    0x8ULL
#define RISCV_SLIST_POINTER     (~0xFULL)

C_ASSERT(sizeof(SLIST_HEADER) == 16);
C_ASSERT(__alignof(SLIST_HEADER) == 16);
C_ASSERT(FIELD_OFFSET(SLIST_HEADER, Region) == 8);

BOOLEAN RtlpUse16ByteSLists = TRUE;

static
DECLSPEC_NORETURN
VOID
RtlpRiscvSListFail(_In_ ULONG Code)
{
    __fastfail(Code);
}

static
ULONGLONG
RtlpRiscvAcquireSList(_Inout_ PSLIST_HEADER SListHead)
{
    ULONGLONG Region, LockedRegion;

    if ((SListHead == NULL) || ((ULONG_PTR)SListHead & 15))
        RtlpRiscvSListFail(FAST_FAIL_INVALID_ARG);

    for (;;)
    {
        Region = __atomic_load_n(&SListHead->Region, __ATOMIC_ACQUIRE);
        if (Region & RISCV_SLIST_LOCK_BIT)
        {
            YieldProcessor();
            continue;
        }

        if (!(Region & RISCV_SLIST_HEADER_TYPE) ||
            (Region & RISCV_SLIST_RESERVED))
        {
            RtlpRiscvSListFail(FAST_FAIL_CORRUPT_LIST_ENTRY);
        }

        LockedRegion = Region | RISCV_SLIST_LOCK_BIT;
        if (__atomic_compare_exchange_n(&SListHead->Region,
                                        &Region,
                                        LockedRegion,
                                        FALSE,
                                        __ATOMIC_ACQUIRE,
                                        __ATOMIC_RELAXED))
        {
            return Region;
        }
    }
}

static
VOID
RtlpRiscvPublishSList(
    _Inout_ PSLIST_HEADER SListHead,
    _In_ const SLIST_HEADER *NewHeader)
{
    ULONGLONG Region = NewHeader->Region & ~RISCV_SLIST_LOCK_BIT;

    __atomic_store_n(&SListHead->Alignment,
                     NewHeader->Alignment,
                     __ATOMIC_RELAXED);
    __atomic_store_n(&SListHead->Region, Region, __ATOMIC_RELEASE);
}

static
PSLIST_ENTRY
RtlpRiscvDecodeSList(_In_ ULONGLONG Region)
{
    return (PSLIST_ENTRY)(ULONG_PTR)(Region & RISCV_SLIST_POINTER);
}

VOID
NTAPI
RtlInitializeSListHead(_Out_ PSLIST_HEADER SListHead)
{
    if ((SListHead == NULL) || ((ULONG_PTR)SListHead & 15))
        RtlpRiscvSListFail(FAST_FAIL_INVALID_ARG);

    __atomic_store_n(&SListHead->Alignment, 0, __ATOMIC_RELAXED);
    __atomic_store_n(&SListHead->Region,
                     RISCV_SLIST_HEADER_TYPE,
                     __ATOMIC_RELEASE);
}

PSLIST_ENTRY
NTAPI
RtlFirstEntrySList(_In_ const SLIST_HEADER *SListHead)
{
    PSLIST_HEADER MutableHead = (PSLIST_HEADER)SListHead;
    SLIST_HEADER Header;

    Header.Region = RtlpRiscvAcquireSList(MutableHead);
    Header.Alignment = __atomic_load_n(&SListHead->Alignment,
                                       __ATOMIC_RELAXED);
    RtlpRiscvPublishSList(MutableHead, &Header);
    return RtlpRiscvDecodeSList(Header.Region);
}

WORD
NTAPI
RtlQueryDepthSList(_In_ PSLIST_HEADER SListHead)
{
    SLIST_HEADER Header;

    Header.Region = RtlpRiscvAcquireSList(SListHead);
    Header.Alignment = __atomic_load_n(&SListHead->Alignment,
                                       __ATOMIC_RELAXED);
    RtlpRiscvPublishSList(SListHead, &Header);
    return Header.Header16.Depth;
}

PSLIST_ENTRY
FASTCALL
RtlInterlockedPushListSList(
    _Inout_ PSLIST_HEADER SListHead,
    _Inout_ __drv_aliasesMem PSLIST_ENTRY List,
    _Inout_ PSLIST_ENTRY ListEnd,
    _In_ ULONG Count)
{
    SLIST_HEADER Header;
    PSLIST_ENTRY FirstEntry;

    if ((List == NULL) || (ListEnd == NULL) || (Count == 0) ||
        (((ULONG_PTR)List | (ULONG_PTR)ListEnd) & 15))
    {
        RtlpRiscvSListFail(FAST_FAIL_INVALID_ARG);
    }

    Header.Region = RtlpRiscvAcquireSList(SListHead);
    Header.Alignment = __atomic_load_n(&SListHead->Alignment,
                                       __ATOMIC_RELAXED);
    FirstEntry = RtlpRiscvDecodeSList(Header.Region);
    ListEnd->Next = FirstEntry;
    Header.Header16.Depth = (USHORT)(Header.Header16.Depth + Count);
    Header.Header16.Sequence++;
    Header.Header16.HeaderType = 1;
    Header.Header16.Init = 1;
    Header.Header16.Reserved = 0;
    Header.Header16.NextEntry = (ULONG_PTR)List >> 4;
    RtlpRiscvPublishSList(SListHead, &Header);
    return FirstEntry;
}

PSLIST_ENTRY
NTAPI
RtlInterlockedPushEntrySList(
    _Inout_ PSLIST_HEADER SListHead,
    _Inout_ __drv_aliasesMem PSLIST_ENTRY SListEntry)
{
    return RtlInterlockedPushListSList(SListHead,
                                       SListEntry,
                                       SListEntry,
                                       1);
}

PSLIST_ENTRY
NTAPI
RtlInterlockedPopEntrySList(_Inout_ PSLIST_HEADER SListHead)
{
    SLIST_HEADER Header;
    PSLIST_ENTRY FirstEntry, NextEntry;

    Header.Region = RtlpRiscvAcquireSList(SListHead);
    Header.Alignment = __atomic_load_n(&SListHead->Alignment,
                                       __ATOMIC_RELAXED);
    FirstEntry = RtlpRiscvDecodeSList(Header.Region);
    if (FirstEntry == NULL)
    {
        RtlpRiscvPublishSList(SListHead, &Header);
        return NULL;
    }

    NextEntry = FirstEntry->Next;
    if ((ULONG_PTR)NextEntry & 15)
        RtlpRiscvSListFail(FAST_FAIL_CORRUPT_LIST_ENTRY);

    Header.Header16.Depth--;
    Header.Header16.Sequence++;
    Header.Header16.HeaderType = 1;
    Header.Header16.Init = 1;
    Header.Header16.Reserved = 0;
    Header.Header16.NextEntry = (ULONG_PTR)NextEntry >> 4;
    RtlpRiscvPublishSList(SListHead, &Header);
    return FirstEntry;
}

PSLIST_ENTRY
NTAPI
RtlInterlockedFlushSList(_Inout_ PSLIST_HEADER SListHead)
{
    SLIST_HEADER Header;
    PSLIST_ENTRY FirstEntry;

    Header.Region = RtlpRiscvAcquireSList(SListHead);
    Header.Alignment = __atomic_load_n(&SListHead->Alignment,
                                       __ATOMIC_RELAXED);
    FirstEntry = RtlpRiscvDecodeSList(Header.Region);
    if (FirstEntry != NULL)
    {
        Header.Header16.Depth = 0;
        Header.Header16.Sequence++;
        Header.Header16.HeaderType = 1;
        Header.Header16.Init = 1;
        Header.Header16.Reserved = 0;
        Header.Header16.NextEntry = 0;
    }

    RtlpRiscvPublishSList(SListHead, &Header);
    return FirstEntry;
}
