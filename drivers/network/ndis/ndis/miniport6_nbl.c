/*
 * COPYRIGHT:       2026 Ahmed ARIF (arif.ing@outlook.com)
 * PROJECT:     ReactOS NDIS library
 * FILE:        ndis/miniport6_nbl.c
 * PURPOSE:     NDIS 6.x NET_BUFFER and NET_BUFFER_LIST Pool Management
 * PROGRAMMERS: ReactOS Development Team
 * NOTES:       This file implements the NDIS 6.x NBL-based send/receive APIs
 *              which replace the legacy packet-based model. It provides
 *              efficient pool-based allocation using NPAGED_LOOKASIDE_LISTs.
 */

#include "ndissys.h"

#if NDIS_SUPPORT_NDIS6

/*
 * Pool tags for NBL/NB allocations
 */
#define NDIS_NBL_POOL_TAG   'LBNn'   /* nNBL - NDIS NBL Pool */
#define NDIS_NB_POOL_TAG    'fBNn'   /* nNBf - NDIS NB Pool */
#define NDIS_NBL_TAG        'lBNn'   /* nNBl - NDIS NBL allocation */
#define NDIS_NB_TAG         'bBNn'   /* nNBb - NDIS NB allocation */
#define NDIS_NBL_CTX_TAG    'cBNn'   /* nNBc - NDIS NBL Context */

/*
 * Internal structure to track NDIS 6.x NET_BUFFER_LIST pools
 */
typedef struct _NDIS6_NBL_POOL {
    NDIS_OBJECT_HEADER Header;
    NDIS_HANDLE OwnerHandle;
    NPAGED_LOOKASIDE_LIST LookasideList;
    USHORT ContextSize;
    USHORT ContextBackFill;
    ULONG Tag;
    ULONG Flags;
    UCHAR ProtocolId;
    BOOLEAN fAllocateNetBuffer;
    ULONG DataSize;
    volatile LONG AllocatedCount;
    /* Associated NB pool for combined allocations */
    struct _NDIS6_NB_POOL *AssociatedNbPool;
} NDIS6_NBL_POOL, *PNDIS6_NBL_POOL;

/*
 * Internal structure to track NDIS 6.x NET_BUFFER pools
 */
typedef struct _NDIS6_NB_POOL {
    NDIS_OBJECT_HEADER Header;
    NDIS_HANDLE OwnerHandle;
    NPAGED_LOOKASIDE_LIST LookasideList;
    ULONG DataSize;
    ULONG Tag;
    volatile LONG AllocatedCount;
} NDIS6_NB_POOL, *PNDIS6_NB_POOL;

/*
 * Internal allocation structure for NBL with context
 * This structure is allocated from the lookaside list
 */
typedef struct _NDIS6_NBL_ALLOCATION {
    NET_BUFFER_LIST Nbl;
    /* Context follows immediately after NBL if ContextSize > 0 */
    /* UCHAR Context[]; */
} NDIS6_NBL_ALLOCATION, *PNDIS6_NBL_ALLOCATION;

/*
 * Ndis6iInitializeNetBufferList
 * Internal helper function to initialize a NET_BUFFER_LIST structure
 *
 * Parameters:
 *   NetBufferList - Pointer to the NET_BUFFER_LIST to initialize
 *   PoolHandle - Handle to the owning pool
 *   ContextSize - Size of context area
 *   ContextBackFill - Backfill for context
 */
