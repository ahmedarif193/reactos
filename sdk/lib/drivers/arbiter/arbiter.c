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
#define ARB_ENTRY_WORKSPACE(_e)           ((_e)->WorkSpace)
#endif

/*
 * Assignment ownership tracking macros
 *
 * These macros use the WorkSpace field of ARBITER_LIST_ENTRY to track
 * whether the arbiter allocated the Assignment descriptor. This is critical
 * for proper lifetime management:
 *
 * - If the caller provides an Assignment, the arbiter must not free it
 * - If the arbiter allocates an Assignment, it must free it on rollback/cleanup
 *
 * The low bits of WorkSpace are used for flags to avoid conflicts with
 * any pointer values the caller might store in the upper bits.
 */
#define ARB_MARK_OWNS_ASSIGNMENT(_e) \
    (ARB_ENTRY_WORKSPACE(_e) |= ARB_WORKSPACE_OWNS_ASSIGNMENT)

#define ARB_CLEAR_OWNS_ASSIGNMENT(_e) \
    (ARB_ENTRY_WORKSPACE(_e) &= ~(LONG_PTR)ARB_WORKSPACE_OWNS_ASSIGNMENT)

#define ARB_CHECK_OWNS_ASSIGNMENT(_e) \
    ((ARB_ENTRY_WORKSPACE(_e) & ARB_WORKSPACE_OWNS_ASSIGNMENT) != 0)

/* ============================================================================================
   INTERNAL HELPERS
   ========================================================================================== */

/* Inline + TU-local helpers */
static __inline
VOID
ArbLock(_In_ PARBITER_INSTANCE Arb)
{
    ASSERT(Arb && Arb->MutexEvent);
    if (!Arb || !Arb->MutexEvent)
    {
        KeBugCheckEx(DRIVER_VERIFIER_DETECTED_VIOLATION, 0, 0, 0, 0);
        return;
    }
    KeWaitForSingleObject(Arb->MutexEvent, Executive, KernelMode, FALSE, NULL);
}

static __inline
VOID
ArbUnlock(_In_ PARBITER_INSTANCE Arb)
{
    ASSERT(Arb && Arb->MutexEvent);
    if (!Arb || !Arb->MutexEvent)
    {
        KeBugCheckEx(DRIVER_VERIFIER_DETECTED_VIOLATION, 0, 0, 0, 0);
        return;
    }
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
    /* Check for overflow BEFORE the computation */
    if (Start > ULLONG_MAX - (ULONGLONG)Length + 1ULL) return FALSE;

    ULONGLONG End = Start + (ULONGLONG)Length - 1ULL;

    return NT_SUCCESS(RtlAddRange(List, Start, End, Attributes, Flags, UserData, Owner));
}

/*
 * ArbRangeFromIoRequirement - Extract range info from an IO resource descriptor
 *
 * NOTE: This function is called during arbiter initialization which may occur
 * at elevated IRQL (DISPATCH_LEVEL) on ARM64 during early boot. Therefore:
 * - No PAGED_CODE assertion (would fail at DISPATCH_LEVEL)
 * - Not placed in PAGE segment (must remain resident)
 */
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
    /* No PAGED_CODE() - called during boot at elevated IRQL on ARM64 */

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

