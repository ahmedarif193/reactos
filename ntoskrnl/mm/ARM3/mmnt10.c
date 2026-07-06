/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            ntoskrnl/mm/ARM3/mmnt10.c
 * PURPOSE:         ARM Memory Manager modern (Win8+/Win10+) API surface.
 *                  Thin wrappers over the existing ARM3 primitives for a
 *                  single-node, single-group system.
 * PROGRAMMERS:     ReactOS Portable Systems Group
 */

/* INCLUDES *******************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

#define MODULE_INVOLVED_IN_ARM3
#include <mm/ARM3/miarm.h>

/* GLOBALS ********************************************************************/

//
// MmCopyMemory input descriptor and flags (WDK 10.0.16299 ntddk.h).
// Not yet declared by the ReactOS DDK headers, so declare them here.
//
#ifndef MM_COPY_MEMORY_PHYSICAL

typedef struct _MM_COPY_ADDRESS
{
    union
    {
        PVOID VirtualAddress;
        PHYSICAL_ADDRESS PhysicalAddress;
    };
} MM_COPY_ADDRESS, *PMMCOPY_ADDRESS;

#define MM_COPY_MEMORY_PHYSICAL 0x1
#define MM_COPY_MEMORY_VIRTUAL  0x2

#endif

//
// MDL allocation flags above the set defined by the ReactOS DDK headers
// (WDK 10.0.16299 wdm.h).
//
#ifndef MM_ALLOCATE_FAST_LARGE_PAGES
#define MM_ALLOCATE_FAST_LARGE_PAGES  0x00000040
#endif
#ifndef MM_ALLOCATE_TRIM_IF_NECESSARY
#define MM_ALLOCATE_TRIM_IF_NECESSARY 0x00000080
#endif
#ifndef MM_ALLOCATE_AND_HOT_REMOVE
#define MM_ALLOCATE_AND_HOT_REMOVE    0x00000100
#endif

//
// Bound each physical copy mapping so we never reserve an unbounded number
// of system PTEs for a single MmCopyMemory call.
//
#define MI_COPY_PHYSICAL_CHUNK (16 * PAGE_SIZE)

PVOID
NTAPI
MmMapIoSpaceEx(
    _In_ PHYSICAL_ADDRESS PhysicalAddress,
    _In_ SIZE_T NumberOfBytes,
    _In_ ULONG Protect);

PVOID
NTAPI
MmAllocateContiguousNodeMemory(
    _In_ SIZE_T NumberOfBytes,
    _In_ PHYSICAL_ADDRESS LowestAcceptableAddress,
    _In_ PHYSICAL_ADDRESS HighestAcceptableAddress,
    _In_opt_ PHYSICAL_ADDRESS BoundaryAddressMultiple,
    _In_ ULONG Protect,
    _In_ NODE_REQUIREMENT PreferredNode);

PMDL
NTAPI
MmAllocateNodePagesForMdlEx(
    _In_ PHYSICAL_ADDRESS LowAddress,
    _In_ PHYSICAL_ADDRESS HighAddress,
    _In_ PHYSICAL_ADDRESS SkipBytes,
    _In_ SIZE_T TotalBytes,
    _In_ MEMORY_CACHING_TYPE CacheType,
    _In_ ULONG IdealNode,
    _In_ ULONG Flags);

NTSTATUS
NTAPI
MmCopyMemory(
    _In_ PVOID TargetAddress,
    _In_ MM_COPY_ADDRESS SourceAddress,
    _In_ SIZE_T NumberOfBytes,
    _In_ ULONG Flags,
    _Out_ PSIZE_T NumberOfBytesTransferred);

/* PRIVATE FUNCTIONS **********************************************************/

static
BOOLEAN
MiConvertProtectToCacheType(
    _In_ ULONG Protect,
    _In_ ULONG ValidAccessBits,
    _Out_ MEMORY_CACHING_TYPE *CacheType)
{
    ULONG AccessBits, CacheBits;

    //
    // Split the protection into its access and caching components and
    // reject anything outside those two groups
    //
    AccessBits = Protect & 0xFF;
    CacheBits = Protect & (PAGE_NOCACHE | PAGE_WRITECOMBINE);
    if (Protect != (AccessBits | CacheBits)) return FALSE;

    //
    // Exactly one access bit must be set, out of the caller's valid set
    //
    if ((AccessBits == 0) ||
        (AccessBits & (AccessBits - 1)) ||
        !(AccessBits & ValidAccessBits))
    {
        return FALSE;
    }

    //
    // PAGE_NOCACHE and PAGE_WRITECOMBINE are mutually exclusive
    //
    if (CacheBits == (PAGE_NOCACHE | PAGE_WRITECOMBINE)) return FALSE;

    //
    // Translate the caching selection
    //
    if (CacheBits & PAGE_NOCACHE)
    {
        *CacheType = MmNonCached;
    }
    else if (CacheBits & PAGE_WRITECOMBINE)
    {
        *CacheType = MmWriteCombined;
    }
    else
    {
        *CacheType = MmCached;
    }

    return TRUE;
}

