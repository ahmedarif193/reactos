/*
 * PROJECT:     ReactOS DirectX Graphics Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Retained composition frames and context lookup
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#include <ntifs.h>
#include "composition_frame.h"

NTKERNELAPI VOID FASTCALL ExfAcquirePushLockExclusive(PEX_PUSH_LOCK Lock);
NTKERNELAPI VOID FASTCALL ExfReleasePushLockExclusive(PEX_PUSH_LOCK Lock);

#define DXGK_FRAME_COLLECTION_TAG 'cfMT'
#define DXGK_FRAME_TAG 'rfMT'
#define DXGK_FRAME_BLOCK_TAG 'bfMT'

typedef enum _DXGKP_FRAME_STATE
{
    DxgkpFramePending,
    DxgkpFrameConfirmed,
    DxgkpFrameRetired,
    DxgkpFrameDiscarded
} DXGKP_FRAME_STATE;

typedef struct _DXGKP_FRAME_BLOCK
{
    LIST_ENTRY Entry;
    ULONG Size;
    ULONG Count;
    UCHAR Data[ANYSIZE_ARRAY];
} DXGKP_FRAME_BLOCK;

typedef struct _DXGKP_COMPOSITION_FRAME
{
    DXGK_COMPOSITION_FRAME_HEADER Header;
    DXGK_COMPOSITION_FRAME_OWNER *Owner;
    ULONGLONG Identifier;
    DXGKP_FRAME_STATE State;
    DXGK_COMPOSITION_BATCH_LIST *Batches;
    LIST_ENTRY LegacyBlocks;
    PLIST_ENTRY NextLegacyBlock;
    LIST_ENTRY Tokens[DxgkFrameTokenKindCount];
    ULONG SurfacePhase;
    PLIST_ENTRY SurfaceCursor;
    ULONG SurfaceIndex;
} DXGKP_COMPOSITION_FRAME;

static DXGKP_COMPOSITION_FRAME *
DxgkpCompositionFrame(DXGK_COMPOSITION_FRAME_INTERFACE *Interface)
{
    return CONTAINING_RECORD(Interface, DXGKP_COMPOSITION_FRAME, Header.Interface);
}

static VOID
DxgkpResetSurfaceIteration(DXGKP_COMPOSITION_FRAME *Frame)
{
    Frame->SurfacePhase = DxgkFramePresentToken;
    Frame->SurfaceCursor = NULL;
    Frame->SurfaceIndex = 0;
}

static VOID
DxgkpReleaseLegacyBlocks(DXGKP_COMPOSITION_FRAME *Frame)
{
    Frame->NextLegacyBlock = &Frame->LegacyBlocks;
    while (!IsListEmpty(&Frame->LegacyBlocks))
    {
        DXGKP_FRAME_BLOCK *Block = CONTAINING_RECORD(RemoveHeadList(&Frame->LegacyBlocks), DXGKP_FRAME_BLOCK, Entry);
        ExFreePoolWithTag(Block, DXGK_FRAME_BLOCK_TAG);
    }
}

static VOID
DxgkpFinishFrameTokens(DXGKP_COMPOSITION_FRAME *Frame, DXGK_COMPOSITION_FRAME_TOKEN_KIND Kind, BOOLEAN Confirm)
{
    while (!IsListEmpty(&Frame->Tokens[Kind]))
    {
        DXGK_COMPOSITION_FRAME_TOKEN *Token = CONTAINING_RECORD(RemoveHeadList(&Frame->Tokens[Kind]), DXGK_COMPOSITION_FRAME_TOKEN, FrameEntry);

        InitializeListHead(&Token->FrameEntry);
        if (Confirm)
            Token->Operations->Confirm(Token, Frame->Identifier);
        else
            Token->Operations->Discard(Token, Frame->Identifier);
        Token->Operations->Destroy(Token);
    }
}

static LONG NTAPI
DxgkpCompositionFrameAddRef(DXGK_COMPOSITION_FRAME_INTERFACE *Interface)
{
    return InterlockedIncrement(&DxgkpCompositionFrame(Interface)->Header.References);
}

static DXGK_COMPOSITION_BATCH_LIST *NTAPI
DxgkpCompositionFrameGetBatches(DXGK_COMPOSITION_FRAME_INTERFACE *Interface)
{
    DXGKP_COMPOSITION_FRAME *Frame = DxgkpCompositionFrame(Interface);
    DXGK_COMPOSITION_BATCH_LIST *Batches = Frame->Batches;

    Frame->Batches = NULL;
    return Batches;
}

static VOID NTAPI
DxgkpCompositionFrameDiscard(DXGK_COMPOSITION_FRAME_INTERFACE *Interface)
{
    DXGKP_COMPOSITION_FRAME *Frame = DxgkpCompositionFrame(Interface);
    DXGK_COMPOSITION_BATCH_LIST *Batch = DxgkpCompositionFrameGetBatches(Interface);

    Frame->State = DxgkpFrameDiscarded;
    while (Batch != NULL)
    {
        DXGK_COMPOSITION_BATCH_LIST *Next = Batch->Vtbl->GetNext(Batch);
        Batch->Vtbl->ReturnToApplication(Batch, TRUE);
        Batch = Next;
    }
    DxgkpReleaseLegacyBlocks(Frame);
    DxgkpResetSurfaceIteration(Frame);
    DxgkpFinishFrameTokens(Frame, DxgkFramePresentToken, FALSE);
    DxgkpFinishFrameTokens(Frame, DxgkFrameSurfaceToken, FALSE);
    DxgkpFinishFrameTokens(Frame, DxgkFrameBindToken, FALSE);
}

static LONG NTAPI
DxgkpCompositionFrameRelease(DXGK_COMPOSITION_FRAME_INTERFACE *Interface)
{
    DXGKP_COMPOSITION_FRAME *Frame = DxgkpCompositionFrame(Interface);
    LONG References = InterlockedDecrement(&Frame->Header.References);

    ASSERT(References >= 0);
    if (References == 0)
    {
        DXGK_COMPOSITION_FRAME_OWNER *Owner = Frame->Owner;

        ASSERT(IsListEmpty(&Frame->Header.CollectionEntry));
        /* Retirement is not destruction. Unfetched batches and retained
         * tokens must still be returned when a retired frame loses its owner. */
        if (Frame->State != DxgkpFrameDiscarded)
            DxgkpCompositionFrameDiscard(Interface);
        ExFreePoolWithTag(Frame, DXGK_FRAME_TAG);
        Owner->Dereference(Owner->Context);
    }
    return References;
}

