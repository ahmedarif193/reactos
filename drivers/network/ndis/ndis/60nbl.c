/*
 * PROJECT:     ReactOS NDIS library
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * FILE:        drivers/network/ndis/ndis/60nbl.c
 * PURPOSE:     NDIS 6 NET_BUFFER / NET_BUFFER_LIST allocation.
 *
 *              Real implementations of the NBL/NB pool API. Backed by
 *              ExAllocatePoolWithTag — no lookaside list yet, that's a
 *              future optimization. The pool handle is just a small
 *              header struct that remembers context size and DataSize
 *              the caller asked for; alloc/free goes straight to pool.
 *
 *              Created on the dev-nt6-1 branch by the NDIS 5↔6 bridge
 *              work that lets e1000e move real packets.
 *
 * COPYRIGHT:   Copyright 2026 dev-nt6-1 branch contributors
 */

#include "ndis6_internal.h"

#define NBL_POOL_TAG  'pBNn'  /* "nNBp" */
#define NBL_TAG       'lBNn'  /* "nNBl" */
#define NB_TAG        ' BNn'  /* "nNB " */
#define MDL_TAG       'lDMn'  /* "nMDl" */

/* ============================================================================
 *  Pool descriptor — what NBL pool handles actually point at
 * ============================================================================ */

typedef struct _NDIS6_NBL_POOL
{
    ULONG       Magic;          /* sanity check */
    ULONG       PoolTag;        /* caller's PoolTag */
    USHORT      ContextSize;    /* default context size for NBLs from this pool */
    USHORT      Reserved;
    ULONG       DataSize;       /* default DataSize for the embedded NB */
    BOOLEAN     fAllocateNetBuffer;
    UCHAR       ProtocolId;
    USHORT      Pad;

    /* Lookaside list for the fixed-size hot path. Allocations that match
     * the pool's default geometry (ContextSize == pool default,
     * ContextBackFill == 0) come from this list; everything else falls
     * back to direct ExAllocatePoolWithTag. The fixed size is computed
     * once at pool creation time. */
    BOOLEAN              LookasideValid;
    ULONG                FixedAllocSize;
    NPAGED_LOOKASIDE_LIST Lookaside;
} NDIS6_NBL_POOL, *PNDIS6_NBL_POOL;

#define NDIS6_NBL_POOL_MAGIC  0xB16CB16C

typedef struct _NDIS6_NB_POOL
{
    ULONG       Magic;
    ULONG       PoolTag;
    ULONG       DataSize;

    /* Same scheme as the NBL pool — fixed-size NB allocations come from
     * the lookaside list. */
    BOOLEAN              LookasideValid;
    NPAGED_LOOKASIDE_LIST Lookaside;
} NDIS6_NB_POOL, *PNDIS6_NB_POOL;

#define NDIS6_NB_POOL_MAGIC   0xB1601001

/* ============================================================================
 *  NBL pool — Allocate / Free
 * ============================================================================ */

NDIS_HANDLE
NTAPI
NdisAllocateNetBufferListPool(
    _In_opt_ NDIS_HANDLE NdisHandle,
    _In_ PNET_BUFFER_LIST_POOL_PARAMETERS Parameters)
{
    PNDIS6_NBL_POOL Pool;

    UNREFERENCED_PARAMETER(NdisHandle);

    if (Parameters == NULL)
        return NULL;

    Pool = (PNDIS6_NBL_POOL)ExAllocatePoolWithTag(
        NonPagedPool, sizeof(NDIS6_NBL_POOL), NBL_POOL_TAG);
    if (Pool == NULL)
        return NULL;

    Pool->Magic              = NDIS6_NBL_POOL_MAGIC;
    Pool->PoolTag            = Parameters->PoolTag ? Parameters->PoolTag : NBL_POOL_TAG;
    Pool->ContextSize        = Parameters->ContextSize;
    Pool->Reserved           = 0;
    Pool->DataSize           = Parameters->DataSize;
    Pool->fAllocateNetBuffer = Parameters->fAllocateNetBuffer;
    Pool->ProtocolId         = Parameters->ProtocolId;
    Pool->Pad                = 0;

    /* Pre-compute the fixed-size hot-path allocation for this pool's
     * default geometry. NBLs allocated with caller-provided ContextSize
     * matching the pool default and ContextBackFill==0 use this size
     * and come from the lookaside list. */
    Pool->FixedAllocSize = sizeof(NET_BUFFER_LIST);
    if (Pool->ContextSize)
    {
        Pool->FixedAllocSize +=
            FIELD_OFFSET(NET_BUFFER_LIST_CONTEXT, ContextData) +
            Pool->ContextSize;
    }
    if (Pool->fAllocateNetBuffer)
        Pool->FixedAllocSize += sizeof(NET_BUFFER);

    ExInitializeNPagedLookasideList(
        &Pool->Lookaside,
        NULL, NULL, 0,
        Pool->FixedAllocSize,
        Pool->PoolTag,
        0);
    Pool->LookasideValid = TRUE;

    return (NDIS_HANDLE)Pool;
}

