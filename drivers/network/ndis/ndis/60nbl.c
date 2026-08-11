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

#if defined(_WIN64)
/* Windows 11 24H2 (NDIS 6.89) 64-bit wrapper geometry. The public driver ABI
 * has 27 OOB slots; NDIS_WRAPPER adds two private slots to its own allocation.
 * These guards keep that internal geometry from silently shrinking. */
C_ASSERT(MaxNetBufferListInfo == 29);
C_ASSERT(sizeof(NET_BUFFER) == 176);
C_ASSERT(FIELD_OFFSET(NET_BUFFER, NdisReserved) == 64);
C_ASSERT(FIELD_OFFSET(NET_BUFFER, ProtocolReserved) == 80);
C_ASSERT(FIELD_OFFSET(NET_BUFFER, MiniportReserved) == 128);
C_ASSERT(sizeof(NET_BUFFER_LIST) == 384);
C_ASSERT(FIELD_OFFSET(NET_BUFFER_LIST, NdisReserved) == 48);
C_ASSERT(FIELD_OFFSET(NET_BUFFER_LIST, NetBufferListInfo) == 144);
C_ASSERT(NDIS_SIZEOF_NET_BUFFER_LIST_POOL_PARAMETERS_REVISION_1 == 16);
C_ASSERT(NDIS_SIZEOF_NET_BUFFER_LIST_POOL_PARAMETERS_REVISION_2 == 20);
C_ASSERT(NDIS_SIZEOF_NET_BUFFER_POOL_PARAMETERS_REVISION_1 == 12);
C_ASSERT(NDIS_SIZEOF_NET_BUFFER_POOL_PARAMETERS_REVISION_2 == 16);
#endif

#define NBL_POOL_TAG  'pBNn'  /* "nNBp" */
#define NBL_TAG       'lBNn'  /* "nNBl" */
#define NB_TAG        ' BNn'  /* "nNB " */
#define MDL_TAG       'lDMn'  /* "nMDl" */
#define DERIVED_NBL_TAG 'dBNn'
#define DERIVED_NB_TAG  'rBNn'
#define INTERNAL_NB_TAG 'iBNn'
#define DERIVED_DATA_TAG 'bBNn'

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
    ULONG       Flags;

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
    ULONG PoolTag;
    PMDL OwnedMdl;
    PVOID OwnedData;
    PVOID DerivedContext;
} NDIS6_NBL_ALLOCATION_HEADER, *PNDIS6_NBL_ALLOCATION_HEADER;

#define NDIS6_NBL_ALLOCATION_MAGIC 0xA16CB16C
#define NDIS6_NBL_ALLOCATION_HEADER_SIZE \
    ALIGN_UP_BY(sizeof(NDIS6_NBL_ALLOCATION_HEADER), MEMORY_ALLOCATION_ALIGNMENT)