static VOID NTAPI
DxgkpCompositionFrameConfirm(DXGK_COMPOSITION_FRAME_INTERFACE *Interface)
{
    DXGKP_COMPOSITION_FRAME *Frame = DxgkpCompositionFrame(Interface);
    PLIST_ENTRY Entry;

    Frame->State = DxgkpFrameConfirmed;
    for (Entry = Frame->Tokens[DxgkFramePresentToken].Flink; Entry != &Frame->Tokens[DxgkFramePresentToken]; Entry = Entry->Flink)
    {
        DXGK_COMPOSITION_FRAME_TOKEN *Token = CONTAINING_RECORD(Entry, DXGK_COMPOSITION_FRAME_TOKEN, FrameEntry);
        Token->Operations->Confirm(Token, Frame->Identifier);
    }
    /* Native surface-update tokens are discarded after confirmation; present
     * tokens remain for display retirement. Bind tokens finish at this frame. */
    DxgkpResetSurfaceIteration(Frame);
    DxgkpFinishFrameTokens(Frame, DxgkFrameSurfaceToken, FALSE);
    DxgkpFinishFrameTokens(Frame, DxgkFrameBindToken, TRUE);
}

static VOID NTAPI
DxgkpCompositionFrameRetire(DXGK_COMPOSITION_FRAME_INTERFACE *Interface, const struct DXGI_FRAME_STATISTICS *Composition, const struct DXGI_FRAME_STATISTICS *Application)
{
    DXGKP_COMPOSITION_FRAME *Frame = DxgkpCompositionFrame(Interface);
    PLIST_ENTRY Entry;

    Frame->State = DxgkpFrameRetired;
    for (Entry = Frame->Tokens[DxgkFramePresentToken].Flink; Entry != &Frame->Tokens[DxgkFramePresentToken]; Entry = Entry->Flink)
    {
        DXGK_COMPOSITION_FRAME_TOKEN *Token = CONTAINING_RECORD(Entry, DXGK_COMPOSITION_FRAME_TOKEN, FrameEntry);
        Token->Operations->Retire(Token, Composition, Application);
    }
    DxgkpReleaseLegacyBlocks(Frame);
}