VOID
NTAPI
NdisFreeNetBufferListPool(
    _In_ NDIS_HANDLE PoolHandle)
{
    PNDIS6_NBL_POOL Pool = (PNDIS6_NBL_POOL)PoolHandle;

    if (Pool == NULL || Pool->Magic != NDIS6_NBL_POOL_MAGIC)
        return;

    if (Pool->LookasideValid)
    {
        ExDeleteNPagedLookasideList(&Pool->Lookaside);
        Pool->LookasideValid = FALSE;
    }

    Pool->Magic = 0;
    ExFreePoolWithTag(Pool, NBL_POOL_TAG);
}

/* ============================================================================
 *  NB pool — Allocate / Free
 * ============================================================================ */

NDIS_HANDLE
NTAPI
NdisAllocateNetBufferPool(
    _In_opt_ NDIS_HANDLE NdisHandle,
    _In_ PNET_BUFFER_POOL_PARAMETERS Parameters)
{
    PNDIS6_NB_POOL Pool;

    UNREFERENCED_PARAMETER(NdisHandle);

    if (Parameters == NULL)
        return NULL;

    Pool = (PNDIS6_NB_POOL)ExAllocatePoolWithTag(
        NonPagedPool, sizeof(NDIS6_NB_POOL), NBL_POOL_TAG);
    if (Pool == NULL)
        return NULL;

    Pool->Magic   = NDIS6_NB_POOL_MAGIC;
    Pool->PoolTag = Parameters->PoolTag ? Parameters->PoolTag : NB_TAG;
    Pool->DataSize = Parameters->DataSize;
    return (NDIS_HANDLE)Pool;
}

VOID
NTAPI
NdisFreeNetBufferPool(
    _In_ NDIS_HANDLE PoolHandle)
{
    PNDIS6_NB_POOL Pool = (PNDIS6_NB_POOL)PoolHandle;

    if (Pool == NULL || Pool->Magic != NDIS6_NB_POOL_MAGIC)
        return;

    Pool->Magic = 0;
    ExFreePoolWithTag(Pool, NBL_POOL_TAG);
}

/* ============================================================================
 *  NET_BUFFER_LIST allocation
 *
 *  Layout we allocate:
 *      [ NET_BUFFER_LIST | NET_BUFFER_LIST_CONTEXT? | NET_BUFFER ]
 *  - The NBL itself is always present.
 *  - If ContextSize > 0 we append a context block immediately after.
 *  - If the pool was created with fAllocateNetBuffer == TRUE, we append
 *    one NET_BUFFER after that.
 * ============================================================================ */

