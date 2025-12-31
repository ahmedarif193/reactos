/*
 * COPYRIGHT:       See COPYING in the top level directory
 * PROJECT:         ReactOS Runtime Library
 * PURPOSE:         Slist Routines
 * FILE:            lib/rtl/slist.c
 * PROGRAMERS:      Stefan Ginsberg (stefan.ginsberg@reactos.org)
 *                  Timo Kreuzer (timo.kreuzer@reactos.org)
 */

/* INCLUDES *****************************************************************/

#include <rtl.h>

#define NDEBUG
#include <debug.h>

#if defined(_M_ARM64)
typedef union _RTL_SLIST_CAS128
{
    __int128 Value;
    struct
    {
        ULONGLONG Low;
        ULONGLONG High;
    } Parts;
} RTL_SLIST_CAS128, *PRTL_SLIST_CAS128;

static __inline unsigned char
RtlpInterlockedCompareExchange128(
    _Interlocked_operand_ volatile __int64 *Destination,
    __int64 ExchangeHigh,
    __int64 ExchangeLow,
    __int64 *ComparandResult)
{
    RTL_SLIST_CAS128 Expected;
    RTL_SLIST_CAS128 Desired;

    Expected.Parts.Low = (ULONGLONG)ComparandResult[0];
    Expected.Parts.High = (ULONGLONG)ComparandResult[1];
    Desired.Parts.Low = (ULONGLONG)ExchangeLow;
    Desired.Parts.High = (ULONGLONG)ExchangeHigh;

    if (__atomic_compare_exchange_n((volatile __int128 *)Destination,
                                    &Expected.Value,
                                    Desired.Value,
                                    0,
                                    __ATOMIC_ACQ_REL,
                                    __ATOMIC_ACQUIRE))
    {
        return 1;
    }

    ComparandResult[0] = (LONG64)Expected.Parts.Low;
    ComparandResult[1] = (LONG64)Expected.Parts.High;
    return 0;
}
#define _InterlockedCompareExchange128 RtlpInterlockedCompareExchange128
#endif

#ifdef _WIN64
BOOLEAN RtlpUse16ByteSLists = -1;
#endif

/* FUNCTIONS ***************************************************************/

VOID
NTAPI
RtlInitializeSListHead(
    _Out_ PSLIST_HEADER SListHead)
{
#if defined(_WIN64)
    /* Make sure the header is 16 byte aligned */
    if (((ULONG_PTR)SListHead & 0xf) != 0)
    {
        DPRINT1("Unaligned SListHead: 0x%p\n", SListHead);
        RtlRaiseStatus(STATUS_DATATYPE_MISALIGNMENT);
    }

    /* Initialize the Region member */
#if defined(_IA64_)
    /* On Itanium we store the region in the list head */
    SListHead->Region = (ULONG_PTR)SListHead & VRN_MASK;
#else
    /* On amd64 and ARM64 we must initialize both fields to zero */
    SListHead->Region = 0;
#endif /* _IA64_ */

#if defined(_M_ARM64)
    /* ARM64 always uses 16-byte SLIST headers. */
    if (RtlpUse16ByteSLists == (BOOLEAN)-1)
    {
        RtlpUse16ByteSLists = TRUE;
    }
#endif
#endif /* _WIN64 */

    /*
     * CRITICAL: On ARM64 and AMD64, the SLIST_HEADER is a 16-byte structure
     * with separate Alignment and Region fields. Setting only Alignment = 0
     * is insufficient because Region is a separate 64-bit field that must
     * also be explicitly zeroed (already done above for _WIN64).
     */
    SListHead->Alignment = 0;
}

PSLIST_ENTRY
NTAPI
RtlFirstEntrySList(
    _In_ const SLIST_HEADER *SListHead)
{
#if defined(_WIN64)
    /* Check if the header is initialized as 16 byte header */
    if (SListHead->Header16.HeaderType)
    {
        return (PVOID)(SListHead->Region & ~0xFLL);
    }
    else
    {
        union {
            ULONG64 Region;
            struct {
                ULONG64 Reserved:4;
                ULONG64 NextEntry:39;
                ULONG64 Reserved2:21;
            } Bits;
        } Pointer;

#if defined(_IA64_)
        /* On Itanium we stored the region in the list head */
        Pointer.Region = SListHead->Region;
#else
        /* On amd64 we just use the list head itself */
        Pointer.Region = (ULONG64)SListHead;
#endif
        Pointer.Bits.NextEntry = SListHead->Header8.NextEntry;
        return (PVOID)Pointer.Region;
    }
#else
    return SListHead->Next.Next;
#endif
}

WORD
NTAPI
RtlQueryDepthSList(
    _In_ PSLIST_HEADER SListHead)
{
#if defined(_WIN64)
    return (USHORT)(SListHead->Alignment & 0xffff);
#else
    return SListHead->Depth;
#endif
}