static
SIZE_T
MiCopyFromSystemVirtual(
    _Out_writes_bytes_(NumberOfBytes) PVOID TargetAddress,
    _In_ PVOID SourceAddress,
    _In_ SIZE_T NumberOfBytes)
{
    PUCHAR Target = TargetAddress;
    PUCHAR Source = SourceAddress;
    SIZE_T BytesCopied = 0, ChunkSize;
    KIRQL OldIrql;
    BOOLEAN Valid;

    while (BytesCopied < NumberOfBytes)
    {
        //
        // Never cross a page boundary within one chunk, so a single
        // validity check covers the whole chunk
        //
        ChunkSize = PAGE_SIZE - BYTE_OFFSET(Source);
        if (ChunkSize > (NumberOfBytes - BytesCopied))
        {
            ChunkSize = NumberOfBytes - BytesCopied;
        }

        //
        // A valid system PTE only remains valid while the PFN lock is
        // held, so validate and copy under it. A trimmed or freed source
        // page thus ends the copy instead of raising an unhandled kernel
        // fault (which would bugcheck). The chunk is at most a page, so
        // the time at DISPATCH_LEVEL is bounded.
        //
        OldIrql = MiAcquirePfnLock();
        Valid = MmIsAddressValid(Source);
        if (Valid) RtlCopyMemory(Target, Source, ChunkSize);
        MiReleasePfnLock(OldIrql);
        if (!Valid) break;

        Target += ChunkSize;
        Source += ChunkSize;
        BytesCopied += ChunkSize;
    }

    return BytesCopied;
}

static
SIZE_T
MiCopyFromPhysical(
    _Out_writes_bytes_(NumberOfBytes) PVOID TargetAddress,
    _In_ PHYSICAL_ADDRESS SourceAddress,
    _In_ SIZE_T NumberOfBytes)
{
    PUCHAR Target = TargetAddress;
    PHYSICAL_ADDRESS Source = SourceAddress;
    PVOID MappedVa;
    SIZE_T BytesCopied = 0, ChunkSize, ValidSize;
    PFN_NUMBER Pfn, LastPfn;

    while (BytesCopied < NumberOfBytes)
    {
        //
        // Bound the mapping size
        //
        ChunkSize = MI_COPY_PHYSICAL_CHUNK - BYTE_OFFSET(Source.LowPart);
        if (ChunkSize > (NumberOfBytes - BytesCopied))
        {
            ChunkSize = NumberOfBytes - BytesCopied;
        }

        //
        // Only copy from pages the PFN database knows about, i.e. actual
        // RAM. Mapping arbitrary physical space (device registers) with a
        // cached attribute is unsafe. Shrink the chunk to the valid prefix
        //
        Pfn = (PFN_NUMBER)(Source.QuadPart >> PAGE_SHIFT);
        LastPfn = (PFN_NUMBER)((Source.QuadPart + ChunkSize - 1) >> PAGE_SHIFT);
        ValidSize = 0;
        while (Pfn <= LastPfn)
        {
            if (!MiGetPfnEntry(Pfn)) break;
            ValidSize += PAGE_SIZE;
            Pfn++;
        }
        if (ValidSize == 0) break;
        ValidSize -= BYTE_OFFSET(Source.LowPart);
        if (ChunkSize > ValidSize) ChunkSize = ValidSize;

        //
        // Map the chunk cached, copy it out and tear the mapping down
        //
        MappedVa = MmMapIoSpace(Source, ChunkSize, MmCached);
        if (!MappedVa) break;
        RtlCopyMemory(Target, MappedVa, ChunkSize);
        MmUnmapIoSpace(MappedVa, ChunkSize);

        Target += ChunkSize;
        Source.QuadPart += ChunkSize;
        BytesCopied += ChunkSize;
    }

    return BytesCopied;
}