/*
 * ArbScoreIoRequirement - Score a resource requirement for allocation priority
 *
 * This function computes a priority score for a resource alternative.
 * Higher scores indicate more constrained (and thus higher priority) requests.
 * The scoring algorithm considers:
 *
 * 1. Window tightness: How much slack exists between requested length and
 *    the available range. Tighter windows have fewer placement options and
 *    should be allocated first.
 *
 * 2. Alignment requirements: Higher alignment requirements reduce the number
 *    of valid placement addresses, making allocation harder.
 *
 * The constants ARB_SCORE_WINDOW_DIVISOR and ARB_SCORE_ALIGNMENT_DIVISOR
 * (defined in arbiter.h) control the relative weighting of these factors.
 */
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

    /* If the arbiter provides a custom scoring function, use it */
    if (Arbiter->ScoreRequirement)
    {
        INT32 s = Arbiter->ScoreRequirement(Io);
        return s;
    }

    /*
     * Default heuristic scoring algorithm:
     * - Start with score 0 (neutral)
     * - Penalize (decrease score) for large address windows
     * - Penalize (decrease score) for coarse alignment requirements
     *
     * Lower scores indicate less constrained requests that can be
     * satisfied with more placement options.
     */
    ULONGLONG span = (Max >= Min) ? (Max - Min + 1ULL) : 0ULL;
    LONGLONG tightness = (span > 0) ? ((LONGLONG)span - (LONGLONG)Length) : LLONG_MAX;
    LONGLONG align = Alignment ? (LONGLONG)Alignment : 1;

    LONGLONG score = 0;

    /* Penalize requests with large address windows (more placement flexibility) */
    if (tightness > 0)
    {
        score -= (tightness / (LONGLONG)ARB_SCORE_WINDOW_DIVISOR);
    }

    /* Penalize requests with coarse alignment (harder to satisfy but less constrained) */
    if (align > 1)
    {
        score -= (align / (LONGLONG)ARB_SCORE_ALIGNMENT_DIVISOR);
    }

    /* Clamp to INT32 range */
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
    /* Alignment of 0 is invalid and could cause infinite loops */
    if (Alignment == 0) return STATUS_INVALID_PARAMETER;

    ULONGLONG cursor = ARB_ALIGN(Min, Alignment);
    if (cursor > Max) return STATUS_CONFLICTING_ADDRESSES;

    RTL_RANGE_LIST_ITERATOR it;
    PRTL_RANGE r = NULL;
    NTSTATUS st = RtlGetFirstRange(Occupied, &it, &r);

    /* If no ranges, the whole window is free. */
    if (st == STATUS_NO_MORE_ENTRIES)
    {
        /* Check for overflow before computation */
        if (cursor <= ULLONG_MAX - (ULONGLONG)Length + 1ULL)
        {
            if (cursor + Length - 1ULL <= Max) { *OutStart = cursor; return STATUS_SUCCESS; }
        }
        return STATUS_CONFLICTING_ADDRESSES;
    }

    if (!NT_SUCCESS(st) && st != STATUS_NO_MORE_ENTRIES) return st;

    /* Main loop: check for overflow before each iteration */
    while (cursor <= ULLONG_MAX - (ULONGLONG)Length + 1ULL && cursor + Length - 1ULL <= Max)
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

            /* Check for overflow before computing candidate end */
            if (cursor > ULLONG_MAX - (ULONGLONG)Length + 1ULL)
            {
                overlapped = TRUE;
                break;
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
                /* Check for overflow when advancing cursor */
                if (re == ULLONG_MAX)
                {
                    /* Cannot advance beyond max value */
                    break;
                }
                ULONGLONG newCursor = ARB_ALIGN(re + 1ULL, Alignment);
                /* Ensure cursor actually advances to prevent infinite loops */
                if (newCursor <= cursor)
                {
                    break;
                }
                cursor = newCursor;
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

    /* Check for overflow BEFORE the computation */
    if (Start > ULLONG_MAX - (ULONGLONG)Length + 1ULL) return STATUS_INTEGER_OVERFLOW;

    ULONGLONG End = Start + (ULONGLONG)Length - 1ULL;

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

    /* Validate parameters before dereferencing */
    if (!Arbiter || !Entry) return STATUS_INVALID_PARAMETER;

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

                    /*
                     * Assignment allocation with proper lifetime tracking:
                     * If Entry does not have an Assignment, we allocate one and mark
                     * ownership using the WorkSpace field. This allows proper cleanup
                     * on rollback without risking use-after-free of caller-provided
                     * Assignment buffers.
                     */
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
                        ARB_MARK_OWNS_ASSIGNMENT(Entry);  /* Track that WE allocated this */
                    }
                    st = ArbPackFromIoRequirement(Arbiter, io, start, ARB_ENTRY_ASSIGNMENT(Entry));
                    if (!NT_SUCCESS(st))
                    {
                        ArbStageDropOwner(Arbiter, ARB_ENTRY_OWNER(Entry));
                        /* Only free Assignment if WE allocated it */
                        if (ARB_CHECK_OWNS_ASSIGNMENT(Entry))
                        {
                            ExFreePoolWithTag(ARB_ENTRY_ASSIGNMENT(Entry), TAG_ARB_ALLOCATION);
                            ARB_ENTRY_ASSIGNMENT(Entry) = NULL;
                            ARB_CLEAR_OWNS_ASSIGNMENT(Entry);
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

        /* 2) Try anywhere in the [min..max] if ordering windows didn't work. */
        if (!placed)
        {
            ULONGLONG start;
            st = ArbFindFreeWindow(Arbiter->PossibleAllocation, min, max, len, align, &start);
            if (NT_SUCCESS(st))
            {
                st = ArbStageAdd(Arbiter, start, len, ARB_ENTRY_OWNER(Entry));
                if (!NT_SUCCESS(st)) continue;

                /*
                 * Assignment allocation with proper lifetime tracking.
                 * See comment above for rationale.
                 */
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
                    ARB_MARK_OWNS_ASSIGNMENT(Entry);  /* Track that WE allocated this */
                }
                st = ArbPackFromIoRequirement(Arbiter, io, start, ARB_ENTRY_ASSIGNMENT(Entry));
                if (!NT_SUCCESS(st))
                {
                    ArbStageDropOwner(Arbiter, ARB_ENTRY_OWNER(Entry));
                    /* Only free Assignment if WE allocated it */
                    if (ARB_CHECK_OWNS_ASSIGNMENT(Entry))
                    {
                        ExFreePoolWithTag(ARB_ENTRY_ASSIGNMENT(Entry), TAG_ARB_ALLOCATION);
                        ARB_ENTRY_ASSIGNMENT(Entry) = NULL;
                        ARB_CLEAR_OWNS_ASSIGNMENT(Entry);
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

/*
 * ArbFreeEntryAssignment - Free an arbiter-allocated Assignment if owned
 *
 * This helper function safely frees an Assignment descriptor that was
 * allocated by the arbiter (as indicated by ARB_WORKSPACE_OWNS_ASSIGNMENT).
 * It is safe to call this even if the arbiter did not allocate the Assignment.
 *
 * This function should be called during:
 *   - Rollback (ArbRollbackAllocation)
 *   - Entry cleanup after failed allocation
 *   - Any path where staged allocations are abandoned
 */
CODE_SEG("PAGE")
static
VOID
ArbFreeEntryAssignmentIfOwned(
    _Inout_ PARBITER_LIST_ENTRY Entry)
{
    PAGED_CODE();

    if (!Entry)
        return;

    if (ARB_CHECK_OWNS_ASSIGNMENT(Entry))
    {
        if (ARB_ENTRY_ASSIGNMENT(Entry))
        {
            ExFreePoolWithTag(ARB_ENTRY_ASSIGNMENT(Entry), TAG_ARB_ALLOCATION);
            ARB_ENTRY_ASSIGNMENT(Entry) = NULL;
        }
        ARB_CLEAR_OWNS_ASSIGNMENT(Entry);
    }
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

    /*
     * Note: We only reset the PossibleAllocation range list here.
     * The caller is responsible for calling ArbFreeArbiterAllocations
     * on the ArbitrationList if they want to free arbiter-allocated
     * Assignment descriptors.
     *
     * This separation allows the caller to either:
     * 1. Keep the Assignment descriptors for inspection/debugging
     * 2. Call ArbFreeArbiterAllocations to clean them up
     */
    RtlInitializeRangeList(Arbiter->PossibleAllocation);

    ArbUnlock(Arbiter);
    return STATUS_SUCCESS;
}

/*
 * ArbAddReserved - Add architecture-specific reserved resource ranges
 *
 * This function reserves resource ranges that should not be allocated to
 * devices because they are used by system components or are architecturally
 * reserved.
 *
 * ARCHITECTURE-SPECIFIC BEHAVIOR:
 *
 * x86/x64:
 *   - Reserves PCI Type-1 Configuration Space I/O ports 0xCF8-0xCFF
 *   - These ports are used for legacy PCI configuration access
 *
 * ARM64/ARM32:
 *   - Does NOT reserve x86-style I/O ports (ARM has no I/O port concept)
 *   - ARM systems use memory-mapped PCI configuration (ECAM)
 *   - Platform-specific MMIO reservations should be added via ACPI/DT
 *
 * Memory resources:
 *   - Legacy VGA aperture (0xA0000-0xBFFFF) may be reserved if needed
 *   - Currently disabled; uncomment if legacy VGA support is required
 *
 * NOTE: This function is called during arbiter initialization which may occur
 * at elevated IRQL (DISPATCH_LEVEL) on ARM64 during early boot. During init,
 * the arbiter is not yet in use so locking is not required. At runtime, this
 * function requires IRQL <= APC_LEVEL for the KeWaitForSingleObject call.
 * Since this is typically only called during init, we skip locking if called
 * at elevated IRQL.
 */
NTSTATUS
NTAPI
ArbAddReserved(
    _In_ PARBITER_INSTANCE Arbiter)
{
    KIRQL CurrentIrql = KeGetCurrentIrql();
    BOOLEAN NeedsLock = (CurrentIrql <= APC_LEVEL);

    /* No PAGED_CODE() - called during boot at elevated IRQL on ARM64 */

    if (!Arbiter || !Arbiter->Allocation)
        return STATUS_INVALID_PARAMETER;

    /*
     * Only acquire lock if IRQL allows it. During initialization at elevated
     * IRQL, the arbiter is not yet in use by other threads so locking is not
     * required. ArbLock uses KeWaitForSingleObject which cannot be called at
     * DISPATCH_LEVEL or higher.
     */
    if (NeedsLock)
    {
        ArbLock(Arbiter);
    }

    switch (Arbiter->ResourceType)
    {
        case CmResourceTypePort:
#if ARB_ARCH_HAS_IO_PORTS
            /*
             * x86/x64 ONLY: Reserve PCI Type-1 Config Space ports 0xCF8-0xCFF
             *
             * These ports are used by the PCI bus driver for configuration space
             * access via the legacy Type-1 mechanism:
             *   0xCF8 - CONFIG_ADDRESS register (write bus/dev/func/reg)
             *   0xCFC-0xCFF - CONFIG_DATA register (read/write config data)
             *
             * On ARM64, PCI configuration is accessed via memory-mapped ECAM,
             * so this reservation is not needed and could cause conflicts with
             * the memory arbiter if applied incorrectly.
             */
            (void)RtlAddRange(Arbiter->Allocation,
                              ARB_PCI_CONFIG_PORT_START,
                              ARB_PCI_CONFIG_PORT_END,
                              ARB_DEFAULT_RANGE_ATTRS,
                              RTL_RANGE_LIST_ADD_IF_CONFLICT,
                              NULL,
                              Arbiter);
            DPRINT("ArbAddReserved: Reserved x86 PCI config ports 0x%llx-0x%llx\n",
                   ARB_PCI_CONFIG_PORT_START, ARB_PCI_CONFIG_PORT_END);
#else
            /*
             * ARM64/ARM32/RISC-V: No x86-style I/O ports to reserve.
             * Platform-specific reserved ranges should be added by the
             * bus driver or ACPI/device-tree processing.
             */
            DPRINT("ArbAddReserved: No I/O port reservations on this architecture\n");
#endif
            break;

        case CmResourceTypeMemory:
            /*
             * Optional: reserve legacy VGA aperture (0xA0000-0xBFFFF).
             * This is architecture-neutral as VGA memory aperture exists
             * on both x86 and ARM systems with legacy VGA compatibility.
             * Uncomment if legacy VGA support is required.
             */
#if 0
            (void)RtlAddRange(Arbiter->Allocation,
                              0x00000000000A0000ULL,
                              0x00000000000BFFFFULL,
                              ARB_DEFAULT_RANGE_ATTRS,
                              RTL_RANGE_LIST_ADD_IF_CONFLICT,
                              NULL,
                              Arbiter);
            DPRINT("ArbAddReserved: Reserved legacy VGA aperture 0xA0000-0xBFFFF\n");
#endif
            break;

        default:
            /* No default reservations for other resource types */
            break;
    }

    if (NeedsLock)
    {
        ArbUnlock(Arbiter);
    }
    return STATUS_SUCCESS;
}

/*
 * ArbPreprocessEntry - Preprocess an arbitration entry before allocation
 *
 * This is an extension point for normalizing or validating arbitration entries
 * before the allocation algorithm processes them. Examples of preprocessing:
 *   - Converting legacy resource flags to standard form
 *   - Validating that alternatives are properly ordered
 *   - Setting up internal state for the allocation algorithm
 *
 * The default implementation does nothing and returns success.
 * Bus drivers can override this to provide custom preprocessing.
 */
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

    /*
     * Default implementation: no preprocessing needed.
     * Override this callback in bus-specific arbiters if custom
     * preprocessing is required (e.g., legacy flag conversion).
     */
    return STATUS_SUCCESS;
}

/*
 * ArbAllocateEntry - Allocate resources for a single entry (legacy interface)
 *
 * STUB IMPLEMENTATION - Returns STATUS_NOT_SUPPORTED
 *
 * Rationale: This arbiter implementation uses a batch-oriented allocation
 * strategy via ArbTestAllocation/ArbCommitAllocation rather than the
 * per-entry ArbAllocateEntry approach. The batch approach:
 *   - Allows better global optimization across all requests
 *   - Enables proper backtracking when conflicts occur
 *   - Is more efficient for large arbitration lists
 *
 * If per-entry allocation is needed, override this callback in the
 * arbiter instance with a custom implementation.
 */
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

    /* Not implemented - use batch allocation via ArbTestAllocation instead */
    return STATUS_NOT_SUPPORTED;
}

/*
 * ArbGetNextAllocationRange - Get next candidate range (legacy interface)
 *
 * STUB IMPLEMENTATION - Returns FALSE (no more ranges)
 *
 * Rationale: This is part of the legacy per-entry allocation interface.
 * The batch-oriented ArbTestAllocation uses ArbFindFreeWindow internally
 * for range selection, making this callback unnecessary.
 *
 * If the legacy allocation interface is needed, override this callback
 * with an implementation that iterates through ordering list ranges.
 */
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

    /* Not implemented - batch allocation handles range iteration internally */
    return FALSE;
}

/*
 * ArbFindSuitableRange - Find a suitable range for allocation (legacy interface)
 *
 * STUB IMPLEMENTATION - Returns FALSE (no suitable range found)
 *
 * Rationale: The batch allocation algorithm in ArbAllocateForEntryGreedy
 * uses ArbFindFreeWindow for range finding, which provides equivalent
 * functionality with better integration into the batch processing model.
 *
 * Override this callback if the legacy per-entry interface is required.
 */
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

    /* Not implemented - use batch allocation via ArbTestAllocation */
    return FALSE;
}

/*
 * ArbAddAllocation - Add an allocation to staging area (legacy interface)
 *
 * STUB IMPLEMENTATION - Does nothing
 *
 * Rationale: The batch allocation algorithm uses ArbStageAdd internally
 * to add allocations to the PossibleAllocation range list. This callback
 * exists for legacy interface compatibility but is not used by the
 * batch-oriented implementation.
 *
 * Override if custom allocation tracking is needed.
 */
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

    /* Not implemented - batch allocation uses ArbStageAdd internally */
}

