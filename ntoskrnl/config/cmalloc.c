/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            ntoskrnl/config/cmalloc.c
 * PURPOSE:         Routines for allocating and freeing registry structures
 * PROGRAMMERS:     Alex Ionescu (alex.ionescu@reactos.org)
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* GLOBALS *******************************************************************/

BOOLEAN CmpAllocInited;
KGUARDED_MUTEX CmpAllocBucketLock;
LIST_ENTRY CmpFreeKCBListHead;

KGUARDED_MUTEX CmpDelayAllocBucketLock;
LIST_ENTRY CmpFreeDelayItemsListHead;

/*
 * ARM64 FIX: Track recently freed page addresses to detect stale frees.
 * The pool allocator can immediately reuse a freed page, making sentinel
 * detection unreliable. We keep a small circular buffer of recently freed
 * page addresses and reject any attempt to free a page that's in this list.
 */
#define CM_FREED_PAGE_CACHE_SIZE 32
static PVOID CmpFreedPageCache[CM_FREED_PAGE_CACHE_SIZE];
static volatile LONG CmpFreedPageCacheIndex = 0;

/* FUNCTIONS *****************************************************************/

CODE_SEG("INIT")
VOID
NTAPI
CmpInitCmPrivateAlloc(VOID)
{
    /* Make sure we didn't already do this */
    if (!CmpAllocInited)
    {
        /* Setup the lock and list */
        KeInitializeGuardedMutex(&CmpAllocBucketLock);
        InitializeListHead(&CmpFreeKCBListHead);
        CmpAllocInited = TRUE;
    }
}

CODE_SEG("INIT")
VOID
NTAPI
CmpInitCmPrivateDelayAlloc(VOID)
{
    /* Initialize the delay allocation list and lock */
    KeInitializeGuardedMutex(&CmpDelayAllocBucketLock);
    InitializeListHead(&CmpFreeDelayItemsListHead);
}

/*
 * Sentinel value to mark a CM_ALLOC_PAGE as freed.
 * This is stored in the Reserved field before freeing the page.
 */
#define CM_ALLOC_PAGE_FREED_SENTINEL 0xDEADBEEF

