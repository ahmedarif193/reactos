/*
 * PROJECT:         ReactOS Runtime Library
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            lib/rtl/generictable.c
 * PURPOSE:         Splay Tree Generic Table Implementation (robust/optimized variant)
 * PROGRAMMERS:     Alex Ionescu (alex.ionescu@reactos.org)
 *                  (Minor robustness & micro-optimizations; safe-size checks; 2025)
 */

 /* INCLUDES ******************************************************************/

#include <rtl.h>
#define NDEBUG
#include <debug.h>

#if defined(RTL_USE_AVL_TABLES)
#warning "RTL_USE_AVL_TABLES defined while building generictable.c"
#endif

/* Internal header for table entries */
typedef struct _TABLE_ENTRY_HEADER
{
    RTL_SPLAY_LINKS SplayLinks;
    LIST_ENTRY ListEntry;
    ULONGLONG Alignment; /* keep user data naturally aligned */
    UCHAR UserData[];
} TABLE_ENTRY_HEADER, *PTABLE_ENTRY_HEADER;

/* UserData must stay naturally pointer-aligned */
C_ASSERT((FIELD_OFFSET(TABLE_ENTRY_HEADER, UserData) % sizeof(PVOID)) == 0);

/* PRIVATE HELPERS ***********************************************************/

/* Safe unsigned addition for ULONGs: returns FALSE on overflow */
__forceinline
BOOLEAN
RtlpSafeAddUlong(_In_ ULONG a, _In_ ULONG b, _Out_ PULONG out)
{
    if (a > (MAXULONG - b)) return FALSE;
    *out = a + b;
    return TRUE;
}

/* Compute allocation size (header + payload) and verify it fits CLONG */
__forceinline
BOOLEAN
RtlpComputeAllocSize(_In_ ULONG BufferSize, _Out_ CLONG* AllocSizeOut)
{
    ULONG total;
    if (!RtlpSafeAddUlong(BufferSize,
                          FIELD_OFFSET(TABLE_ENTRY_HEADER, UserData),
                          &total))
    {
        return FALSE; /* overflow */
    }

    /* PRTL_GENERIC_ALLOCATE_ROUTINE takes CLONG */
    if (total > (ULONG)MAXLONG) return FALSE;

    *AllocSizeOut = (CLONG)total;
    return TRUE;
}

/* Fetch TABLE_ENTRY_HEADER from splay link */
__forceinline
PTABLE_ENTRY_HEADER
RtlpEntryFromLinks(_In_ PRTL_SPLAY_LINKS Links)
{
    return CONTAINING_RECORD(Links, TABLE_ENTRY_HEADER, SplayLinks);
}

/* PRIVATE FUNCTIONS *********************************************************/

TABLE_SEARCH_RESULT
NTAPI
RtlpFindGenericTableNodeOrParent(IN PRTL_GENERIC_TABLE Table,
                                 IN PVOID Buffer,
                                 OUT PRTL_SPLAY_LINKS *NodeOrParent)
{
    PRTL_SPLAY_LINKS CurrentNode, ChildNode;
    RTL_GENERIC_COMPARE_RESULTS Result;
    PRTL_GENERIC_COMPARE_ROUTINE Compare;

    /* Quick check to see if the table is empty */
    if (RtlIsGenericTableEmpty(Table))
    {
        *NodeOrParent = NULL;
        return TableEmptyTree;
    }

    /* Set the current node and double-check */
    CurrentNode = Table->TableRoot;
    if (CurrentNode == NULL)
    {
        *NodeOrParent = NULL;
        return TableEmptyTree;
    }

    Compare = Table->CompareRoutine;
    ASSERT(Compare != NULL);

    /* Start compare loop */
    for (;;)
    {
        /* Defensive check: bail if the current node unexpectedly disappears */
        if (CurrentNode == NULL)
        {
            *NodeOrParent = NULL;
            return TableEmptyTree;
        }

        Result = Compare(Table,
                         Buffer,
                         RtlpEntryFromLinks(CurrentNode)->UserData);

        if (Result == GenericLessThan)
        {
            /* We're less, check if this is the left child */
            ChildNode = RtlLeftChild(CurrentNode);
            if (ChildNode)
            {
                /* Continue searching from this node */
                CurrentNode = ChildNode;
            }
            else
            {
                /* Otherwise, the element isn't in this tree */
                *NodeOrParent = CurrentNode;
                return TableInsertAsLeft;
            }
        }
        else if (Result == GenericGreaterThan)
        {
            /* We're greater, check if this is the right child */
            ChildNode = RtlRightChild(CurrentNode);
            if (ChildNode)
            {
                /* Continue searching from this node */
                CurrentNode = ChildNode;
            }
            else
            {
                /* Otherwise, the element isn't in this tree */
                *NodeOrParent = CurrentNode;
                return TableInsertAsRight;
            }
        }
        else
        {
            /* We should've found the node */
            ASSERT(Result == GenericEqual);

            /* Return node found */
            *NodeOrParent = CurrentNode;
            return TableFoundNode;
        }
    }
}