static BOOLEAN NTAPI
DxgkpCompositionFrameGetLegacyBlock(DXGK_COMPOSITION_FRAME_INTERFACE *Interface, const UCHAR **Data, ULONG *Size, ULONG *Count)
{
    DXGKP_COMPOSITION_FRAME *Frame = DxgkpCompositionFrame(Interface);
    DXGKP_FRAME_BLOCK *Block;

    if (Frame->NextLegacyBlock == &Frame->LegacyBlocks)
    {
        *Data = NULL;
        *Size = 0;
        *Count = 0;
        return FALSE;
    }
    Block = CONTAINING_RECORD(Frame->NextLegacyBlock, DXGKP_FRAME_BLOCK, Entry);
    *Data = Block->Data;
    *Size = Block->Size;
    *Count = Block->Count;
    Frame->NextLegacyBlock = Block->Entry.Flink;
    /* FALSE can accompany a nonempty final block: it means no MORE blocks. */
    return Frame->NextLegacyBlock != &Frame->LegacyBlocks;
}

static BOOLEAN NTAPI
DxgkpCompositionFrameGetSurfaceUpdates(DXGK_COMPOSITION_FRAME_INTERFACE *Interface, struct CSM_SURFACE_UPDATE_ *Updates, ULONG Capacity, ULONG *Count)
{
    DXGKP_COMPOSITION_FRAME *Frame = DxgkpCompositionFrame(Interface);
    UCHAR *Output = (UCHAR *)Updates;

    *Count = 0;
    for (;;)
    {
        PLIST_ENTRY Head = &Frame->Tokens[Frame->SurfacePhase];
        DXGK_COMPOSITION_FRAME_TOKEN *Token;

        if (Frame->SurfaceCursor == NULL)
            Frame->SurfaceCursor = Head->Flink;
        if (Frame->SurfaceCursor == Head)
        {
            if (Frame->SurfacePhase == DxgkFrameSurfaceToken)
            {
                DxgkpResetSurfaceIteration(Frame);
                return FALSE;
            }
            Frame->SurfacePhase = DxgkFrameSurfaceToken;
            Frame->SurfaceCursor = NULL;
            continue;
        }
        if (Capacity == 0)
            return TRUE;

        Token = CONTAINING_RECORD(Frame->SurfaceCursor, DXGK_COMPOSITION_FRAME_TOKEN, FrameEntry);
        if (Frame->SurfaceIndex < Token->SurfaceUpdateCount)
        {
            if (Token->Operations->GetSurfaceUpdate(Token, Frame->Identifier, Frame->SurfaceIndex, (struct CSM_SURFACE_UPDATE_ *)Output))
            {
                Output += DXGK_COMPOSITION_SURFACE_UPDATE_SIZE;
                --Capacity;
                ++*Count;
            }
            ++Frame->SurfaceIndex;
        }
        if (Frame->SurfaceIndex == Token->SurfaceUpdateCount)
        {
            Frame->SurfaceCursor = Frame->SurfaceCursor->Flink;
            Frame->SurfaceIndex = 0;
        }
    }
}

static VOID NTAPI
DxgkpCompositionFrameSetBatches(DXGK_COMPOSITION_FRAME_INTERFACE *Interface, DXGK_COMPOSITION_BATCH_LIST *Batches)
{
    DXGKP_COMPOSITION_FRAME *Frame = DxgkpCompositionFrame(Interface);

    ASSERT(Frame->Batches == NULL);
    Frame->Batches = Batches;
}

static ULONGLONG NTAPI
DxgkpCompositionFrameGetId(const DXGK_COMPOSITION_FRAME_INTERFACE *Interface)
{
    return CONTAINING_RECORD(Interface, DXGKP_COMPOSITION_FRAME, Header.Interface)->Identifier;
}

static const DXGK_COMPOSITION_FRAME_VTABLE DxgkpCompositionFrameVtbl =
{
    DxgkpCompositionFrameAddRef,
    DxgkpCompositionFrameRelease,
    DxgkpCompositionFrameConfirm,
    DxgkpCompositionFrameRetire,
    DxgkpCompositionFrameDiscard,
    DxgkpCompositionFrameGetLegacyBlock,
    DxgkpCompositionFrameGetSurfaceUpdates,
    DxgkpCompositionFrameSetBatches,
    DxgkpCompositionFrameGetBatches,
    DxgkpCompositionFrameGetId
};

