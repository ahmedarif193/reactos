/*
 * PROJECT:     ReactOS Kernel&Driver SDK
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Hardware Resources Arbiter Library
 * COPYRIGHT:   Copyright 2020 Vadim Galyant <vgal@rambler.ru>
 * COPYRIGHT:   Copyright 2025 Ahmed ARIF <arif.ing@outlook.com>
 *
 * COMPATIBILITY:
 *   - Matches the function pointer signatures in the provided header.
 *   - Uses RTL range-list APIs for committed and staged allocations.
 *   - Expects typical ARBITER_LIST_ENTRY structure present in ReactOS tree:
 *       ListEntry (LIST_ENTRY)
 *       AlternativeCount (ULONG)
 *       Alternatives (PARBITER_ALTERNATIVE)
 *       Assignment (CM_PARTIAL_RESOURCE_DESCRIPTOR)
 *       Result (ARBITER_RESULT)
 *       PhysicalDeviceObject (PDEVICE_OBJECT)  // optional but commonly present
 *       Flags, RequestSource, etc.             // ignored here if not needed
 *
 *   If your ARBITER_LIST_ENTRY field names differ, adjust the access macros in
 *   the "LIST ENTRY ACCESSORS" section below (one place) rather than the engine.
 */

#include <ntifs.h>
#include <ndk/rtlfuncs.h>
#include <ndk/rtltypes.h>
#include "arbiter.h"

#define NDEBUG
#include <debug.h>
#include <limits.h>

/* ============================================================================================
   CONFIGURATION & POLICY SWITCHES
   ========================================================================================== */

#ifndef ARB_ALIGN
#define ARB_ALIGN(_addr, _align) \
    ( ((_align) <= 1) ? (_addr) : (((_addr) + ((_align) - 1ULL)) & ~((ULONGLONG)((_align) - 1ULL))) )
#endif

/* If TRUE, we tolerate overlaps from the same owner during staging (rarely needed). */
#ifndef ARB_ALLOW_SAME_OWNER_OVERLAP
#define ARB_ALLOW_SAME_OWNER_OVERLAP 0
#endif

/* Depth limit for any future backtracking (current implementation is greedy). */
#ifndef ARB_MAX_BACKTRACK_DEPTH
#define ARB_MAX_BACKTRACK_DEPTH 64
#endif

/* Attributes for ranges we add; you may OR flags for legacy decode, etc. */
#ifndef ARB_DEFAULT_RANGE_ATTRS
#define ARB_DEFAULT_RANGE_ATTRS 0
#endif

/* ============================================================================================
   RTL RANGE ITERATOR
   Use the SDK-provided definition from <ndk/rtltypes.h>.
   ========================================================================================== */

/* ============================================================================================
   LIST ENTRY ACCESSORS (adjust here if your fields differ)
   ========================================================================================== */

#ifndef ARB_GET_LIST_ENTRY
/* We assume the standard ReactOS fields below. Change in one place if your names differ. */
#define ARB_GET_LIST_ENTRY(_le) \
    CONTAINING_RECORD((_le), ARBITER_LIST_ENTRY, ListEntry)

#define ARB_ENTRY_ALTERNATIVE_COUNT(_e)   ((_e)->AlternativeCount)
#define ARB_ENTRY_ALTERNATIVES(_e)        ((_e)->Alternatives) /* PIO_RESOURCE_DESCRIPTOR */
#define ARB_ENTRY_ASSIGNMENT(_e)          ((_e)->Assignment)   /* PCM_PARTIAL_RESOURCE_DESCRIPTOR */
#define ARB_ENTRY_RESULT(_e)              ((_e)->Result)
#define ARB_ENTRY_OWNER(_e)               ((PVOID)(_e)) /* Use entry itself as owner tag */
#endif

/* ============================================================================================
   INTERNAL HELPERS
   ========================================================================================== */

/* Inline + TU-local helpers */
static __inline
VOID
ArbLock(_In_ PARBITER_INSTANCE Arb)
{
    ASSERT(Arb && Arb->MutexEvent);
    KeWaitForSingleObject(Arb->MutexEvent, Executive, KernelMode, FALSE, NULL);
}

static __inline
VOID
ArbUnlock(_In_ PARBITER_INSTANCE Arb)
{
    ASSERT(Arb && Arb->MutexEvent);
    KeSetEvent(Arb->MutexEvent, IO_NO_INCREMENT, FALSE);
}

CODE_SEG("PAGE")
static
BOOLEAN
ArbSafeAddRange(
    _Inout_ PRTL_RANGE_LIST List,
    _In_ ULONGLONG Start,
    _In_ ULONG Length,
    _In_ ULONG Attributes,
    _In_ ULONG Flags,
    _In_opt_ PVOID UserData,
    _In_opt_ PVOID Owner)
{
    PAGED_CODE();

    if (Length == 0) return FALSE;

    /* Overflow-safe end computation: End = Start + Length - 1 */
    ULONGLONG End = Start + (ULONGLONG)Length - 1ULL;
    if (End < Start) return FALSE; /* overflow */

    return NT_SUCCESS(RtlAddRange(List, Start, End, Attributes, Flags, UserData, Owner));
}

