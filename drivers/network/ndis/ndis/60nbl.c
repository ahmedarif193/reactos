/*
 * PROJECT:     ReactOS NDIS library
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * FILE:        drivers/network/ndis/ndis/60nbl.c
 * PURPOSE:     NDIS 6 NET_BUFFER / NET_BUFFER_LIST allocation.
 *
 *              Real implementations of the NBL/NB pool API. Fixed geometry
 *              allocations use nonpaged lookaside lists; private allocation
 *              headers retain MDL/data ownership without consuming the public
 *              NDIS reserved fields.
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

typedef struct _NDIS6_NBL_ALLOCATION_HEADER
{
    ULONG Magic;
    BOOLEAN FromLookaside;
    UCHAR Reserved[3];
    PMDL OwnedMdl;
    PVOID OwnedData;
} NDIS6_NBL_ALLOCATION_HEADER, *PNDIS6_NBL_ALLOCATION_HEADER;

#define NDIS6_NBL_ALLOCATION_MAGIC 0xA16CB16C
#define NDIS6_NBL_ALLOCATION_HEADER_SIZE \
    ALIGN_UP_BY(sizeof(NDIS6_NBL_ALLOCATION_HEADER), MEMORY_ALLOCATION_ALIGNMENT)

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

typedef struct _NDIS6_NB_ALLOCATION_HEADER
{
    ULONG Magic;
    BOOLEAN FromLookaside;
    UCHAR Reserved[3];
    PMDL OwnedMdl;
    PVOID OwnedData;
} NDIS6_NB_ALLOCATION_HEADER, *PNDIS6_NB_ALLOCATION_HEADER;

#define NDIS6_NB_POOL_MAGIC   0xB1601001
#define NDIS6_NB_ALLOCATION_MAGIC 0xA1601001
#define NDIS6_NB_ALLOCATION_HEADER_SIZE \
    ALIGN_UP_BY(sizeof(NDIS6_NB_ALLOCATION_HEADER), MEMORY_ALLOCATION_ALIGNMENT)
#define NDIS6_RETREAT_CONTEXT_TAG 'cRbN'
#define NDIS6_RETREAT_RESERVED_SIGNATURE ((PVOID)(ULONG_PTR)0x5254424E)

typedef struct _NDIS6_CONTEXT_ALLOCATION_HEADER
{
    ULONG Magic;
    ULONG PoolTag;
} NDIS6_CONTEXT_ALLOCATION_HEADER, *PNDIS6_CONTEXT_ALLOCATION_HEADER;

#define NDIS6_CONTEXT_ALLOCATION_MAGIC 0xA16CC7E1
#define NDIS6_CONTEXT_ALLOCATION_HEADER_SIZE \
    ALIGN_UP_BY(sizeof(NDIS6_CONTEXT_ALLOCATION_HEADER), MEMORY_ALLOCATION_ALIGNMENT)

typedef struct _NDIS6_RETREAT_MDL_CONTEXT
{
    struct _NDIS6_RETREAT_MDL_CONTEXT *Next;
    PMDL Mdl;
    PVOID DefaultBuffer;
    PVOID SavedReserved0;
    PVOID SavedReserved1;
} NDIS6_RETREAT_MDL_CONTEXT, *PNDIS6_RETREAT_MDL_CONTEXT;

static BOOLEAN
Ndis6SetNetBufferDataOffset(
    _Inout_ PNET_BUFFER NetBuffer,
    _In_ ULONG DataOffset);

static BOOLEAN
Ndis6NetBufferDataRangeFits(
    _In_ PNET_BUFFER NetBuffer,
    _In_ ULONG DataLength)
{
    PMDL Mdl = NetBuffer->CurrentMdl;
    ULONG MdlOffset = NetBuffer->CurrentMdlOffset;
    ULONG Remaining = DataLength;

    while (Mdl != NULL && Remaining != 0)
    {
        ULONG MdlLength = MmGetMdlByteCount(Mdl);
        ULONG Available;

        if (MdlOffset > MdlLength)
            return FALSE;

        Available = MdlLength - MdlOffset;
        if (Remaining <= Available)
            return TRUE;

        Remaining -= Available;
        Mdl = Mdl->Next;
        MdlOffset = 0;
    }

    return Remaining == 0;
}

static PNDIS6_RETREAT_MDL_CONTEXT
Ndis6GetRetreatContextHead(
    _In_ PNET_BUFFER NetBuffer)
{
    if (NetBuffer->NdisReserved[1] != NDIS6_RETREAT_RESERVED_SIGNATURE)
        return NULL;

    return (PNDIS6_RETREAT_MDL_CONTEXT)NetBuffer->NdisReserved[0];
}

static VOID
Ndis6PushRetreatContext(
    _Inout_ PNET_BUFFER NetBuffer,
    _Inout_ PNDIS6_RETREAT_MDL_CONTEXT Context)
{
    PNDIS6_RETREAT_MDL_CONTEXT Head =
        Ndis6GetRetreatContextHead(NetBuffer);

    Context->Next = Head;
    if (Head == NULL)
    {
        Context->SavedReserved0 = NetBuffer->NdisReserved[0];
        Context->SavedReserved1 = NetBuffer->NdisReserved[1];
    }
    NetBuffer->NdisReserved[0] = Context;
    NetBuffer->NdisReserved[1] = NDIS6_RETREAT_RESERVED_SIGNATURE;
}

static VOID
Ndis6PopRetreatContext(
    _Inout_ PNET_BUFFER NetBuffer,
    _In_ PNDIS6_RETREAT_MDL_CONTEXT Context)
{
    ASSERT(Ndis6GetRetreatContextHead(NetBuffer) == Context);

    if (Context->Next != NULL)
    {
        NetBuffer->NdisReserved[0] = Context->Next;
    }
    else
    {
        NetBuffer->NdisReserved[0] = Context->SavedReserved0;
        NetBuffer->NdisReserved[1] = Context->SavedReserved1;
    }
}

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

    if (Parameters == NULL ||
        Parameters->Header.Type != NDIS_OBJECT_TYPE_DEFAULT ||
        Parameters->Header.Revision < NET_BUFFER_LIST_POOL_PARAMETERS_REVISION_1 ||
        Parameters->Header.Size < sizeof(NET_BUFFER_LIST_POOL_PARAMETERS) ||
        (Parameters->ContextSize & (MEMORY_ALLOCATION_ALIGNMENT - 1)) != 0 ||
        (Parameters->DataSize != 0 && !Parameters->fAllocateNetBuffer))
        return NULL;

    Pool = (PNDIS6_NBL_POOL)ExAllocatePoolWithTag(
        NonPagedPool, sizeof(NDIS6_NBL_POOL), NBL_POOL_TAG);
    if (Pool == NULL)
        return NULL;

    RtlZeroMemory(Pool, sizeof(*Pool));

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
    Pool->FixedAllocSize = NDIS6_NBL_ALLOCATION_HEADER_SIZE +
                           sizeof(NET_BUFFER_LIST);
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

    if (Parameters == NULL ||
        Parameters->Header.Type != NDIS_OBJECT_TYPE_DEFAULT ||
        Parameters->Header.Revision < NET_BUFFER_POOL_PARAMETERS_REVISION_1 ||
        Parameters->Header.Size < sizeof(NET_BUFFER_POOL_PARAMETERS))
        return NULL;

    Pool = (PNDIS6_NB_POOL)ExAllocatePoolWithTag(
        NonPagedPool, sizeof(NDIS6_NB_POOL), NBL_POOL_TAG);
    if (Pool == NULL)
        return NULL;

    RtlZeroMemory(Pool, sizeof(*Pool));

    Pool->Magic   = NDIS6_NB_POOL_MAGIC;
    Pool->PoolTag = Parameters->PoolTag ? Parameters->PoolTag : NB_TAG;
    Pool->DataSize = Parameters->DataSize;

    ExInitializeNPagedLookasideList(
        &Pool->Lookaside,
        NULL, NULL, 0,
        NDIS6_NB_ALLOCATION_HEADER_SIZE + sizeof(NET_BUFFER),
        Pool->PoolTag,
        0);
    Pool->LookasideValid = TRUE;
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

    if (Pool->LookasideValid)
    {
        ExDeleteNPagedLookasideList(&Pool->Lookaside);
        Pool->LookasideValid = FALSE;
    }

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
    PNDIS6_NBL_ALLOCATION_HEADER Allocation;
    PNET_BUFFER_LIST Nbl;
    PNET_BUFFER_LIST_CONTEXT Ctx;
    PNET_BUFFER Nb;
    PVOID DataBuffer;
    PMDL Mdl;
    ULONG AllocationSize;
    ULONG TotalSize;
    USHORT EffectiveContextSize;

    if (Pool == NULL || Pool->Magic != NDIS6_NBL_POOL_MAGIC)
        return NULL;

    /* The caller's per-NBL ContextSize overrides the pool's default. */
    EffectiveContextSize = ContextSize ? ContextSize : Pool->ContextSize;
    if ((ContextSize & (MEMORY_ALLOCATION_ALIGNMENT - 1)) != 0 ||
        (ContextBackFill & (MEMORY_ALLOCATION_ALIGNMENT - 1)) != 0 ||
        (ULONG)EffectiveContextSize + ContextBackFill > MAXUSHORT)
        return NULL;

    TotalSize = sizeof(NET_BUFFER_LIST);
    if (EffectiveContextSize != 0 || ContextBackFill != 0)
        TotalSize += FIELD_OFFSET(NET_BUFFER_LIST_CONTEXT, ContextData)
                   + EffectiveContextSize + ContextBackFill;
    if (Pool->fAllocateNetBuffer)
        TotalSize += sizeof(NET_BUFFER);
    AllocationSize = NDIS6_NBL_ALLOCATION_HEADER_SIZE + TotalSize;

    /* Hot path: if the requested geometry matches the pool default
     * (no per-call context override and no backfill), pull from the
     * lookaside list. The TX wrapper pool the bridge uses for legacy
     * NDIS_PACKET wrapping always hits this path. The private allocation
     * header records where the block must be returned without consuming
     * any driver-visible reserved field in the NBL. */
    {
        BOOLEAN UseLookaside =
            Pool->LookasideValid &&
            AllocationSize == Pool->FixedAllocSize &&
            ContextBackFill == 0;

        if (UseLookaside)
        {
            Allocation = (PNDIS6_NBL_ALLOCATION_HEADER)
                ExAllocateFromNPagedLookasideList(&Pool->Lookaside);
        }
        else
        {
            Allocation = (PNDIS6_NBL_ALLOCATION_HEADER)ExAllocatePoolWithTag(
                NonPagedPool, AllocationSize, Pool->PoolTag);
        }
        if (Allocation == NULL)
            return NULL;

        RtlZeroMemory(Allocation, AllocationSize);
        Allocation->Magic = NDIS6_NBL_ALLOCATION_MAGIC;
        Allocation->FromLookaside = UseLookaside;
        Nbl = (PNET_BUFFER_LIST)((PUCHAR)Allocation +
                                 NDIS6_NBL_ALLOCATION_HEADER_SIZE);
    }

    Nbl->NdisPoolHandle = PoolHandle;

    if (EffectiveContextSize != 0 || ContextBackFill != 0)
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
        if (EffectiveContextSize != 0 || ContextBackFill != 0)
            NbOffset += FIELD_OFFSET(NET_BUFFER_LIST_CONTEXT, ContextData)
                      + EffectiveContextSize + ContextBackFill;
        Nb = (PNET_BUFFER)((PUCHAR)Nbl + NbOffset);
        Nbl->FirstNetBuffer = Nb;
        Nb->NdisPoolHandle = NULL;  /* this NB is owned by the NBL allocation */

        if (Pool->DataSize != 0)
        {
            DataBuffer = ExAllocatePoolWithTag(NonPagedPool,
                                               Pool->DataSize,
                                               Pool->PoolTag);
            if (DataBuffer == NULL)
            {
                NdisFreeNetBufferList(Nbl);
                return NULL;
            }
            Allocation->OwnedData = DataBuffer;

            Mdl = IoAllocateMdl(DataBuffer,
                                Pool->DataSize,
                                FALSE,
                                FALSE,
                                NULL);
            if (Mdl == NULL)
            {
                NdisFreeNetBufferList(Nbl);
                return NULL;
            }
            MmBuildMdlForNonPagedPool(Mdl);
            Allocation->OwnedMdl = Mdl;

            Nb->MdlChain = Mdl;
            Nb->CurrentMdl = Mdl;
            Nb->CurrentMdlOffset = 0;
            Nb->DataOffset = 0;
            Nb->DataLength = Pool->DataSize;
        }
    }

    return Nbl;
}