PNET_BUFFER_LIST
NTAPI
NdisAllocateNetBufferList(
    _In_ NDIS_HANDLE PoolHandle,
    _In_ USHORT ContextSize,
    _In_ USHORT ContextBackFill)
{
    PNDIS6_NBL_POOL Pool = (PNDIS6_NBL_POOL)PoolHandle;
    PNET_BUFFER_LIST Nbl;
    PNET_BUFFER_LIST_CONTEXT Ctx;
    PNET_BUFFER Nb;
    ULONG TotalSize;
    USHORT EffectiveContextSize;

    if (Pool == NULL || Pool->Magic != NDIS6_NBL_POOL_MAGIC)
        return NULL;

    /* The caller's per-NBL ContextSize overrides the pool's default. */
    EffectiveContextSize = ContextSize ? ContextSize : Pool->ContextSize;

    TotalSize = sizeof(NET_BUFFER_LIST);
    if (EffectiveContextSize)
        TotalSize += FIELD_OFFSET(NET_BUFFER_LIST_CONTEXT, ContextData)
                   + EffectiveContextSize + ContextBackFill;
    if (Pool->fAllocateNetBuffer)
        TotalSize += sizeof(NET_BUFFER);

    /* Hot path: if the requested geometry matches the pool default
     * (no per-call context override and no backfill), pull from the
     * lookaside list. The TX wrapper pool the bridge uses for legacy
     * NDIS_PACKET wrapping always hits this path.
     *
     * Mark lookaside-sourced NBLs by setting NdisReserved[0] = (PVOID)1
     * so the free path knows where to return them. NdisReserved is
     * reserved for the NDIS module that owns the NBL, which is us. */
    {
        BOOLEAN UseLookaside =
            Pool->LookasideValid &&
            TotalSize == Pool->FixedAllocSize &&
            ContextBackFill == 0;

        if (UseLookaside)
        {
            Nbl = (PNET_BUFFER_LIST)ExAllocateFromNPagedLookasideList(&Pool->Lookaside);
        }
        else
        {
            Nbl = (PNET_BUFFER_LIST)ExAllocatePoolWithTag(
                NonPagedPool, TotalSize, Pool->PoolTag);
        }
        if (Nbl == NULL)
            return NULL;

        RtlZeroMemory(Nbl, TotalSize);
        if (UseLookaside)
            Nbl->NdisReserved[0] = (PVOID)(ULONG_PTR)1;
    }

    Nbl->NdisPoolHandle = PoolHandle;

    if (EffectiveContextSize)
    {
        Ctx = (PNET_BUFFER_LIST_CONTEXT)((PUCHAR)Nbl + sizeof(NET_BUFFER_LIST));
        Ctx->Next = NULL;
        Ctx->Size = EffectiveContextSize;
        Ctx->Offset = ContextBackFill;
        Nbl->Context = Ctx;
    }

    if (Pool->fAllocateNetBuffer)
    {
        ULONG NbOffset = sizeof(NET_BUFFER_LIST);
        if (EffectiveContextSize)
            NbOffset += FIELD_OFFSET(NET_BUFFER_LIST_CONTEXT, ContextData)
                      + EffectiveContextSize + ContextBackFill;
        Nb = (PNET_BUFFER)((PUCHAR)Nbl + NbOffset);
        Nbl->FirstNetBuffer = Nb;
        Nb->NdisPoolHandle = NULL;  /* this NB is owned by the NBL allocation */
    }

    return Nbl;
}

VOID
NTAPI
NdisFreeNetBufferList(
    _In_ PNET_BUFFER_LIST NetBufferList)
{
    PNDIS6_NBL_POOL Pool;
    BOOLEAN FromLookaside;

    if (NetBufferList == NULL)
        return;

    Pool = (PNDIS6_NBL_POOL)NetBufferList->NdisPoolHandle;
    if (Pool == NULL || Pool->Magic != NDIS6_NBL_POOL_MAGIC)
    {
        /* Allocator unknown — leak rather than corrupt heap. */
        return;
    }

    FromLookaside = (NetBufferList->NdisReserved[0] == (PVOID)(ULONG_PTR)1);

    if (FromLookaside && Pool->LookasideValid)
    {
        ExFreeToNPagedLookasideList(&Pool->Lookaside, NetBufferList);
    }
    else
    {
        ExFreePoolWithTag(NetBufferList, Pool->PoolTag);
    }
}

/* ============================================================================
 *  Combined NBL + NB allocation (the helper used on the RX hot path)
 * ============================================================================ */