CODE_SEG("PAGE")
static
NTSTATUS
ArbRangeFromIoRequirement(
    _In_ PARBITER_INSTANCE Arbiter,
    _In_ PIO_RESOURCE_DESCRIPTOR Io,
    _Out_ PULONGLONG Min,
    _Out_ PULONGLONG Max,
    _Out_ PULONG Length,
    _Out_ PULONG Alignment)
{
    PAGED_CODE();

    if (!Arbiter || !Arbiter->UnpackRequirement || !Io || !Min || !Max || !Length || !Alignment)
        return STATUS_INVALID_PARAMETER;

    return Arbiter->UnpackRequirement(Io, Min, Max, Length, Alignment);
}

CODE_SEG("PAGE")
static
NTSTATUS
ArbPackFromIoRequirement(
    _In_ PARBITER_INSTANCE Arbiter,
    _In_ PIO_RESOURCE_DESCRIPTOR Io,
    _In_ ULONGLONG Start,
    _Out_ PCM_PARTIAL_RESOURCE_DESCRIPTOR Cm)
{
    PAGED_CODE();

    if (!Arbiter || !Arbiter->PackResource || !Io || !Cm)
        return STATUS_INVALID_PARAMETER;

    RtlZeroMemory(Cm, sizeof(*Cm));
    return Arbiter->PackResource(Io, Start, Cm);
}

CODE_SEG("PAGE")
static
NTSTATUS
ArbUnpackCommitted(
    _In_ PARBITER_INSTANCE Arbiter,
    _In_ PCM_PARTIAL_RESOURCE_DESCRIPTOR Cm,
    _Out_ PULONGLONG Start,
    _Out_ PULONG Length)
{
    PAGED_CODE();

    if (!Arbiter || !Arbiter->UnpackResource || !Cm || !Start || !Length)
        return STATUS_INVALID_PARAMETER;

    return Arbiter->UnpackResource(Cm, Start, Length);
}

/* If the instance provides ScoreRequirement, use it; else score by heuristics. */
CODE_SEG("PAGE")
static
INT32
ArbScoreIoRequirement(
    _In_ PARBITER_INSTANCE Arbiter,
    _In_ PIO_RESOURCE_DESCRIPTOR Io,
    _In_ ULONGLONG Min,
    _In_ ULONGLONG Max,
    _In_ ULONG Length,
    _In_ ULONG Alignment)
{
    PAGED_CODE();

    if (Arbiter->ScoreRequirement)
    {
        INT32 s = Arbiter->ScoreRequirement(Io);
        return s;
    }

    /* Heuristic: prefer tighter windows and smaller alignment. */
    ULONGLONG span = (Max >= Min) ? (Max - Min + 1ULL) : 0ULL;
    LONGLONG tightness = (span > 0) ? ((LONGLONG)span - (LONGLONG)Length) : LLONG_MAX;
    LONGLONG align = Alignment ? (LONGLONG)Alignment : 1;

    LONGLONG score = 0;
    if (tightness > 0) score -= (tightness / 4096);  /* penalize huge windows */
    if (align > 1)     score -= (align / 16);        /* penalize coarse alignment */

    if (score < INT_MIN) score = INT_MIN;
    if (score > INT_MAX) score = INT_MAX;
    return (INT32)score;
}

/* Find aligned free window [start..start+len-1] inside [min..max] against Occupied. */
CODE_SEG("PAGE")
static
NTSTATUS
ArbFindFreeWindow(
    _In_ PRTL_RANGE_LIST Occupied,
    _In_ ULONGLONG Min,
    _In_ ULONGLONG Max,
    _In_ ULONG Length,
    _In_ ULONG Alignment,
    _Out_ PULONGLONG OutStart)
{
    PAGED_CODE();

    if (!Occupied || !OutStart) return STATUS_INVALID_PARAMETER;
    if (Length == 0) return STATUS_INVALID_PARAMETER;
    if (Min > Max)    return STATUS_CONFLICTING_ADDRESSES;

    ULONGLONG cursor = ARB_ALIGN(Min, Alignment);
    if (cursor > Max) return STATUS_CONFLICTING_ADDRESSES;

    RTL_RANGE_LIST_ITERATOR it;
    PRTL_RANGE r = NULL;
    NTSTATUS st = RtlGetFirstRange(Occupied, &it, &r);

    /* If no ranges, the whole window is free. */
    if (st == STATUS_NO_MORE_ENTRIES)
    {
        if (cursor + Length - 1ULL <= Max) { *OutStart = cursor; return STATUS_SUCCESS; }
        return STATUS_CONFLICTING_ADDRESSES;
    }

    if (!NT_SUCCESS(st) && st != STATUS_NO_MORE_ENTRIES) return st;

    while (cursor + Length - 1ULL <= Max)
    {
        BOOLEAN overlapped = FALSE;

        RTL_RANGE_LIST_ITERATOR it2 = it;
        PRTL_RANGE rr = r;
        NTSTATUS st2 = (r ? STATUS_SUCCESS : STATUS_NO_MORE_ENTRIES);

        while (st2 != STATUS_NO_MORE_ENTRIES)
        {
            ULONGLONG rs = rr->Start;
            ULONGLONG re = rr->End;

            if (re < cursor)
            {
                st2 = RtlGetNextRange(&it2, &rr, TRUE);
                continue;
            }

            ULONGLONG ce = cursor + Length - 1ULL;
            if (ce < rs)
            {
                /* candidate lies before next occupied range */
                break;
            }

            /* overlap? advance cursor beyond this occupied range */
            if (!(ce < rs || cursor > re))
            {
                overlapped = TRUE;
                cursor = ARB_ALIGN(re + 1ULL, Alignment);
                break;
            }

            st2 = RtlGetNextRange(&it2, &rr, TRUE);
        }

        if (!overlapped)
        {
            *OutStart = cursor;
            return STATUS_SUCCESS;
        }
    }

    return STATUS_CONFLICTING_ADDRESSES;
}

