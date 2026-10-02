/*
 * PROJECT:     LiberNT DDK
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * PURPOSE:     NetAdapterCx shared data path definitions
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#pragma once

#pragma region Desktop Family or OneCore Family
#if WINAPI_FAMILY_PARTITION(WINAPI_PARTITION_DESKTOP | WINAPI_PARTITION_SYSTEM)

#ifndef NETCX_ADAPTER_2
#error include netadaptercx.h
#endif

EXTERN_C_START

struct _NET_PACKET;
typedef struct _NET_PACKET NET_PACKET;

struct _NET_FRAGMENT;
typedef struct _NET_FRAGMENT NET_FRAGMENT;

#pragma warning(push)
#pragma warning(disable:4201)

typedef struct DECLSPEC_CACHEALIGN _NET_RING
{

    UINT16 OSReserved1;

    UINT16 ElementStride;

    UINT32 NumberOfElements;

    UINT32 ElementIndexMask;

    UINT32 EndIndex;

    union {
        UINT32 OSReserved0;

        void * OSReserved2[4];
    } DUMMYUNIONNAME;

    UINT32 BeginIndex;

    UINT32 NextIndex;

    void * Scratch;

    DECLSPEC_CACHEALIGN
    _Field_size_(NumberOfElements * ElementStride)
    unsigned char
        Buffer[ANYSIZE_ARRAY];

} NET_RING;

C_ASSERT(FIELD_OFFSET(NET_RING, Buffer) == SYSTEM_CACHE_ALIGNMENT_SIZE);

#pragma warning(pop)

inline
void *
NetRingGetElementAtIndex(
    _In_ NET_RING const * Ring,
    _In_ UINT32 Index
)
{
    return (void *)(Ring->Buffer + (SIZE_T)Index * Ring->ElementStride);
}

inline
UINT32
NetRingAdvanceIndex(
    _In_ NET_RING const * Ring,
    _In_ UINT32 Index,
    _In_ INT32 Distance
)
{
    return (Index + Distance) & Ring->ElementIndexMask;
}

inline
UINT32
NetRingIncrementIndex(
    _In_ NET_RING const * Ring,
    _In_ UINT32 Index
)
{
    return NetRingAdvanceIndex(Ring, Index, 1);
}

inline
UINT32
NetRingGetRangeCount(
    _In_ NET_RING const * Ring,
    _In_ UINT32 StartIndex,
    _In_ UINT32 EndIndex
)

{

    NT_ASSERT(StartIndex < Ring->NumberOfElements);
    NT_ASSERT(EndIndex < Ring->NumberOfElements);

    return (EndIndex - StartIndex) & Ring->ElementIndexMask;
}

inline
NET_PACKET *
NetRingGetPacketAtIndex(
    NET_RING const * Ring,
    UINT32 Index
)
{
    return (NET_PACKET *)NetRingGetElementAtIndex(Ring, Index);
}

inline
NET_FRAGMENT *
NetRingGetFragmentAtIndex(
    NET_RING const * Ring,
    UINT32 Index
)
{
    return (NET_FRAGMENT *)NetRingGetElementAtIndex(Ring, Index);
}

inline
SIZE_T *
NetRingGetDataBufferAtIndex(
    NET_RING const * Ring,
    UINT32 Index
)
{
    return (SIZE_T *)NetRingGetElementAtIndex(Ring, Index);
}

EXTERN_C_END

#endif
#pragma endregion