static VOID
Ndis6iInitializeNetBufferList(
    _Out_ PNET_BUFFER_LIST NetBufferList,
    _In_ NDIS_HANDLE PoolHandle,
    _In_ USHORT ContextSize,
    _In_ USHORT ContextBackFill)
{
    RtlZeroMemory(NetBufferList, sizeof(NET_BUFFER_LIST));

    /* Initialize the NBL fields */
    NetBufferList->NdisPoolHandle = PoolHandle;
    NetBufferList->Next = NULL;
    NetBufferList->FirstNetBuffer = NULL;
    NetBufferList->Context = NULL;
    NetBufferList->ParentNetBufferList = NULL;
    NetBufferList->SourceHandle = NULL;
    NetBufferList->NblFlags = 0;
    NetBufferList->ChildRefCount = 0;
    NetBufferList->Flags = 0;
    NetBufferList->Status = NDIS_STATUS_SUCCESS;

    /* Allocate and initialize context if requested */
    if (ContextSize > 0)
    {
        PNET_BUFFER_LIST_CONTEXT Context;

        /* Context is allocated immediately after the NBL structure in pool allocation */
        /* Total context size = sizeof(NET_BUFFER_LIST_CONTEXT) + ContextSize + ContextBackFill */
        Context = (PNET_BUFFER_LIST_CONTEXT)((PUCHAR)NetBufferList + sizeof(NET_BUFFER_LIST));

        Context->Next = NULL;
        Context->Size = ContextSize;
        Context->Offset = ContextBackFill;

        NetBufferList->Context = Context;
    }
}

/*
 * Ndis6iInitializeNetBuffer
 * Internal helper function to initialize a NET_BUFFER structure
 *
 * Parameters:
 *   NetBuffer - Pointer to the NET_BUFFER to initialize
 *   PoolHandle - Handle to the owning pool
 *   MdlChain - Optional MDL chain to attach
 *   DataOffset - Offset into the first MDL where data starts
 *   DataLength - Length of data in the MDL chain
 */
static VOID
Ndis6iInitializeNetBuffer(
    _Out_ PNET_BUFFER NetBuffer,
    _In_ NDIS_HANDLE PoolHandle,
    _In_opt_ PMDL MdlChain,
    _In_ ULONG DataOffset,
    _In_ SIZE_T DataLength)
{
    RtlZeroMemory(NetBuffer, sizeof(NET_BUFFER));

    /* Initialize the NB fields */
    NetBuffer->NdisPoolHandle = PoolHandle;
    NetBuffer->Next = NULL;
    NetBuffer->MdlChain = MdlChain;
    NetBuffer->CurrentMdl = MdlChain;
    NetBuffer->CurrentMdlOffset = DataOffset;
    NetBuffer->DataOffset = DataOffset;
    NetBuffer->DataLength = DataLength;
    NetBuffer->ChecksumBias = 0;
    NetBuffer->Reserved = 0;

    /* Set data physical address to zero (will be set by scatter/gather if needed) */
    NetBuffer->DataPhysicalAddress.QuadPart = 0;
}

/*
 * Ndis6iCalculateMdlChainLength
 * Internal helper function to calculate the total data length in an MDL chain
 *
 * Parameters:
 *   MdlChain - Pointer to the first MDL in the chain
 *   DataOffset - Offset into the first MDL where data starts
 *
 * Returns:
 *   Total data length in bytes
 */
static SIZE_T
Ndis6iCalculateMdlChainLength(
    _In_ PMDL MdlChain,
    _In_ ULONG DataOffset)
{
    SIZE_T TotalLength = 0;
    PMDL CurrentMdl;
    BOOLEAN First = TRUE;

    for (CurrentMdl = MdlChain; CurrentMdl != NULL; CurrentMdl = CurrentMdl->Next)
    {
        ULONG MdlLength = MmGetMdlByteCount(CurrentMdl);

        if (First)
        {
            /* Subtract offset from first MDL */
            if (MdlLength > DataOffset)
            {
                TotalLength += (MdlLength - DataOffset);
            }
            First = FALSE;
        }
        else
        {
            TotalLength += MdlLength;
        }
    }

    return TotalLength;
}

/*
 * Ndis6iAdvanceCurrentMdl
 * Internal helper to advance CurrentMdl and CurrentMdlOffset
 * Note: Currently unused, reserved for future NdisAdvanceNetBufferDataStart implementation
 *
 * Parameters:
 *   NetBuffer - Pointer to the NET_BUFFER
 *   BytesToAdvance - Number of bytes to advance
 */