/* Stage (PossibleAllocation) add for an owner. */
CODE_SEG("PAGE")
static
NTSTATUS
ArbStageAdd(
    _Inout_ PARBITER_INSTANCE Arbiter,
    _In_ ULONGLONG Start,
    _In_ ULONG Length,
    _In_opt_ PVOID Owner /* usually the ARBITER_LIST_ENTRY pointer */)
{
    PAGED_CODE();

    if (!Arbiter || !Arbiter->PossibleAllocation) return STATUS_INVALID_PARAMETER;
    if (Length == 0) return STATUS_INVALID_PARAMETER;

    ULONGLONG End = Start + (ULONGLONG)Length - 1ULL;
    if (End < Start) return STATUS_INTEGER_OVERFLOW;

    return RtlAddRange(Arbiter->PossibleAllocation,
                       Start,
                       End,
                       ARB_DEFAULT_RANGE_ATTRS,
                       0 /*Flags*/,
                       Owner,
                       Arbiter);
}

/* Drop staged ranges belonging to Owner (used if we ever backtrack a single entry). */
CODE_SEG("PAGE")
static
VOID
ArbStageDropOwner(
    _Inout_ PARBITER_INSTANCE Arbiter,
    _In_ PVOID Owner)
{
    PAGED_CODE();

    if (!Arbiter || !Arbiter->PossibleAllocation) return;
    (void)RtlDeleteOwnersRanges(Arbiter->PossibleAllocation, Owner);
}

/* ============================================================================================
   CORE ALLOCATION LOGIC (batch style)
   ========================================================================================== */