PSLIST_ENTRY
FASTCALL
RtlInterlockedPushListSList(
    _Inout_ PSLIST_HEADER SListHead,
    _Inout_ __drv_aliasesMem PSLIST_ENTRY List,
    _Inout_ PSLIST_ENTRY ListEnd,
    _In_ ULONG Count)
{
#ifdef _WIN64
    SLIST_HEADER OldSListHead, NewSListHead;
    PSLIST_ENTRY FirstEntry;

    ASSERT(((ULONG_PTR)SListHead & 0xF) == 0);
    ASSERT(((ULONG_PTR)List & 0xF) == 0);

    if (RtlpUse16ByteSLists)
    {
         BOOLEAN exchanged;

        do
        {
            /* Capture the current SListHead */
            OldSListHead = *SListHead;

            /* Link the last list entry */
            FirstEntry = (PSLIST_ENTRY)(SListHead->Region & ~0xFLL);
            ListEnd->Next = FirstEntry;

            /* Set up new SListHead */
            NewSListHead = OldSListHead;
            NewSListHead.Header16.Depth += Count;
            NewSListHead.Header16.Sequence++;
            NewSListHead.Region = (ULONG64)List;
            NewSListHead.Header16.HeaderType = 1;
            NewSListHead.Header16.Init = 1;

            /* Atomically exchange the SlistHead with the new one */
            exchanged = _InterlockedCompareExchange128((PLONG64)SListHead,
                                                       NewSListHead.Region,
                                                       NewSListHead.Alignment,
                                                       (PLONG64)&OldSListHead);
        } while (!exchanged);

        return FirstEntry;
    }
    else
    {
        ULONG64 Compare;

        /* ListHead and List must be in the same region */
        ASSERT(((ULONG64)SListHead & 0xFFFFF80000000000ull) ==
               ((ULONG64)List & 0xFFFFF80000000000ull));

        /* Read the header */
        OldSListHead = *SListHead;

        do
        {
            /* Construct the address from the header bits and the list head pointer */
            FirstEntry = (PSLIST_ENTRY)((OldSListHead.Header8.NextEntry << 4) |
                                        ((ULONG64)SListHead & 0xFFFFF80000000000ull));

            /* Link the last list entry */
            ListEnd->Next = FirstEntry;

            /* Create a new header */
            NewSListHead = OldSListHead;
            NewSListHead.Header8.NextEntry = (ULONG64)List >> 4;
            NewSListHead.Header8.Depth += Count;
            NewSListHead.Header8.Sequence++;

            /* Try to exchange atomically */
            Compare = OldSListHead.Alignment;
            OldSListHead.Alignment = InterlockedCompareExchange64((PLONG64)&SListHead->Alignment,
                                                                  NewSListHead.Alignment,
                                                                  Compare);
        } while (OldSListHead.Alignment != Compare);

        /* Return the old first entry */
        return FirstEntry;
    }
#else
    SLIST_HEADER OldHeader, NewHeader;
    ULONGLONG Compare;

    /* Read the header */
    OldHeader = *SListHead;

    do
    {
        /* Link the last list entry */
        ListEnd->Next = OldHeader.Next.Next;

        /* Create a new header */
        NewHeader = OldHeader;
        NewHeader.Next.Next = List;
        NewHeader.Depth += Count;
        NewHeader.Sequence++;

        /* Try to exchange atomically */
        Compare = OldHeader.Alignment;
        OldHeader.Alignment = InterlockedCompareExchange64((PLONGLONG)&SListHead->Alignment,
                                                           NewHeader.Alignment,
                                                           Compare);
    }
    while (OldHeader.Alignment != Compare);

    /* Return the old first entry */
    return OldHeader.Next.Next;
#endif /* _WIN64 */
}


#if !defined(_M_IX86) && !defined(_M_AMD64)

_WARN("C based S-List functions can bugcheck, if not handled properly in kernel")

#if defined(_M_ARM64)