#ifdef __GNUC__
__attribute__((unused))
#endif
static VOID
Ndis6iAdvanceCurrentMdl(
    _Inout_ PNET_BUFFER NetBuffer,
    _In_ ULONG BytesToAdvance)
{
    PMDL CurrentMdl = NetBuffer->CurrentMdl;
    ULONG CurrentOffset = NetBuffer->CurrentMdlOffset;
    ULONG Remaining = BytesToAdvance;

    while (Remaining > 0 && CurrentMdl != NULL)
    {
        ULONG MdlLength = MmGetMdlByteCount(CurrentMdl);
        ULONG AvailableInMdl = MdlLength - CurrentOffset;

        if (Remaining < AvailableInMdl)
        {
            /* Can satisfy within current MDL */
            CurrentOffset += Remaining;
            Remaining = 0;
        }
        else
        {
            /* Move to next MDL */
            Remaining -= AvailableInMdl;
            CurrentMdl = CurrentMdl->Next;
            CurrentOffset = 0;
        }
    }

    NetBuffer->CurrentMdl = CurrentMdl;
    NetBuffer->CurrentMdlOffset = CurrentOffset;
}

/*
 * @implemented
 */
NDIS_HANDLE
EXPORT
NdisAllocateNetBufferListPool(
    _In_opt_ NDIS_HANDLE NdisHandle,
    _In_ PNET_BUFFER_LIST_POOL_PARAMETERS Parameters)
{
    PNDIS6_NBL_POOL Pool;
    ULONG AllocationSize;
    ULONG PoolTag;

    DPRINT("NdisAllocateNetBufferListPool called\n");

    /* Validate parameters */
    if (Parameters == NULL)
    {
        DPRINT1("NULL parameters\n");
        return NULL;
    }

    /* Validate header */
    if (Parameters->Header.Type != NDIS_OBJECT_TYPE_DEFAULT ||
        Parameters->Header.Revision < NET_BUFFER_LIST_POOL_PARAMETERS_REVISION_1)
    {
        DPRINT1("Invalid parameters header\n");
        return NULL;
    }

    /* Allocate pool structure */
    Pool = ExAllocatePoolWithTag(NonPagedPool,
                                 sizeof(NDIS6_NBL_POOL),
                                 NDIS_NBL_POOL_TAG);
    if (Pool == NULL)
    {
        DPRINT1("Failed to allocate NBL pool structure\n");
        return NULL;
    }

    RtlZeroMemory(Pool, sizeof(NDIS6_NBL_POOL));

    /* Initialize header */
    Pool->Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    Pool->Header.Revision = 1;
    Pool->Header.Size = sizeof(NDIS6_NBL_POOL);

    /* Store pool parameters */
    Pool->OwnerHandle = NdisHandle;
    Pool->ContextSize = Parameters->ContextSize;
    Pool->ContextBackFill = 0; /* Will be set during allocation */
    Pool->ProtocolId = Parameters->ProtocolId;
    Pool->fAllocateNetBuffer = Parameters->fAllocateNetBuffer;
    Pool->DataSize = Parameters->DataSize;
    Pool->AllocatedCount = 0;
    Pool->AssociatedNbPool = NULL;

    /* Use provided tag or default */
    PoolTag = (Parameters->PoolTag != 0) ? Parameters->PoolTag : NDIS_NBL_TAG;
    Pool->Tag = PoolTag;

    /* Calculate allocation size for lookaside list */
    AllocationSize = sizeof(NET_BUFFER_LIST);
    if (Parameters->ContextSize > 0)
    {
        AllocationSize += sizeof(NET_BUFFER_LIST_CONTEXT) + Parameters->ContextSize;
    }
    if (Parameters->fAllocateNetBuffer)
    {
        AllocationSize += sizeof(NET_BUFFER);
    }

    /* Initialize lookaside list */
    ExInitializeNPagedLookasideList(&Pool->LookasideList,
                                    NULL,  /* Use default allocator */
                                    NULL,  /* Use default free */
                                    0,     /* Flags */
                                    AllocationSize,
                                    PoolTag,
                                    0);    /* Depth - use default */

    DPRINT1("Created NBL pool %p, AllocSize=%lu, Tag=0x%x\n",
        Pool, AllocationSize, PoolTag);

    return (NDIS_HANDLE)Pool;
}

