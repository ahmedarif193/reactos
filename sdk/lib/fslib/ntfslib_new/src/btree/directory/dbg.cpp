/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     NTFS filesystem driver
 * COPYRIGHT:   Copyright 2024 Carl Bialorucki <carl.bialorucki@reactos.org>
 *              Copyright 2024 Justin Miller <justin.miller@reactos.org>
 */

#include "ntfslib_new.h"
#include "ntfslib_new_internal.h"

/* Intentionally retained as debugger-invoked bringup tooling. */

static
void
DumpBTreeNode(PBTreeNode Node,
              ULONG Depth);
static
void
DumpBTreeKey(PBTreeKey Key,
             ULONG Depth)
{
    ULONG i;
    for (i = 0; i < Depth; i++)
        DbgPrint("    ");

    if (!(Key->Entry->Flags & INDEX_ENTRY_END))
    {
        UNICODE_STRING FileName;
        FileName = NtfsMakeCountedUnicodeString(
            GetFileName(Key)->Name,
            GetFileName(Key)->NameLength * sizeof(WCHAR));
        DbgPrint("|===%wZ (MFT ID: %ld, Flags: 0x%X)\n",
                 &FileName,
                 GetFRNFromFileRef(FileRef(Key)),
                 Key->Flags);
    }
    else
    {
        DbgPrint("L===(Dummy Key)\n");
    }

    // Is there a child node?
    if (Key->Entry->Flags & INDEX_ENTRY_NODE)
    {
        ASSERT(Key->ChildNode);
        DumpBTreeNode(Key->ChildNode, (Depth + 1));
    }
}

static
void
DumpBTreeNode(PBTreeNode Node,
              ULONG Depth)
{
    PBTreeKey CurrentKey;
    CurrentKey = Node->FirstKey;
    while(CurrentKey)
    {
        DumpBTreeKey(CurrentKey, Depth);
        CurrentKey = CurrentKey->NextKey;
    }
}

void
Directory::DumpFileTree()
{
    // Print header
    DbgPrint("Index Root\n");
    DumpBTreeNode(RootNode, 0);
}