NTSTATUS
DxgkpCreateCompositionFrame(DXGK_COMPOSITION_FRAME_OWNER *Owner, DXGK_COMPOSITION_FRAME_INTERFACE **Result)
{
    DXGKP_COMPOSITION_FRAME *Frame;
    ULONG Index;

    Frame = ExAllocatePoolWithTag(PagedPool, sizeof(*Frame), DXGK_FRAME_TAG);
    if (Frame == NULL)
        return STATUS_NO_MEMORY;
    RtlZeroMemory(Frame, sizeof(*Frame));
    Frame->Header.Interface.Vtbl = &DxgkpCompositionFrameVtbl;
    Frame->Header.References = 1;
    InitializeListHead(&Frame->Header.CollectionEntry);
    InitializeListHead(&Frame->LegacyBlocks);
    Frame->NextLegacyBlock = &Frame->LegacyBlocks;
    for (Index = 0; Index < DxgkFrameTokenKindCount; ++Index)
        InitializeListHead(&Frame->Tokens[Index]);
    if (++Owner->LastFrameId == 0)
        ++Owner->LastFrameId;
    Frame->Identifier = Owner->LastFrameId;
    Frame->Owner = Owner;
    Owner->Reference(Owner->Context);
    *Result = &Frame->Header.Interface;
    return STATUS_SUCCESS;
}

VOID
DxgkpAddCompositionFrameToken(DXGK_COMPOSITION_FRAME_INTERFACE *Interface, DXGK_COMPOSITION_FRAME_TOKEN_KIND Kind, DXGK_COMPOSITION_FRAME_TOKEN *Token)
{
    DXGKP_COMPOSITION_FRAME *Frame = DxgkpCompositionFrame(Interface);

    ASSERT((ULONG)Kind < DxgkFrameTokenKindCount);
    ASSERT(Frame->State == DxgkpFramePending);
    ASSERT(IsListEmpty(&Token->FrameEntry));
    ASSERT(Token->Operations != NULL);
    ASSERT(Token->Operations->Discard && Token->Operations->Destroy);
    ASSERT(Kind == DxgkFrameSurfaceToken || Token->Operations->Confirm != NULL);
    ASSERT(Kind != DxgkFramePresentToken || Token->Operations->Retire != NULL);
    ASSERT(Token->SurfaceUpdateCount == 0 || (Kind != DxgkFrameBindToken && Token->Operations->GetSurfaceUpdate != NULL));
    InsertTailList(&Frame->Tokens[Kind], &Token->FrameEntry);
}

NTSTATUS
DxgkpAppendCompositionLegacyBlock(DXGK_COMPOSITION_FRAME_INTERFACE *Interface, const UCHAR *Data, ULONG Size, ULONG Count)
{
    DXGKP_COMPOSITION_FRAME *Frame = DxgkpCompositionFrame(Interface);
    DXGKP_FRAME_BLOCK *Block;

    ASSERT(Frame->State == DxgkpFramePending);
    if ((SIZE_T)Size > (SIZE_T)-1 - FIELD_OFFSET(DXGKP_FRAME_BLOCK, Data))
        return STATUS_NO_MEMORY;
    Block = ExAllocatePoolWithTag(PagedPool, FIELD_OFFSET(DXGKP_FRAME_BLOCK, Data) + Size, DXGK_FRAME_BLOCK_TAG);
    if (Block == NULL)
        return STATUS_NO_MEMORY;
    Block->Size = Size;
    Block->Count = Count;
    if (Size != 0)
        RtlCopyMemory(Block->Data, Data, Size);
    InsertTailList(&Frame->LegacyBlocks, &Block->Entry);
    if (Frame->NextLegacyBlock == &Frame->LegacyBlocks)
        Frame->NextLegacyBlock = &Block->Entry;
    return STATUS_SUCCESS;
}

typedef struct _DXGKP_FRAME_COLLECTION
{
    DXGK_COMPOSITION_FRAME_COLLECTION Interface;
    volatile LONG References;
    EX_PUSH_LOCK Lock;
    LIST_ENTRY Frames;
} DXGKP_FRAME_COLLECTION;