/*
 * @implemented
 */
VOID
EXPORT
NdisFreeNetBufferListPool(
    _In_ NDIS_HANDLE PoolHandle)
{
    PNDIS6_NBL_POOL Pool;

    DPRINT("NdisFreeNetBufferListPool called for handle %p\n", PoolHandle);

    if (PoolHandle == NULL)
    {
        DPRINT1("NULL pool handle\n");
        return;
    }

    Pool = (PNDIS6_NBL_POOL)PoolHandle;

    /* Check for leaked allocations */
    if (Pool->AllocatedCount > 0)
    {
        DPRINT1("WARNING: Freeing NBL pool with %ld outstanding allocations\n",
            Pool->AllocatedCount);
    }

    /* Delete the lookaside list */
    ExDeleteNPagedLookasideList(&Pool->LookasideList);

    /* Free the pool structure */
    ExFreePoolWithTag(Pool, NDIS_NBL_POOL_TAG);

    DPRINT1("NBL pool freed\n");
}

/*
 * @implemented
 */
PNET_BUFFER_LIST
EXPORT
NdisAllocateNetBufferList(
    _In_ NDIS_HANDLE PoolHandle,
    _In_ USHORT ContextSize,
    _In_ USHORT ContextBackFill)
{
    PNDIS6_NBL_POOL Pool;
    PNET_BUFFER_LIST NetBufferList;
    USHORT EffectiveContextSize;

    DPRINT("NdisAllocateNetBufferList called\n");

    if (PoolHandle == NULL)
    {
        DPRINT1("NULL pool handle\n");
        return NULL;
    }

    Pool = (PNDIS6_NBL_POOL)PoolHandle;

    /* Use pool's context size if caller didn't specify one */
    EffectiveContextSize = (ContextSize > 0) ? ContextSize : Pool->ContextSize;

    /* Allocate from lookaside list */
    NetBufferList = ExAllocateFromNPagedLookasideList(&Pool->LookasideList);
    if (NetBufferList == NULL)
    {
        DPRINT1("Failed to allocate from lookaside list\n");
        return NULL;
    }

    /* Initialize the NBL */
    Ndis6iInitializeNetBufferList(NetBufferList, PoolHandle, EffectiveContextSize, ContextBackFill);

    /* If pool was configured to allocate NB, attach one */
    if (Pool->fAllocateNetBuffer)
    {
        PNET_BUFFER NetBuffer;
        PUCHAR NbLocation;

        /* NB is allocated after NBL and context */
        NbLocation = (PUCHAR)NetBufferList + sizeof(NET_BUFFER_LIST);
        if (EffectiveContextSize > 0)
        {
            NbLocation += sizeof(NET_BUFFER_LIST_CONTEXT) + EffectiveContextSize + ContextBackFill;
        }

        NetBuffer = (PNET_BUFFER)NbLocation;
        Ndis6iInitializeNetBuffer(NetBuffer, PoolHandle, NULL, 0, 0);

        NetBufferList->FirstNetBuffer = NetBuffer;
    }

    /* Increment allocation count */
    InterlockedIncrement(&Pool->AllocatedCount);

    DPRINT("Allocated NBL %p from pool %p (count=%ld)\n",
           NetBufferList, Pool, Pool->AllocatedCount);

    return NetBufferList;
}

/*
 * @implemented
 */