PNET_BUFFER_LIST
NTAPI
NdisAllocateNetBufferAndNetBufferList(
    _In_ NDIS_HANDLE PoolHandle,
    _In_ USHORT ContextSize,
    _In_ USHORT ContextBackFill,
    _In_opt_ PMDL MdlChain,
    _In_ ULONG DataOffset,
    _In_ SIZE_T DataLength)
{
    PNET_BUFFER_LIST Nbl;
    PNET_BUFFER Nb;

    Nbl = NdisAllocateNetBufferList(PoolHandle, ContextSize, ContextBackFill);
    if (Nbl == NULL)
        return NULL;

    Nb = Nbl->FirstNetBuffer;
    if (Nb == NULL)
    {
        /* Pool wasn't created with fAllocateNetBuffer — caller error.
         * Free the NBL we just made and bail. */
        NdisFreeNetBufferList(Nbl);
        return NULL;
    }

    Nb->Next             = NULL;
    Nb->MdlChain         = MdlChain;
    Nb->CurrentMdl       = MdlChain;
    Nb->CurrentMdlOffset = 0;
    Nb->DataLength       = (ULONG)DataLength;
    Nb->DataOffset       = DataOffset;

    return Nbl;
}

/* ============================================================================
 *  Standalone NET_BUFFER allocation (rare — most callers use the combined
 *  helper above)
 * ============================================================================ */

PNET_BUFFER
NTAPI
NdisAllocateNetBuffer(
    _In_ NDIS_HANDLE PoolHandle,
    _In_opt_ PMDL MdlChain,
    _In_ ULONG DataOffset,
    _In_ SIZE_T DataLength)
{
    PNDIS6_NB_POOL Pool = (PNDIS6_NB_POOL)PoolHandle;
    PNET_BUFFER Nb;

    if (Pool == NULL || Pool->Magic != NDIS6_NB_POOL_MAGIC)
        return NULL;

    Nb = (PNET_BUFFER)ExAllocatePoolWithTag(
        NonPagedPool, sizeof(NET_BUFFER), Pool->PoolTag);
    if (Nb == NULL)
        return NULL;

    RtlZeroMemory(Nb, sizeof(NET_BUFFER));
    Nb->NdisPoolHandle   = PoolHandle;
    Nb->MdlChain         = MdlChain;
    Nb->CurrentMdl       = MdlChain;
    Nb->CurrentMdlOffset = 0;
    Nb->DataLength       = (ULONG)DataLength;
    Nb->DataOffset       = DataOffset;
    return Nb;
}

VOID
NTAPI
NdisFreeNetBuffer(
    _In_ PNET_BUFFER NetBuffer)
{
    PNDIS6_NB_POOL Pool;

    if (NetBuffer == NULL || NetBuffer->NdisPoolHandle == NULL)
    {
        /* This NB was embedded inside an NBL allocation — its memory is
         * owned by the NBL. NdisFreeNetBufferList handles cleanup. */
        return;
    }

    Pool = (PNDIS6_NB_POOL)NetBuffer->NdisPoolHandle;
    if (Pool->Magic != NDIS6_NB_POOL_MAGIC)
        return;

    ExFreePoolWithTag(NetBuffer, Pool->PoolTag);
}

/* ============================================================================
 *  NET_BUFFER data-start manipulation
 *
 *  These adjust where the protocol header begins inside the MDL chain.
 *  The contract is similar to how IP/TCP/UDP layers in mbuf-style
 *  systems push and pop headers without copying.
 * ============================================================================ */

