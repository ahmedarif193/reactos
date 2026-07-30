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
    if ((ULONG)EffectiveContextSize + ContextBackFill > MAXUSHORT)
        return NULL;

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
        Ctx->Size = EffectiveContextSize + ContextBackFill;
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

PVOID
NTAPI
NdisGetDataBuffer(
    _In_ PNET_BUFFER NetBuffer,
    _In_ ULONG BytesNeeded,
    _Out_writes_bytes_all_opt_(BytesNeeded) PVOID Storage,
    _In_ UINT AlignMultiple,
    _In_ UINT AlignOffset)
{
    PMDL CurrentMdl;
    ULONG CurrentMdlOffset;
    ULONG BytesCopied;
    ULONG MdlByteCount;
    PUCHAR MdlVa;
    PUCHAR DataVa;
    PUCHAR StoragePtr;

    if (NetBuffer == NULL || BytesNeeded == 0 ||
        BytesNeeded > NET_BUFFER_DATA_LENGTH(NetBuffer))
    {
        return NULL;
    }

    CurrentMdl = NET_BUFFER_CURRENT_MDL(NetBuffer);
    CurrentMdlOffset = NET_BUFFER_CURRENT_MDL_OFFSET(NetBuffer);
    if (CurrentMdl == NULL)
        return NULL;

    MdlByteCount = MmGetMdlByteCount(CurrentMdl);
    if (CurrentMdlOffset >= MdlByteCount)
        return NULL;

    if (BytesNeeded <= MdlByteCount - CurrentMdlOffset)
    {
        MdlVa = (PUCHAR)MmGetSystemAddressForMdlSafe(CurrentMdl, NormalPagePriority);
        if (MdlVa == NULL)
            return NULL;

        DataVa = MdlVa + CurrentMdlOffset;
        if (AlignMultiple <= 1 ||
            (((ULONG_PTR)DataVa + AlignOffset) % AlignMultiple) == 0)
        {
            return DataVa;
        }

        if (Storage == NULL)
            return NULL;

        RtlCopyMemory(Storage, DataVa, BytesNeeded);
        return Storage;
    }

    if (Storage == NULL)
        return NULL;

    StoragePtr = (PUCHAR)Storage;
    BytesCopied = 0;
    while (BytesCopied < BytesNeeded && CurrentMdl != NULL)
    {
        ULONG AvailableBytes;
        ULONG BytesToCopy;

        MdlByteCount = MmGetMdlByteCount(CurrentMdl);
        if (CurrentMdlOffset >= MdlByteCount)
            return NULL;

        MdlVa = (PUCHAR)MmGetSystemAddressForMdlSafe(CurrentMdl, NormalPagePriority);
        if (MdlVa == NULL)
            return NULL;

        AvailableBytes = MdlByteCount - CurrentMdlOffset;
        BytesToCopy = (AvailableBytes < BytesNeeded - BytesCopied) ?
                      AvailableBytes : BytesNeeded - BytesCopied;
        RtlCopyMemory(StoragePtr + BytesCopied,
                      MdlVa + CurrentMdlOffset,
                      BytesToCopy);

        BytesCopied += BytesToCopy;
        CurrentMdl = CurrentMdl->Next;
        CurrentMdlOffset = 0;
    }

    if (BytesCopied != BytesNeeded)
        return NULL;

    return Storage;
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

VOID
NTAPI
NdisAdvanceNetBufferListDataStart(
    _In_ PNET_BUFFER_LIST NetBufferList,
    _In_ ULONG DataOffsetDelta,
    _In_ BOOLEAN FreeMdl,
    _In_opt_ PVOID FreeMdlHandler)
{
    PNET_BUFFER NetBuffer;

    if (NetBufferList == NULL)
        return;

    for (NetBuffer = NetBufferList->FirstNetBuffer;
         NetBuffer != NULL;
         NetBuffer = NetBuffer->Next)
    {
        NdisAdvanceNetBufferDataStart(NetBuffer,
                                      DataOffsetDelta,
                                      FreeMdl,
                                      FreeMdlHandler);
    }
}

static VOID
Ndis6CopyNetBufferListInfo(
    _Out_ PNET_BUFFER_LIST Destination,
    _In_ const NET_BUFFER_LIST* Source,
    _In_ BOOLEAN SendPath)
{
    static const UCHAR SendIndexes[] =
    {
        TcpIpChecksumNetBufferListInfo,
        IPsecOffloadV1NetBufferListInfo,
        TcpLargeSendNetBufferListInfo,
        Ieee8021QNetBufferListInfo,
        NetBufferListCancelId,
        MediaSpecificInformation,
        NetBufferListFrameType,
        IPsecOffloadV2TunnelNetBufferListInfo,
        IPsecOffloadV2HeaderNetBufferListInfo
    };
    static const UCHAR ReceiveIndexes[] =
    {
        TcpIpChecksumNetBufferListInfo,
        IPsecOffloadV1NetBufferListInfo,
        TcpReceiveNoPush,
        Ieee8021QNetBufferListInfo,
        MediaSpecificInformation,
        NetBufferListFrameType,
        NetBufferListHashValue,
        NetBufferListHashInfo,
        IPsecOffloadV2TunnelNetBufferListInfo,
        IPsecOffloadV2HeaderNetBufferListInfo
    };
    const UCHAR* Indexes = SendPath ? SendIndexes : ReceiveIndexes;
    ULONG Count = SendPath ? RTL_NUMBER_OF(SendIndexes)
                           : RTL_NUMBER_OF(ReceiveIndexes);
    ULONG Index;

    Destination->NblFlags = Source->NblFlags;
    Destination->Flags = Source->Flags;
    Destination->SourceHandle = Source->SourceHandle;

    for (Index = 0; Index < Count; Index++)
    {
        Destination->NetBufferListInfo[Indexes[Index]] =
            Source->NetBufferListInfo[Indexes[Index]];
    }
}

VOID
NTAPI
NdisCopySendNetBufferListInfo(
    _Out_ PNET_BUFFER_LIST Destination,
    _In_ const NET_BUFFER_LIST* Source)
{
    if (Destination && Source)
        Ndis6CopyNetBufferListInfo(Destination, Source, TRUE);
}

VOID
NTAPI
NdisCopyReceiveNetBufferListInfo(
    _Out_ PNET_BUFFER_LIST Destination,
    _In_ const NET_BUFFER_LIST* Source)
{
    if (Destination && Source)
        Ndis6CopyNetBufferListInfo(Destination, Source, FALSE);
}

VOID
NTAPI
NdisFreeNetBufferListContext(
    _Inout_ PNET_BUFFER_LIST NetBufferList,
    _In_ USHORT ContextSize)
{
    PNET_BUFFER_LIST_CONTEXT Context;
    ULONG AlignedSize;

    if (NetBufferList == NULL || NetBufferList->Context == NULL ||
        ContextSize == 0)
    {
        return;
    }

    AlignedSize = ALIGN_UP_BY(ContextSize, sizeof(PVOID));
    Context = NetBufferList->Context;
    if (AlignedSize > MAXUSHORT ||
        Context->Offset > Context->Size ||
        AlignedSize > Context->Size - Context->Offset)
    {
        return;
    }

    Context->Offset += (USHORT)AlignedSize;
    if (Context->Offset == Context->Size && Context->Next != NULL)
    {
        NetBufferList->Context = Context->Next;
        ExFreePool(Context);
    }
}

static BOOLEAN
Ndis6SetNetBufferDataOffset(
    _Inout_ PNET_BUFFER NetBuffer,
    _In_ ULONG DataOffset)
{
    PMDL Mdl = NetBuffer->MdlChain;
    ULONG Remaining = DataOffset;

    while (Mdl != NULL)
    {
        ULONG MdlLength = MmGetMdlByteCount(Mdl);

        if (Remaining < MdlLength ||
            (Remaining == MdlLength && Mdl->Next == NULL))
        {
            NetBuffer->CurrentMdl = Mdl;
            NetBuffer->CurrentMdlOffset = Remaining;
            NetBuffer->DataOffset = DataOffset;
            return TRUE;
        }

        Remaining -= MdlLength;
        Mdl = Mdl->Next;
    }

    return FALSE;
}

PNET_BUFFER_LIST
NTAPI
NdisAllocateFragmentNetBufferList(
    _In_ PNET_BUFFER_LIST OriginalNetBufferList,
    _In_ NDIS_HANDLE NetBufferListPool,
    _In_ NDIS_HANDLE NetBufferPool,
    _In_ ULONG StartOffset,
    _In_ ULONG MaximumLength,
    _In_ ULONG DataOffsetDelta,
    _In_ ULONG DataBackFill,
    _In_ ULONG AllocateFragmentFlags)
{
    PNET_BUFFER_LIST FragmentNetBufferList;
    PNET_BUFFER EmbeddedNetBuffer;
    PNET_BUFFER LastFragment = NULL;
    PNET_BUFFER SourceNetBuffer;

    UNREFERENCED_PARAMETER(DataBackFill);
    UNREFERENCED_PARAMETER(AllocateFragmentFlags);

    if (OriginalNetBufferList == NULL || NetBufferListPool == NULL ||
        MaximumLength == 0)
    {
        return NULL;
    }

    FragmentNetBufferList =
        NdisAllocateNetBufferList(NetBufferListPool, 0, 0);
    if (FragmentNetBufferList == NULL)
        return NULL;

    EmbeddedNetBuffer = FragmentNetBufferList->FirstNetBuffer;
    FragmentNetBufferList->FirstNetBuffer = NULL;

    for (SourceNetBuffer = OriginalNetBufferList->FirstNetBuffer;
         SourceNetBuffer != NULL;
         SourceNetBuffer = SourceNetBuffer->Next)
    {
        ULONG Consumed = 0;
        ULONG Remaining;

        if (StartOffset >= SourceNetBuffer->DataLength)
            continue;

        Remaining = SourceNetBuffer->DataLength - StartOffset;
        while (Remaining != 0)
        {
            PNET_BUFFER Fragment;
            ULONG FragmentLength = min(Remaining, MaximumLength);
            ULONG FragmentOffset;

            if (EmbeddedNetBuffer != NULL)
            {
                Fragment = EmbeddedNetBuffer;
                EmbeddedNetBuffer = NULL;
                RtlZeroMemory(Fragment, sizeof(*Fragment));
            }
            else
            {
                Fragment = NdisAllocateNetBuffer(NetBufferPool,
                                                 SourceNetBuffer->MdlChain,
                                                 0,
                                                 FragmentLength);
                if (Fragment == NULL)
                    goto Failure;
            }

            FragmentOffset = SourceNetBuffer->DataOffset +
                             StartOffset +
                             Consumed;
            Fragment->MdlChain = SourceNetBuffer->MdlChain;
            Fragment->DataLength = FragmentLength;
            if (!Ndis6SetNetBufferDataOffset(Fragment, FragmentOffset))
            {
                if (Fragment->NdisPoolHandle)
                    NdisFreeNetBuffer(Fragment);
                goto Failure;
            }

            /* The common NetAdapterCx path has sufficient source headroom.
             * Refuse the fragment if satisfying DataOffsetDelta would need a
             * private MDL allocation that this shallow fragment cannot own. */
            if (DataOffsetDelta != 0)
            {
                if (Fragment->DataOffset < DataOffsetDelta ||
                    Fragment->CurrentMdlOffset < DataOffsetDelta)
                {
                    if (Fragment->NdisPoolHandle)
                        NdisFreeNetBuffer(Fragment);
                    goto Failure;
                }

                Fragment->DataOffset -= DataOffsetDelta;
                Fragment->CurrentMdlOffset -= DataOffsetDelta;
                Fragment->DataLength += DataOffsetDelta;
            }

            if (LastFragment)
                LastFragment->Next = Fragment;
            else
                FragmentNetBufferList->FirstNetBuffer = Fragment;
            LastFragment = Fragment;

            Consumed += FragmentLength;
            Remaining -= FragmentLength;
        }
    }

    FragmentNetBufferList->ParentNetBufferList = OriginalNetBufferList;
    InterlockedIncrement(&OriginalNetBufferList->ChildRefCount);
    NdisCopySendNetBufferListInfo(FragmentNetBufferList,
                                  OriginalNetBufferList);
    return FragmentNetBufferList;

Failure:
    while (FragmentNetBufferList->FirstNetBuffer)
    {
        PNET_BUFFER Fragment = FragmentNetBufferList->FirstNetBuffer;
        FragmentNetBufferList->FirstNetBuffer = Fragment->Next;
        if (Fragment->NdisPoolHandle)
            NdisFreeNetBuffer(Fragment);
    }
    NdisFreeNetBufferList(FragmentNetBufferList);
    return NULL;
}

VOID
NTAPI
NdisFreeFragmentNetBufferList(
    _In_ PNET_BUFFER_LIST FragmentNetBufferList,
    _In_ ULONG DataOffsetDelta,
    _In_ ULONG FreeFragmentFlags)
{
    PNET_BUFFER Fragment;
    PNET_BUFFER_LIST Parent;

    UNREFERENCED_PARAMETER(FreeFragmentFlags);

    if (FragmentNetBufferList == NULL)
        return;

    Parent = FragmentNetBufferList->ParentNetBufferList;
    Fragment = FragmentNetBufferList->FirstNetBuffer;
    FragmentNetBufferList->FirstNetBuffer = NULL;

    while (Fragment != NULL)
    {
        PNET_BUFFER Next = Fragment->Next;

        if (DataOffsetDelta != 0)
        {
            Fragment->DataOffset += DataOffsetDelta;
            Fragment->CurrentMdlOffset += DataOffsetDelta;
            Fragment->DataLength -= DataOffsetDelta;
        }

        if (Fragment->NdisPoolHandle)
            NdisFreeNetBuffer(Fragment);
        Fragment = Next;
    }

    NdisFreeNetBufferList(FragmentNetBufferList);
    if (Parent)
        InterlockedDecrement(&Parent->ChildRefCount);
}

/* EOF */