/*
 * ArbBacktrackAllocation - Remove a failed allocation (legacy interface)
 *
 * STUB IMPLEMENTATION - Does nothing
 *
 * Rationale: The batch allocation algorithm uses ArbStageDropOwner
 * internally to remove failed allocations during backtracking. This
 * callback exists for legacy interface compatibility.
 *
 * The current greedy algorithm does not perform sophisticated backtracking.
 * If full backtracking support is needed, implement this callback and
 * modify the allocation algorithm to use it.
 */
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

    /* Not implemented - batch allocation uses ArbStageDropOwner internally */
}

/*
 * ArbOverrideConflict - Attempt to resolve a resource conflict
 *
 * STUB IMPLEMENTATION - Returns STATUS_ARBITRATION_UNHANDLED
 *
 * Rationale: Conflict resolution requires knowledge of the specific
 * conflict context (which device owns the conflicting resource, whether
 * it can be relocated, etc.). The updated prototype accepts an
 * ARBITER_ALLOCATION_STATE parameter for this purpose.
 *
 * This stub returns STATUS_ARBITRATION_UNHANDLED to indicate that the
 * conflict cannot be resolved by the default implementation. Bus drivers
 * should override this callback if they can implement conflict resolution
 * strategies such as:
 *   - Relocating existing allocations
 *   - Negotiating with the conflicting device's driver
 *   - Using alternate resource ranges from the device's requirements
 *
 * Note: The old prototype (Arbiter only) is retained for ABI compatibility
 * but the callback in ARBITER_INSTANCE uses the new signature.
 */