CODE_SEG("PAGE")
static
NTSTATUS
ArbAllocateForEntryGreedy(
    _Inout_ PARBITER_INSTANCE Arbiter,
    _Inout_ PARBITER_LIST_ENTRY Entry)
{
    PAGED_CODE();

    ULONG altCount = ARB_ENTRY_ALTERNATIVE_COUNT(Entry);
    PIO_RESOURCE_DESCRIPTOR alts = ARB_ENTRY_ALTERNATIVES(Entry);
    if (!altCount || !alts) return STATUS_INVALID_PARAMETER;

    /* Rank alternatives by score (stable selection without extra allocation). */
    /* We’ll try the best first; if it fails, we iterate remaining. */
    INT32* scores = (INT32*)ExAllocatePoolWithTag(PagedPool, sizeof(INT32) * altCount, TAG_ARB_ALLOCATION);
    if (!scores) return STATUS_INSUFFICIENT_RESOURCES;

    for (ULONG i = 0; i < altCount; ++i)
    {
        PIO_RESOURCE_DESCRIPTOR io = &alts[i];
        ULONGLONG min, max; ULONG len, align;
        if (!io || !NT_SUCCESS(ArbRangeFromIoRequirement(Arbiter, io, &min, &max, &len, &align)))
        {
            scores[i] = INT_MIN;
            continue;
        }
        scores[i] = ArbScoreIoRequirement(Arbiter, io, min, max, len, align);
    }

    /* Greedy pass: pick the best remaining alternative that fits. */
    NTSTATUS finalStatus = STATUS_CONFLICTING_ADDRESSES;

    for (ULONG probe = 0; probe < altCount; ++probe)
    {
        /* Select next best index */
        LONG bestIdx = -1;
        INT32 bestScore = INT_MIN;
        for (ULONG i = 0; i < altCount; ++i)
        {
            if (scores[i] > bestScore)
            {
                bestScore = scores[i];
                bestIdx = (LONG)i;
            }
        }
        if (bestIdx < 0) break; /* none left */

        /* Mark this alternative as consumed (prevent retry). */
        scores[bestIdx] = INT_MIN;

        PIO_RESOURCE_DESCRIPTOR io = &alts[bestIdx];
        if (!io) continue;

        ULONGLONG min, max; ULONG len, align;
        NTSTATUS st = ArbRangeFromIoRequirement(Arbiter, io, &min, &max, &len, &align);
        if (!NT_SUCCESS(st)) continue;

        if (len == 0 || min > max) continue;

        BOOLEAN placed = FALSE;

        /* 1) Try preferred ordering windows first. */
        if (Arbiter->OrderingList.Count)
        {
            for (USHORT oi = 0; oi < Arbiter->OrderingList.Count && !placed; ++oi)
            {
                ULONGLONG oMin = Arbiter->OrderingList.Orderings[oi].Start;
                ULONGLONG oMax = Arbiter->OrderingList.Orderings[oi].End;

                ULONGLONG tryMin = (oMin > min) ? oMin : min;
                ULONGLONG tryMax = (oMax < max) ? oMax : max;
                if (tryMin > tryMax) continue;

                ULONGLONG start;
                st = ArbFindFreeWindow(Arbiter->PossibleAllocation, tryMin, tryMax, len, align, &start);
                if (NT_SUCCESS(st))
                {
                    st = ArbStageAdd(Arbiter, start, len, ARB_ENTRY_OWNER(Entry));
                    if (!NT_SUCCESS(st)) break;

                    BOOLEAN _arbOwnsAssignment = FALSE;
                    if (!ARB_ENTRY_ASSIGNMENT(Entry))
                    {
                        PCM_PARTIAL_RESOURCE_DESCRIPTOR desc =
                            (PCM_PARTIAL_RESOURCE_DESCRIPTOR)ExAllocatePoolWithTag(PagedPool,
                                                                                   sizeof(CM_PARTIAL_RESOURCE_DESCRIPTOR),
                                                                                   TAG_ARB_ALLOCATION);
                        if (!desc)
                        {
                            ArbStageDropOwner(Arbiter, ARB_ENTRY_OWNER(Entry));
                            break;
                        }
                        ARB_ENTRY_ASSIGNMENT(Entry) = desc;
                        _arbOwnsAssignment = TRUE;
                    }
                    st = ArbPackFromIoRequirement(Arbiter, io, start, ARB_ENTRY_ASSIGNMENT(Entry));
                    if (!NT_SUCCESS(st))
                    {
                        ArbStageDropOwner(Arbiter, ARB_ENTRY_OWNER(Entry));
                        if (_arbOwnsAssignment)
                        {
                            ExFreePoolWithTag(ARB_ENTRY_ASSIGNMENT(Entry), TAG_ARB_ALLOCATION);
                            ARB_ENTRY_ASSIGNMENT(Entry) = NULL;
                        }
                        break;
                    }

                    Entry->SelectedAlternative = io;
                    ARB_ENTRY_RESULT(Entry) = ArbiterResultSuccess;
                    placed = TRUE;
                    finalStatus = STATUS_SUCCESS;
                }
            }
            if (!NT_SUCCESS(finalStatus) && placed == FALSE)
            {
                /* fall through to try full window */
            }
        }

        /* 2) Try anywhere in the [min..max] if ordering windows didn’t work. */
        if (!placed)
        {
            ULONGLONG start;
            st = ArbFindFreeWindow(Arbiter->PossibleAllocation, min, max, len, align, &start);
            if (NT_SUCCESS(st))
            {
                st = ArbStageAdd(Arbiter, start, len, ARB_ENTRY_OWNER(Entry));
                if (!NT_SUCCESS(st)) continue;

                BOOLEAN _arbOwnsAssignment = FALSE;
                if (!ARB_ENTRY_ASSIGNMENT(Entry))
                {
                    PCM_PARTIAL_RESOURCE_DESCRIPTOR desc =
                        (PCM_PARTIAL_RESOURCE_DESCRIPTOR)ExAllocatePoolWithTag(PagedPool,
                                                                               sizeof(CM_PARTIAL_RESOURCE_DESCRIPTOR),
                                                                               TAG_ARB_ALLOCATION);
                    if (!desc)
                    {
                        ArbStageDropOwner(Arbiter, ARB_ENTRY_OWNER(Entry));
                        continue;
                    }
                    ARB_ENTRY_ASSIGNMENT(Entry) = desc;
                    _arbOwnsAssignment = TRUE;
                }
                st = ArbPackFromIoRequirement(Arbiter, io, start, ARB_ENTRY_ASSIGNMENT(Entry));
                if (!NT_SUCCESS(st))
                {
                    ArbStageDropOwner(Arbiter, ARB_ENTRY_OWNER(Entry));
                    if (_arbOwnsAssignment)
                    {
                        ExFreePoolWithTag(ARB_ENTRY_ASSIGNMENT(Entry), TAG_ARB_ALLOCATION);
                        ARB_ENTRY_ASSIGNMENT(Entry) = NULL;
                    }
                    continue;
                }

                Entry->SelectedAlternative = io;
                ARB_ENTRY_RESULT(Entry) = ArbiterResultSuccess;
                finalStatus = STATUS_SUCCESS;
                break;
            }
        }
    }

    ExFreePoolWithTag(scores, TAG_ARB_ALLOCATION);

    if (!NT_SUCCESS(finalStatus))
    {
        ARB_ENTRY_RESULT(Entry) = ArbiterResultExternalConflict;
    }
    return finalStatus;
}

/* ============================================================================================
   PUBLIC API
   ========================================================================================== */