VOID
NTAPI
NdisFreeNetBufferList(
    _In_ PNET_BUFFER_LIST NetBufferList)
{
    PNDIS6_NBL_POOL Pool;
    PNDIS6_NBL_ALLOCATION_HEADER Allocation;
    BOOLEAN FromLookaside;

    if (NetBufferList == NULL)
        return;

    Pool = (PNDIS6_NBL_POOL)NetBufferList->NdisPoolHandle;
    if (Pool == NULL || Pool->Magic != NDIS6_NBL_POOL_MAGIC)
    {
        /* Allocator unknown — leak rather than corrupt heap. */
        return;
    }

    Allocation = (PNDIS6_NBL_ALLOCATION_HEADER)
        ((PUCHAR)NetBufferList - NDIS6_NBL_ALLOCATION_HEADER_SIZE);
    if (Allocation->Magic != NDIS6_NBL_ALLOCATION_MAGIC)
        return;

    FromLookaside = Allocation->FromLookaside;
    if (Allocation->OwnedMdl != NULL)
        IoFreeMdl(Allocation->OwnedMdl);
    if (Allocation->OwnedData != NULL)
        ExFreePoolWithTag(Allocation->OwnedData, Pool->PoolTag);
    Allocation->Magic = 0;

    if (FromLookaside && Pool->LookasideValid)
    {
        ExFreeToNPagedLookasideList(&Pool->Lookaside, Allocation);
    }
    else
    {
        ExFreePoolWithTag(Allocation, Pool->PoolTag);
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
    PNDIS6_NBL_POOL Pool = (PNDIS6_NBL_POOL)PoolHandle;
    PNET_BUFFER_LIST Nbl;
    PNET_BUFFER Nb;

    if (Pool == NULL || Pool->Magic != NDIS6_NBL_POOL_MAGIC ||
        !Pool->fAllocateNetBuffer || Pool->DataSize != 0)
    {
        return NULL;
    }

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

    if (DataLength > MAXULONG ||
        (MdlChain == NULL && (DataOffset != 0 || DataLength != 0)) ||
        (MdlChain != NULL &&
         (!Ndis6SetNetBufferDataOffset(Nb, DataOffset) ||
          !Ndis6NetBufferDataRangeFits(Nb, (ULONG)DataLength))))
    {
        NdisFreeNetBufferList(Nbl);
        return NULL;
    }

    Nb->DataLength       = (ULONG)DataLength;
    Nb->DataOffset       = DataOffset;

    return Nbl;
}

/* ============================================================================
 *  Standalone NET_BUFFER allocation (rare — most callers use the combined
 *  helper above)
 * ============================================================================ */

static PNET_BUFFER
Ndis6AllocateNetBuffer(
    _In_ PNDIS6_NB_POOL Pool,
    _In_ NDIS_HANDLE PoolHandle,
    _In_opt_ PMDL MdlChain,
    _In_ ULONG DataOffset,
    _In_ SIZE_T DataLength)
{
    PNDIS6_NB_ALLOCATION_HEADER Allocation;
    PNET_BUFFER Nb;
    BOOLEAN FromLookaside;

    FromLookaside = Pool->LookasideValid;
    if (FromLookaside)
    {
        Allocation = (PNDIS6_NB_ALLOCATION_HEADER)
            ExAllocateFromNPagedLookasideList(&Pool->Lookaside);
    }
    else
    {
        Allocation = (PNDIS6_NB_ALLOCATION_HEADER)ExAllocatePoolWithTag(
            NonPagedPool,
            NDIS6_NB_ALLOCATION_HEADER_SIZE + sizeof(NET_BUFFER),
            Pool->PoolTag);
    }
    if (Allocation == NULL)
        return NULL;

    RtlZeroMemory(Allocation,
                  NDIS6_NB_ALLOCATION_HEADER_SIZE + sizeof(NET_BUFFER));
    Allocation->Magic = NDIS6_NB_ALLOCATION_MAGIC;
    Allocation->FromLookaside = FromLookaside;
    Nb = (PNET_BUFFER)((PUCHAR)Allocation +
                       NDIS6_NB_ALLOCATION_HEADER_SIZE);
    Nb->NdisPoolHandle   = PoolHandle;
    Nb->MdlChain         = MdlChain;

    if (DataLength > MAXULONG ||
        (MdlChain == NULL && (DataOffset != 0 || DataLength != 0)) ||
        (MdlChain != NULL &&
         (!Ndis6SetNetBufferDataOffset(Nb, DataOffset) ||
          !Ndis6NetBufferDataRangeFits(Nb, (ULONG)DataLength))))
    {
        Allocation->Magic = 0;
        if (FromLookaside && Pool->LookasideValid)
            ExFreeToNPagedLookasideList(&Pool->Lookaside, Allocation);
        else
            ExFreePoolWithTag(Allocation, Pool->PoolTag);
        return NULL;
    }

    Nb->DataLength       = (ULONG)DataLength;
    Nb->DataOffset       = DataOffset;
    return Nb;
}

PNET_BUFFER
NTAPI
NdisAllocateNetBuffer(
    _In_ NDIS_HANDLE PoolHandle,
    _In_opt_ PMDL MdlChain,
    _In_ ULONG DataOffset,
    _In_ SIZE_T DataLength)
{
    PNDIS6_NB_POOL Pool = (PNDIS6_NB_POOL)PoolHandle;

    if (Pool == NULL || Pool->Magic != NDIS6_NB_POOL_MAGIC ||
        Pool->DataSize != 0)
    {
        return NULL;
    }

    return Ndis6AllocateNetBuffer(Pool,
                                  PoolHandle,
                                  MdlChain,
                                  DataOffset,
                                  DataLength);
}

PNET_BUFFER
NTAPI
NdisAllocateNetBufferMdlAndData(
    _In_ NDIS_HANDLE PoolHandle)
{
    PNDIS6_NB_POOL Pool = (PNDIS6_NB_POOL)PoolHandle;
    PNDIS6_NB_ALLOCATION_HEADER Allocation;
    PNET_BUFFER Nb;
    PMDL Mdl;
    PVOID DataBuffer;

    if (Pool == NULL || Pool->Magic != NDIS6_NB_POOL_MAGIC ||
        Pool->DataSize == 0)
    {
        return NULL;
    }

    DataBuffer = ExAllocatePoolWithTag(NonPagedPool,
                                       Pool->DataSize,
                                       Pool->PoolTag);
    if (DataBuffer == NULL)
        return NULL;

    Mdl = IoAllocateMdl(DataBuffer,
                        Pool->DataSize,
                        FALSE,
                        FALSE,
                        NULL);
    if (Mdl == NULL)
    {
        ExFreePoolWithTag(DataBuffer, Pool->PoolTag);
        return NULL;
    }
    MmBuildMdlForNonPagedPool(Mdl);

    Nb = Ndis6AllocateNetBuffer(Pool,
                                PoolHandle,
                                Mdl,
                                0,
                                Pool->DataSize);
    if (Nb == NULL)
    {
        IoFreeMdl(Mdl);
        ExFreePoolWithTag(DataBuffer, Pool->PoolTag);
        return NULL;
    }

    Allocation = (PNDIS6_NB_ALLOCATION_HEADER)
        ((PUCHAR)Nb - NDIS6_NB_ALLOCATION_HEADER_SIZE);
    Allocation->OwnedMdl = Mdl;
    Allocation->OwnedData = DataBuffer;
    return Nb;
}

VOID
NTAPI
NdisFreeNetBuffer(
    _In_ PNET_BUFFER NetBuffer)
{
    PNDIS6_NB_POOL Pool;
    PNDIS6_NB_ALLOCATION_HEADER Allocation;
    BOOLEAN FromLookaside;

    if (NetBuffer == NULL || NetBuffer->NdisPoolHandle == NULL)
    {
        /* This NB was embedded inside an NBL allocation — its memory is
         * owned by the NBL. NdisFreeNetBufferList handles cleanup. */
        return;
    }

    Pool = (PNDIS6_NB_POOL)NetBuffer->NdisPoolHandle;
    if (Pool == NULL || Pool->Magic != NDIS6_NB_POOL_MAGIC)
        return;

    Allocation = (PNDIS6_NB_ALLOCATION_HEADER)
        ((PUCHAR)NetBuffer - NDIS6_NB_ALLOCATION_HEADER_SIZE);
    if (Allocation->Magic != NDIS6_NB_ALLOCATION_MAGIC)
        return;

    FromLookaside = Allocation->FromLookaside;
    if (Allocation->OwnedMdl != NULL)
        IoFreeMdl(Allocation->OwnedMdl);
    if (Allocation->OwnedData != NULL)
        ExFreePoolWithTag(Allocation->OwnedData, Pool->PoolTag);
    Allocation->Magic = 0;

    if (FromLookaside && Pool->LookasideValid)
        ExFreeToNPagedLookasideList(&Pool->Lookaside, Allocation);
    else
        ExFreePoolWithTag(Allocation, Pool->PoolTag);
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
    _In_opt_ NET_BUFFER_ALLOCATE_MDL_HANDLER AllocateMdlHandler)
{
    PMDL    NewMdl;
    PVOID   NewMdlVa;
    PNDIS6_RETREAT_MDL_CONTEXT RetreatContext;
    ULONG   MissingData;
    ULONG   NewMdlSize;
    ULONG   NewMdlOffset;

    if (NetBuffer == NULL)
        return NDIS_STATUS_INVALID_PARAMETER;

    if (DataOffsetDelta > MAXULONG - NetBuffer->DataLength)
        return NDIS_STATUS_FAILURE;

    /* Existing headroom can span MDLs before CurrentMdl. Recompute the
     * optimized CurrentMdl view from the chain-wide DataOffset instead of
     * considering only CurrentMdlOffset. */
    if (NetBuffer->DataOffset >= DataOffsetDelta)
    {
        if (NetBuffer->MdlChain != NULL &&
            !Ndis6SetNetBufferDataOffset(
                NetBuffer, NetBuffer->DataOffset - DataOffsetDelta))
        {
            return NDIS_STATUS_FAILURE;
        }
        NetBuffer->DataLength += DataOffsetDelta;
        return NDIS_STATUS_SUCCESS;
    }

    /* Slow path: allocate only the part of the retreat not covered by the
     * existing chain-wide DataOffset, plus the requested backfill. Prepend
     * that MDL and leave its extra bytes before the new data start. */
    MissingData = DataOffsetDelta - NetBuffer->DataOffset;
    if (MissingData > MAXULONG - DataBackFill)
        return NDIS_STATUS_FAILURE;
    NewMdlSize = MissingData + DataBackFill;

    RetreatContext = (PNDIS6_RETREAT_MDL_CONTEXT)ExAllocatePoolWithTag(
        NonPagedPool, sizeof(*RetreatContext), NDIS6_RETREAT_CONTEXT_TAG);
    if (RetreatContext == NULL)
        return NDIS_STATUS_RESOURCES;
    RtlZeroMemory(RetreatContext, sizeof(*RetreatContext));

    if (AllocateMdlHandler != NULL)
    {
        ULONG BufferSize = NewMdlSize;

        NewMdl = AllocateMdlHandler(&BufferSize);
        if (NewMdl == NULL)
        {
            ExFreePoolWithTag(RetreatContext, NDIS6_RETREAT_CONTEXT_TAG);
            return NDIS_STATUS_RESOURCES;
        }
        NewMdlSize = BufferSize;
    }
    else
    {
        /* Default allocation is reciprocal with the default free path in
         * NdisAdvanceNetBufferDataStart. */
        NewMdlVa = ExAllocatePoolWithTag(NonPagedPool, NewMdlSize, 'rNbR');
        if (NewMdlVa == NULL)
        {
            ExFreePoolWithTag(RetreatContext, NDIS6_RETREAT_CONTEXT_TAG);
            return NDIS_STATUS_RESOURCES;
        }

        NewMdl = IoAllocateMdl(NewMdlVa, NewMdlSize, FALSE, FALSE, NULL);
        if (NewMdl == NULL)
        {
            ExFreePoolWithTag(NewMdlVa, 'rNbR');
            ExFreePoolWithTag(RetreatContext, NDIS6_RETREAT_CONTEXT_TAG);
            return NDIS_STATUS_RESOURCES;
        }

        MmBuildMdlForNonPagedPool(NewMdl);
    }

    if (NewMdlSize < MissingData ||
        MmGetMdlByteCount(NewMdl) < NewMdlSize)
    {
        /* A caller-supplied allocator owns its failure cleanup. The default
         * allocation can be released here directly. */
        if (AllocateMdlHandler == NULL)
        {
            NewMdlVa = MmGetMdlVirtualAddress(NewMdl);
            IoFreeMdl(NewMdl);
            ExFreePoolWithTag(NewMdlVa, 'rNbR');
        }
        ExFreePoolWithTag(RetreatContext, NDIS6_RETREAT_CONTEXT_TAG);
        return NDIS_STATUS_FAILURE;
    }

    RetreatContext->Mdl = NewMdl;
    RetreatContext->DefaultBuffer =
        (AllocateMdlHandler == NULL) ? NewMdlVa : NULL;
    Ndis6PushRetreatContext(NetBuffer, RetreatContext);

    /* Prepend the new MDL to the head of the chain. The original
     * NetBuffer->MdlChain head becomes the new MDL's Next link. */
    NewMdl->Next        = NetBuffer->MdlChain;
    NetBuffer->MdlChain = NewMdl;

    /* The original chain's headroom supplies part of the requested retreat.
     * The new MDL contains only the missing bytes plus any backfill, so its
     * data start is the unused part of the actual allocation. */
    NewMdlOffset = NewMdlSize - MissingData;

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
    _In_opt_ NET_BUFFER_FREE_MDL_HANDLER FreeMdlHandler)
{
    PMDL OldMdl;
    PMDL NewCurrentMdl;
    ULONG NewCurrentMdlOffset;
    ULONG NewDataOffset;
    ULONG RemovedBytes = 0;

    if (NetBuffer == NULL || DataOffsetDelta > NetBuffer->DataLength ||
        DataOffsetDelta > MAXULONG - NetBuffer->DataOffset)
        return;

    NewDataOffset = NetBuffer->DataOffset + DataOffsetDelta;
    if (NetBuffer->MdlChain == NULL)
    {
        if (NewDataOffset != 0)
            return;
        NetBuffer->DataLength -= DataOffsetDelta;
        return;
    }

    if (!Ndis6SetNetBufferDataOffset(NetBuffer, NewDataOffset))
        return;

    NewCurrentMdl = NetBuffer->CurrentMdl;
    NewCurrentMdlOffset = NetBuffer->CurrentMdlOffset;
    NetBuffer->DataLength -= DataOffsetDelta;

    if (!FreeMdl)
        return;

    /* MDLs before the new CurrentMdl no longer describe used or backfill
     * data. Remove and release them, then make DataOffset relative to the
     * shortened chain. */
    OldMdl = NetBuffer->MdlChain;
    while (OldMdl != NewCurrentMdl)
    {
        PNDIS6_RETREAT_MDL_CONTEXT RetreatContext =
            Ndis6GetRetreatContextHead(NetBuffer);
        PMDL NextMdl = OldMdl->Next;
        ULONG OldMdlLength = MmGetMdlByteCount(OldMdl);

        /* Only MDLs allocated by our retreat path may be released. Original
         * receive/transmit MDLs remain in the chain even when FreeMdl is set. */
        if (RetreatContext == NULL || RetreatContext->Mdl != OldMdl)
            break;

        if (RetreatContext->DefaultBuffer == NULL && FreeMdlHandler == NULL)
            break;

        Ndis6PopRetreatContext(NetBuffer, RetreatContext);

        if (RetreatContext->DefaultBuffer != NULL)
        {
            IoFreeMdl(OldMdl);
            ExFreePoolWithTag(RetreatContext->DefaultBuffer, 'rNbR');
        }
        else
        {
            FreeMdlHandler(OldMdl);
        }
        ExFreePoolWithTag(RetreatContext, NDIS6_RETREAT_CONTEXT_TAG);
        RemovedBytes += OldMdlLength;
        OldMdl = NextMdl;
    }

    NetBuffer->MdlChain = OldMdl;
    NetBuffer->CurrentMdl = NewCurrentMdl;
    NetBuffer->CurrentMdlOffset = NewCurrentMdlOffset;
    NetBuffer->DataOffset = NewDataOffset - RemovedBytes;
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

#define NDIS6_DATA_BUFFER_ALIGNED(_Address) \
    ((AlignMultiple <= 1) || \
     ((((ULONG_PTR)(_Address) - AlignOffset) & (AlignMultiple - 1)) == 0))

    if (NetBuffer == NULL || BytesNeeded == 0 ||
        BytesNeeded > NET_BUFFER_DATA_LENGTH(NetBuffer) ||
        (AlignMultiple > 1 &&
         (AlignMultiple & (AlignMultiple - 1)) != 0))
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
        if (NDIS6_DATA_BUFFER_ALIGNED(DataVa))
        {
            return DataVa;
        }

        if (Storage == NULL || !NDIS6_DATA_BUFFER_ALIGNED(Storage))
            return NULL;

        RtlCopyMemory(Storage, DataVa, BytesNeeded);
        return Storage;
    }

    if (Storage == NULL || !NDIS6_DATA_BUFFER_ALIGNED(Storage))
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

#undef NDIS6_DATA_BUFFER_ALIGNED
}