/* SPLAY FUNCTIONS ***********************************************************/

/*
 * @implemented
 */
VOID
NTAPI
RtlInitializeGenericTable(IN PRTL_GENERIC_TABLE Table,
                          IN PRTL_GENERIC_COMPARE_ROUTINE CompareRoutine,
                          IN PRTL_GENERIC_ALLOCATE_ROUTINE AllocateRoutine,
                          IN PRTL_GENERIC_FREE_ROUTINE FreeRoutine,
                          IN PVOID TableContext)
{
    ASSERT(Table != NULL);
    ASSERT(CompareRoutine != NULL);
    ASSERT(AllocateRoutine != NULL);
    ASSERT(FreeRoutine != NULL);

    /* Initialize the table to default and passed values */
    InitializeListHead(&Table->InsertOrderList);
    Table->TableRoot = NULL;
    Table->NumberGenericTableElements = 0;
    Table->WhichOrderedElement = 0;
    Table->OrderedPointer = &Table->InsertOrderList;
    Table->CompareRoutine = CompareRoutine;
    Table->AllocateRoutine = AllocateRoutine;
    Table->FreeRoutine = FreeRoutine;
    Table->TableContext = TableContext;
}

/*
 * @implemented
 */
PVOID
NTAPI
RtlInsertElementGenericTable(IN PRTL_GENERIC_TABLE Table,
                             IN PVOID Buffer,
                             IN ULONG BufferSize,
                             OUT PBOOLEAN NewElement OPTIONAL)
{
    PRTL_SPLAY_LINKS NodeOrParent;
    TABLE_SEARCH_RESULT Result;

    /* Avoid the expensive lookup path when the tree is empty */
    if (RtlIsGenericTableEmpty(Table))
    {
        NodeOrParent = NULL;
        Result = TableEmptyTree;
    }
    else
    {
        /* Get the splay links and table search result immediately */
        Result = RtlpFindGenericTableNodeOrParent(Table, Buffer, &NodeOrParent);
    }

    /* Now call the routine to do the full insert */
    return RtlInsertElementGenericTableFull(Table,
                                            Buffer,
                                            BufferSize,
                                            NewElement,
                                            NodeOrParent,
                                            Result);
}

/*
 * @implemented
 */
PVOID
NTAPI
RtlInsertElementGenericTableFull(IN PRTL_GENERIC_TABLE Table,
                                 IN PVOID Buffer,
                                 IN ULONG BufferSize,
                                 OUT PBOOLEAN NewElement OPTIONAL,
                                 IN PVOID NodeOrParent,
                                 IN TABLE_SEARCH_RESULT SearchResult)
{
    PRTL_SPLAY_LINKS NewNode;

    /* Check if the entry wasn't already found */
    if (SearchResult != TableFoundNode)
    {
        CLONG AllocSize;

        /* We're doing an allocation, sanity check */
        ASSERT(Table->NumberGenericTableElements != (MAXULONG - 1));

        /* Safely compute allocation size; bail on overflow */
        if (!RtlpComputeAllocSize(BufferSize, &AllocSize))
        {
            if (NewElement) *NewElement = FALSE;
            return NULL;
        }

        /* Allocate a node */
        NewNode = Table->AllocateRoutine(Table, AllocSize);
        if (!NewNode)
        {
            /* No memory or other allocation error, fail */
            if (NewElement) *NewElement = FALSE;
            return NULL;
        }

        /* Initialize the new inserted element */
        RtlInitializeSplayLinks(NewNode);
        InsertTailList(&Table->InsertOrderList,
                       &RtlpEntryFromLinks(NewNode)->ListEntry);

        /* Increase element count */
        Table->NumberGenericTableElements++;

        /* Check where we should insert the entry */
        if (SearchResult == TableEmptyTree)
        {
            /* This is the new root node */
            Table->TableRoot = NewNode;
        }
        else if (SearchResult == TableInsertAsLeft)
        {
            /* Insert it left */
            RtlInsertAsLeftChild(NodeOrParent, NewNode);
        }
        else
        {
            /* Right node */
            RtlInsertAsRightChild(NodeOrParent, NewNode);
        }

        /* Copy user buffer (no-op if size == 0) */
        if (BufferSize != 0)
        {
            RtlCopyMemory(RtlpEntryFromLinks(NewNode)->UserData,
                          Buffer,
                          BufferSize);
        }
    }
    else
    {
        /* Return the node we already found */
        NewNode = NodeOrParent;
    }

    /* Splay the tree */
    Table->TableRoot = RtlSplay(NewNode);

    /* Return status */
    if (NewElement) *NewElement = (SearchResult != TableFoundNode);

    /* Return pointer to user data */
    return RtlpEntryFromLinks(NewNode)->UserData;
}