CODE_SEG("PAGE")
NTSTATUS
NTAPI
ArbTestAllocation(
    _In_ PARBITER_INSTANCE Arbiter,
    _In_ PLIST_ENTRY ArbitrationList)
{
    PAGED_CODE();

    if (!Arbiter || !Arbiter->PossibleAllocation || !Arbiter->Allocation || !ArbitrationList)
        return STATUS_INVALID_PARAMETER;

    ArbLock(Arbiter);

    /* Reset staged to current committed state. */
    RtlInitializeRangeList(Arbiter->PossibleAllocation);
    NTSTATUS st = RtlCopyRangeList(Arbiter->PossibleAllocation, Arbiter->Allocation);
    if (!NT_SUCCESS(st)) { ArbUnlock(Arbiter); return st; }

    /* Iterate entries in the list and try to place each. */
    for (PLIST_ENTRY le = ArbitrationList->Flink; le != ArbitrationList; le = le->Flink)
    {
        PARBITER_LIST_ENTRY entry = ARB_GET_LIST_ENTRY(le);

        /* Optional preprocess hook uses allocation-state overlay; we pass the entry for context. */
        if (Arbiter->PreprocessEntry)
        {
            ARBITER_ALLOCATION_STATE dummy = {0};
            dummy.Entry = entry;
            (void)Arbiter->PreprocessEntry(Arbiter, &dummy);
        }

        st = ArbAllocateForEntryGreedy(Arbiter, entry);
        if (!NT_SUCCESS(st))
        {
            /* Greedy allocator failed this batch; drop any staged ranges for this entry and fail. */
            ArbStageDropOwner(Arbiter, ARB_ENTRY_OWNER(entry));
            ArbUnlock(Arbiter);
            return st;
        }
    }

    ArbUnlock(Arbiter);
    return STATUS_SUCCESS;
}

CODE_SEG("PAGE")
NTSTATUS
NTAPI
ArbRetestAllocation(
    _In_ PARBITER_INSTANCE Arbiter,
    _In_ PLIST_ENTRY ArbitrationList)
{
    PAGED_CODE();

    /* Re-run test from committed state. */
    return ArbTestAllocation(Arbiter, ArbitrationList);
}

CODE_SEG("PAGE")
NTSTATUS
NTAPI
ArbCommitAllocation(
    _In_ PARBITER_INSTANCE Arbiter)
{
    PAGED_CODE();

    if (!Arbiter || !Arbiter->PossibleAllocation || !Arbiter->Allocation)
        return STATUS_INVALID_PARAMETER;

    ArbLock(Arbiter);
    RtlInitializeRangeList(Arbiter->Allocation);
    NTSTATUS st = RtlCopyRangeList(Arbiter->Allocation, Arbiter->PossibleAllocation);
    ArbUnlock(Arbiter);
    return st;
}

CODE_SEG("PAGE")
NTSTATUS
NTAPI
ArbRollbackAllocation(
    _In_ PARBITER_INSTANCE Arbiter)
{
    PAGED_CODE();

    if (!Arbiter || !Arbiter->PossibleAllocation)
        return STATUS_INVALID_PARAMETER;

    ArbLock(Arbiter);
    RtlInitializeRangeList(Arbiter->PossibleAllocation);
    ArbUnlock(Arbiter);
    return STATUS_SUCCESS;
}

CODE_SEG("PAGE")
NTSTATUS
NTAPI
ArbAddReserved(
    _In_ PARBITER_INSTANCE Arbiter)
{
    PAGED_CODE();

    if (!Arbiter || !Arbiter->Allocation)
        return STATUS_INVALID_PARAMETER;

    ArbLock(Arbiter);

    switch (Arbiter->ResourceType)
    {
        case CmResourceTypePort:
            /* Reserve PCI Type-1 Config Space ports CF8h..CFFh (classic). */
            (void)RtlAddRange(Arbiter->Allocation,
                              0x0000000000000CF8ULL,
                              0x0000000000000CFFULL,
                              ARB_DEFAULT_RANGE_ATTRS,
                              RTL_RANGE_LIST_ADD_IF_CONFLICT,
                              NULL,
                              Arbiter);
            break;

        case CmResourceTypeMemory:
            /* Optional: reserve legacy VGA aperture (A0000h-BFFFFh). Uncomment as needed. */
            /* (void)RtlAddRange(Arbiter->Allocation, 0x00000000000A0000ULL, 0x00000000000BFFFFULL,
                               ARB_DEFAULT_RANGE_ATTRS, RTL_RANGE_LIST_ADD_IF_CONFLICT, NULL, Arbiter); */
            break;

        default:
            break;
    }

    ArbUnlock(Arbiter);
    return STATUS_SUCCESS;
}

CODE_SEG("PAGE")
NTSTATUS
NTAPI
ArbPreprocessEntry(
    _In_ PARBITER_INSTANCE Arbiter,
    _Inout_ PARBITER_ALLOCATION_STATE ArbState)
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(Arbiter);
    UNREFERENCED_PARAMETER(ArbState);
    /* Extension point: normalize legacy flags in ArbState->Entry if needed. */
    return STATUS_SUCCESS;
}

/* Legacy callback surface retained for compatibility—engine is batch-oriented. */
CODE_SEG("PAGE")
NTSTATUS
NTAPI
ArbAllocateEntry(
    _In_ PARBITER_INSTANCE Arbiter,
    _Inout_ PARBITER_ALLOCATION_STATE ArbState)
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(Arbiter);
    UNREFERENCED_PARAMETER(ArbState);
    return STATUS_NOT_SUPPORTED;
}

CODE_SEG("PAGE")
BOOLEAN
NTAPI
ArbGetNextAllocationRange(
    _In_ PARBITER_INSTANCE Arbiter,
    _Inout_ PARBITER_ALLOCATION_STATE ArbState)
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(Arbiter);
    UNREFERENCED_PARAMETER(ArbState);
    return FALSE;
}