/* PUBLIC FUNCTIONS ***********************************************************/

/*
 * @implemented
 */
PVOID
NTAPI
MmMapIoSpaceEx(IN PHYSICAL_ADDRESS PhysicalAddress,
               IN SIZE_T NumberOfBytes,
               IN ULONG Protect)
{
    MEMORY_CACHING_TYPE CacheType;

    //
    // Protect must carry exactly one access specifier, plus at most one
    // caching modifier. NOTE: the ARM3 I/O mapper only creates read-write
    // kernel mappings, so the access specifier is validated but a stricter
    // protection (e.g. PAGE_READONLY) is not enforced on the mapping.
    //
    if (!MiConvertProtectToCacheType(Protect,
                                     PAGE_READONLY | PAGE_READWRITE |
                                     PAGE_EXECUTE | PAGE_EXECUTE_READ |
                                     PAGE_EXECUTE_READWRITE,
                                     &CacheType))
    {
        return NULL;
    }

    //
    // Let the classic mapper do the work
    //
    return MmMapIoSpace(PhysicalAddress, NumberOfBytes, CacheType);
}

/*
 * @implemented
 */
PVOID
NTAPI
MmAllocateContiguousMemorySpecifyCacheNode(IN SIZE_T NumberOfBytes,
                                           IN PHYSICAL_ADDRESS LowestAcceptableAddress OPTIONAL,
                                           IN PHYSICAL_ADDRESS HighestAcceptableAddress,
                                           IN PHYSICAL_ADDRESS BoundaryAddressMultiple OPTIONAL,
                                           IN MEMORY_CACHING_TYPE CacheType OPTIONAL,
                                           IN NODE_REQUIREMENT PreferredNode)
{
    //
    // The node is only a preference; on this single-node system all
    // memory is node 0, so any preference is trivially satisfied
    //
    UNREFERENCED_PARAMETER(PreferredNode);

    return MmAllocateContiguousMemorySpecifyCache(NumberOfBytes,
                                                  LowestAcceptableAddress,
                                                  HighestAcceptableAddress,
                                                  BoundaryAddressMultiple,
                                                  CacheType);
}

/*
 * @implemented
 */
PVOID
NTAPI
MmAllocateContiguousNodeMemory(IN SIZE_T NumberOfBytes,
                               IN PHYSICAL_ADDRESS LowestAcceptableAddress OPTIONAL,
                               IN PHYSICAL_ADDRESS HighestAcceptableAddress,
                               IN PHYSICAL_ADDRESS BoundaryAddressMultiple OPTIONAL,
                               IN ULONG Protect,
                               IN NODE_REQUIREMENT PreferredNode)
{
    MEMORY_CACHING_TYPE CacheType;

    //
    // Protect must be PAGE_READWRITE or PAGE_EXECUTE_READWRITE with at
    // most one caching modifier. NOTE: contiguous memory comes from the
    // regular kernel mapping path, so PAGE_EXECUTE_READWRITE is validated
    // but not reflected in the page protection
    //
    if (!MiConvertProtectToCacheType(Protect,
                                     PAGE_READWRITE | PAGE_EXECUTE_READWRITE,
                                     &CacheType))
    {
        return NULL;
    }

    //
    // Single-node system: the node preference is trivially satisfied
    //
    UNREFERENCED_PARAMETER(PreferredNode);

    return MmAllocateContiguousMemorySpecifyCache(NumberOfBytes,
                                                  LowestAcceptableAddress,
                                                  HighestAcceptableAddress,
                                                  BoundaryAddressMultiple,
                                                  CacheType);
}

/*
 * @implemented
 */