typedef struct _NDIS6_NB_POOL
{
    ULONG       Magic;
    ULONG       PoolTag;
    ULONG       DataSize;
    ULONG       Flags;

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
#define NDIS6_NBL_FLAG_ALLOCATED_CONTEXT 0x00000400UL

typedef struct _NDIS6_RETREAT_MDL_CONTEXT
{
    struct _NDIS6_RETREAT_MDL_CONTEXT *Next;
    PMDL Mdl;
    PVOID DefaultBuffer;
    PVOID SavedReserved0;
    PVOID SavedReserved1;
} NDIS6_RETREAT_MDL_CONTEXT, *PNDIS6_RETREAT_MDL_CONTEXT;

typedef enum _NDIS6_DERIVED_NBL_KIND
{
    Ndis6DerivedClone = 1,
    Ndis6DerivedFragment,
    Ndis6DerivedReassembled
} NDIS6_DERIVED_NBL_KIND;

typedef enum _NDIS6_DERIVED_NB_STORAGE
{
    Ndis6DerivedNbEmbedded = 1,
    Ndis6DerivedNbPool,
    Ndis6DerivedNbInternal
} NDIS6_DERIVED_NB_STORAGE;

typedef struct _NDIS6_DERIVED_NB_RECORD
{
    struct _NDIS6_DERIVED_NB_RECORD *Next;
    PNET_BUFFER NetBuffer;
    PMDL OwnedMdlChain;
    PVOID OwnedData;
    NDIS6_DERIVED_NB_STORAGE Storage;
} NDIS6_DERIVED_NB_RECORD, *PNDIS6_DERIVED_NB_RECORD;

typedef struct _NDIS6_DERIVED_NBL_CONTEXT
{
    ULONG Magic;
    NDIS6_DERIVED_NBL_KIND Kind;
    ULONG AllocateFlags;
    ULONG DataOffsetDelta;
    PNET_BUFFER EmbeddedNetBuffer;
    PNDIS6_DERIVED_NB_RECORD NetBuffers;
} NDIS6_DERIVED_NBL_CONTEXT, *PNDIS6_DERIVED_NBL_CONTEXT;

#define NDIS6_DERIVED_NBL_MAGIC 0xD16CB16C

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
    ULONG Flags = 0;

    UNREFERENCED_PARAMETER(NdisHandle);

    if (Parameters == NULL ||
        Parameters->Header.Type != NDIS_OBJECT_TYPE_DEFAULT)
        return NULL;

    if (Parameters->Header.Revision == NET_BUFFER_LIST_POOL_PARAMETERS_REVISION_1)
    {
        if (Parameters->Header.Size <
            NDIS_SIZEOF_NET_BUFFER_LIST_POOL_PARAMETERS_REVISION_1)
        {
            return NULL;
        }
    }
#if NDIS_SUPPORT_NDIS687
    else if (Parameters->Header.Revision == NET_BUFFER_LIST_POOL_PARAMETERS_REVISION_2)
    {
        if (Parameters->Header.Size <
            NDIS_SIZEOF_NET_BUFFER_LIST_POOL_PARAMETERS_REVISION_2)
        {
            return NULL;
        }
        Flags = Parameters->Flags;
        if ((Flags & ~NET_BUFFER_LIST_POOL_FLAG_VERIFY) != 0)
            return NULL;
    }
#endif
    else
    {
        return NULL;
    }

    if ((Parameters->ContextSize & (MEMORY_ALLOCATION_ALIGNMENT - 1)) != 0 ||
        (Parameters->DataSize != 0 && !Parameters->fAllocateNetBuffer))
    {
        return NULL;
    }

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
    Pool->Flags              = Flags;

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
    ULONG Flags = 0;

    UNREFERENCED_PARAMETER(NdisHandle);

    if (Parameters == NULL ||
        Parameters->Header.Type != NDIS_OBJECT_TYPE_DEFAULT)
        return NULL;

    if (Parameters->Header.Revision == NET_BUFFER_POOL_PARAMETERS_REVISION_1)
    {
        if (Parameters->Header.Size <
            NDIS_SIZEOF_NET_BUFFER_POOL_PARAMETERS_REVISION_1)
        {
            return NULL;
        }
    }
#if NDIS_SUPPORT_NDIS687
    else if (Parameters->Header.Revision == NET_BUFFER_POOL_PARAMETERS_REVISION_2)
    {
        if (Parameters->Header.Size <
            NDIS_SIZEOF_NET_BUFFER_POOL_PARAMETERS_REVISION_2)
        {
            return NULL;
        }
        Flags = Parameters->Flags;
        if ((Flags & ~NET_BUFFER_POOL_FLAG_VERIFY) != 0)
            return NULL;
    }
#endif
    else
    {
        return NULL;
    }

    Pool = (PNDIS6_NB_POOL)ExAllocatePoolWithTag(
        NonPagedPool, sizeof(NDIS6_NB_POOL), NBL_POOL_TAG);
    if (Pool == NULL)
        return NULL;

    RtlZeroMemory(Pool, sizeof(*Pool));

    Pool->Magic   = NDIS6_NB_POOL_MAGIC;
    Pool->PoolTag = Parameters->PoolTag ? Parameters->PoolTag : NB_TAG;
    Pool->DataSize = Parameters->DataSize;
    Pool->Flags = Flags;

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
 *  - If the pool reserves context capacity, we append that unused context
 *    block immediately after the NBL. Larger per-allocation requests use a
 *    separately allocated context block.
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
    USHORT ContextCapacity;

    if (Pool == NULL || Pool->Magic != NDIS6_NBL_POOL_MAGIC)
        return NULL;

    if ((ContextSize & (MEMORY_ALLOCATION_ALIGNMENT - 1)) != 0 ||
        (ContextBackFill & (MEMORY_ALLOCATION_ALIGNMENT - 1)) != 0 ||
        (ULONG)ContextSize + ContextBackFill > MAXUSHORT)
        return NULL;

    /* The pool ContextSize is unused capacity preallocated in every NBL.  The
     * per-allocation ContextSize is the amount initially consumed by this
     * caller; ContextBackFill matters only if the preallocated block cannot
     * satisfy that request and NDIS must allocate another context block. */
    ContextCapacity = Pool->ContextSize;

    TotalSize = sizeof(NET_BUFFER_LIST);
    if (ContextCapacity != 0)
        TotalSize += FIELD_OFFSET(NET_BUFFER_LIST_CONTEXT, ContextData)
                   + ContextCapacity;
    if (Pool->fAllocateNetBuffer)
        TotalSize += sizeof(NET_BUFFER);
    AllocationSize = NDIS6_NBL_ALLOCATION_HEADER_SIZE + TotalSize;

    /* The NBL's inline geometry is fixed by its pool, so every allocation can
     * use the pool lookaside. A per-call context that does not fit is a
     * separate allocation. The private allocation header records where the
     * core block must be returned without consuming driver-visible fields. */
    {
        BOOLEAN UseLookaside =
            Pool->LookasideValid &&
            AllocationSize == Pool->FixedAllocSize;

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
        Allocation->PoolTag = Pool->PoolTag;
        Nbl = (PNET_BUFFER_LIST)((PUCHAR)Allocation +
                                 NDIS6_NBL_ALLOCATION_HEADER_SIZE);
    }

    Ctx = NULL;
    Nbl->NdisPoolHandle = PoolHandle;
    NdisSetNetBufferListProtocolId(Nbl, Pool->ProtocolId);

    if (ContextCapacity != 0)
    {
        Ctx = (PNET_BUFFER_LIST_CONTEXT)((PUCHAR)Nbl + sizeof(NET_BUFFER_LIST));
        Ctx->Next = NULL;
        Ctx->Size = ContextCapacity;
        Ctx->Offset = ContextCapacity;
        Nbl->Context = Ctx;
    }

    if (ContextSize != 0)
    {
        NDIS_STATUS Status = NdisAllocateNetBufferListContext(
            Nbl, ContextSize, ContextBackFill, Pool->PoolTag);

        if (Status != NDIS_STATUS_SUCCESS)
        {
            NdisFreeNetBufferList(Nbl);
            return NULL;
        }

        if (Nbl->Context != Ctx)
            Nbl->Flags |= NDIS6_NBL_FLAG_ALLOCATED_CONTEXT;
    }

    if (Pool->fAllocateNetBuffer)
    {
        ULONG NbOffset = sizeof(NET_BUFFER_LIST);
        if (ContextCapacity != 0)
            NbOffset += FIELD_OFFSET(NET_BUFFER_LIST_CONTEXT, ContextData)
                      + ContextCapacity;
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
    ULONG PoolTag;

    if (NetBufferList == NULL)
        return;

    Allocation = (PNDIS6_NBL_ALLOCATION_HEADER)
        ((PUCHAR)NetBufferList - NDIS6_NBL_ALLOCATION_HEADER_SIZE);
    if (Allocation->Magic != NDIS6_NBL_ALLOCATION_MAGIC)
        return;

    Pool = (PNDIS6_NBL_POOL)NetBufferList->NdisPoolHandle;
    if (Pool != NULL && Pool->Magic != NDIS6_NBL_POOL_MAGIC)
    {
        /* Allocator unknown — leak rather than corrupt heap. */
        return;
    }

    if (Allocation->DerivedContext != NULL)
    {
        /* Derived NBLs must be released by their matching clone, fragment,
         * or reassembly free routine so that their MDL ownership is honored. */
        return;
    }

    if ((NetBufferList->Flags & NDIS6_NBL_FLAG_ALLOCATED_CONTEXT) != 0)
    {
        PNET_BUFFER_LIST_CONTEXT Context = NetBufferList->Context;

        if (Context != NULL && Context->Offset <= Context->Size)
        {
            NdisFreeNetBufferListContext(
                NetBufferList, Context->Size - Context->Offset);
        }
        NetBufferList->Flags &= ~NDIS6_NBL_FLAG_ALLOCATED_CONTEXT;
    }

    FromLookaside = Allocation->FromLookaside;
    PoolTag = Allocation->PoolTag;
    if (Allocation->OwnedMdl != NULL)
        IoFreeMdl(Allocation->OwnedMdl);
    if (Allocation->OwnedData != NULL)
        ExFreePoolWithTag(Allocation->OwnedData, PoolTag);
    Allocation->Magic = 0;

    if (FromLookaside && Pool != NULL && Pool->LookasideValid)
    {
        ExFreeToNPagedLookasideList(&Pool->Lookaside, Allocation);
    }
    else
    {
        ExFreePoolWithTag(Allocation, PoolTag);
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
    ((((ULONG_PTR)(_Address)) & (AlignMultiple - 1)) == AlignOffset)

    if (NetBuffer == NULL || BytesNeeded == 0 ||
        BytesNeeded > NET_BUFFER_DATA_LENGTH(NetBuffer) ||
        AlignMultiple == 0 ||
        (AlignMultiple & (AlignMultiple - 1)) != 0)
    {
        return NULL;
    }

    CurrentMdl = NET_BUFFER_CURRENT_MDL(NetBuffer);
    CurrentMdlOffset = NET_BUFFER_CURRENT_MDL_OFFSET(NetBuffer);
    if (CurrentMdl == NULL)
        return NULL;

    while (CurrentMdl != NULL)
    {
        MdlByteCount = MmGetMdlByteCount(CurrentMdl);
        if (CurrentMdlOffset < MdlByteCount)
            break;

        CurrentMdlOffset -= MdlByteCount;
        CurrentMdl = CurrentMdl->Next;
    }
    if (CurrentMdl == NULL)
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
        {
            CurrentMdlOffset -= MdlByteCount;
            CurrentMdl = CurrentMdl->Next;
            continue;
        }

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
    _In_ const NET_BUFFER *Source,
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

    if ((AllocateMdlHandler == NULL) != (FreeMdlHandler == NULL))
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
        ClassificationHandleNetBufferListInfo,
        Ieee8021QNetBufferListInfo,
        NetBufferListCancelId,
        MediaSpecificInformation,
        NetBufferListHashValue,
        IPsecOffloadV2TunnelNetBufferListInfo,
        IPsecOffloadV2HeaderNetBufferListInfo,
#if NDIS_SUPPORT_NDIS620
        NblOriginalInterfaceIfIndex,
        NetBufferListFilteringInfo,
#endif
#if NDIS_SUPPORT_NDIS630 && (defined(_AMD64_) || defined(_ARM64_))
        VirtualSubnetInfo,
#endif
#if NDIS_SUPPORT_NDIS630
        TcpRecvSegCoalesceInfo,
        RscTcpTimestampDelta,
#endif
#if NDIS_SUPPORT_NDIS650 && (defined(_AMD64_) || defined(_ARM64_))
        GftOffloadInformation,
        GftFlowEntryId,
#endif
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
        IPsecOffloadV2HeaderNetBufferListInfo,
#if NDIS_SUPPORT_NDIS620
        NblOriginalInterfaceIfIndex,
        TcpReceiveBytesTransferred,
        NetBufferListFilteringInfo,
#endif
#if NDIS_SUPPORT_NDIS630 && (defined(_AMD64_) || defined(_ARM64_))
        VirtualSubnetInfo,
#endif
#if NDIS_SUPPORT_NDIS630
        TcpRecvSegCoalesceInfo,
        RscTcpTimestampDelta,
#endif
#if NDIS_SUPPORT_NDIS650 && (defined(_AMD64_) || defined(_ARM64_))
        GftOffloadInformation,
        GftFlowEntryId,
#endif
    };
    const UCHAR* Indexes = SendPath ? SendIndexes : ReceiveIndexes;
    ULONG Count = SendPath ? RTL_NUMBER_OF(SendIndexes)
                           : RTL_NUMBER_OF(ReceiveIndexes);
    ULONG Index;

    for (Index = 0; Index < Count; Index++)
    {
        Destination->NetBufferListInfo[Indexes[Index]] =
            Source->NetBufferListInfo[Indexes[Index]];
    }

    /* Frame type and protocol ID share one OOB slot. On sends, Windows copies
     * only the protocol byte; on receives, the full frame-type slot is copied
     * through ReceiveIndexes above. SourceHandle and the general Flags fields
     * describe ownership and are intentionally not propagated. */
    if (SendPath)
    {
        NdisSetNetBufferListProtocolId(
            Destination,
            NdisGetNetBufferListProtocolId(Source));
#if NDIS_SUPPORT_NDIS682
        if (Source->NblFlags & NDIS_NBL_FLAGS_CAPTURE_TIMESTAMP_ON_TRANSMIT)
        {
            Destination->NblFlags |=
                NDIS_NBL_FLAGS_CAPTURE_TIMESTAMP_ON_TRANSMIT;
        }
#endif
    }
    else if (Source->NblFlags & NDIS_NBL_FLAGS_IS_LOOPBACK_PACKET)
    {
        Destination->NblFlags |= NDIS_NBL_FLAGS_IS_LOOPBACK_PACKET;
        Destination->NetBufferListInfo[NetBufferListCancelId] =
            Source->NetBufferListInfo[NetBufferListCancelId];
    }
#if NDIS_SUPPORT_NDIS680
    else
    {
        Destination->NetBufferListInfo[NetBufferListInfoReserved3] =
            Source->NetBufferListInfo[NetBufferListInfoReserved3];
    }
#endif

    /* WfpNetBufferListInfo is reference-counted by a private NDIS helper on
     * Windows and cannot be propagated safely as a raw pointer here. */
}

UCHAR
NTAPI
NdisGetNetBufferListProtocolId(
    _In_ const NET_BUFFER_LIST *NetBufferList)
{
    PNDIS6_NBL_POOL Pool;
    UCHAR ProtocolId;

    if (NetBufferList == NULL)
        return NDIS_PROTOCOL_ID_DEFAULT;

    ProtocolId = *((const UCHAR *)&NET_BUFFER_LIST_INFO(
        NetBufferList, NetBufferListProtocolId)) & NDIS_PROTOCOL_ID_MASK;
    if (ProtocolId != NDIS_PROTOCOL_ID_DEFAULT)
        return ProtocolId;

    Pool = (PNDIS6_NBL_POOL)NetBufferList->NdisPoolHandle;
    if (Pool != NULL && Pool->Magic == NDIS6_NBL_POOL_MAGIC)
        return Pool->ProtocolId;

    return NDIS_PROTOCOL_ID_DEFAULT;
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
        if (Context->Offset > Context->Size)
            return NDIS_STATUS_FAILURE;

        Context->Offset -= ContextSize;
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
    Context->Size = ContextSize + ContextBackFill;
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
    USHORT Available;
    USHORT Remaining;
    ULONG PoolTag;

    if (NetBufferList == NULL || NetBufferList->Context == NULL ||
        ContextSize == 0 ||
        (ContextSize & (sizeof(PVOID) - 1)) != 0)
    {
        return;
    }

    EmbeddedContext = (PNET_BUFFER_LIST_CONTEXT)
        ((PUCHAR)NetBufferList + sizeof(NET_BUFFER_LIST));
    Remaining = ContextSize;

    while (Remaining != 0)
    {
        Context = NetBufferList->Context;
        if (Context == NULL || Context->Offset > Context->Size)
            return;

        Available = Context->Size - Context->Offset;
        if (Context == EmbeddedContext)
        {
            if (Remaining > Available)
                return;

            Context->Offset += Remaining;
            return;
        }

        Allocation = (PNDIS6_CONTEXT_ALLOCATION_HEADER)
            ((PUCHAR)Context - NDIS6_CONTEXT_ALLOCATION_HEADER_SIZE);
        if (Allocation->Magic != NDIS6_CONTEXT_ALLOCATION_MAGIC)
            return;

        if (Remaining < Available)
        {
            Context->Offset += Remaining;
            return;
        }

        Remaining -= Available;
        NetBufferList->Context = Context->Next;
        PoolTag = Allocation->PoolTag;
        Allocation->Magic = 0;
        ExFreePoolWithTag(Allocation, PoolTag);

        if (NetBufferList->Context == EmbeddedContext ||
            NetBufferList->Context == NULL)
        {
            NetBufferList->Flags &= ~NDIS6_NBL_FLAG_ALLOCATED_CONTEXT;
        }
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

static PNDIS6_NBL_ALLOCATION_HEADER
Ndis6GetNetBufferListAllocation(
    _In_ PNET_BUFFER_LIST NetBufferList)
{
    PNDIS6_NBL_ALLOCATION_HEADER Allocation;

    if (NetBufferList == NULL)
        return NULL;

    Allocation = (PNDIS6_NBL_ALLOCATION_HEADER)
        ((PUCHAR)NetBufferList - NDIS6_NBL_ALLOCATION_HEADER_SIZE);
    return Allocation->Magic == NDIS6_NBL_ALLOCATION_MAGIC ? Allocation : NULL;
}

static PNET_BUFFER_LIST
Ndis6AllocateInternalNetBufferList(VOID)
{
    PNDIS6_NBL_ALLOCATION_HEADER Allocation;
    PNET_BUFFER_LIST NetBufferList;
    ULONG AllocationSize;

    AllocationSize = NDIS6_NBL_ALLOCATION_HEADER_SIZE +
                     sizeof(NET_BUFFER_LIST);
    Allocation = (PNDIS6_NBL_ALLOCATION_HEADER)ExAllocatePoolWithTag(NonPagedPool, AllocationSize, NBL_TAG);
    if (Allocation == NULL)
        return NULL;

    RtlZeroMemory(Allocation, AllocationSize);
    Allocation->Magic = NDIS6_NBL_ALLOCATION_MAGIC;
    Allocation->PoolTag = NBL_TAG;
    NetBufferList = (PNET_BUFFER_LIST)
        ((PUCHAR)Allocation + NDIS6_NBL_ALLOCATION_HEADER_SIZE);
    return NetBufferList;
}

static VOID
Ndis6FreeMdlChain(
    _In_opt_ PMDL MdlChain)
{
    while (MdlChain != NULL)
    {
        PMDL Next = MdlChain->Next;

        MdlChain->Next = NULL;
        IoFreeMdl(MdlChain);
        MdlChain = Next;
    }
}

static VOID
Ndis6AppendMdlChain(
    _Inout_ PMDL *MdlChain,
    _In_opt_ PMDL AppendedChain)
{
    PMDL Tail;

    if (AppendedChain == NULL)
        return;

    if (*MdlChain == NULL)
    {
        *MdlChain = AppendedChain;
        return;
    }

    Tail = *MdlChain;
    while (Tail->Next != NULL)
        Tail = Tail->Next;
    Tail->Next = AppendedChain;
}

static BOOLEAN
Ndis6MdlRangeFits(
    _In_opt_ PMDL MdlChain,
    _In_ ULONG DataOffset,
    _In_ ULONG DataLength)
{
    PMDL Mdl = MdlChain;
    ULONG Remaining = DataLength;

    while (Mdl != NULL)
    {
        ULONG MdlLength = MmGetMdlByteCount(Mdl);

        if (DataOffset < MdlLength)
            break;
        if (DataOffset == MdlLength)
        {
            if (Remaining == 0)
                return TRUE;
            DataOffset = 0;
            Mdl = Mdl->Next;
            continue;
        }

        DataOffset -= MdlLength;
        Mdl = Mdl->Next;
    }

    if (Remaining == 0)
        return DataOffset == 0 || Mdl != NULL;

    while (Mdl != NULL)
    {
        ULONG MdlLength = MmGetMdlByteCount(Mdl);
        ULONG Available;

        if (DataOffset > MdlLength)
            return FALSE;
        Available = MdlLength - DataOffset;
        if (Remaining <= Available)
            return TRUE;
        Remaining -= Available;
        DataOffset = 0;
        Mdl = Mdl->Next;
    }

    return FALSE;
}

static BOOLEAN
Ndis6BuildPartialMdlChain(
    _In_opt_ PMDL SourceMdlChain,
    _In_ ULONG DataOffset,
    _In_ ULONG DataLength,
    _Out_ PMDL *PartialMdlChain)
{
    PMDL SourceMdl = SourceMdlChain;
    PMDL NewChain = NULL;
    ULONG Remaining = DataLength;

    *PartialMdlChain = NULL;
    if (DataLength == 0)
        return Ndis6MdlRangeFits(SourceMdlChain, DataOffset, 0);

    while (SourceMdl != NULL)
    {
        ULONG SourceLength = MmGetMdlByteCount(SourceMdl);

        if (DataOffset < SourceLength)
            break;
        DataOffset -= SourceLength;
        SourceMdl = SourceMdl->Next;
    }

    while (SourceMdl != NULL && Remaining != 0)
    {
        ULONG SourceLength = MmGetMdlByteCount(SourceMdl);
        ULONG PartialLength;
        PVOID VirtualAddress;
        PMDL PartialMdl;

        if (DataOffset > SourceLength)
            goto Failure;
        PartialLength = min(Remaining, SourceLength - DataOffset);
        if (PartialLength == 0)
        {
            DataOffset = 0;
            SourceMdl = SourceMdl->Next;
            continue;
        }

        VirtualAddress = (PUCHAR)MmGetMdlVirtualAddress(SourceMdl) +
                         DataOffset;
        PartialMdl = IoAllocateMdl(VirtualAddress, PartialLength, FALSE, FALSE, NULL);
        if (PartialMdl == NULL)
            goto Failure;

        IoBuildPartialMdl(SourceMdl, PartialMdl, VirtualAddress, PartialLength);
        PartialMdl->Next = NULL;
        Ndis6AppendMdlChain(&NewChain, PartialMdl);

        Remaining -= PartialLength;
        DataOffset = 0;
        SourceMdl = SourceMdl->Next;
    }

    if (Remaining != 0)
        goto Failure;

    *PartialMdlChain = NewChain;
    return TRUE;

Failure:
    Ndis6FreeMdlChain(NewChain);
    return FALSE;
}

static BOOLEAN
Ndis6BuildDerivedMdlRange(
    _In_ PNET_BUFFER SourceNetBuffer,
    _In_ ULONG AbsoluteDataOffset,
    _In_ ULONG DataLength,
    _In_ ULONG DataOffsetDelta,
    _In_ ULONG DataBackFill,
    _Out_ PMDL *MdlChain,
    _Out_ PULONG NewDataOffset,
    _Out_ PULONG NewDataLength,
    _Outptr_result_maybenull_ PVOID *OwnedData)
{
    PMDL PrefixMdl = NULL;
    PMDL SourceMdlChain = NULL;
    PVOID PrefixData = NULL;
    ULONG SourceOffset;
    ULONG SourceLength;
    ULONG PrefixLength;

    *MdlChain = NULL;
    *NewDataOffset = 0;
    *NewDataLength = 0;
    *OwnedData = NULL;

    if (DataOffsetDelta > MAXULONG - DataLength)
        return FALSE;

    if (AbsoluteDataOffset >= DataOffsetDelta)
    {
        SourceOffset = AbsoluteDataOffset - DataOffsetDelta;
        SourceLength = DataLength + DataOffsetDelta;
    }
    else
    {
        ULONG MissingHeadroom = DataOffsetDelta - AbsoluteDataOffset;

        if (MissingHeadroom > MAXULONG - DataBackFill ||
            AbsoluteDataOffset > MAXULONG - DataLength)
        {
            return FALSE;
        }

        PrefixLength = MissingHeadroom + DataBackFill;
        PrefixData = ExAllocatePoolWithTag(NonPagedPool, PrefixLength, DERIVED_DATA_TAG);
        if (PrefixData == NULL)
            return FALSE;

        PrefixMdl = IoAllocateMdl(PrefixData, PrefixLength, FALSE, FALSE, NULL);
        if (PrefixMdl == NULL)
        {
            ExFreePoolWithTag(PrefixData, DERIVED_DATA_TAG);
            return FALSE;
        }
        MmBuildMdlForNonPagedPool(PrefixMdl);
        PrefixMdl->Next = NULL;

        SourceOffset = 0;
        SourceLength = AbsoluteDataOffset + DataLength;
        *NewDataOffset = DataBackFill;
    }

    if (!Ndis6BuildPartialMdlChain(SourceNetBuffer->MdlChain, SourceOffset, SourceLength, &SourceMdlChain))
    {
        if (PrefixMdl != NULL)
            IoFreeMdl(PrefixMdl);
        if (PrefixData != NULL)
            ExFreePoolWithTag(PrefixData, DERIVED_DATA_TAG);
        return FALSE;
    }

    *MdlChain = PrefixMdl;
    Ndis6AppendMdlChain(MdlChain, SourceMdlChain);
    *NewDataLength = DataLength + DataOffsetDelta;
    *OwnedData = PrefixData;
    return TRUE;
}

static PNET_BUFFER_LIST
Ndis6AllocateDerivedNetBufferList(
    _In_opt_ NDIS_HANDLE NetBufferListPool,
    _In_ NDIS6_DERIVED_NBL_KIND Kind,
    _In_ ULONG AllocateFlags,
    _In_ ULONG DataOffsetDelta)
{
    PNDIS6_DERIVED_NBL_CONTEXT Context;
    PNDIS6_NBL_ALLOCATION_HEADER Allocation;
    PNDIS6_NBL_POOL Pool = (PNDIS6_NBL_POOL)NetBufferListPool;
    PNET_BUFFER_LIST NetBufferList;

    if (Pool != NULL && Pool->Magic != NDIS6_NBL_POOL_MAGIC)
        return NULL;

    Context = (PNDIS6_DERIVED_NBL_CONTEXT)ExAllocatePoolWithTag(NonPagedPool, sizeof(*Context), DERIVED_NBL_TAG);
    if (Context == NULL)
        return NULL;
    RtlZeroMemory(Context, sizeof(*Context));

    if (Pool != NULL)
        NetBufferList = NdisAllocateNetBufferList(NetBufferListPool, 0, 0);
    else
        NetBufferList = Ndis6AllocateInternalNetBufferList();
    if (NetBufferList == NULL)
    {
        ExFreePoolWithTag(Context, DERIVED_NBL_TAG);
        return NULL;
    }

    Allocation = Ndis6GetNetBufferListAllocation(NetBufferList);
    if (Allocation == NULL)
    {
        NdisFreeNetBufferList(NetBufferList);
        ExFreePoolWithTag(Context, DERIVED_NBL_TAG);
        return NULL;
    }

    Context->Magic = NDIS6_DERIVED_NBL_MAGIC;
    Context->Kind = Kind;
    Context->AllocateFlags = AllocateFlags;
    Context->DataOffsetDelta = DataOffsetDelta;
    Context->EmbeddedNetBuffer = NetBufferList->FirstNetBuffer;

    /* Clone, fragment, and reassembled NBLs never expose a pool's initial
     * context. The storage can remain part of the allocation for reuse/free. */
    NetBufferList->Context = NULL;
    NetBufferList->FirstNetBuffer = NULL;
    Allocation->DerivedContext = Context;
    return NetBufferList;
}

static PNET_BUFFER
Ndis6AllocateDerivedNetBuffer(
    _Inout_ PNDIS6_DERIVED_NBL_CONTEXT Context,
    _In_opt_ NDIS_HANDLE NetBufferPool,
    _In_opt_ PMDL MdlChain,
    _In_ ULONG DataOffset,
    _In_ ULONG DataLength,
    _In_opt_ PMDL OwnedMdlChain,
    _In_opt_ PVOID OwnedData)
{
    PNDIS6_DERIVED_NB_RECORD Record;
    PNDIS6_NB_POOL Pool = (PNDIS6_NB_POOL)NetBufferPool;
    PNET_BUFFER NetBuffer = NULL;
    NDIS6_DERIVED_NB_STORAGE Storage;

    if (Pool != NULL &&
        (Pool->Magic != NDIS6_NB_POOL_MAGIC || Pool->DataSize != 0))
    {
        goto Failure;
    }

    Record = (PNDIS6_DERIVED_NB_RECORD)ExAllocatePoolWithTag(NonPagedPool, sizeof(*Record), DERIVED_NB_TAG);
    if (Record == NULL)
        goto Failure;
    RtlZeroMemory(Record, sizeof(*Record));

    if (Context->EmbeddedNetBuffer != NULL)
    {
        NetBuffer = Context->EmbeddedNetBuffer;
        Context->EmbeddedNetBuffer = NULL;
        RtlZeroMemory(NetBuffer, sizeof(*NetBuffer));
        Storage = Ndis6DerivedNbEmbedded;
    }
    else if (Pool != NULL)
    {
        NetBuffer = NdisAllocateNetBuffer(NetBufferPool, MdlChain, DataOffset, DataLength);
        Storage = Ndis6DerivedNbPool;
    }
    else
    {
        NetBuffer = (PNET_BUFFER)ExAllocatePoolWithTag(NonPagedPool, sizeof(*NetBuffer), INTERNAL_NB_TAG);
        if (NetBuffer != NULL)
            RtlZeroMemory(NetBuffer, sizeof(*NetBuffer));
        Storage = Ndis6DerivedNbInternal;
    }

    if (NetBuffer == NULL)
    {
        ExFreePoolWithTag(Record, DERIVED_NB_TAG);
        goto Failure;
    }

    if (Storage != Ndis6DerivedNbPool)
    {
        NetBuffer->MdlChain = MdlChain;
        if ((MdlChain == NULL && (DataOffset != 0 || DataLength != 0)) ||
            (MdlChain != NULL &&
             (!Ndis6SetNetBufferDataOffset(NetBuffer, DataOffset) ||
              !Ndis6NetBufferDataRangeFits(NetBuffer, DataLength))))
        {
            if (Storage == Ndis6DerivedNbEmbedded)
                Context->EmbeddedNetBuffer = NetBuffer;
            else
                ExFreePoolWithTag(NetBuffer, INTERNAL_NB_TAG);
            ExFreePoolWithTag(Record, DERIVED_NB_TAG);
            goto Failure;
        }
        NetBuffer->DataLength = DataLength;
        NetBuffer->DataOffset = DataOffset;
    }

    Record->NetBuffer = NetBuffer;
    Record->OwnedMdlChain = OwnedMdlChain;
    Record->OwnedData = OwnedData;
    Record->Storage = Storage;
    Record->Next = Context->NetBuffers;
    Context->NetBuffers = Record;
    return NetBuffer;

Failure:
    Ndis6FreeMdlChain(OwnedMdlChain);
    if (OwnedData != NULL)
        ExFreePoolWithTag(OwnedData, DERIVED_DATA_TAG);
    return NULL;
}

static VOID
Ndis6FreeDerivedNetBufferList(
    _In_ PNET_BUFFER_LIST NetBufferList,
    _In_ NDIS6_DERIVED_NBL_KIND ExpectedKind,
    _In_ ULONG DataOffsetDelta,
    _In_ ULONG FreeFlags)
{
    PNDIS6_NBL_ALLOCATION_HEADER Allocation;
    PNDIS6_DERIVED_NBL_CONTEXT Context;
    PNDIS6_DERIVED_NB_RECORD Record;
    PNET_BUFFER_LIST ParentNetBufferList = NULL;

    UNREFERENCED_PARAMETER(DataOffsetDelta);
    UNREFERENCED_PARAMETER(FreeFlags);

    Allocation = Ndis6GetNetBufferListAllocation(NetBufferList);
    if (Allocation == NULL)
        return;

    Context = (PNDIS6_DERIVED_NBL_CONTEXT)Allocation->DerivedContext;
    if (Context == NULL || Context->Magic != NDIS6_DERIVED_NBL_MAGIC ||
        Context->Kind != ExpectedKind)
    {
        return;
    }

    if (ExpectedKind == Ndis6DerivedClone)
    {
        ParentNetBufferList = NetBufferList->ParentNetBufferList;
        NetBufferList->ParentNetBufferList = NULL;
    }

    Record = Context->NetBuffers;
    while (Record != NULL)
    {
        PNDIS6_DERIVED_NB_RECORD Next = Record->Next;

        Ndis6FreeMdlChain(Record->OwnedMdlChain);
        if (Record->OwnedData != NULL)
            ExFreePoolWithTag(Record->OwnedData, DERIVED_DATA_TAG);

        Record->NetBuffer->MdlChain = NULL;
        Record->NetBuffer->CurrentMdl = NULL;
        if (Record->Storage == Ndis6DerivedNbPool)
            NdisFreeNetBuffer(Record->NetBuffer);
        else if (Record->Storage == Ndis6DerivedNbInternal)
            ExFreePoolWithTag(Record->NetBuffer, INTERNAL_NB_TAG);

        ExFreePoolWithTag(Record, DERIVED_NB_TAG);
        Record = Next;
    }

    NetBufferList->FirstNetBuffer = NULL;
    Context->Magic = 0;
    Allocation->DerivedContext = NULL;
    NdisFreeNetBufferList(NetBufferList);
    ExFreePoolWithTag(Context, DERIVED_NBL_TAG);

    if (ParentNetBufferList != NULL)
    {
        LONG ChildReferences =
            InterlockedDecrement(&ParentNetBufferList->ChildRefCount);
        ASSERT(ChildReferences >= 0);
    }
}

static VOID
Ndis6CopyDerivedNetBufferListInfo(
    _Out_ PNET_BUFFER_LIST Destination,
    _In_ const NET_BUFFER_LIST *Source,
    _In_ BOOLEAN Clone)
{
    /* NdisAllocateCloneNetBufferList preserves SourceHandle. Fragment and
     * reassembly do not. */
    if (Clone)
        Destination->SourceHandle = Source->SourceHandle;

#if NDIS_SUPPORT_NDIS620
    Destination->NetBufferListInfo[NetBufferListCorrelationId] =
        Source->NetBufferListInfo[NetBufferListCorrelationId];
    Destination->NetBufferListInfo[NblOriginalInterfaceIfIndex] =
        Source->NetBufferListInfo[NblOriginalInterfaceIfIndex];
#endif

    /* WfpNetBufferListInfo is refcounted by the Windows filtering platform.
     * ReactOS has no matching ownership helper, so never raw-copy that slot. */
}

PNET_BUFFER_LIST
NTAPI
NdisAllocateCloneNetBufferList(
    _In_ PNET_BUFFER_LIST OriginalNetBufferList,
    _In_opt_ NDIS_HANDLE NetBufferListPoolHandle,
    _In_opt_ NDIS_HANDLE NetBufferPoolHandle,
    _In_ ULONG AllocateCloneFlags)
{
    PNDIS6_NB_POOL NetBufferPool = (PNDIS6_NB_POOL)NetBufferPoolHandle;
    PNDIS6_DERIVED_NBL_CONTEXT Context;
    PNDIS6_NBL_ALLOCATION_HEADER Allocation;
    PNET_BUFFER_LIST CloneNetBufferList;
    PNET_BUFFER SourceNetBuffer;
    PNET_BUFFER LastClone = NULL;

    if (OriginalNetBufferList == NULL ||
        (AllocateCloneFlags & ~(NDIS_CLONE_FLAGS_RESERVED |
                                NDIS_CLONE_FLAGS_USE_ORIGINAL_MDLS)) != 0 ||
        (NetBufferPool != NULL &&
         (NetBufferPool->Magic != NDIS6_NB_POOL_MAGIC ||
          NetBufferPool->DataSize != 0)))
    {
        return NULL;
    }

    CloneNetBufferList = Ndis6AllocateDerivedNetBufferList(NetBufferListPoolHandle, Ndis6DerivedClone, AllocateCloneFlags, 0);
    if (CloneNetBufferList == NULL)
        return NULL;

    Allocation = Ndis6GetNetBufferListAllocation(CloneNetBufferList);
    Context = (PNDIS6_DERIVED_NBL_CONTEXT)Allocation->DerivedContext;

    for (SourceNetBuffer = OriginalNetBufferList->FirstNetBuffer;
         SourceNetBuffer != NULL;
         SourceNetBuffer = SourceNetBuffer->Next)
    {
        PMDL CloneMdlChain;
        PMDL OwnedMdlChain = NULL;
        PNET_BUFFER CloneNetBuffer;
        ULONG CloneDataOffset;

        if (AllocateCloneFlags & NDIS_CLONE_FLAGS_USE_ORIGINAL_MDLS)
        {
            if (!Ndis6MdlRangeFits(SourceNetBuffer->MdlChain, SourceNetBuffer->DataOffset, SourceNetBuffer->DataLength))
            {
                goto Failure;
            }
            CloneMdlChain = SourceNetBuffer->MdlChain;
            CloneDataOffset = SourceNetBuffer->DataOffset;
        }
        else
        {
            if (!Ndis6BuildPartialMdlChain(SourceNetBuffer->MdlChain, SourceNetBuffer->DataOffset, SourceNetBuffer->DataLength, &OwnedMdlChain))
            {
                goto Failure;
            }
            CloneMdlChain = OwnedMdlChain;
            CloneDataOffset = 0;
        }

        CloneNetBuffer = Ndis6AllocateDerivedNetBuffer(Context, NetBufferPoolHandle, CloneMdlChain, CloneDataOffset, SourceNetBuffer->DataLength, OwnedMdlChain, NULL);
        if (CloneNetBuffer == NULL)
            goto Failure;

        if (LastClone != NULL)
            LastClone->Next = CloneNetBuffer;
        else
            CloneNetBufferList->FirstNetBuffer = CloneNetBuffer;
        LastClone = CloneNetBuffer;
    }

    /* The Windows helper allocates its first NET_BUFFER before walking the
     * source list, so even an empty source NBL produces one empty clone NB. */
    if (CloneNetBufferList->FirstNetBuffer == NULL)
    {
        CloneNetBufferList->FirstNetBuffer = Ndis6AllocateDerivedNetBuffer(Context, NetBufferPoolHandle, NULL, 0, 0, NULL, NULL);
        if (CloneNetBufferList->FirstNetBuffer == NULL)
            goto Failure;
    }

    Ndis6CopyDerivedNetBufferListInfo(CloneNetBufferList, OriginalNetBufferList, TRUE);
    CloneNetBufferList->ParentNetBufferList = OriginalNetBufferList;
    InterlockedIncrement(&OriginalNetBufferList->ChildRefCount);
    return CloneNetBufferList;

Failure:
    Ndis6FreeDerivedNetBufferList(CloneNetBufferList, Ndis6DerivedClone, 0, AllocateCloneFlags);
    return NULL;
}

VOID
NTAPI
NdisFreeCloneNetBufferList(
    _In_ PNET_BUFFER_LIST CloneNetBufferList,
    _In_ ULONG FreeCloneFlags)
{
    Ndis6FreeDerivedNetBufferList(CloneNetBufferList, Ndis6DerivedClone, 0, FreeCloneFlags);
}

PNET_BUFFER_LIST
NTAPI
NdisAllocateFragmentNetBufferList(
    _In_ PNET_BUFFER_LIST OriginalNetBufferList,
    _In_opt_ NDIS_HANDLE NetBufferListPool,
    _In_opt_ NDIS_HANDLE NetBufferPool,
    _In_ ULONG StartOffset,
    _In_ ULONG MaximumLength,
    _In_ ULONG DataOffsetDelta,
    _In_ ULONG DataBackFill,
    _In_ ULONG AllocateFragmentFlags)
{
    PNDIS6_NB_POOL Pool = (PNDIS6_NB_POOL)NetBufferPool;
    PNDIS6_DERIVED_NBL_CONTEXT Context;
    PNDIS6_NBL_ALLOCATION_HEADER Allocation;
    PNET_BUFFER_LIST FragmentNetBufferList;
    PNET_BUFFER LastFragment = NULL;
    PNET_BUFFER SourceNetBuffer;

    if (OriginalNetBufferList == NULL || MaximumLength == 0 ||
        AllocateFragmentFlags != 0 ||
        (Pool != NULL &&
         (Pool->Magic != NDIS6_NB_POOL_MAGIC || Pool->DataSize != 0)))
    {
        return NULL;
    }

    FragmentNetBufferList = Ndis6AllocateDerivedNetBufferList(NetBufferListPool, Ndis6DerivedFragment, AllocateFragmentFlags, DataOffsetDelta);
    if (FragmentNetBufferList == NULL)
        return NULL;

    Allocation = Ndis6GetNetBufferListAllocation(FragmentNetBufferList);
    Context = (PNDIS6_DERIVED_NBL_CONTEXT)Allocation->DerivedContext;

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
            PMDL FragmentMdlChain;
            PVOID OwnedData;
            PNET_BUFFER Fragment;
            ULONG FragmentLength = min(Remaining, MaximumLength);
            ULONG FragmentOffset;
            ULONG NewDataOffset;
            ULONG NewDataLength;

            if (SourceNetBuffer->DataOffset > MAXULONG - StartOffset ||
                SourceNetBuffer->DataOffset + StartOffset >
                    MAXULONG - Consumed)
            {
                goto Failure;
            }
            FragmentOffset = SourceNetBuffer->DataOffset +
                             StartOffset + Consumed;

            if (!Ndis6BuildDerivedMdlRange(SourceNetBuffer, FragmentOffset, FragmentLength, DataOffsetDelta, DataBackFill, &FragmentMdlChain, &NewDataOffset, &NewDataLength, &OwnedData))
            {
                goto Failure;
            }

            Fragment = Ndis6AllocateDerivedNetBuffer(Context, NetBufferPool, FragmentMdlChain, NewDataOffset, NewDataLength, FragmentMdlChain, OwnedData);
            if (Fragment == NULL)
                goto Failure;

            if (LastFragment != NULL)
                LastFragment->Next = Fragment;
            else
                FragmentNetBufferList->FirstNetBuffer = Fragment;
            LastFragment = Fragment;

            Consumed += FragmentLength;
            Remaining -= FragmentLength;
        }
    }

    if (FragmentNetBufferList->FirstNetBuffer == NULL)
        goto Failure;

    Ndis6CopyDerivedNetBufferListInfo(FragmentNetBufferList, OriginalNetBufferList, FALSE);
    return FragmentNetBufferList;

Failure:
    Ndis6FreeDerivedNetBufferList(FragmentNetBufferList, Ndis6DerivedFragment, DataOffsetDelta, AllocateFragmentFlags);
    return NULL;
}

VOID
NTAPI
NdisFreeFragmentNetBufferList(
    _In_ PNET_BUFFER_LIST FragmentNetBufferList,
    _In_ ULONG DataOffsetDelta,
    _In_ ULONG FreeFragmentFlags)
{
    Ndis6FreeDerivedNetBufferList(FragmentNetBufferList, Ndis6DerivedFragment, DataOffsetDelta, FreeFragmentFlags);
}

PNET_BUFFER_LIST
NTAPI
NdisAllocateReassembledNetBufferList(
    _In_ PNET_BUFFER_LIST FragmentNetBufferList,
    _In_opt_ NDIS_HANDLE NetBufferAndNetBufferListPoolHandle,
    _In_ ULONG StartOffset,
    _In_ ULONG DataOffsetDelta,
    _In_ ULONG DataBackFill,
    _In_ ULONG AllocateReassembleFlags)
{
    PNDIS6_NBL_POOL Pool =
        (PNDIS6_NBL_POOL)NetBufferAndNetBufferListPoolHandle;
    PNDIS6_DERIVED_NBL_CONTEXT Context;
    PNDIS6_NBL_ALLOCATION_HEADER Allocation;
    PNET_BUFFER_LIST ReassembledNetBufferList;
    PNET_BUFFER SourceNetBuffer;
    PNET_BUFFER ReassembledNetBuffer;
    PMDL ReassembledMdlChain = NULL;
    PVOID OwnedData = NULL;
    ULONG NewDataOffset = 0;
    ULONG NewDataLength = 0;
    BOOLEAN FirstRange = TRUE;

    if (FragmentNetBufferList == NULL || AllocateReassembleFlags != 0 ||
        (Pool != NULL &&
         (Pool->Magic != NDIS6_NBL_POOL_MAGIC ||
          !Pool->fAllocateNetBuffer || Pool->DataSize != 0)))
    {
        return NULL;
    }

    ReassembledNetBufferList = Ndis6AllocateDerivedNetBufferList(NetBufferAndNetBufferListPoolHandle, Ndis6DerivedReassembled, AllocateReassembleFlags, DataOffsetDelta);
    if (ReassembledNetBufferList == NULL)
        return NULL;

    Allocation = Ndis6GetNetBufferListAllocation(ReassembledNetBufferList);
    Context = (PNDIS6_DERIVED_NBL_CONTEXT)Allocation->DerivedContext;

    for (SourceNetBuffer = FragmentNetBufferList->FirstNetBuffer;
         SourceNetBuffer != NULL;
         SourceNetBuffer = SourceNetBuffer->Next)
    {
        PMDL PartialMdlChain;
        ULONG SourceOffset;
        ULONG SourceLength;

        if (StartOffset >= SourceNetBuffer->DataLength)
            continue;
        if (SourceNetBuffer->DataOffset > MAXULONG - StartOffset)
            goto Failure;

        SourceOffset = SourceNetBuffer->DataOffset + StartOffset;
        SourceLength = SourceNetBuffer->DataLength - StartOffset;

        if (FirstRange)
        {
            if (!Ndis6BuildDerivedMdlRange(SourceNetBuffer, SourceOffset, SourceLength, DataOffsetDelta, DataBackFill, &ReassembledMdlChain, &NewDataOffset, &NewDataLength, &OwnedData))
            {
                goto Failure;
            }
            FirstRange = FALSE;
        }
        else
        {
            if (NewDataLength > MAXULONG - SourceLength ||
                !Ndis6BuildPartialMdlChain(SourceNetBuffer->MdlChain, SourceOffset, SourceLength, &PartialMdlChain))
            {
                goto Failure;
            }
            Ndis6AppendMdlChain(&ReassembledMdlChain, PartialMdlChain);
            NewDataLength += SourceLength;
        }
    }

    if (FirstRange)
        goto Failure;

    ReassembledNetBuffer = Ndis6AllocateDerivedNetBuffer(Context, NULL, ReassembledMdlChain, NewDataOffset, NewDataLength, ReassembledMdlChain, OwnedData);
    if (ReassembledNetBuffer == NULL)
        goto FailureAfterOwnershipTransfer;

    ReassembledNetBufferList->FirstNetBuffer = ReassembledNetBuffer;
    Ndis6CopyDerivedNetBufferListInfo(ReassembledNetBufferList, FragmentNetBufferList, FALSE);
    return ReassembledNetBufferList;

Failure:
    Ndis6FreeMdlChain(ReassembledMdlChain);
    if (OwnedData != NULL)
        ExFreePoolWithTag(OwnedData, DERIVED_DATA_TAG);
FailureAfterOwnershipTransfer:
    Ndis6FreeDerivedNetBufferList(ReassembledNetBufferList, Ndis6DerivedReassembled, DataOffsetDelta, AllocateReassembleFlags);
    return NULL;
}

VOID
NTAPI
NdisFreeReassembledNetBufferList(
    _In_ PNET_BUFFER_LIST ReassembledNetBufferList,
    _In_ ULONG DataOffsetDelta,
    _In_ ULONG FreeReassembleFlags)
{
    Ndis6FreeDerivedNetBufferList(ReassembledNetBufferList, Ndis6DerivedReassembled, DataOffsetDelta, FreeReassembleFlags);
}

/* EOF */