PSLIST_ENTRY
NTAPI
RtlInterlockedPushEntrySList(
    _Inout_ PSLIST_HEADER SListHead,
    _Inout_ __drv_aliasesMem PSLIST_ENTRY SListEntry)
{
    SLIST_HEADER OldHeader, NewHeader;
    PSLIST_ENTRY FirstEntry;
    BOOLEAN exchanged;

    /*
     * ARM64 CRITICAL: Check for NULL parameters before any operations.
     */
    if (SListHead == NULL)
    {
        USHORT Frames;
        PVOID Stack[8];
        USHORT i;

        Frames = RtlCaptureStackBackTrace(1, RTL_NUMBER_OF(Stack), Stack, NULL);
        DPRINT1("RtlInterlockedPushEntrySList: NULL SListHead passed - caller bug!\n");
        DPRINT1("    SListEntry=%p\n", SListEntry);
        DPRINT1("    Backtrace (%u frames):\n", Frames);
        for (i = 0; i < Frames; i++)
        {
            DPRINT1("      [%u] %p\n", i, Stack[i]);
        }
        return NULL;
    }
    if (SListEntry == NULL)
    {
        DPRINT1("RtlInterlockedPushEntrySList: NULL SListEntry passed - caller bug!\n");
        DPRINT1("    SListHead=%p\n", SListHead);
        return NULL;
    }

    ASSERT(((ULONG_PTR)SListHead & 0xF) == 0);
    ASSERT(((ULONG_PTR)SListEntry & 0xF) == 0);

    if (RtlpUse16ByteSLists == (BOOLEAN)-1)
    {
        RtlpUse16ByteSLists = TRUE;
    }

    do
    {
        OldHeader = *SListHead;
        FirstEntry = (PSLIST_ENTRY)(OldHeader.Region & ~0xFLL);
        SListEntry->Next = FirstEntry;

        NewHeader = OldHeader;
        NewHeader.Header16.Depth++;
        NewHeader.Header16.Sequence++;
        NewHeader.Region = (ULONG64)SListEntry;
        NewHeader.Header16.HeaderType = 1;
        NewHeader.Header16.Init = 1;

        exchanged = _InterlockedCompareExchange128((PLONG64)SListHead,
                                                   NewHeader.Region,
                                                   NewHeader.Alignment,
                                                   (PLONG64)&OldHeader);
    } while (!exchanged);

    return FirstEntry;
}

PSLIST_ENTRY
NTAPI
RtlInterlockedPopEntrySList(
    _Inout_ PSLIST_HEADER SListHead)
{
    SLIST_HEADER OldHeader, NewHeader;
    PSLIST_ENTRY FirstEntry, NextEntry;
    BOOLEAN exchanged;

    /*
     * ARM64 CRITICAL: Check for NULL SListHead before any operations.
     * This can happen if lookaside lists are not properly initialized
     * or if there's corruption in the PRCB lookaside pointer arrays.
     */
    if (SListHead == NULL)
    {
        DPRINT1("RtlInterlockedPopEntrySList: NULL SListHead passed - caller bug!\n");
        return NULL;
    }

    ASSERT(((ULONG_PTR)SListHead & 0xF) == 0);

    if (RtlpUse16ByteSLists == (BOOLEAN)-1)
    {
        RtlpUse16ByteSLists = TRUE;
    }

    do
    {
        OldHeader = *SListHead;
        FirstEntry = (PSLIST_ENTRY)(OldHeader.Region & ~0xFLL);
        if (FirstEntry == NULL)
        {
            return NULL;
        }

        NextEntry = FirstEntry->Next;

        NewHeader = OldHeader;
        NewHeader.Header16.Depth--;
        NewHeader.Header16.Sequence++;
        NewHeader.Region = (ULONG64)NextEntry;
        NewHeader.Header16.HeaderType = 1;
        NewHeader.Header16.Init = 1;

        exchanged = _InterlockedCompareExchange128((PLONG64)SListHead,
                                                   NewHeader.Region,
                                                   NewHeader.Alignment,
                                                   (PLONG64)&OldHeader);
    } while (!exchanged);

    return FirstEntry;
}

PSLIST_ENTRY
NTAPI
RtlInterlockedFlushSList(
    _Inout_ PSLIST_HEADER SListHead)
{
    SLIST_HEADER OldHeader, NewHeader;
    PSLIST_ENTRY FirstEntry;
    BOOLEAN exchanged;

    /*
     * ARM64 CRITICAL: Check for NULL SListHead before any operations.
     */
    if (SListHead == NULL)
    {
        DPRINT1("RtlInterlockedFlushSList: NULL SListHead passed - caller bug!\n");
        return NULL;
    }

    ASSERT(((ULONG_PTR)SListHead & 0xF) == 0);

    if (RtlpUse16ByteSLists == (BOOLEAN)-1)
    {
        RtlpUse16ByteSLists = TRUE;
    }

    do
    {
        OldHeader = *SListHead;
        FirstEntry = (PSLIST_ENTRY)(OldHeader.Region & ~0xFLL);
        if (FirstEntry == NULL)
        {
            return NULL;
        }

        NewHeader = OldHeader;
        NewHeader.Header16.Depth = 0;
        NewHeader.Header16.Sequence++;
        NewHeader.Region = 0;
        NewHeader.Header16.HeaderType = 1;
        NewHeader.Header16.Init = 1;

        exchanged = _InterlockedCompareExchange128((PLONG64)SListHead,
                                                   NewHeader.Region,
                                                   NewHeader.Alignment,
                                                   (PLONG64)&OldHeader);
    } while (!exchanged);

    return FirstEntry;
}