static BOOLEAN
Ndis6AdvanceMdlPosition(
    _Inout_ PMDL *Mdl,
    _Inout_ PULONG MdlOffset,
    _In_ ULONG Delta)
{
    while (*Mdl != NULL)
    {
        ULONG MdlLength = MmGetMdlByteCount(*Mdl);
        ULONG Available;

        if (*MdlOffset > MdlLength)
            return FALSE;

        Available = MdlLength - *MdlOffset;
        if (Delta < Available)
        {
            *MdlOffset += Delta;
            return TRUE;
        }

        Delta -= Available;
        if ((*Mdl)->Next == NULL)
        {
            *MdlOffset = MdlLength;
            return Delta == 0;
        }

        *Mdl = (*Mdl)->Next;
        *MdlOffset = 0;
        if (Delta == 0)
            return TRUE;
    }

    return FALSE;
}

NDIS_HANDLE
NTAPI
NdisGetPoolFromNetBufferList(
    _In_ PNET_BUFFER_LIST NetBufferList)
{
    return NetBufferList ? NetBufferList->NdisPoolHandle : NULL;
}

NDIS_HANDLE
NTAPI
NdisGetPoolFromNetBuffer(
    _In_ PNET_BUFFER NetBuffer)
{
    return NetBuffer ? NetBuffer->NdisPoolHandle : NULL;
}