VOID
EXPORT
NdisFreeNetBufferList(
    _In_ PNET_BUFFER_LIST NetBufferList)
{
    PNDIS6_NBL_POOL Pool;

    DPRINT("NdisFreeNetBufferList called for NBL %p\n", NetBufferList);

    if (NetBufferList == NULL)
    {
        DPRINT1("NULL NetBufferList\n");
        return;
    }

    /* Get the pool handle */
    Pool = (PNDIS6_NBL_POOL)NetBufferList->NdisPoolHandle;
    if (Pool == NULL)
    {
        DPRINT1("NBL has NULL pool handle\n");
        return;
    }

    /* Free any separately allocated context */
    /* Note: Inline context (allocated with NBL) is freed automatically */

    /* Decrement allocation count */
    InterlockedDecrement(&Pool->AllocatedCount);

    /* Return to lookaside list */
    ExFreeToNPagedLookasideList(&Pool->LookasideList, NetBufferList);

    DPRINT("Freed NBL to pool (count=%ld)\n", Pool->AllocatedCount);
}

/*
 * @implemented
 */
NDIS_HANDLE
EXPORT
NdisAllocateNetBufferPool(
    _In_opt_ NDIS_HANDLE NdisHandle,
    _In_ PNET_BUFFER_POOL_PARAMETERS Parameters)
{
    PNDIS6_NB_POOL Pool;
    ULONG AllocationSize;
    ULONG PoolTag;

    DPRINT("NdisAllocateNetBufferPool called\n");

    /* Validate parameters */
    if (Parameters == NULL)
    {
        DPRINT1("NULL parameters\n");
        return NULL;
    }

    /* Validate header */
    if (Parameters->Header.Type != NDIS_OBJECT_TYPE_DEFAULT ||
        Parameters->Header.Revision < NET_BUFFER_POOL_PARAMETERS_REVISION_1)
    {
        DPRINT1("Invalid parameters header\n");
        return NULL;
    }

    /* Allocate pool structure */
    Pool = ExAllocatePoolWithTag(NonPagedPool,
                                 sizeof(NDIS6_NB_POOL),
                                 NDIS_NB_POOL_TAG);
    if (Pool == NULL)
    {
        DPRINT1("Failed to allocate NB pool structure\n");
        return NULL;
    }

    RtlZeroMemory(Pool, sizeof(NDIS6_NB_POOL));

    /* Initialize header */
    Pool->Header.Type = NDIS_OBJECT_TYPE_DEFAULT;
    Pool->Header.Revision = 1;
    Pool->Header.Size = sizeof(NDIS6_NB_POOL);

    /* Store pool parameters */
    Pool->OwnerHandle = NdisHandle;
    Pool->DataSize = Parameters->DataSize;
    Pool->AllocatedCount = 0;

    /* Use provided tag or default */
    PoolTag = (Parameters->PoolTag != 0) ? Parameters->PoolTag : NDIS_NB_TAG;
    Pool->Tag = PoolTag;

    /* Calculate allocation size for lookaside list */
    AllocationSize = sizeof(NET_BUFFER);

    /* Initialize lookaside list */
    ExInitializeNPagedLookasideList(&Pool->LookasideList,
                                    NULL,  /* Use default allocator */
                                    NULL,  /* Use default free */
                                    0,     /* Flags */
                                    AllocationSize,
                                    PoolTag,
                                    0);    /* Depth - use default */

    DPRINT1("Created NB pool %p, AllocSize=%lu, Tag=0x%x\n",
        Pool, AllocationSize, PoolTag);

    return (NDIS_HANDLE)Pool;
}

/*
 * @implemented
 */
VOID
EXPORT
NdisFreeNetBufferPool(
    _In_ NDIS_HANDLE PoolHandle)
{
    PNDIS6_NB_POOL Pool;

    DPRINT("NdisFreeNetBufferPool called for handle %p\n", PoolHandle);

    if (PoolHandle == NULL)
    {
        DPRINT1("NULL pool handle\n");
        return;
    }

    Pool = (PNDIS6_NB_POOL)PoolHandle;

    /* Check for leaked allocations */
    if (Pool->AllocatedCount > 0)
    {
        DPRINT1("WARNING: Freeing NB pool with %ld outstanding allocations\n",
            Pool->AllocatedCount);
    }

    /* Delete the lookaside list */
    ExDeleteNPagedLookasideList(&Pool->LookasideList);

    /* Free the pool structure */
    ExFreePoolWithTag(Pool, NDIS_NB_POOL_TAG);

    DPRINT1("NB pool freed\n");
}