PMDL
NTAPI
MmAllocateNodePagesForMdlEx(IN PHYSICAL_ADDRESS LowAddress,
                            IN PHYSICAL_ADDRESS HighAddress,
                            IN PHYSICAL_ADDRESS SkipBytes,
                            IN SIZE_T TotalBytes,
                            IN MEMORY_CACHING_TYPE CacheType,
                            IN ULONG IdealNode,
                            IN ULONG Flags)
{
    PMDL Mdl;

    //
    // Single-node system: the ideal node is only a preference
    //
    UNREFERENCED_PARAMETER(IdealNode);

    //
    // Reject unknown flags, as well as the ones whose semantics are hard
    // requirements this implementation cannot honor
    //
    if (Flags & ~(MM_DONT_ZERO_ALLOCATION |
                  MM_ALLOCATE_FROM_LOCAL_NODE_ONLY |
                  MM_ALLOCATE_FULLY_REQUIRED |
                  MM_ALLOCATE_NO_WAIT |
                  MM_ALLOCATE_PREFER_CONTIGUOUS |
                  MM_ALLOCATE_FAST_LARGE_PAGES |
                  MM_ALLOCATE_TRIM_IF_NECESSARY))
    {
        return NULL;
    }

    //
    // Forward only the flags the classic allocator understands. The
    // allocator never blocks and never allocates large pages, so
    // MM_ALLOCATE_NO_WAIT is trivially satisfied while the contiguity
    // and trim preferences are best-effort ones we do not act upon
    //
    Mdl = MmAllocatePagesForMdlEx(LowAddress,
                                  HighAddress,
                                  SkipBytes,
                                  TotalBytes,
                                  CacheType,
                                  Flags & (MM_DONT_ZERO_ALLOCATION |
                                           MM_ALLOCATE_FROM_LOCAL_NODE_ONLY));
    if (!Mdl) return NULL;

    //
    // The classic allocator is allowed to return fewer pages than asked
    // for; MM_ALLOCATE_FULLY_REQUIRED makes that a failure
    //
    if ((Flags & MM_ALLOCATE_FULLY_REQUIRED) &&
        ((SIZE_T)Mdl->ByteCount <
         ((SIZE_T)ADDRESS_AND_SIZE_TO_SPAN_PAGES(0, TotalBytes) << PAGE_SHIFT)))
    {
        MmFreePagesFromMdl(Mdl);
        ExFreePoolWithTag(Mdl, TAG_MDL);
        return NULL;
    }

    return Mdl;
}

/*
 * @implemented
 */
NTSTATUS
NTAPI
MmCopyMemory(IN PVOID TargetAddress,
             IN MM_COPY_ADDRESS SourceAddress,
             IN SIZE_T NumberOfBytes,
             IN ULONG Flags,
             OUT PSIZE_T NumberOfBytesTransferred)
{
    SIZE_T BytesCopied;

    ASSERT(KeGetCurrentIrql() <= APC_LEVEL);

    //
    // Validate the output parameter and the transfer size
    //
    if (!NumberOfBytesTransferred) return STATUS_INVALID_PARAMETER_5;
    *NumberOfBytesTransferred = 0;
    if (NumberOfBytes == 0) return STATUS_INVALID_PARAMETER_3;

    //
    // Exactly one of the source interpretation flags must be set
    //
    if ((Flags != MM_COPY_MEMORY_PHYSICAL) &&
        (Flags != MM_COPY_MEMORY_VIRTUAL))
    {
        return STATUS_INVALID_PARAMETER_4;
    }

    if (Flags & MM_COPY_MEMORY_VIRTUAL)
    {
        //
        // The virtual source must be entirely in system space; copying
        // from user space is not supported by this interface
        //
        if (((ULONG_PTR)SourceAddress.VirtualAddress < (ULONG_PTR)MmSystemRangeStart) ||
            (((ULONG_PTR)SourceAddress.VirtualAddress + NumberOfBytes) <
             (ULONG_PTR)SourceAddress.VirtualAddress))
        {
            return STATUS_INVALID_ADDRESS;
        }

        BytesCopied = MiCopyFromSystemVirtual(TargetAddress,
                                              SourceAddress.VirtualAddress,
                                              NumberOfBytes);
    }
    else
    {
        //
        // The physical range must not wrap
        //
        if ((ULONG64)(SourceAddress.PhysicalAddress.QuadPart + NumberOfBytes) <
            (ULONG64)SourceAddress.PhysicalAddress.QuadPart)
        {
            return STATUS_INVALID_ADDRESS;
        }

        BytesCopied = MiCopyFromPhysical(TargetAddress,
                                         SourceAddress.PhysicalAddress,
                                         NumberOfBytes);
    }

    //
    // Partial copies are allowed; report what was actually transferred
    //
    *NumberOfBytesTransferred = BytesCopied;
    if (BytesCopied == NumberOfBytes) return STATUS_SUCCESS;
    return BytesCopied ? STATUS_PARTIAL_COPY : STATUS_INVALID_ADDRESS;
}

/* EOF */