VOID
NTAPI
CmpFreeKeyControlBlock(IN PCM_KEY_CONTROL_BLOCK Kcb)
{
    ULONG i;
    PCM_ALLOC_PAGE AllocPage;
    PAGED_CODE();

    /*
     * ARM64 FIX: Detect double-free by checking the signature.
     * The signature is set to CM_KCB_INVALID_SIGNATURE by CmpCleanUpKcbCacheWithLock
     * before calling this function. After we add the KCB to the free list, we set
     * the signature to CM_KCB_ON_FREE_LIST_SIGNATURE to detect subsequent double-frees.
     *
     * ONLY valid incoming signature:
     * - CM_KCB_INVALID_SIGNATURE: KCB was properly marked invalid before free
     *
     * Invalid signatures that indicate double-free or corruption:
     * - CM_KCB_ON_FREE_LIST_SIGNATURE: KCB is already on the free list!
     * - CM_KCB_SIGNATURE: KCB is still in use (stale pointer trying to free it)
     * - Anything else: Memory corruption
     *
     * Note: Previously we also allowed CM_KCB_SIGNATURE, but this caused issues
     * where stale pointers could free in-use KCBs.
     */
    if (Kcb->Signature == CM_KCB_ON_FREE_LIST_SIGNATURE)
    {
        DPRINT1("CmpFreeKeyControlBlock: KCB %p already on free list! Skipping double-free.\n", Kcb);
        return;
    }
    if (Kcb->Signature == CM_KCB_SIGNATURE)
    {
        DPRINT1("CmpFreeKeyControlBlock: KCB %p has CM_KCB_SIGNATURE (still in use), skipping stale free\n", Kcb);
        DPRINT1("  Caller: %p\n", _ReturnAddress());
        return;
    }
    if (Kcb->Signature != CM_KCB_INVALID_SIGNATURE)
    {
        DPRINT1("CmpFreeKeyControlBlock: Invalid signature 0x%lx for KCB %p, skipping free\n",
                Kcb->Signature, Kcb);
        DPRINT1("  Expected CM_KCB_INVALID_SIGNATURE (0x%lx)\n", CM_KCB_INVALID_SIGNATURE);
        DPRINT1("  Caller: %p\n", _ReturnAddress());
        return;
    }

    /* Sanity checks */
    ASSERT(IsListEmpty(&Kcb->KeyBodyListHead) == TRUE);
    for (i = 0; i < 4; i++) ASSERT(Kcb->KeyBodyArray[i] == NULL);

    /* Check if it wasn't privately allocated */
    if (!Kcb->PrivateAlloc)
    {
        /* Free it from the pool */
        CmpFree(Kcb, TAG_KCB);
        return;
    }

    /* Acquire the private allocation lock */
    KeAcquireGuardedMutex(&CmpAllocBucketLock);

    /* Sanity check on lock ownership */
    CMP_ASSERT_HASH_ENTRY_LOCK(Kcb->ConvKey);

    /* Get the allocation page */
    AllocPage = CmpGetAllocPageFromKcb(Kcb);

    /*
     * ARM64 FIX: Detect if this page has already been freed and potentially reused.
     *
     * The problem: After CmpFree(AllocPage) returns, the pool may immediately
     * reuse the page for a new allocation. If there are stale KCB references
     * that still point to the old page, those KCBs will calculate the same
     * AllocPage address when freed, but AllocPage now contains completely
     * different data.
     *
     * Detection strategy:
     * 1. Check if the Reserved field equals our sentinel (page freed, not yet reused)
     * 2. Check if FreeCount is out of valid range (page reused, contains garbage)
     * 3. Check if Reserved has a valid-looking value (0 for fresh pages)
     *
     * If any of these checks indicate the page is no longer valid for KCB use,
     * skip the free to prevent pool corruption.
     */
    if (AllocPage->Reserved == CM_ALLOC_PAGE_FREED_SENTINEL)
    {
        DPRINT1("CmpFreeKeyControlBlock: Page %p already freed (sentinel, stale KCB %p)\n",
                AllocPage, Kcb);
        KeReleaseGuardedMutex(&CmpAllocBucketLock);
        return;
    }

    /* Check if FreeCount is within valid range */
    if (AllocPage->FreeCount > CM_KCBS_PER_PAGE)
    {
        DPRINT1("CmpFreeKeyControlBlock: Page %p has invalid FreeCount %lu (stale KCB %p)\n",
                AllocPage, (ULONG)AllocPage->FreeCount, Kcb);
        KeReleaseGuardedMutex(&CmpAllocBucketLock);
        return;
    }

    /*
     * ARM64 FIX: Additional check - if Reserved is non-zero and not our sentinel,
     * the page may have been reused for something else.
     */
    if (AllocPage->Reserved != 0)
    {
        DPRINT1("CmpFreeKeyControlBlock: Page %p has non-zero Reserved 0x%lx (stale KCB %p)\n",
                AllocPage, AllocPage->Reserved, Kcb);
        KeReleaseGuardedMutex(&CmpAllocBucketLock);
        return;
    }

    /*
     * ARM64 FIX: Check if this page was recently freed.
     * The sentinel approach fails because the pool can reallocate and reinitialize
     * the page before stale references try to free it. This cache tracks recently
     * freed page addresses.
     */
    {
        ULONG j;
        for (j = 0; j < CM_FREED_PAGE_CACHE_SIZE; j++)
        {
            if (CmpFreedPageCache[j] == AllocPage)
            {
                /* Silently ignore stale KCB frees - this is expected on ARM64 */
                KeReleaseGuardedMutex(&CmpAllocBucketLock);
                return;
            }
        }
    }

    /* Add us to the free list */
    InsertTailList(&CmpFreeKCBListHead, &Kcb->FreeListEntry);

    /*
     * ARM64 FIX: Mark KCB as being on the free list to detect double-frees.
     * If this KCB is freed again while on the free list, the signature check
     * at the beginning of this function will detect it and return early.
     */
    Kcb->Signature = CM_KCB_ON_FREE_LIST_SIGNATURE;

    /* Sanity check - FreeCount should not already be at maximum */
    ASSERT(AllocPage->FreeCount != CM_KCBS_PER_PAGE);

    /* Increase free count */
    if (++AllocPage->FreeCount == CM_KCBS_PER_PAGE)
    {
        /* Loop all the entries */
        for (i = 0; i < CM_KCBS_PER_PAGE; i++)
        {
            /* Get the KCB */
            Kcb = (PVOID)((ULONG_PTR)AllocPage +
                          FIELD_OFFSET(CM_ALLOC_PAGE, AllocPage) +
                          i * sizeof(CM_KEY_CONTROL_BLOCK));

            /* Remove the entry */
            RemoveEntryList(&Kcb->FreeListEntry);
        }

        /*
         * ARM64 FIX: Mark the page as freed before actually freeing it.
         * This allows subsequent stale free attempts to detect the double-free
         * and return early. We use the Reserved field as a sentinel.
         */
        AllocPage->Reserved = CM_ALLOC_PAGE_FREED_SENTINEL;

        /*
         * ARM64 FIX: Add the page to the freed cache.
         * This is a circular buffer so we only track the most recent pages.
         */
        {
            LONG Index = InterlockedIncrement(&CmpFreedPageCacheIndex) % CM_FREED_PAGE_CACHE_SIZE;
            CmpFreedPageCache[Index] = AllocPage;
        }

        /* Free the page */
        CmpFree(AllocPage, TAG_KCB);
    }

    /* Release the lock */
    KeReleaseGuardedMutex(&CmpAllocBucketLock);
}