/*
 * @implemented
 */
PNET_BUFFER
EXPORT
NdisAllocateNetBuffer(
    _In_ NDIS_HANDLE PoolHandle,
    _In_opt_ PMDL MdlChain,
    _In_ ULONG DataOffset,
    _In_ SIZE_T DataLength)
{
    PNDIS6_NB_POOL Pool;
    PNET_BUFFER NetBuffer;

    DPRINT("NdisAllocateNetBuffer called\n");

    if (PoolHandle == NULL)
    {
        DPRINT1("NULL pool handle\n");
        return NULL;
    }

    Pool = (PNDIS6_NB_POOL)PoolHandle;

    /* Allocate from lookaside list */
    NetBuffer = ExAllocateFromNPagedLookasideList(&Pool->LookasideList);
    if (NetBuffer == NULL)
    {
        DPRINT1("Failed to allocate from lookaside list\n");
        return NULL;
    }

    /* Initialize the NB */
    Ndis6iInitializeNetBuffer(NetBuffer, PoolHandle, MdlChain, DataOffset, DataLength);

    /* Increment allocation count */
    InterlockedIncrement(&Pool->AllocatedCount);

    DPRINT1("Allocated NB %p from pool %p (count=%ld)\n",
        NetBuffer, Pool, Pool->AllocatedCount);

    return NetBuffer;
}

/*
 * @implemented
 */
VOID
EXPORT
NdisFreeNetBuffer(
    _In_ PNET_BUFFER NetBuffer)
{
    PNDIS6_NB_POOL Pool;

    DPRINT("NdisFreeNetBuffer called for NB %p\n", NetBuffer);

    if (NetBuffer == NULL)
    {
        DPRINT1("NULL NetBuffer\n");
        return;
    }

    /* Get the pool handle */
    Pool = (PNDIS6_NB_POOL)NetBuffer->NdisPoolHandle;
    if (Pool == NULL)
    {
        DPRINT1("NB has NULL pool handle\n");
        return;
    }

    /* Note: We don't free the MDL chain here - that's the caller's responsibility */

    /* Decrement allocation count */
    InterlockedDecrement(&Pool->AllocatedCount);

    /* Return to lookaside list */
    ExFreeToNPagedLookasideList(&Pool->LookasideList, NetBuffer);

    DPRINT1("Freed NB to pool (count=%ld)\n", Pool->AllocatedCount);
}

/*
 * @implemented
 */