CODE_SEG("PAGE")
BOOLEAN
NTAPI
ArbFindSuitableRange(
    _In_ PARBITER_INSTANCE Arbiter,
    _Inout_ PARBITER_ALLOCATION_STATE ArbState)
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(Arbiter);
    UNREFERENCED_PARAMETER(ArbState);
    return FALSE;
}

CODE_SEG("PAGE")
VOID
NTAPI
ArbAddAllocation(
    _In_ PARBITER_INSTANCE Arbiter,
    _Inout_ PARBITER_ALLOCATION_STATE ArbState)
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(Arbiter);
    UNREFERENCED_PARAMETER(ArbState);
}

CODE_SEG("PAGE")
VOID
NTAPI
ArbBacktrackAllocation(
    _In_ PARBITER_INSTANCE Arbiter,
    _Inout_ PARBITER_ALLOCATION_STATE ArbState)
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(Arbiter);
    UNREFERENCED_PARAMETER(ArbState);
}

/* With no conflict context in the prototype, there’s nothing actionable to override. */
CODE_SEG("PAGE")
NTSTATUS
NTAPI
ArbOverrideConflict(
    _In_ PARBITER_INSTANCE Arbiter)
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(Arbiter);
    return STATUS_NOT_SUPPORTED;
}

/* Boot: adopt any pre-assigned descriptors into the committed set. */
CODE_SEG("PAGE")
NTSTATUS
NTAPI
ArbBootAllocation(
    _In_ PARBITER_INSTANCE Arbiter,
    _In_ PLIST_ENTRY ArbitrationList)
{
    PAGED_CODE();

    if (!Arbiter || !Arbiter->Allocation || !ArbitrationList) return STATUS_INVALID_PARAMETER;

    ArbLock(Arbiter);

    for (PLIST_ENTRY le = ArbitrationList->Flink; le != ArbitrationList; le = le->Flink)
    {
        PARBITER_LIST_ENTRY entry = ARB_GET_LIST_ENTRY(le);

        ULONGLONG start;
        ULONG len;
        if (NT_SUCCESS(ArbUnpackCommitted(Arbiter, ARB_ENTRY_ASSIGNMENT(entry), &start, &len)))
        {
            if (len)
            {
                (void)ArbSafeAddRange(Arbiter->Allocation,
                                      start,
                                      len,
                                      ARB_DEFAULT_RANGE_ATTRS,
                                      0,
                                      ARB_ENTRY_OWNER(entry),
                                      Arbiter);
            }
        }
    }

    ArbUnlock(Arbiter);
    return STATUS_SUCCESS;
}

/* QueryArbitrate: prototype marked FIXME in header; cannot implement usefully. */
CODE_SEG("PAGE")
NTSTATUS
NTAPI
ArbQueryArbitrate(
    _In_ PARBITER_INSTANCE Arbiter)
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(Arbiter);
    return STATUS_NOT_SUPPORTED;
}

/* QueryConflict: prototype marked FIXME. Without an input requirement, we cannot answer. */
CODE_SEG("PAGE")
NTSTATUS
NTAPI
ArbQueryConflict(
    _In_ PARBITER_INSTANCE Arbiter)
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(Arbiter);
    return STATUS_NOT_SUPPORTED;
}

/* Start: ensure reserved windows exist. */
CODE_SEG("PAGE")
NTSTATUS
NTAPI
ArbStartArbiter(
    _In_ PARBITER_INSTANCE Arbiter)
{
    PAGED_CODE();

    if (!Arbiter || !Arbiter->Allocation || !Arbiter->PossibleAllocation)
        return STATUS_INVALID_PARAMETER;

    return ArbAddReserved(Arbiter);
}

/* ============================================================================================
   ORDERING LIST UTILITIES
   ========================================================================================== */

CODE_SEG("PAGE")
NTSTATUS
NTAPI
ArbAddOrdering(
    _Out_ PARBITER_ORDERING_LIST OrderList,
    _In_ UINT64 MinimumAddress,
    _In_ UINT64 MaximumAddress)
{
    PAGED_CODE();

    if (!OrderList) return STATUS_INVALID_PARAMETER;
    if (MaximumAddress < MinimumAddress) return STATUS_INVALID_PARAMETER;

    if (OrderList->Count == OrderList->Maximum)
    {
        USHORT newMax = (OrderList->Maximum == 0) ? 8 : (OrderList->Maximum * 2);
        SIZE_T bytes = sizeof(ARBITER_ORDERING) * newMax;
        PARBITER_ORDERING newBuf = (PARBITER_ORDERING)ExAllocatePoolWithTag(PagedPool, bytes, TAG_ARB_RANGE);
        if (!newBuf) return STATUS_INSUFFICIENT_RESOURCES;

        if (OrderList->Orderings && OrderList->Count)
        {
            RtlCopyMemory(newBuf, OrderList->Orderings, sizeof(ARBITER_ORDERING) * OrderList->Count);
            ExFreePoolWithTag(OrderList->Orderings, TAG_ARB_RANGE);
        }
        OrderList->Orderings = newBuf;
        OrderList->Maximum = newMax;
    }

    OrderList->Orderings[OrderList->Count].Start = MinimumAddress;
    OrderList->Orderings[OrderList->Count].End   = MaximumAddress;
    OrderList->Count++;
    return STATUS_SUCCESS;
}