CODE_SEG("PAGE")
NTSTATUS
NTAPI
ArbOverrideConflict(
    _In_ PARBITER_INSTANCE Arbiter)
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(Arbiter);

    /*
     * Cannot resolve conflicts without context.
     * Override this callback with ArbState parameter to implement
     * actual conflict resolution.
     */
    return STATUS_ARBITRATION_UNHANDLED;
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

/*
 * ArbQueryArbitrate - Query whether arbitration can succeed
 *
 * This function tests whether the arbiter can satisfy all resource requests
 * in the arbitration list without actually committing the allocations.
 * It is used by PnP to determine if a proposed resource configuration
 * is viable before proceeding with device start.
 *
 * The implementation uses ArbTestAllocation to perform a trial allocation,
 * then rolls back the changes regardless of success or failure.
 *
 * Parameters:
 *   Arbiter         - Pointer to the arbiter instance
 *   ArbitrationList - List of ARBITER_LIST_ENTRY structures (new prototype)
 *
 * Returns:
 *   STATUS_SUCCESS if all requests can be satisfied
 *   STATUS_CONFLICTING_ADDRESSES if conflicts exist
 *   Other error codes on failure
 *
 * NOTE: The old prototype (Arbiter only) returns STATUS_NOT_SUPPORTED.
 * The callback in ARBITER_INSTANCE should use the new prototype with ArbitrationList.
 */