PNET_BUFFER_LIST
EXPORT
NdisAllocateNetBufferAndNetBufferList(
    _In_ NDIS_HANDLE PoolHandle,
    _In_ USHORT ContextSize,
    _In_ USHORT ContextBackFill,
    _In_opt_ PMDL MdlChain,
    _In_ ULONG DataOffset,
    _In_ SIZE_T DataLength)
{
    PNDIS6_NBL_POOL Pool;
    PNET_BUFFER_LIST NetBufferList;
    PNET_BUFFER NetBuffer;
    SIZE_T ActualDataLength;

    DPRINT("NdisAllocateNetBufferAndNetBufferList called\n");

    if (PoolHandle == NULL)
    {
        DPRINT1("NULL pool handle\n");
        return NULL;
    }

    Pool = (PNDIS6_NBL_POOL)PoolHandle;

    /* Allocate the NBL first */
    NetBufferList = NdisAllocateNetBufferList(PoolHandle, ContextSize, ContextBackFill);
    if (NetBufferList == NULL)
    {
        DPRINT1("Failed to allocate NBL\n");
        return NULL;
    }

    /* If the pool was configured to allocate NB with NBL, use that NB */
    if (Pool->fAllocateNetBuffer && NetBufferList->FirstNetBuffer != NULL)
    {
        NetBuffer = NetBufferList->FirstNetBuffer;

        /* Update the NB with the MDL chain info */
        NetBuffer->MdlChain = MdlChain;
        NetBuffer->CurrentMdl = MdlChain;
        NetBuffer->CurrentMdlOffset = DataOffset;
        NetBuffer->DataOffset = DataOffset;
        NetBuffer->DataLength = DataLength;
    }
    else
    {
        /* Need to allocate a separate NB */
        /* First, we need an NB pool - use a simple allocation for now */
        NetBuffer = ExAllocatePoolWithTag(NonPagedPool,
                                          sizeof(NET_BUFFER),
                                          NDIS_NB_TAG);
        if (NetBuffer == NULL)
        {
            NdisFreeNetBufferList(NetBufferList);
            DPRINT1("Failed to allocate NB\n");
            return NULL;
        }

        /* Calculate actual data length if not specified */
        if (DataLength == 0 && MdlChain != NULL)
        {
            ActualDataLength = Ndis6iCalculateMdlChainLength(MdlChain, DataOffset);
        }
        else
        {
            ActualDataLength = DataLength;
        }

        /* Initialize the NB */
        Ndis6iInitializeNetBuffer(NetBuffer, NULL, MdlChain, DataOffset, ActualDataLength);

        NetBufferList->FirstNetBuffer = NetBuffer;
    }

    DPRINT("Allocated NBL %p with NB %p\n", NetBufferList, NetBuffer);

    return NetBufferList;
}

/*
 * @implemented
 */
NDIS_STATUS
EXPORT
NdisRetreatNetBufferDataStart(
    _In_ PNET_BUFFER NetBuffer,
    _In_ ULONG DataOffsetDelta,
    _In_ ULONG DataBackFill,
    _In_opt_ NET_BUFFER_ALLOCATE_MDL_HANDLER AllocateMdlHandler)
{
    ULONG CurrentOffset;
    PMDL NewMdl;
    ULONG NewMdlSize;

    DPRINT1("NdisRetreatNetBufferDataStart: NB=%p, Delta=%lu, BackFill=%lu\n",
        NetBuffer, DataOffsetDelta, DataBackFill);

    if (NetBuffer == NULL)
    {
        return NDIS_STATUS_INVALID_PARAMETER;
    }

    if (DataOffsetDelta == 0)
    {
        return NDIS_STATUS_SUCCESS;
    }

    CurrentOffset = NetBuffer->DataOffset;

    /*
     * Check if we can retreat within existing MDL space (using backfill)
     * The DataOffset represents unused space at the beginning that we can use
     */
    if (DataOffsetDelta <= CurrentOffset)
    {
        /* Can satisfy retreat within existing MDL space */
        NetBuffer->DataOffset -= DataOffsetDelta;
        NetBuffer->CurrentMdlOffset -= DataOffsetDelta;
        NetBuffer->DataLength += DataOffsetDelta;

        /* May need to adjust CurrentMdl if we crossed MDL boundaries going backward */
        /* For simplicity, reset CurrentMdl to start of chain and recalculate */
        if (NetBuffer->CurrentMdlOffset > MmGetMdlByteCount(NetBuffer->CurrentMdl))
        {
            NetBuffer->CurrentMdl = NetBuffer->MdlChain;
            NetBuffer->CurrentMdlOffset = NetBuffer->DataOffset;
        }

        DPRINT1("Retreat satisfied within existing space\n");
        return NDIS_STATUS_SUCCESS;
    }

    /*
     * Need to allocate new MDL space
     * This happens when we need more header space than available in backfill
     */
    if (AllocateMdlHandler == NULL)
    {
        DPRINT1("Need new MDL but no allocator provided\n");
        return NDIS_STATUS_RESOURCES;
    }

    /* Calculate size needed */
    NewMdlSize = DataOffsetDelta - CurrentOffset + DataBackFill;

    /* Call the allocator */
    NewMdl = AllocateMdlHandler(NewMdlSize);
    if (NewMdl == NULL)
    {
        DPRINT1("MDL allocation failed\n");
        return NDIS_STATUS_RESOURCES;
    }

    /* Chain the new MDL at the front */
    NewMdl->Next = NetBuffer->MdlChain;
    NetBuffer->MdlChain = NewMdl;
    NetBuffer->CurrentMdl = NewMdl;

    /* Update offsets */
    NetBuffer->DataOffset = DataBackFill;
    NetBuffer->CurrentMdlOffset = DataBackFill;
    NetBuffer->DataLength += DataOffsetDelta;

    DPRINT1("Retreat required new MDL, NewSize=%lu\n", NewMdlSize);

    return NDIS_STATUS_SUCCESS;
}