CODE_SEG("PAGE")
NTSTATUS
NTAPI
ArbPruneOrdering(
    _Out_ PARBITER_ORDERING_LIST OrderingList,
    _In_ UINT64 MinimumAddress,
    _In_ UINT64 MaximumAddress)
{
    PAGED_CODE();

    if (!OrderingList) return STATUS_INVALID_PARAMETER;
    if (OrderingList->Count == 0) return STATUS_SUCCESS;

    USHORT write = 0;
    for (USHORT read = 0; read < OrderingList->Count; ++read)
    {
        ARBITER_ORDERING o = OrderingList->Orderings[read];

        if (o.End < MinimumAddress || o.Start > MaximumAddress)
            continue;

        if (o.Start < MinimumAddress) o.Start = MinimumAddress;
        if (o.End   > MaximumAddress) o.End   = MaximumAddress;

        OrderingList->Orderings[write++] = o;
    }

    OrderingList->Count = write;
    return STATUS_SUCCESS;
}

CODE_SEG("PAGE")
NTSTATUS
NTAPI
ArbInitializeOrderingList(
    _Out_ PARBITER_ORDERING_LIST OrderList)
{
    PAGED_CODE();

    if (!OrderList) return STATUS_INVALID_PARAMETER;
    OrderList->Count    = 0;
    OrderList->Maximum  = 0;
    OrderList->Orderings = NULL;
    return STATUS_SUCCESS;
}

CODE_SEG("PAGE")
VOID
NTAPI
ArbFreeOrderingList(
    _Out_ PARBITER_ORDERING_LIST OrderList)
{
    PAGED_CODE();

    if (!OrderList) return;

    if (OrderList->Orderings)
    {
        ExFreePoolWithTag(OrderList->Orderings, TAG_ARB_RANGE);
        OrderList->Orderings = NULL;
    }
    OrderList->Count = 0;
    OrderList->Maximum = 0;
}

/* Build preferred and reserved ordering from a translator. Your header’s translator signature
 * (OutIoDescriptor, IoDescriptor) doesn’t carry a name, so we can only translate each alternative’s
 * descriptor. If you want ACPI/firmware-defined named orderings, wire them externally and call
 * ArbAddOrdering/ArbPruneOrdering before allocations.
 */
CODE_SEG("PAGE")
NTSTATUS
NTAPI
ArbBuildAssignmentOrdering(
    _Inout_ PARBITER_INSTANCE ArbInstance,
    _In_ PCWSTR OrderName,                 /* not used with provided translator signature */
    _In_ PCWSTR ReservedOrderName,         /* not used with provided translator signature */
    _In_ PARB_TRANSLATE_ORDERING TranslateOrderingFunction)
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(OrderName);
    UNREFERENCED_PARAMETER(ReservedOrderName);

    NTSTATUS st;

    st = ArbInitializeOrderingList(&ArbInstance->OrderingList);
    if (!NT_SUCCESS(st)) return st;

    st = ArbInitializeOrderingList(&ArbInstance->ReservedList);
    if (!NT_SUCCESS(st)) return st;

    /* Optional: derive order windows from a canonical IO requirement via translator. */
    if (TranslateOrderingFunction)
    {
        /* We do a conservative pass: provide a generic descriptor (all 0s) per resource type,
           ask the translator to emit a normalized descriptor, then convert that into an ordering
           window if it constrains [min..max].  You can replace this with bus/ACPI-fed windows. */

        IO_RESOURCE_DESCRIPTOR in = {0}, out = {0};
        in.Type = (UCHAR)ArbInstance->ResourceType;

        if (NT_SUCCESS(TranslateOrderingFunction(&out, &in)))
        {
            ULONGLONG min, max; ULONG len, align;
            if (NT_SUCCESS(ArbRangeFromIoRequirement(ArbInstance, &out, &min, &max, &len, &align)))
            {
                /* Create a broad preferred ordering window if range is sane. */
                if (max >= min)
                {
                    (void)ArbAddOrdering(&ArbInstance->OrderingList, min, max);
                }
                /* If translator signaled “reserved” via Flags/ShareDisposition, add to ReservedList as appropriate. */
                /* (Bus-specific policy hook; left as extension point.) */
            }
        }
    }

    return STATUS_SUCCESS;
}

/* ============================================================================================
   INSTANCE INITIALIZATION
   ========================================================================================== */