NDIS_STATUS
NTAPI
NdisCopyFromNetBufferToNetBuffer(
    _In_ PNET_BUFFER Destination,
    _In_ ULONG DestinationOffset,
    _In_ ULONG BytesToCopy,
    _In_ PNET_BUFFER Source,
    _In_ ULONG SourceOffset,
    _Out_ PULONG BytesCopied)
{
    PMDL DestinationMdl;
    PMDL SourceMdl;
    ULONG DestinationMdlOffset;
    ULONG SourceMdlOffset;

    if (BytesCopied == NULL)
        return NDIS_STATUS_INVALID_PARAMETER;
    *BytesCopied = 0;

    if (Destination == NULL || Source == NULL)
        return NDIS_STATUS_INVALID_PARAMETER;
    if (BytesToCopy == 0)
        return NDIS_STATUS_SUCCESS;

    DestinationMdl = Destination->CurrentMdl;
    DestinationMdlOffset = Destination->CurrentMdlOffset;
    SourceMdl = Source->CurrentMdl;
    SourceMdlOffset = Source->CurrentMdlOffset;
    if (!Ndis6AdvanceMdlPosition(&DestinationMdl,
                                 &DestinationMdlOffset,
                                 DestinationOffset) ||
        !Ndis6AdvanceMdlPosition(&SourceMdl,
                                 &SourceMdlOffset,
                                 SourceOffset))
    {
        return NDIS_STATUS_SUCCESS;
    }

    /* NDIS copies through the available MDL ranges. DataLength describes the
     * current packet view, but does not bound this primitive while a caller is
     * constructing or extending a view. */
    while (*BytesCopied < BytesToCopy &&
           SourceMdl != NULL && DestinationMdl != NULL)
    {
        ULONG DestinationMdlLength = MmGetMdlByteCount(DestinationMdl);
        ULONG SourceMdlLength = MmGetMdlByteCount(SourceMdl);
        ULONG DestinationAvailable;
        ULONG SourceAvailable;
        ULONG CopyLength;
        PUCHAR DestinationVa;
        PUCHAR SourceVa;

        if (DestinationMdlOffset > DestinationMdlLength ||
            SourceMdlOffset > SourceMdlLength)
        {
            break;
        }
        if (DestinationMdlOffset == DestinationMdlLength)
        {
            DestinationMdl = DestinationMdl->Next;
            DestinationMdlOffset = 0;
            continue;
        }
        if (SourceMdlOffset == SourceMdlLength)
        {
            SourceMdl = SourceMdl->Next;
            SourceMdlOffset = 0;
            continue;
        }

        DestinationAvailable = DestinationMdlLength - DestinationMdlOffset;
        SourceAvailable = SourceMdlLength - SourceMdlOffset;
        CopyLength = min(DestinationAvailable, SourceAvailable);
        CopyLength = min(CopyLength, BytesToCopy - *BytesCopied);

        DestinationVa = (PUCHAR)MmGetSystemAddressForMdlSafe(
            DestinationMdl, NormalPagePriority);
        SourceVa = (PUCHAR)MmGetSystemAddressForMdlSafe(
            SourceMdl, NormalPagePriority);
        if (DestinationVa == NULL || SourceVa == NULL)
            return NDIS_STATUS_RESOURCES;

        RtlMoveMemory(DestinationVa + DestinationMdlOffset,
                      SourceVa + SourceMdlOffset,
                      CopyLength);
        DestinationMdlOffset += CopyLength;
        SourceMdlOffset += CopyLength;
        *BytesCopied += CopyLength;
    }

    return NDIS_STATUS_SUCCESS;
}