NDIS_STATUS
NTAPI
NdisRetreatNetBufferDataStart(
    _In_ PNET_BUFFER NetBuffer,
    _In_ ULONG DataOffsetDelta,
    _In_ ULONG DataBackFill,
    _In_opt_ PVOID AllocateMdlHandler)
{
    PMDL    NewMdl;
    PVOID   NewMdlVa;
    ULONG   NewMdlSize;
    ULONG   NewMdlOffset;

    UNREFERENCED_PARAMETER(AllocateMdlHandler);

    if (NetBuffer == NULL)
        return NDIS_STATUS_INVALID_PARAMETER;

    /* Hot path — there's enough room in the current MDL to back up the
     * header pointer in place, no allocation needed. */
    if (NetBuffer->DataOffset >= DataOffsetDelta &&
        NetBuffer->CurrentMdlOffset >= DataOffsetDelta)
    {
        NetBuffer->DataOffset       -= DataOffsetDelta;
        NetBuffer->CurrentMdlOffset -= DataOffsetDelta;
        NetBuffer->DataLength       += DataOffsetDelta;
        return NDIS_STATUS_SUCCESS;
    }

    /* Slow path — the retreat would back the data start up past the
     * front of the current MDL. We allocate a new MDL of size
     * DataOffsetDelta + DataBackFill, prepend it to the chain, and
     * point CurrentMdl at it. The caller's intended use is "I want
     * DataOffsetDelta bytes of header space; please ensure DataBackFill
     * bytes are also reserved behind that for future retreats." We
     * place the new data start DataBackFill bytes into the new MDL. */
    NewMdlSize = DataOffsetDelta + DataBackFill;
    if (NewMdlSize == 0)
        return NDIS_STATUS_INVALID_PARAMETER;

    /* Allocate the backing memory and wrap it in an MDL. We use NonPaged
     * pool because the MDL chain may be touched at DISPATCH_LEVEL. */
    NewMdlVa = ExAllocatePoolWithTag(NonPagedPool, NewMdlSize, 'rNbR');
    if (NewMdlVa == NULL)
        return NDIS_STATUS_RESOURCES;

    NewMdl = IoAllocateMdl(NewMdlVa, NewMdlSize, FALSE, FALSE, NULL);
    if (NewMdl == NULL)
    {
        ExFreePoolWithTag(NewMdlVa, 'rNbR');
        return NDIS_STATUS_RESOURCES;
    }

    MmBuildMdlForNonPagedPool(NewMdl);

    /* Prepend the new MDL to the head of the chain. The original
     * NetBuffer->MdlChain head becomes the new MDL's Next link. */
    NewMdl->Next        = NetBuffer->MdlChain;
    NetBuffer->MdlChain = NewMdl;

    /* The new data start sits at offset DataBackFill inside the new MDL.
     * That leaves DataBackFill bytes "behind" the data start for future
     * retreats and DataOffsetDelta bytes "in front" for the new header. */
    NewMdlOffset = DataBackFill;

    NetBuffer->CurrentMdl       = NewMdl;
    NetBuffer->CurrentMdlOffset = NewMdlOffset;
    NetBuffer->DataOffset       = NewMdlOffset;
    NetBuffer->DataLength      += DataOffsetDelta;

    return NDIS_STATUS_SUCCESS;
}

VOID
NTAPI
NdisAdvanceNetBufferDataStart(
    _In_ PNET_BUFFER NetBuffer,
    _In_ ULONG DataOffsetDelta,
    _In_ BOOLEAN FreeMdl,
    _In_opt_ PVOID FreeMdlHandler)
{
    PMDL CurrentMdl;
    ULONG MdlBytesAvailable;

    UNREFERENCED_PARAMETER(FreeMdl);
    UNREFERENCED_PARAMETER(FreeMdlHandler);

    if (NetBuffer == NULL)
        return;

    while (DataOffsetDelta > 0)
    {
        CurrentMdl = NetBuffer->CurrentMdl;
        if (CurrentMdl == NULL)
            return;

        MdlBytesAvailable = MmGetMdlByteCount(CurrentMdl) - NetBuffer->CurrentMdlOffset;

        if (DataOffsetDelta < MdlBytesAvailable)
        {
            /* Stays inside the current MDL */
            NetBuffer->CurrentMdlOffset += DataOffsetDelta;
            NetBuffer->DataOffset       += DataOffsetDelta;
            NetBuffer->DataLength       -= DataOffsetDelta;
            return;
        }

        /* Consumes the rest of the current MDL; advance to next */
        NetBuffer->DataLength       -= MdlBytesAvailable;
        NetBuffer->DataOffset       += MdlBytesAvailable;
        DataOffsetDelta             -= MdlBytesAvailable;
        NetBuffer->CurrentMdl       = CurrentMdl->Next;
        NetBuffer->CurrentMdlOffset = 0;
    }
}

/* ============================================================================
 *  MDL helpers (NDIS 6 versions of NdisAllocateBuffer / NdisFreeBuffer)
 * ============================================================================ */

PMDL
NTAPI
NdisAllocateMdl(
    _In_ NDIS_HANDLE NdisHandle,
    _In_ PVOID VirtualAddress,
    _In_ UINT Length)
{
    UNREFERENCED_PARAMETER(NdisHandle);
    return IoAllocateMdl(VirtualAddress, Length, FALSE, FALSE, NULL);
}

VOID
NTAPI
NdisFreeMdl(
    _In_ PMDL Mdl)
{
    if (Mdl == NULL)
        return;
    IoFreeMdl(Mdl);
}

/* EOF */