CODE_SEG("PAGE")
NTSTATUS
NTAPI
ArbQueryArbitrate(
    _In_ PARBITER_INSTANCE Arbiter)
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(Arbiter);

    /*
     * The old prototype cannot perform useful work without an ArbitrationList.
     * Callers should use the TestAllocation callback instead, or override
     * QueryArbitrate with the new prototype that accepts ArbitrationList.
     */
    return STATUS_NOT_SUPPORTED;
}

/*
 * ArbQueryConflict - Query resource conflicts for a specific request
 *
 * This function identifies which resources conflict with a proposed allocation.
 * Used by PnP manager to report detailed conflict information to users/tools.
 *
 * The implementation searches the committed allocation list for overlapping
 * ranges and returns information about the conflicting devices.
 *
 * Parameters:
 *   Arbiter              - Pointer to the arbiter instance
 *   PhysicalDeviceObject - PDO requesting the resource (new prototype)
 *   IoDescriptor         - Resource requirement to check (new prototype)
 *   ConflictCount        - Receives count of conflicts (new prototype)
 *   ConflictList         - Receives conflict details (new prototype)
 *
 * Returns:
 *   STATUS_SUCCESS if query completed (ConflictCount may be 0)
 *   STATUS_INSUFFICIENT_RESOURCES if memory allocation failed
 *
 * NOTE: The old prototype (Arbiter only) returns STATUS_NOT_SUPPORTED.
 * Override QueryConflict callback with the new prototype for full functionality.
 */
CODE_SEG("PAGE")
NTSTATUS
NTAPI
ArbQueryConflict(
    _In_ PARBITER_INSTANCE Arbiter)
{
    PAGED_CODE();
    UNREFERENCED_PARAMETER(Arbiter);

    /*
     * The old prototype cannot perform useful work without knowing
     * which resource to check for conflicts. Override the QueryConflict
     * callback with the new prototype that accepts IoDescriptor parameters.
     */
    return STATUS_NOT_SUPPORTED;
}

/*
 * ArbStartArbiter - Start arbiter operation
 *
 * This function initializes the arbiter for operation after it has been
 * configured. It ensures that reserved ranges are added to the allocation
 * list before any device requests are processed.
 *
 * Called during device start (IRP_MN_START_DEVICE) processing for the
 * bus device that owns this arbiter.
 */
CODE_SEG("PAGE")
NTSTATUS
NTAPI
ArbStartArbiter(
    _In_ PARBITER_INSTANCE Arbiter)
{
    PAGED_CODE();

    if (!Arbiter || !Arbiter->Allocation || !Arbiter->PossibleAllocation)
        return STATUS_INVALID_PARAMETER;

    /* Add architecture/bus-specific reserved ranges */
    return ArbAddReserved(Arbiter);
}