PCM_KEY_CONTROL_BLOCK
NTAPI
CmpAllocateKeyControlBlock(VOID)
{
    PLIST_ENTRY NextEntry;
    PCM_KEY_CONTROL_BLOCK CurrentKcb;
    PCM_ALLOC_PAGE AllocPage;
    ULONG i;
    PAGED_CODE();

    /* Check if private allocations are initialized */
    if (CmpAllocInited)
    {
        /* They are, acquire the bucket lock */
        KeAcquireGuardedMutex(&CmpAllocBucketLock);

        /* See if there's something on the free KCB list */
SearchKcbList:
        if (!IsListEmpty(&CmpFreeKCBListHead))
        {
            /* Remove the entry */
            NextEntry = RemoveHeadList(&CmpFreeKCBListHead);

            /* Get the KCB */
            CurrentKcb = CONTAINING_RECORD(NextEntry,
                                           CM_KEY_CONTROL_BLOCK,
                                           FreeListEntry);

            /* Get the allocation page */
            AllocPage = CmpGetAllocPageFromKcb(CurrentKcb);

            /* Decrease the free count */
            ASSERT(AllocPage->FreeCount != 0);
            AllocPage->FreeCount--;

            /* Make sure this KCB is privately allocated */
            ASSERT(CurrentKcb->PrivateAlloc == 1);

            /* Release the allocation lock */
            KeReleaseGuardedMutex(&CmpAllocBucketLock);

            /* Return the KCB */
            return CurrentKcb;
        }

        /* Allocate an allocation page */
        AllocPage = CmpAllocate(PAGE_SIZE, TRUE, TAG_KCB);
        if (AllocPage)
        {
            /* Set default entries */
            AllocPage->FreeCount = CM_KCBS_PER_PAGE;
            /* Initialize Reserved to 0 (not freed sentinel) */
            AllocPage->Reserved = 0;

            /* Loop each entry */
            for (i = 0; i < CM_KCBS_PER_PAGE; i++)
            {
                /* Get this entry */
                CurrentKcb = (PVOID)((ULONG_PTR)AllocPage +
                                     FIELD_OFFSET(CM_ALLOC_PAGE, AllocPage) +
                                     i * sizeof(CM_KEY_CONTROL_BLOCK));

                /* Set it up */
                CurrentKcb->PrivateAlloc = TRUE;
                CurrentKcb->DelayCloseEntry = NULL;
                CurrentKcb->Signature = CM_KCB_ON_FREE_LIST_SIGNATURE;
                InsertTailList(&CmpFreeKCBListHead,
                               &CurrentKcb->FreeListEntry);
            }

            /* Now go back and search the list */
            goto SearchKcbList;
        }

        /* Release the allocation lock */
        KeReleaseGuardedMutex(&CmpAllocBucketLock);
    }

    /* Allocate a KCB only */
    CurrentKcb = CmpAllocate(sizeof(CM_KEY_CONTROL_BLOCK), TRUE, TAG_KCB);
    if (CurrentKcb)
    {
        /* Set it up */
        CurrentKcb->PrivateAlloc = 0;
        CurrentKcb->DelayCloseEntry = NULL;
    }

    /* Return it */
    return CurrentKcb;
}