CODE_SEG("PAGE")
NTSTATUS
NTAPI
ArbInitializeArbiterInstance(
    _Inout_ PARBITER_INSTANCE Arbiter,
    _In_ PDEVICE_OBJECT BusDeviceObject,
    _In_ CM_RESOURCE_TYPE ResourceType,
    _In_ PCWSTR ArbiterName,
    _In_ PCWSTR OrderName,
    _In_ PARB_TRANSLATE_ORDERING TranslateOrderingFunction)
{
    NTSTATUS Status;

    PAGED_CODE();

    DPRINT("ArbInitializeArbiterInstance: '%S'\n", ArbiterName);

    ASSERT(Arbiter->UnpackRequirement != NULL);
    ASSERT(Arbiter->PackResource != NULL);
    ASSERT(Arbiter->UnpackResource != NULL);
    ASSERT(Arbiter->MutexEvent == NULL);
    ASSERT(Arbiter->Allocation == NULL);
    ASSERT(Arbiter->PossibleAllocation == NULL);
    ASSERT(Arbiter->AllocationStack == NULL);

    Arbiter->Signature = ARBITER_SIGNATURE;
    Arbiter->BusDeviceObject = BusDeviceObject;

    Arbiter->MutexEvent = ExAllocatePoolWithTag(NonPagedPool, sizeof(KEVENT), TAG_ARBITER);
    if (!Arbiter->MutexEvent)
    {
        DPRINT1("ArbInitializeArbiterInstance: STATUS_INSUFFICIENT_RESOURCES\n");
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    KeInitializeEvent(Arbiter->MutexEvent, SynchronizationEvent, TRUE);

    Arbiter->AllocationStack = ExAllocatePoolWithTag(PagedPool, PAGE_SIZE, TAG_ARB_ALLOCATION);
    if (!Arbiter->AllocationStack)
    {
        DPRINT1("ArbInitializeArbiterInstance: STATUS_INSUFFICIENT_RESOURCES\n");
        ExFreePoolWithTag(Arbiter->MutexEvent, TAG_ARBITER);
        Arbiter->MutexEvent = NULL;
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Arbiter->AllocationStackMaxSize = PAGE_SIZE;

    Arbiter->Allocation = ExAllocatePoolWithTag(PagedPool, sizeof(RTL_RANGE_LIST), TAG_ARB_RANGE);
    if (!Arbiter->Allocation)
    {
        DPRINT1("ArbInitializeArbiterInstance: STATUS_INSUFFICIENT_RESOURCES\n");
        ExFreePoolWithTag(Arbiter->AllocationStack, TAG_ARB_ALLOCATION);
        ExFreePoolWithTag(Arbiter->MutexEvent, TAG_ARBITER);
        Arbiter->AllocationStack = NULL;
        Arbiter->MutexEvent = NULL;
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Arbiter->PossibleAllocation = ExAllocatePoolWithTag(PagedPool, sizeof(RTL_RANGE_LIST), TAG_ARB_RANGE);
    if (!Arbiter->PossibleAllocation)
    {
        DPRINT1("ArbInitializeArbiterInstance: STATUS_INSUFFICIENT_RESOURCES\n");
        ExFreePoolWithTag(Arbiter->Allocation, TAG_ARB_RANGE);
        ExFreePoolWithTag(Arbiter->AllocationStack, TAG_ARB_ALLOCATION);
        ExFreePoolWithTag(Arbiter->MutexEvent, TAG_ARBITER);
        Arbiter->Allocation = NULL;
        Arbiter->AllocationStack = NULL;
        Arbiter->MutexEvent = NULL;
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlInitializeRangeList(Arbiter->Allocation);
    RtlInitializeRangeList(Arbiter->PossibleAllocation);

    Arbiter->Name = ArbiterName;
    Arbiter->ResourceType = ResourceType;
    Arbiter->TransactionInProgress = FALSE;

    /* Wire default callbacks if caller left them NULL */
    if (!Arbiter->TestAllocation)         Arbiter->TestAllocation = ArbTestAllocation;
    if (!Arbiter->RetestAllocation)       Arbiter->RetestAllocation = ArbRetestAllocation;
    if (!Arbiter->CommitAllocation)       Arbiter->CommitAllocation = ArbCommitAllocation;
    if (!Arbiter->RollbackAllocation)     Arbiter->RollbackAllocation = ArbRollbackAllocation;
    if (!Arbiter->BootAllocation)         Arbiter->BootAllocation = ArbBootAllocation;
    if (!Arbiter->QueryArbitrate)         Arbiter->QueryArbitrate = ArbQueryArbitrate;
    if (!Arbiter->QueryConflict)          Arbiter->QueryConflict = ArbQueryConflict;
    if (!Arbiter->AddReserved)            Arbiter->AddReserved = ArbAddReserved;
    if (!Arbiter->StartArbiter)           Arbiter->StartArbiter = ArbStartArbiter;
    if (!Arbiter->PreprocessEntry)        Arbiter->PreprocessEntry = ArbPreprocessEntry;
    if (!Arbiter->AllocateEntry)          Arbiter->AllocateEntry = ArbAllocateEntry;
    if (!Arbiter->GetNextAllocationRange) Arbiter->GetNextAllocationRange = ArbGetNextAllocationRange;
    if (!Arbiter->FindSuitableRange)      Arbiter->FindSuitableRange = ArbFindSuitableRange;
    if (!Arbiter->AddAllocation)          Arbiter->AddAllocation = ArbAddAllocation;
    if (!Arbiter->BacktrackAllocation)    Arbiter->BacktrackAllocation = ArbBacktrackAllocation;
    if (!Arbiter->OverrideConflict)       Arbiter->OverrideConflict = ArbOverrideConflict;

    Status = ArbBuildAssignmentOrdering(Arbiter, OrderName, OrderName, TranslateOrderingFunction);
    if (NT_SUCCESS(Status))
    {
        if (Arbiter->AddReserved)
        {
            (void)Arbiter->AddReserved(Arbiter);
        }
        return STATUS_SUCCESS;
    }

    DPRINT1("ArbInitializeArbiterInstance: Status %X\n", Status);
    return Status;
}