/*
 * @implemented
 */
VOID
EXPORT
NdisAdvanceNetBufferDataStart(
    _In_ PNET_BUFFER NetBuffer,
    _In_ ULONG DataOffsetDelta,
    _In_ BOOLEAN FreeMdl,
    _In_opt_ NET_BUFFER_FREE_MDL_HANDLER FreeMdlHandler)
{
    PMDL CurrentMdl;
    PMDL MdlToFree;
    ULONG RemainingAdvance;
    ULONG MdlDataLength;

    DPRINT1("NdisAdvanceNetBufferDataStart: NB=%p, Delta=%lu, FreeMdl=%d\n",
        NetBuffer, DataOffsetDelta, FreeMdl);

    if (NetBuffer == NULL || DataOffsetDelta == 0)
    {
        return;
    }

    /* Ensure we don't advance past the data */
    if (DataOffsetDelta > NetBuffer->DataLength)
    {
        DPRINT1("WARNING: Advancing past data length\n");
        DataOffsetDelta = (ULONG)NetBuffer->DataLength;
    }

    RemainingAdvance = DataOffsetDelta;
    CurrentMdl = NetBuffer->CurrentMdl;

    /*
     * Walk through MDLs, potentially freeing them if:
     * 1. FreeMdl is TRUE
     * 2. The entire MDL data becomes unused
     * 3. The MDL was allocated (not part of original chain)
     */
    while (RemainingAdvance > 0 && CurrentMdl != NULL)
    {
        MdlDataLength = MmGetMdlByteCount(CurrentMdl) - NetBuffer->CurrentMdlOffset;

        if (RemainingAdvance < MdlDataLength)
        {
            /* Advance stays within current MDL */
            NetBuffer->CurrentMdlOffset += RemainingAdvance;
            NetBuffer->DataOffset += RemainingAdvance;
            RemainingAdvance = 0;
        }
        else
        {
            /* Need to move to next MDL */
            RemainingAdvance -= MdlDataLength;
            NetBuffer->DataOffset += MdlDataLength;

            MdlToFree = CurrentMdl;
            CurrentMdl = CurrentMdl->Next;
            NetBuffer->CurrentMdlOffset = 0;

            /*
             * If FreeMdl is TRUE and this MDL is now completely unused
             * (i.e., it's before our current data start), we can free it
             */
            if (FreeMdl && MdlToFree != NetBuffer->MdlChain)
            {
                /* Only free MDLs that were prepended, not the original chain */
                if (FreeMdlHandler != NULL)
                {
                    /* Unlink from chain */
                    NetBuffer->MdlChain = CurrentMdl;
                    MdlToFree->Next = NULL;
                    FreeMdlHandler(MdlToFree);
                }
            }
        }
    }

    /* Update the current MDL pointer */
    NetBuffer->CurrentMdl = CurrentMdl;

    /* Update data length */
    NetBuffer->DataLength -= DataOffsetDelta;

    DPRINT1("After advance: DataOffset=%lu, DataLength=%lu\n",
        NetBuffer->DataOffset, (ULONG)NetBuffer->DataLength);
}

#endif /* NDIS_SUPPORT_NDIS6 */

/* EOF */