#else /* !_M_ARM64 */

#ifdef _WIN64
#error "No generic S-List functions for WIN64!"
#endif

/* This variable must be used in kernel mode to prevent the system from
   bugchecking on non-present kernel memory. If this variable is set to TRUE
   an exception needs to be dispatched. */
BOOLEAN RtlpExpectSListFault;

PSLIST_ENTRY
NTAPI
RtlInterlockedPushEntrySList(
    _Inout_ PSLIST_HEADER SListHead,
    _Inout_ __drv_aliasesMem PSLIST_ENTRY SListEntry)
{
    SLIST_HEADER OldHeader, NewHeader;
    ULONGLONG Compare;

    /* Read the header */
    OldHeader = *SListHead;

    do
    {
        /* Link the list entry */
        SListEntry->Next = OldHeader.Next.Next;

        /* Create a new header */
        NewHeader = OldHeader;
        NewHeader.Next.Next = SListEntry;
        NewHeader.Depth++;
        NewHeader.Sequence++;

        /* Try to exchange atomically */
        Compare = OldHeader.Alignment;
        OldHeader.Alignment = InterlockedCompareExchange64((PLONGLONG)&SListHead->Alignment,
                                                           NewHeader.Alignment,
                                                           Compare);
    }
    while (OldHeader.Alignment != Compare);

    /* Return the old first entry */
    return OldHeader.Next.Next;
}

PSLIST_ENTRY
NTAPI
RtlInterlockedPopEntrySList(
    _Inout_ PSLIST_HEADER SListHead)
{
    SLIST_HEADER OldHeader, NewHeader;
    ULONGLONG Compare;

restart:

    /* Read the header */
    OldHeader = *SListHead;

    do
    {
        /* Check for empty list */
        if (OldHeader.Next.Next == NULL)
        {
            return NULL;
        }

        /* Create a new header */
        NewHeader = OldHeader;

        /* HACK to let the kernel know that we are doing slist-magic */
        RtlpExpectSListFault = TRUE;

        /* Wrapped in SEH, since OldHeader.Next.Next can already be freed */
        _SEH2_TRY
        {
            NewHeader.Next = *OldHeader.Next.Next;
        }
        _SEH2_EXCEPT((SListHead->Next.Next != OldHeader.Next.Next) ?
                     EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
        {
            /* We got an exception and the list head changed.
               Restart the whole operation. */
            RtlpExpectSListFault = FALSE;
            goto restart;
        }
        _SEH2_END;

        /* We are done */
        RtlpExpectSListFault = FALSE;

        /* Adjust depth */
        NewHeader.Depth--;

        /* Try to exchange atomically */
        Compare = OldHeader.Alignment;
        OldHeader.Alignment = InterlockedCompareExchange64((PLONGLONG)SListHead->Alignment,
                                                           NewHeader.Alignment,
                                                           Compare);
    }
    while (OldHeader.Alignment != Compare);

    return OldHeader.Next.Next;
}

PSLIST_ENTRY
NTAPI
RtlInterlockedFlushSList(
    _Inout_ PSLIST_HEADER SListHead)
{
    SLIST_HEADER OldHeader, NewHeader;
    ULONGLONG Compare;

    /* Read the header */
    OldHeader = *SListHead;

    do
    {
        /* Check for empty list */
        if (OldHeader.Next.Next == NULL)
        {
            return NULL;
        }

        /* Create a new header (keep the sequence number) */
        NewHeader = OldHeader;
        NewHeader.Next.Next = NULL;
        NewHeader.Depth = 0;

        /* Try to exchange atomically */
        Compare = OldHeader.Alignment;
        OldHeader.Alignment = InterlockedCompareExchange64((PLONGLONG)&SListHead->Alignment,
                                                           NewHeader.Alignment,
                                                           Compare);
    }
    while (OldHeader.Alignment != Compare);

    /* Return the old first entry */
    return OldHeader.Next.Next;

}

#endif /* _M_ARM64 */

#ifdef _MSC_VER
#pragma comment(linker, "/alternatename:ExpInterlockedPopEntrySList=RtlInterlockedPopEntrySList")
#pragma comment(linker, "/alternatename:ExpInterlockedPushEntrySList=RtlInterlockedPushEntrySList")
#pragma comment(linker, "/alternatename:ExpInterlockedFlushSList=RtlInterlockedFlushSList")
#else
#pragma redefine_extname RtlInterlockedPopEntrySList ExpInterlockedPopEntrySList
#pragma redefine_extname RtlInterlockedPushEntrySList ExpInterlockedPushEntrySList
#pragma redefine_extname RtlInterlockedFlushSList ExpInterlockedFlushSList
#endif

#endif
