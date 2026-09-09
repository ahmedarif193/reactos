/*
 * PROJECT:     ReactOS DirectX Graphics Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     Kernel composition frame ownership
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif193@gmail.com>
 */

#pragma once

#include <ntddk.h>
#include <reactos/dcompbatch.h>

typedef struct _DXGK_COMPOSITION_FRAME_INTERFACE DXGK_COMPOSITION_FRAME_INTERFACE;
typedef struct _DXGK_COMPOSITION_FRAME_COLLECTION DXGK_COMPOSITION_FRAME_COLLECTION;
struct DXGI_FRAME_STATISTICS;
struct CSM_SURFACE_UPDATE_;

/* ICompositionFrame, used between the graphics kernel and win32k. */
typedef struct _DXGK_COMPOSITION_FRAME_VTABLE
{
    LONG (NTAPI *AddRef)(DXGK_COMPOSITION_FRAME_INTERFACE *Frame);
    LONG (NTAPI *Release)(DXGK_COMPOSITION_FRAME_INTERFACE *Frame);
    VOID (NTAPI *Confirm)(DXGK_COMPOSITION_FRAME_INTERFACE *Frame);
    VOID (NTAPI *Retire)(DXGK_COMPOSITION_FRAME_INTERFACE *Frame, const struct DXGI_FRAME_STATISTICS *Composition, const struct DXGI_FRAME_STATISTICS *Application);
    VOID (NTAPI *Discard)(DXGK_COMPOSITION_FRAME_INTERFACE *Frame);
    BOOLEAN (NTAPI *GetNextLegacyTokenBlock)(DXGK_COMPOSITION_FRAME_INTERFACE *Frame, const UCHAR **Block, ULONG *Size, ULONG *Count);
    BOOLEAN (NTAPI *GetSurfaceUpdates)(DXGK_COMPOSITION_FRAME_INTERFACE *Frame, struct CSM_SURFACE_UPDATE_ *Updates, ULONG Capacity, ULONG *Count);
    VOID (NTAPI *SetBatches)(DXGK_COMPOSITION_FRAME_INTERFACE *Frame, DXGK_COMPOSITION_BATCH_LIST *Batches);
    DXGK_COMPOSITION_BATCH_LIST *(NTAPI *GetBatches)(DXGK_COMPOSITION_FRAME_INTERFACE *Frame);
    ULONGLONG (NTAPI *GetFrameId)(const DXGK_COMPOSITION_FRAME_INTERFACE *Frame);
} DXGK_COMPOSITION_FRAME_VTABLE;

struct _DXGK_COMPOSITION_FRAME_INTERFACE
{
    const DXGK_COMPOSITION_FRAME_VTABLE *Vtbl;
};

/* All kernel frame implementations embed this header first. The collection
 * link is intrusive: publishing an allocated frame cannot fail allocation. */
typedef struct _DXGK_COMPOSITION_FRAME_HEADER
{
    DXGK_COMPOSITION_FRAME_INTERFACE Interface;
    volatile LONG References;
    LIST_ENTRY CollectionEntry;
} DXGK_COMPOSITION_FRAME_HEADER;

/* ICompositionFrameCollection. Finding a frame returns a reference; removing
 * it releases the collection's reference. Neither operation confirms a frame. */
typedef struct _DXGK_COMPOSITION_FRAME_COLLECTION_VTABLE
{
    LONG (NTAPI *AddRef)(DXGK_COMPOSITION_FRAME_COLLECTION *Collection);
    LONG (NTAPI *Release)(DXGK_COMPOSITION_FRAME_COLLECTION *Collection);
    VOID (NTAPI *AddCompositionFrame)(DXGK_COMPOSITION_FRAME_COLLECTION *Collection, DXGK_COMPOSITION_FRAME_INTERFACE *Frame);
    NTSTATUS (NTAPI *RemoveCompositionFrame)(DXGK_COMPOSITION_FRAME_COLLECTION *Collection, ULONGLONG Identifier);
    NTSTATUS (NTAPI *FindCompositionFrame)(DXGK_COMPOSITION_FRAME_COLLECTION *Collection, ULONGLONG Identifier, DXGK_COMPOSITION_FRAME_INTERFACE **Frame);
    VOID (NTAPI *DiscardPreviousFrames)(DXGK_COMPOSITION_FRAME_COLLECTION *Collection, ULONGLONG Identifier);
    VOID (NTAPI *DiscardAllCompositionFrames)(DXGK_COMPOSITION_FRAME_COLLECTION *Collection);
} DXGK_COMPOSITION_FRAME_COLLECTION_VTABLE;