static VOID
DxgkpLockFrameCollection(DXGKP_FRAME_COLLECTION *Collection)
{
    KeEnterCriticalRegion();
    ExfAcquirePushLockExclusive(&Collection->Lock);
}

static VOID
DxgkpUnlockFrameCollection(DXGKP_FRAME_COLLECTION *Collection)
{
    ExfReleasePushLockExclusive(&Collection->Lock);
    KeLeaveCriticalRegion();
}

static DXGK_COMPOSITION_FRAME_INTERFACE *
DxgkpFrameFromEntry(PLIST_ENTRY Entry)
{
    return &CONTAINING_RECORD(Entry, DXGK_COMPOSITION_FRAME_HEADER, CollectionEntry)->Interface;
}

static LONG NTAPI
DxgkpFrameCollectionAddRef(DXGK_COMPOSITION_FRAME_COLLECTION *Interface)
{
    DXGKP_FRAME_COLLECTION *Collection = CONTAINING_RECORD(Interface, DXGKP_FRAME_COLLECTION, Interface);
    return InterlockedIncrement(&Collection->References);
}

static VOID
DxgkpReleaseFrameList(PLIST_ENTRY Frames, BOOLEAN Discard)
{
    while (!IsListEmpty(Frames))
    {
        PLIST_ENTRY Entry = RemoveHeadList(Frames);
        DXGK_COMPOSITION_FRAME_INTERFACE *Frame = DxgkpFrameFromEntry(Entry);

        InitializeListHead(Entry);
        if (Discard)
            Frame->Vtbl->Discard(Frame);
        Frame->Vtbl->Release(Frame);
    }
}

static VOID NTAPI
DxgkpFrameCollectionDiscardAll(DXGK_COMPOSITION_FRAME_COLLECTION *Interface)
{
    DXGKP_FRAME_COLLECTION *Collection = CONTAINING_RECORD(Interface, DXGKP_FRAME_COLLECTION, Interface);
    LIST_ENTRY Retired;

    InitializeListHead(&Retired);
    DxgkpLockFrameCollection(Collection);
    while (!IsListEmpty(&Collection->Frames))
        InsertTailList(&Retired, RemoveTailList(&Collection->Frames));
    DxgkpUnlockFrameCollection(Collection);

    /* A frame's discard/destructor can return batches to win32k. Do not call
     * back into that owner while holding the collection lock. */
    DxgkpReleaseFrameList(&Retired, TRUE);
}

static LONG NTAPI
DxgkpFrameCollectionRelease(DXGK_COMPOSITION_FRAME_COLLECTION *Interface)
{
    DXGKP_FRAME_COLLECTION *Collection = CONTAINING_RECORD(Interface, DXGKP_FRAME_COLLECTION, Interface);
    LONG References = InterlockedDecrement(&Collection->References);

    ASSERT(References >= 0);
    if (References == 0)
    {
        DxgkpFrameCollectionDiscardAll(Interface);
        ExFreePoolWithTag(Collection, DXGK_FRAME_COLLECTION_TAG);
    }
    return References;
}

static VOID NTAPI
DxgkpFrameCollectionAdd(DXGK_COMPOSITION_FRAME_COLLECTION *Interface, DXGK_COMPOSITION_FRAME_INTERFACE *Frame)
{
    DXGKP_FRAME_COLLECTION *Collection = CONTAINING_RECORD(Interface, DXGKP_FRAME_COLLECTION, Interface);
    DXGK_COMPOSITION_FRAME_HEADER *Header = CONTAINING_RECORD(Frame, DXGK_COMPOSITION_FRAME_HEADER, Interface);

    DxgkpLockFrameCollection(Collection);
    ASSERT(IsListEmpty(&Header->CollectionEntry));
    Frame->Vtbl->AddRef(Frame);
    InsertHeadList(&Collection->Frames, &Header->CollectionEntry);
    DxgkpUnlockFrameCollection(Collection);
}