/*
 * @implemented
 */
BOOLEAN
NTAPI
RtlIsGenericTableEmpty(IN PRTL_GENERIC_TABLE Table)
{
    /* Prefer structure-internal invariant; also asserts in debug */
    ASSERT((Table->TableRoot == NULL) == (Table->NumberGenericTableElements == 0));
    return (Table->TableRoot == NULL);
}

/*
 * @implemented
 */
ULONG
NTAPI
RtlNumberGenericTableElements(IN PRTL_GENERIC_TABLE Table)
{
    /* Return the number of elements */
    return Table->NumberGenericTableElements;
}

/*
 * @implemented
 */
PVOID
NTAPI
RtlLookupElementGenericTable(IN PRTL_GENERIC_TABLE Table,
                             IN PVOID Buffer)
{
    PRTL_SPLAY_LINKS NodeOrParent;
    TABLE_SEARCH_RESULT Result;

    /* Call the full version */
    return RtlLookupElementGenericTableFull(Table,
                                            Buffer,
                                            (PVOID)&NodeOrParent,
                                            &Result);
}

/*
 * @implemented
 */
PVOID
NTAPI
RtlLookupElementGenericTableFull(IN PRTL_GENERIC_TABLE Table,
                                 IN PVOID Buffer,
                                 OUT PVOID *NodeOrParent,
                                 OUT TABLE_SEARCH_RESULT *SearchResult)
{
    /* Do the initial lookup */
    *SearchResult = RtlpFindGenericTableNodeOrParent(Table,
                                                     Buffer,
                                                     (PRTL_SPLAY_LINKS *)
                                                     NodeOrParent);

    /* Check if we found anything */
    if (*SearchResult != TableFoundNode)
    {
        /* Nothing found */
        return NULL;
    }

    /* Otherwise, splay the tree and return this entry */
    Table->TableRoot = RtlSplay(*NodeOrParent);
    return ((PTABLE_ENTRY_HEADER)*NodeOrParent)->UserData;
}

/*
 * @implemented
 */
BOOLEAN
NTAPI
RtlDeleteElementGenericTable(IN PRTL_GENERIC_TABLE Table,
                             IN PVOID Buffer)
{
    PRTL_SPLAY_LINKS NodeOrParent;
    TABLE_SEARCH_RESULT Result;

    /* Get the splay links and table search result immediately */
    Result = RtlpFindGenericTableNodeOrParent(Table, Buffer, &NodeOrParent);
    if (Result != TableFoundNode)
    {
        /* Nothing to delete */
        return FALSE;
    }

    /* Delete the entry */
    Table->TableRoot = RtlDelete(NodeOrParent);
    RemoveEntryList(&((PTABLE_ENTRY_HEADER)NodeOrParent)->ListEntry);

    /* Update accounting data */
    Table->NumberGenericTableElements--;
    Table->WhichOrderedElement = 0;
    Table->OrderedPointer = &Table->InsertOrderList;

    /* Free the entry */
    Table->FreeRoutine(Table, NodeOrParent);
    return TRUE;
}

/*
 * @implemented
 */
PVOID
NTAPI
RtlEnumerateGenericTable(IN PRTL_GENERIC_TABLE Table,
                         IN BOOLEAN Restart)
{
    PRTL_SPLAY_LINKS FoundNode;

    /* Check if the table is empty */
    if (RtlIsGenericTableEmpty(Table)) return NULL;

    /* Check if we have to restart */
    if (Restart)
    {
        /* Then find the leftmost element */
        FoundNode = Table->TableRoot;
        while (RtlLeftChild(FoundNode))
        {
            /* Get the left child */
            FoundNode = RtlLeftChild(FoundNode);
        }

        /* Splay it */
        _Analysis_assume_(FoundNode != NULL);
        Table->TableRoot = RtlSplay(FoundNode);
    }
    else
    {
        /* Otherwise, try using the real successor */
        FoundNode = RtlRealSuccessor(Table->TableRoot);
        if (FoundNode) Table->TableRoot = RtlSplay(FoundNode);
    }

    /* Check if we found the node and return it */
    return FoundNode ? ((PTABLE_ENTRY_HEADER)FoundNode)->UserData : NULL;
}