ULONG
NTAPI
NdisQueryNetBufferPhysicalCount(
    _In_ PNET_BUFFER NetBuffer)
{
    PMDL Mdl;
    ULONG MdlOffset;
    ULONG Remaining;
    ULONG PhysicalCount = 0;

    if (NetBuffer == NULL)
        return 0;

    NdisAdjustNetBufferCurrentMdl(NetBuffer);
    Mdl = NetBuffer->CurrentMdl;
    MdlOffset = NetBuffer->CurrentMdlOffset;
    Remaining = NetBuffer->DataLength;
    while (Remaining != 0 && Mdl != NULL)
    {
        ULONG MdlLength = MmGetMdlByteCount(Mdl);
        ULONG EndOffset;
        ULONG SegmentLength;
        ULONG SegmentPages;

        if (MdlOffset > MdlLength)
            return PhysicalCount;
        if (MdlLength != 0 && MdlOffset == MdlLength)
        {
            Mdl = Mdl->Next;
            MdlOffset = 0;
            continue;
        }

        SegmentLength = min(Remaining, MdlLength - MdlOffset);
        EndOffset = MdlOffset + SegmentLength;

        /* Count the maximum breaks mapped by the MDL from its virtual start
         * through the used range's end. Counting only from CurrentMdlOffset
         * underestimates the scatter/gather capacity Windows reports. */
        SegmentPages = MdlLength == 0 ? 1 :
            ADDRESS_AND_SIZE_TO_SPAN_PAGES(
                MmGetMdlVirtualAddress(Mdl), EndOffset);
        if (SegmentPages > MAXULONG - PhysicalCount)
            return MAXULONG;

        PhysicalCount += SegmentPages;
        Remaining -= SegmentLength;
        Mdl = Mdl->Next;
        MdlOffset = 0;
    }

    return PhysicalCount;
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
    PMDL Mdl;

    UNREFERENCED_PARAMETER(NdisHandle);

    if (VirtualAddress == NULL || Length == 0)
        return NULL;

    Mdl = IoAllocateMdl(VirtualAddress, Length, FALSE, FALSE, NULL);
    if (Mdl != NULL)
        MmBuildMdlForNonPagedPool(Mdl);
    return Mdl;
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

NDIS_STATUS
NTAPI
NdisRetreatNetBufferListDataStart(
    _In_ PNET_BUFFER_LIST NetBufferList,
    _In_ ULONG DataOffsetDelta,
    _In_ ULONG DataBackFill,
    _In_opt_ NET_BUFFER_ALLOCATE_MDL_HANDLER AllocateMdlHandler,
    _In_opt_ NET_BUFFER_FREE_MDL_HANDLER FreeMdlHandler)
{
    PNET_BUFFER NetBuffer;
    NDIS_STATUS Status;

    if (NetBufferList == NULL)
        return NDIS_STATUS_INVALID_PARAMETER;

    for (NetBuffer = NetBufferList->FirstNetBuffer;
         NetBuffer != NULL;
         NetBuffer = NetBuffer->Next)
    {
        Status = NdisRetreatNetBufferDataStart(NetBuffer,
                                               DataOffsetDelta,
                                               DataBackFill,
                                               AllocateMdlHandler);
        if (Status != NDIS_STATUS_SUCCESS)
        {
            PNET_BUFFER Rollback;

            /* Restore every NET_BUFFER already changed by this list call.
             * The per-NB retreat tracker ensures FreeMdl releases only MDLs
             * allocated during retreat, never the caller's original chain. */
            for (Rollback = NetBufferList->FirstNetBuffer;
                 Rollback != NetBuffer;
                 Rollback = Rollback->Next)
            {
                NdisAdvanceNetBufferDataStart(Rollback,
                                              DataOffsetDelta,
                                              TRUE,
                                              FreeMdlHandler);
            }
            return Status;
        }
    }

    return NDIS_STATUS_SUCCESS;
}

VOID
NTAPI
NdisAdvanceNetBufferListDataStart(
    _In_ PNET_BUFFER_LIST NetBufferList,
    _In_ ULONG DataOffsetDelta,
    _In_ BOOLEAN FreeMdl,
    _In_opt_ NET_BUFFER_FREE_MDL_HANDLER FreeMdlHandler)
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

VOID
NTAPI
NdisAdjustNetBufferCurrentMdl(
    _In_ PNET_BUFFER NetBuffer)
{
    if (NetBuffer != NULL)
        Ndis6SetNetBufferDataOffset(NetBuffer, NetBuffer->DataOffset);
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

NDIS_STATUS
NTAPI
NdisAllocateNetBufferListContext(
    _In_ PNET_BUFFER_LIST NetBufferList,
    _In_ USHORT ContextSize,
    _In_ USHORT ContextBackFill,
    _In_ ULONG PoolTag)
{
    PNDIS6_CONTEXT_ALLOCATION_HEADER Allocation;
    PNET_BUFFER_LIST_CONTEXT Context;
    ULONG AllocationSize;

    if (NetBufferList == NULL || ContextSize == 0 ||
        (ContextSize & (sizeof(PVOID) - 1)) != 0 ||
        (ContextBackFill & (sizeof(PVOID) - 1)) != 0 ||
        (ULONG)ContextSize + ContextBackFill > MAXUSHORT)
    {
        return NDIS_STATUS_FAILURE;
    }

    Context = NetBufferList->Context;
    if (Context != NULL && Context->Offset >= ContextSize)
    {
        if ((ULONG)Context->Size + ContextSize > MAXUSHORT)
            return NDIS_STATUS_FAILURE;

        Context->Offset -= ContextSize;
        Context->Size += ContextSize;
        return NDIS_STATUS_SUCCESS;
    }

    AllocationSize = NDIS6_CONTEXT_ALLOCATION_HEADER_SIZE +
                     FIELD_OFFSET(NET_BUFFER_LIST_CONTEXT, ContextData) +
                     ContextSize + ContextBackFill;
    Allocation = (PNDIS6_CONTEXT_ALLOCATION_HEADER)ExAllocatePoolWithTag(
        NonPagedPool,
        AllocationSize,
        PoolTag ? PoolTag : NBL_TAG);
    if (Allocation == NULL)
        return NDIS_STATUS_RESOURCES;

    RtlZeroMemory(Allocation, AllocationSize);
    Allocation->Magic = NDIS6_CONTEXT_ALLOCATION_MAGIC;
    Allocation->PoolTag = PoolTag ? PoolTag : NBL_TAG;

    Context = (PNET_BUFFER_LIST_CONTEXT)
        ((PUCHAR)Allocation + NDIS6_CONTEXT_ALLOCATION_HEADER_SIZE);
    Context->Next = NetBufferList->Context;
    Context->Size = ContextSize;
    Context->Offset = ContextBackFill;
    NetBufferList->Context = Context;
    return NDIS_STATUS_SUCCESS;
}

VOID
NTAPI
NdisFreeNetBufferListContext(
    _Inout_ PNET_BUFFER_LIST NetBufferList,
    _In_ USHORT ContextSize)
{
    PNDIS6_CONTEXT_ALLOCATION_HEADER Allocation;
    PNET_BUFFER_LIST_CONTEXT Context;
    PNET_BUFFER_LIST_CONTEXT EmbeddedContext;

    if (NetBufferList == NULL || NetBufferList->Context == NULL ||
        ContextSize == 0 ||
        (ContextSize & (sizeof(PVOID) - 1)) != 0)
    {
        return;
    }

    Context = NetBufferList->Context;
    if (ContextSize > Context->Size)
        return;

    EmbeddedContext = (PNET_BUFFER_LIST_CONTEXT)
        ((PUCHAR)NetBufferList + sizeof(NET_BUFFER_LIST));
    if (ContextSize == Context->Size && Context != EmbeddedContext)
    {
        Allocation = (PNDIS6_CONTEXT_ALLOCATION_HEADER)
            ((PUCHAR)Context - NDIS6_CONTEXT_ALLOCATION_HEADER_SIZE);
        if (Allocation->Magic != NDIS6_CONTEXT_ALLOCATION_MAGIC)
            return;

        NetBufferList->Context = Context->Next;
        Allocation->Magic = 0;
        ExFreePoolWithTag(Allocation, Allocation->PoolTag);
        return;
    }

    Context->Size -= ContextSize;
    Context->Offset += ContextSize;
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

        if (Remaining < MdlLength)
        {
            NetBuffer->CurrentMdl = Mdl;
            NetBuffer->CurrentMdlOffset = Remaining;
            NetBuffer->DataOffset = DataOffset;
            return TRUE;
        }

        Remaining -= MdlLength;
        Mdl = Mdl->Next;
    }

    /* Do not leave a stale optimized cursor behind when DataOffset runs past
     * the MDL chain. Windows stores the exhausted chain and residual offset
     * even for that malformed state. */
    NetBuffer->CurrentMdl = Mdl;
    NetBuffer->CurrentMdlOffset = Remaining;
    NetBuffer->DataOffset = DataOffset;
    return Remaining == 0;
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