struct _DXGK_COMPOSITION_FRAME_COLLECTION
{
    const DXGK_COMPOSITION_FRAME_COLLECTION_VTABLE *Vtbl;
};

NTSTATUS
DxgkpCreateCompositionFrameCollection(DXGK_COMPOSITION_FRAME_COLLECTION **Collection);

/* Internal token storage, not the native ITokenManager/IToken vtables. The
 * session owner serializes frame creation and all frame-content operations;
 * only reference counts and collection membership synchronize themselves.
 * Tokens transfer to a frame once and stay alive until their phase finishes. */
typedef struct _DXGK_COMPOSITION_FRAME_OWNER
{
    PVOID Context;
    VOID (NTAPI *Reference)(PVOID Context);
    VOID (NTAPI *Dereference)(PVOID Context);
    ULONGLONG LastFrameId;
} DXGK_COMPOSITION_FRAME_OWNER;

typedef enum _DXGK_COMPOSITION_FRAME_TOKEN_KIND
{
    DxgkFramePresentToken,
    DxgkFrameSurfaceToken,
    DxgkFrameBindToken,
    DxgkFrameTokenKindCount
} DXGK_COMPOSITION_FRAME_TOKEN_KIND;

typedef struct _DXGK_COMPOSITION_FRAME_TOKEN DXGK_COMPOSITION_FRAME_TOKEN;
typedef struct _DXGK_COMPOSITION_FRAME_TOKEN_OPERATIONS
{
    VOID (NTAPI *Confirm)(DXGK_COMPOSITION_FRAME_TOKEN *Token, ULONGLONG FrameId);
    VOID (NTAPI *Retire)(DXGK_COMPOSITION_FRAME_TOKEN *Token, const struct DXGI_FRAME_STATISTICS *Composition, const struct DXGI_FRAME_STATISTICS *Application);
    VOID (NTAPI *Discard)(DXGK_COMPOSITION_FRAME_TOKEN *Token, ULONGLONG FrameId);
    VOID (NTAPI *Destroy)(DXGK_COMPOSITION_FRAME_TOKEN *Token);
    /* FALSE skips an ineligible/already exported update. The surface owner
     * must deduplicate by FrameId: a completed scan can be repeated. */
    BOOLEAN (NTAPI *GetSurfaceUpdate)(DXGK_COMPOSITION_FRAME_TOKEN *Token, ULONGLONG FrameId, ULONG Index, struct CSM_SURFACE_UPDATE_ *Update);
} DXGK_COMPOSITION_FRAME_TOKEN_OPERATIONS;

struct _DXGK_COMPOSITION_FRAME_TOKEN
{
    LIST_ENTRY FrameEntry;
    const DXGK_COMPOSITION_FRAME_TOKEN_OPERATIONS *Operations;
    ULONG SurfaceUpdateCount;
};

/* The matching kernel advances CSM_SURFACE_UPDATE_ output by 0x178 bytes.
 * Record fields remain the responsibility of the concrete surface provider. */
#define DXGK_COMPOSITION_SURFACE_UPDATE_SIZE 0x178

NTSTATUS
DxgkpCreateCompositionFrame(DXGK_COMPOSITION_FRAME_OWNER *Owner, DXGK_COMPOSITION_FRAME_INTERFACE **Frame);

VOID
DxgkpAddCompositionFrameToken(DXGK_COMPOSITION_FRAME_INTERFACE *Frame, DXGK_COMPOSITION_FRAME_TOKEN_KIND Kind, DXGK_COMPOSITION_FRAME_TOKEN *Token);

NTSTATUS
DxgkpAppendCompositionLegacyBlock(DXGK_COMPOSITION_FRAME_INTERFACE *Frame, const UCHAR *Data, ULONG Size, ULONG Count);

C_ASSERT(FIELD_OFFSET(DXGK_COMPOSITION_FRAME_VTABLE, GetBatches) == 8 * sizeof(PVOID));
C_ASSERT(FIELD_OFFSET(DXGK_COMPOSITION_FRAME_VTABLE, GetFrameId) == 9 * sizeof(PVOID));
C_ASSERT(sizeof(DXGK_COMPOSITION_FRAME_VTABLE) == 10 * sizeof(PVOID));
C_ASSERT(sizeof(DXGK_COMPOSITION_FRAME_COLLECTION_VTABLE) == 7 * sizeof(PVOID));

#ifdef _WIN64
C_ASSERT(FIELD_OFFSET(DXGK_COMPOSITION_FRAME_HEADER, CollectionEntry) == 0x10);
#endif