PVOID
NTAPI
CmpAllocateDelayItem(VOID)
{
    PCM_DELAY_ALLOC Entry;
    PCM_ALLOC_PAGE AllocPage;
    ULONG i;
    PLIST_ENTRY NextEntry;
    PAGED_CODE();

    /* Lock the allocation buckets */
    KeAcquireGuardedMutex(&CmpDelayAllocBucketLock);

    /* Look for an item on the free list */
SearchList:
    if (!IsListEmpty(&CmpFreeDelayItemsListHead))
    {
        /* Get the current entry in the list */
        NextEntry = RemoveHeadList(&CmpFreeDelayItemsListHead);

        /* Grab the item */
        Entry = CONTAINING_RECORD(NextEntry, CM_DELAY_ALLOC, ListEntry);

        /* Clear the list */
        Entry->ListEntry.Flink = Entry->ListEntry.Blink = NULL;

        /* Grab the alloc page */
        AllocPage = CmpGetAllocPageFromDelayAlloc(Entry);

        /* Decrease free entries */
        ASSERT(AllocPage->FreeCount != 0);
        AllocPage->FreeCount--;

        /* Release the lock */
        KeReleaseGuardedMutex(&CmpDelayAllocBucketLock);
        return Entry;
    }

    /* Allocate an allocation page */
    AllocPage = CmpAllocate(PAGE_SIZE, TRUE, TAG_CM);
    if (AllocPage)
    {
        /* Set default entries */
        AllocPage->FreeCount = CM_DELAYS_PER_PAGE;
        /* Initialize Reserved to 0 (not freed sentinel) */
        AllocPage->Reserved = 0;

        /* Loop each entry */
        for (i = 0; i < CM_DELAYS_PER_PAGE; i++)
        {
            /* Get this entry and link it */
            Entry = (PVOID)((ULONG_PTR)AllocPage +
                            FIELD_OFFSET(CM_ALLOC_PAGE, AllocPage) +
                            i * sizeof(CM_DELAY_ALLOC));
            InsertTailList(&CmpFreeDelayItemsListHead,
                           &Entry->ListEntry);

            /* Clear the KCB pointer */
            Entry->Kcb = NULL;
        }

        /* Do the search again */
        goto SearchList;
    }

    /* Release the lock */
    KeReleaseGuardedMutex(&CmpDelayAllocBucketLock);
    return NULL;
}

VOID
NTAPI
CmpFreeDelayItem(PVOID Entry)
{
    PCM_DELAY_ALLOC AllocEntry = (PCM_DELAY_ALLOC)Entry;
    PCM_ALLOC_PAGE AllocPage;
    ULONG i;
    PAGED_CODE();

    /* Lock the table */
    KeAcquireGuardedMutex(&CmpDelayAllocBucketLock);

    /* Get the alloc page */
    AllocPage = CmpGetAllocPageFromDelayAlloc(Entry);

    /*
     * ARM64 FIX: Check if the page has already been freed.
     * This prevents double-free issues with stale delay item references.
     */
    if (AllocPage->Reserved == CM_ALLOC_PAGE_FREED_SENTINEL)
    {
        KeReleaseGuardedMutex(&CmpDelayAllocBucketLock);
        return;
    }

    /* ARM64 FIX: Check freed page cache */
    {
        ULONG j;
        for (j = 0; j < CM_FREED_PAGE_CACHE_SIZE; j++)
        {
            if (CmpFreedPageCache[j] == AllocPage)
            {
                KeReleaseGuardedMutex(&CmpDelayAllocBucketLock);
                return;
            }
        }
    }

    /* Add the entry at the end */
    InsertTailList(&CmpFreeDelayItemsListHead, &AllocEntry->ListEntry);

    ASSERT(AllocPage->FreeCount != CM_DELAYS_PER_PAGE);

    /* Increase the number of free items */
    if (++AllocPage->FreeCount == CM_DELAYS_PER_PAGE)
    {
        /* Page is totally free now, loop each entry */
        for (i = 0; i < CM_DELAYS_PER_PAGE; i++)
        {
            /* Get the entry and unlink it */
            AllocEntry = (PVOID)((ULONG_PTR)AllocPage +
                                 FIELD_OFFSET(CM_ALLOC_PAGE, AllocPage) +
                                 i * sizeof(CM_DELAY_ALLOC));
            RemoveEntryList(&AllocEntry->ListEntry);
        }

        /*
         * ARM64 FIX: Mark the page as freed before actually freeing it.
         */
        AllocPage->Reserved = CM_ALLOC_PAGE_FREED_SENTINEL;

        /* Now free the page */
        CmpFree(AllocPage, TAG_CM);
    }

    /* Release the lock */
    KeReleaseGuardedMutex(&CmpDelayAllocBucketLock);
}