static NTSTATUS NTAPI
DxgkpFrameCollectionFind(DXGK_COMPOSITION_FRAME_COLLECTION *Interface, ULONGLONG Identifier, DXGK_COMPOSITION_FRAME_INTERFACE **Result)
{
    DXGKP_FRAME_COLLECTION *Collection = CONTAINING_RECORD(Interface, DXGKP_FRAME_COLLECTION, Interface);
    PLIST_ENTRY Entry;

    *Result = NULL;
    DxgkpLockFrameCollection(Collection);
    for (Entry = Collection->Frames.Blink; Entry != &Collection->Frames; Entry = Entry->Blink)
    {
        DXGK_COMPOSITION_FRAME_INTERFACE *Frame = DxgkpFrameFromEntry(Entry);
        if (Frame->Vtbl->GetFrameId(Frame) == Identifier)
        {
            Frame->Vtbl->AddRef(Frame);
            *Result = Frame;
            break;
        }
    }
    DxgkpUnlockFrameCollection(Collection);
    return *Result != NULL ? STATUS_SUCCESS : STATUS_NOT_FOUND;
}

static NTSTATUS NTAPI
DxgkpFrameCollectionRemove(DXGK_COMPOSITION_FRAME_COLLECTION *Interface, ULONGLONG Identifier)
{
    DXGKP_FRAME_COLLECTION *Collection = CONTAINING_RECORD(Interface, DXGKP_FRAME_COLLECTION, Interface);
    DXGK_COMPOSITION_FRAME_INTERFACE *Removed = NULL;
    PLIST_ENTRY Entry;

    DxgkpLockFrameCollection(Collection);
    for (Entry = Collection->Frames.Blink; Entry != &Collection->Frames; Entry = Entry->Blink)
    {
        DXGK_COMPOSITION_FRAME_INTERFACE *Frame = DxgkpFrameFromEntry(Entry);
        if (Frame->Vtbl->GetFrameId(Frame) == Identifier)
        {
            RemoveEntryList(Entry);
            InitializeListHead(Entry);
            Removed = Frame;
            break;
        }
    }
    DxgkpUnlockFrameCollection(Collection);
    if (Removed == NULL)
        return STATUS_NOT_FOUND;
    Removed->Vtbl->Release(Removed);
    return STATUS_SUCCESS;
}

static VOID NTAPI
DxgkpFrameCollectionDiscardPrevious(DXGK_COMPOSITION_FRAME_COLLECTION *Interface, ULONGLONG Identifier)
{
    DXGKP_FRAME_COLLECTION *Collection = CONTAINING_RECORD(Interface, DXGKP_FRAME_COLLECTION, Interface);
    LIST_ENTRY Retired;

    InitializeListHead(&Retired);
    DxgkpLockFrameCollection(Collection);
    while (!IsListEmpty(&Collection->Frames))
    {
        PLIST_ENTRY Entry = Collection->Frames.Blink;
        DXGK_COMPOSITION_FRAME_INTERFACE *Frame = DxgkpFrameFromEntry(Entry);
        if (Frame->Vtbl->GetFrameId(Frame) >= Identifier)
            break;
        RemoveEntryList(Entry);
        InsertTailList(&Retired, Entry);
    }
    DxgkpUnlockFrameCollection(Collection);

    /* Removing history drops its ownership reference. A lookup may still
     * hold that frame; final cleanup belongs to the frame's last release. */
    DxgkpReleaseFrameList(&Retired, FALSE);
}

static const DXGK_COMPOSITION_FRAME_COLLECTION_VTABLE DxgkpFrameCollectionVtbl =
{
    DxgkpFrameCollectionAddRef,
    DxgkpFrameCollectionRelease,
    DxgkpFrameCollectionAdd,
    DxgkpFrameCollectionRemove,
    DxgkpFrameCollectionFind,
    DxgkpFrameCollectionDiscardPrevious,
    DxgkpFrameCollectionDiscardAll
};

NTSTATUS
DxgkpCreateCompositionFrameCollection(DXGK_COMPOSITION_FRAME_COLLECTION **Result)
{
    DXGKP_FRAME_COLLECTION *Collection;

    Collection = ExAllocatePoolWithTag(PagedPool, sizeof(*Collection), DXGK_FRAME_COLLECTION_TAG);
    if (Collection == NULL)
        return STATUS_NO_MEMORY;
    RtlZeroMemory(Collection, sizeof(*Collection));
    Collection->Interface.Vtbl = &DxgkpFrameCollectionVtbl;
    Collection->References = 1;
    InitializeListHead(&Collection->Frames);
    *Result = &Collection->Interface;
    return STATUS_SUCCESS;
}
