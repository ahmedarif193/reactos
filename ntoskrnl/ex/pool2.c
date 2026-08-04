/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            ntoskrnl/ex/pool2.c
 * PURPOSE:         NT10 pool allocation APIs (POOL_FLAGS based)
 * PROGRAMMERS:     ReactOS Portable Systems Group
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* PRIVATE FUNCTIONS *********************************************************/

static
POOL_TYPE
ExpTranslatePoolFlags(IN POOL_FLAGS Flags)
{
    ULONG Type;

    /* A paged request maps to PagedPool, everything else to NonPagedPool */
    Type = (Flags & POOL_FLAG_PAGED) ? PagedPool : NonPagedPool;

    /* Honor the cache-aligned request through the CacheAligned pool bit */
    if (Flags & POOL_FLAG_CACHE_ALIGNED) Type |= CACHE_ALIGNED_POOL_MASK;

    /* Only raise on failure when explicitly requested */
    if (Flags & POOL_FLAG_RAISE_ON_FAILURE) Type |= POOL_RAISE_IF_ALLOCATION_FAILURE;

    return (POOL_TYPE)Type;
}

/* PUBLIC FUNCTIONS **********************************************************/

/*
 * @implemented
 */
PVOID
NTAPI
ExAllocatePool2(IN POOL_FLAGS Flags,
                IN SIZE_T NumberOfBytes,
                IN ULONG Tag)
{
    POOL_TYPE PoolType = ExpTranslatePoolFlags(Flags);
    PVOID Buffer;

    Buffer = ExAllocatePoolWithTag(PoolType, NumberOfBytes, Tag);

    /* NT10 zeroes allocations by default unless told otherwise */
    if ((Buffer != NULL) && !(Flags & POOL_FLAG_UNINITIALIZED))
    {
        RtlZeroMemory(Buffer, NumberOfBytes);
    }

    return Buffer;
}

/*
 * @implemented
 */
PVOID
NTAPI
ExAllocatePool3(IN POOL_FLAGS Flags,
                IN SIZE_T NumberOfBytes,
                IN ULONG Tag,
                IN PCPOOL_EXTENDED_PARAMETER ExtendedParameters,
                IN ULONG ExtendedParametersCount)
{
    /* Priority/NUMA/group placement hints carry no meaning on a single-node,
     * single-group system, so the extended parameters are ignored */
    UNREFERENCED_PARAMETER(ExtendedParameters);
    UNREFERENCED_PARAMETER(ExtendedParametersCount);

    return ExAllocatePool2(Flags, NumberOfBytes, Tag);
}

/*
 * @implemented
 */
VOID
NTAPI
ExFreePool2(IN PVOID P,
            IN ULONG Tag,
            IN PCPOOL_EXTENDED_PARAMETER Params,
            IN ULONG Count)
{
    UNREFERENCED_PARAMETER(Params);
    UNREFERENCED_PARAMETER(Count);

    /* Route to the tagged free path (Tag 0 skips the tag match check) */
    ExFreePoolWithTag(P, Tag);
}

/* EOF */