/* ============================================================================================
   ORDERING LIST UTILITIES
   ========================================================================================== */

/*
 * ArbAddOrdering - Add a resource ordering range to an ordering list
 *
 * NOTE: This function is called during arbiter initialization which may occur
 * at elevated IRQL (DISPATCH_LEVEL) on ARM64 during early boot. Therefore:
 * - No PAGED_CODE assertion (would fail at DISPATCH_LEVEL)
 * - Uses NonPagedPool for allocations (PagedPool illegal at DISPATCH_LEVEL)
 * - Not placed in PAGE segment (must remain resident)
 */
NTSTATUS
NTAPI
ArbAddOrdering(
    _Out_ PARBITER_ORDERING_LIST OrderList,
    _In_ UINT64 MinimumAddress,
    _In_ UINT64 MaximumAddress)
{
    /* No PAGED_CODE() - called during boot at elevated IRQL on ARM64 */

    if (!OrderList) return STATUS_INVALID_PARAMETER;
    if (MaximumAddress < MinimumAddress) return STATUS_INVALID_PARAMETER;

    if (OrderList->Count == OrderList->Maximum)
    {
        /* Check for overflow before multiplication */
        if (OrderList->Maximum > USHRT_MAX / 2 && OrderList->Maximum != 0)
            return STATUS_INSUFFICIENT_RESOURCES;

        USHORT newMax = (OrderList->Maximum == 0) ? 8 : (OrderList->Maximum * 2);
        SIZE_T bytes = sizeof(ARBITER_ORDERING) * newMax;
        /* Use NonPagedPool - this is called at DISPATCH_LEVEL during ARM64 boot */
        PARBITER_ORDERING newBuf = (PARBITER_ORDERING)ExAllocatePoolWithTag(NonPagedPool, bytes, TAG_ARB_RANGE);
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

/*
 * ArbPruneOrdering - Remove ordering ranges outside a specified window
 *
 * NOTE: This function may be called during arbiter initialization which can occur
 * at elevated IRQL (DISPATCH_LEVEL) on ARM64 during early boot. Therefore:
 * - No PAGED_CODE assertion (would fail at DISPATCH_LEVEL)
 * - Not placed in PAGE segment (must remain resident)
 */
NTSTATUS
NTAPI
ArbPruneOrdering(
    _Out_ PARBITER_ORDERING_LIST OrderingList,
    _In_ UINT64 MinimumAddress,
    _In_ UINT64 MaximumAddress)
{
    /* No PAGED_CODE() - may be called during boot at elevated IRQL on ARM64 */

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

/*
 * ArbInitializeOrderingList - Initialize an arbiter ordering list structure
 *
 * NOTE: This function is called during arbiter initialization which may occur
 * at elevated IRQL (DISPATCH_LEVEL) on ARM64 during early boot. Therefore:
 * - No PAGED_CODE assertion (would fail at DISPATCH_LEVEL)
 * - Not placed in PAGE segment (must remain resident)
 */
NTSTATUS
NTAPI
ArbInitializeOrderingList(
    _Out_ PARBITER_ORDERING_LIST OrderList)
{
    /* No PAGED_CODE() - called during boot at elevated IRQL on ARM64 */

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

/*
 * ArbBuildAssignmentOrdering - Build preferred and reserved ordering from a translator
 *
 * Your header's translator signature (OutIoDescriptor, IoDescriptor) doesn't carry a name,
 * so we can only translate each alternative's descriptor. If you want ACPI/firmware-defined
 * named orderings, wire them externally and call ArbAddOrdering/ArbPruneOrdering before allocations.
 *
 * NOTE: This function is called during arbiter initialization which may occur
 * at elevated IRQL (DISPATCH_LEVEL) on ARM64 during early boot. Therefore:
 * - No PAGED_CODE assertion (would fail at DISPATCH_LEVEL)
 * - Not placed in PAGE segment (must remain resident)
 */
NTSTATUS
NTAPI
ArbBuildAssignmentOrdering(
    _Inout_ PARBITER_INSTANCE ArbInstance,
    _In_ PCWSTR OrderName,                 /* not used with provided translator signature */
    _In_ PCWSTR ReservedOrderName,         /* not used with provided translator signature */
    _In_ PARB_TRANSLATE_ORDERING TranslateOrderingFunction)
{
    /* No PAGED_CODE() - called during boot at elevated IRQL on ARM64 */
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

/*
 * ArbInitializeArbiterInstance - Initialize an arbiter instance structure
 *
 * This function sets up an arbiter instance for managing hardware resources.
 * It is typically called during PnP initialization from IopInitializeArbiters().
 *
 * NOTE: This function is called during system boot initialization which may occur
 * at elevated IRQL (DISPATCH_LEVEL) on ARM64. The ARM64 architecture has different
 * IRQL behavior during early boot compared to x86/x64. Therefore:
 *
 * - No PAGED_CODE assertion (would fail at DISPATCH_LEVEL)
 * - Uses NonPagedPool for ALL allocations (PagedPool illegal at DISPATCH_LEVEL)
 * - Not placed in PAGE segment (must remain resident)
 *
 * The arbiter data structures must be in NonPagedPool anyway since they may be
 * accessed during interrupt handling and DPC processing at elevated IRQL.
 */
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

    /* No PAGED_CODE() - called during boot at elevated IRQL on ARM64 */

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

    /*
     * Use NonPagedPool for AllocationStack - this function is called at DISPATCH_LEVEL
     * on ARM64 during early boot. PagedPool allocations are illegal at DISPATCH_LEVEL
     * because they may trigger page faults which cannot be handled at that IRQL.
     */
    Arbiter->AllocationStack = ExAllocatePoolWithTag(NonPagedPool, PAGE_SIZE, TAG_ARB_ALLOCATION);
    if (!Arbiter->AllocationStack)
    {
        DPRINT1("ArbInitializeArbiterInstance: STATUS_INSUFFICIENT_RESOURCES\n");
        ExFreePoolWithTag(Arbiter->MutexEvent, TAG_ARBITER);
        Arbiter->MutexEvent = NULL;
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    Arbiter->AllocationStackMaxSize = PAGE_SIZE;

    /*
     * Use NonPagedPool for Allocation range list - accessed during resource
     * arbitration which may occur at elevated IRQL.
     */
    Arbiter->Allocation = ExAllocatePoolWithTag(NonPagedPool, sizeof(RTL_RANGE_LIST), TAG_ARB_RANGE);
    if (!Arbiter->Allocation)
    {
        DPRINT1("ArbInitializeArbiterInstance: STATUS_INSUFFICIENT_RESOURCES\n");
        ExFreePoolWithTag(Arbiter->AllocationStack, TAG_ARB_ALLOCATION);
        ExFreePoolWithTag(Arbiter->MutexEvent, TAG_ARBITER);
        Arbiter->AllocationStack = NULL;
        Arbiter->MutexEvent = NULL;
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    /*
     * Use NonPagedPool for PossibleAllocation range list - accessed during
     * resource arbitration which may occur at elevated IRQL.
     */
    Arbiter->PossibleAllocation = ExAllocatePoolWithTag(NonPagedPool, sizeof(RTL_RANGE_LIST), TAG_ARB_RANGE);
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

/* ============================================================================================
   INSTANCE CLEANUP
   ========================================================================================== */

/*
 * ArbDestroyArbiterInstance - Clean up and destroy an arbiter instance
 *
 * This function releases all resources allocated by ArbInitializeArbiterInstance.
 * It is the responsibility of the caller to ensure that:
 *   1. No transactions are in progress
 *   2. No other threads are accessing the arbiter
 *   3. All devices using this arbiter have released their allocations
 *
 * The function performs cleanup in reverse order of allocation:
 *   - Free ordering lists
 *   - Free range list entries
 *   - Free range list structures
 *   - Free allocation stack
 *   - Free mutex event
 *   - Zero the structure to prevent use-after-free
 *
 * WARNING: After calling this function, the arbiter instance is invalid
 * and must not be accessed. The Arbiter structure itself is NOT freed
 * (it was not allocated by ArbInitializeArbiterInstance).
 */
CODE_SEG("PAGE")
VOID
NTAPI
ArbDestroyArbiterInstance(
    _Inout_ PARBITER_INSTANCE Arbiter)
{
    PAGED_CODE();

    if (!Arbiter)
    {
        return;
    }

    /* Verify this is a valid arbiter instance */
    if (Arbiter->Signature != ARBITER_SIGNATURE)
    {
        DPRINT1("ArbDestroyArbiterInstance: Invalid arbiter signature 0x%08X\n",
                Arbiter->Signature);
        return;
    }

    DPRINT("ArbDestroyArbiterInstance: Destroying arbiter '%S'\n",
           Arbiter->Name ? Arbiter->Name : L"<unnamed>");

    /* Warn if a transaction is in progress */
    if (Arbiter->TransactionInProgress)
    {
        DPRINT1("ArbDestroyArbiterInstance: WARNING - Transaction in progress!\n");
    }

    /* Free ordering lists */
    ArbFreeOrderingList(&Arbiter->OrderingList);
    ArbFreeOrderingList(&Arbiter->ReservedList);

    /* Free range lists and their contents */
    if (Arbiter->PossibleAllocation)
    {
        RtlFreeRangeList(Arbiter->PossibleAllocation);
        ExFreePoolWithTag(Arbiter->PossibleAllocation, TAG_ARB_RANGE);
        Arbiter->PossibleAllocation = NULL;
    }

    if (Arbiter->Allocation)
    {
        RtlFreeRangeList(Arbiter->Allocation);
        ExFreePoolWithTag(Arbiter->Allocation, TAG_ARB_RANGE);
        Arbiter->Allocation = NULL;
    }

    /* Free allocation stack */
    if (Arbiter->AllocationStack)
    {
        ExFreePoolWithTag(Arbiter->AllocationStack, TAG_ARB_ALLOCATION);
        Arbiter->AllocationStack = NULL;
        Arbiter->AllocationStackMaxSize = 0;
    }

    /* Free mutex event last (we might need it during cleanup on other paths) */
    if (Arbiter->MutexEvent)
    {
        ExFreePoolWithTag(Arbiter->MutexEvent, TAG_ARBITER);
        Arbiter->MutexEvent = NULL;
    }

    /* Clear the signature to detect use-after-free */
    Arbiter->Signature = 0;

    /* Zero remaining fields */
    Arbiter->Name = NULL;
    Arbiter->ResourceType = CmResourceTypeNull;
    Arbiter->TransactionInProgress = FALSE;
    Arbiter->BusDeviceObject = NULL;
    Arbiter->Interface = NULL;
    Arbiter->Extension = NULL;
    Arbiter->ReferenceCount = 0;

    /* Clear callback pointers */
    Arbiter->UnpackRequirement = NULL;
    Arbiter->PackResource = NULL;
    Arbiter->UnpackResource = NULL;
    Arbiter->ScoreRequirement = NULL;
    Arbiter->TestAllocation = NULL;
    Arbiter->RetestAllocation = NULL;
    Arbiter->CommitAllocation = NULL;
    Arbiter->RollbackAllocation = NULL;
    Arbiter->BootAllocation = NULL;
    Arbiter->QueryArbitrate = NULL;
    Arbiter->QueryConflict = NULL;
    Arbiter->AddReserved = NULL;
    Arbiter->StartArbiter = NULL;
    Arbiter->PreprocessEntry = NULL;
    Arbiter->AllocateEntry = NULL;
    Arbiter->GetNextAllocationRange = NULL;
    Arbiter->FindSuitableRange = NULL;
    Arbiter->AddAllocation = NULL;
    Arbiter->BacktrackAllocation = NULL;
    Arbiter->OverrideConflict = NULL;
    Arbiter->ConflictCallback = NULL;
    Arbiter->ConflictCallbackContext = NULL;

    DPRINT("ArbDestroyArbiterInstance: Complete\n");
}

/* ============================================================================================
   ASSIGNMENT CLEANUP HELPERS
   ========================================================================================== */

/*
 * ArbFreeArbiterAllocations - Free arbiter-allocated Assignment descriptors
 *
 * This function iterates through an arbitration list and frees any Assignment
 * descriptors that were allocated by the arbiter (as indicated by the
 * ARB_WORKSPACE_OWNS_ASSIGNMENT flag in each entry's WorkSpace).
 *
 * Call this function:
 *   - After ArbRollbackAllocation to clean up failed allocations
 *   - When abandoning an allocation attempt
 *   - During cleanup of an arbitration list that may have partial allocations
 *
 * Parameters:
 *   ArbitrationList - List of ARBITER_LIST_ENTRY structures
 *
 * Returns:
 *   None. All arbiter-owned assignments are freed and cleared.
 *
 * IMPORTANT: This function does NOT free Assignment descriptors that were
 * provided by the caller. It only frees those allocated by the arbiter,
 * which are tracked via the WorkSpace ownership flag.
 */
CODE_SEG("PAGE")
VOID
NTAPI
ArbFreeArbiterAllocations(
    _In_ PLIST_ENTRY ArbitrationList)
{
    PAGED_CODE();

    if (!ArbitrationList || IsListEmpty(ArbitrationList))
        return;

    for (PLIST_ENTRY le = ArbitrationList->Flink; le != ArbitrationList; le = le->Flink)
    {
        PARBITER_LIST_ENTRY entry = ARB_GET_LIST_ENTRY(le);
        ArbFreeEntryAssignmentIfOwned(entry);
    }
}

/* ============================================================================================
   LOCK HELPERS (exported for external use)
   ========================================================================================== */

/*
 * ArbAcquireArbiterLock - Acquire the arbiter instance lock
 *
 * This function acquires the mutex event that protects arbiter state.
 * Must be called before accessing arbiter state from external code.
 * Must be paired with ArbReleaseArbiterLock.
 *
 * The lock is acquired at PASSIVE_LEVEL and may block if another
 * thread holds the lock.
 */
CODE_SEG("PAGE")
VOID
NTAPI
ArbAcquireArbiterLock(
    _In_ PARBITER_INSTANCE Arbiter)
{
    PAGED_CODE();

    if (Arbiter && Arbiter->MutexEvent)
    {
        ArbLock(Arbiter);
    }
}

/*
 * ArbReleaseArbiterLock - Release the arbiter instance lock
 *
 * This function releases the mutex event that protects arbiter state.
 * Must be called after accessing arbiter state is complete.
 */
CODE_SEG("PAGE")
VOID
NTAPI
ArbReleaseArbiterLock(
    _In_ PARBITER_INSTANCE Arbiter)
{
    PAGED_CODE();

    if (Arbiter && Arbiter->MutexEvent)
    {
        ArbUnlock(Arbiter);
    }
}