/*
 * @implemented
 */
PVOID
NTAPI
RtlEnumerateGenericTableWithoutSplaying(IN PRTL_GENERIC_TABLE Table,
                                        IN OUT PVOID *RestartKey)
{
    PRTL_SPLAY_LINKS FoundNode;

    /* Check if the table is empty */
    if (RtlIsGenericTableEmpty(Table)) return NULL;

    /* Check if we have to restart */
    if (!(*RestartKey))
    {
        /* Then find the leftmost element */
        FoundNode = Table->TableRoot;
        while (RtlLeftChild(FoundNode))
        {
            /* Get the left child */
            FoundNode = RtlLeftChild(FoundNode);
        }

        /* Save enumeration state but do not splay */
        *RestartKey = FoundNode;
    }
    else
    {
        /* Otherwise, try using the real successor */
        FoundNode = RtlRealSuccessor(*RestartKey);
        if (FoundNode) *RestartKey = FoundNode;
    }

    /* Check if we found the node and return it */
    return FoundNode ? ((PTABLE_ENTRY_HEADER)FoundNode)->UserData : NULL;
}

/*
 * @unimplemented
 * NOTE: This API belongs to the AVL variant; kept as a stub for drop-in parity.
 */
PVOID
NTAPI
RtlEnumerateGenericTableLikeADirectory(IN PRTL_AVL_TABLE Table,
                                       IN PRTL_AVL_MATCH_FUNCTION MatchFunction,
                                       IN PVOID MatchData,
                                       IN ULONG NextFlag,
                                       IN OUT PVOID *RestartKey,
                                       IN OUT PULONG DeleteCount,
                                       IN OUT PVOID Buffer)
{
    UNIMPLEMENTED;
    UNREFERENCED_PARAMETER(Table);
    UNREFERENCED_PARAMETER(MatchFunction);
    UNREFERENCED_PARAMETER(MatchData);
    UNREFERENCED_PARAMETER(NextFlag);
    UNREFERENCED_PARAMETER(RestartKey);
    UNREFERENCED_PARAMETER(DeleteCount);
    UNREFERENCED_PARAMETER(Buffer);
    return 0;
}

/*
 * @implemented
 */
PVOID
NTAPI
RtlGetElementGenericTable(IN PRTL_GENERIC_TABLE Table,
                          IN ULONG I)
{
    ULONG OrderedElement, ElementCount;
    PLIST_ENTRY OrderedNode;
    ULONG DeltaUp, DeltaDown;
    const ULONG TargetIndex = I + 1;
    ULONG WorkingIndex = TargetIndex;

    /* Setup current accounting data */
    OrderedNode = Table->OrderedPointer;
    OrderedElement = Table->WhichOrderedElement;
    ElementCount = Table->NumberGenericTableElements;

    /* Sanity checks */
    if ((I == MAXULONG) || (TargetIndex > ElementCount)) return NULL;

    /* Check if we already found the entry */
    if (TargetIndex == OrderedElement)
    {
        /* Return it */
        return CONTAINING_RECORD(OrderedNode,
                                 TABLE_ENTRY_HEADER,
                                 ListEntry)->UserData;
    }

    /* Now check if we're farther behind */
    if (OrderedElement > TargetIndex)
    {
        /* Find out if the distance is more then the half-way point */
        if (TargetIndex > (OrderedElement / 2))
        {
            /* Do the search backwards, since this takes fewer iterations */
            ULONG steps = OrderedElement - TargetIndex;
            while (steps--)
            {
                OrderedNode = OrderedNode->Blink;
            }
        }
        else
        {
            /* Follow the list directly instead */
            OrderedNode = &Table->InsertOrderList;
            while (WorkingIndex--)
            {
                OrderedNode = OrderedNode->Flink;
            }
        }
    }
    else
    {
        /* We are farther ahead, calculate distances */
        DeltaUp = TargetIndex - OrderedElement;
        DeltaDown = (ElementCount - TargetIndex) + 1;

        /* Choose direction with fewer iterations */
        if (DeltaUp <= DeltaDown)
        {
            while (DeltaUp--)
            {
                OrderedNode = OrderedNode->Flink;
            }
        }
        else
        {
            OrderedNode = &Table->InsertOrderList;
            while (DeltaDown--)
            {
                OrderedNode = OrderedNode->Blink;
            }
        }
    }

    /* Got the element, save it */
    Table->OrderedPointer = OrderedNode;
    Table->WhichOrderedElement = TargetIndex;

    /* Return the element */
    return CONTAINING_RECORD(OrderedNode,
                             TABLE_ENTRY_HEADER,
                             ListEntry)->UserData;
}

/* EOF */