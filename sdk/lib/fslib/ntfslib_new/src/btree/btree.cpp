/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     NTFS filesystem driver
 * COPYRIGHT:   Copyright 2024 Carl Bialorucki <carl.bialorucki@reactos.org>
 *              Copyright 2024 Justin Miller <justin.miller@reactos.org>
 */

#include "ntfslib_new.h"
#include "ntfslib_new_internal.h"

static void
DestroyBTreeKey(PBTreeKey Key)
{
    if (Key->Entry &&
        !(Key->Flags & BTREE_KEY_BORROWED_ENTRY))
    {
        NtfsFreePool(Key->Entry);
    }

    if (Key->ChildNode)
        NtfsDestroyBTreeNode(Key->ChildNode);

    if (!(Key->Flags & BTREE_KEY_ARENA_OBJECT))
        delete Key;
}

void
NtfsDestroyBTreeNode(_In_opt_ PBTreeNode Node)
{
    if (!Node)
        return;

    PBTreeKey NextKey = NULL;
    PBTreeKey CurrentKey = Node->FirstKey;

    while (CurrentKey)
    {
        NextKey = CurrentKey->NextKey;
        DestroyBTreeKey(CurrentKey);
        CurrentKey = NextKey;
    }

    ASSERT(NextKey == NULL);
    delete Node;
}

BTree::~BTree()
{
    // DestroyBTreeNode() also handles a root without keys.
    if (RootNode)
        NtfsDestroyBTreeNode(RootNode);
}

NTSTATUS
BTree::ResetCurrentKey()
{
    CurrentKey = GetFirstKey(RootNode);
    return STATUS_SUCCESS;
}
