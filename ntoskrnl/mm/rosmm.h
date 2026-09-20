#pragma once

#include <internal/arch/mm.h>

/* TYPES *********************************************************************/
#define MM_SEGMENT_FINALIZE (0x40000000)


#define MIN(x,y) (((x)<(y))?(x):(y))
#define MAX(x,y) (((x)>(y))?(x):(y))

/* Determine what's needed to make paged pool fit in this category.
 * it seems that something more is required to satisfy arm3. */
#define BALANCER_CAN_EVICT(Consumer) \
    (((Consumer) == MC_USER) || \
     ((Consumer) == MC_CACHE))

#define SEC_CACHE                           (0x20000000)

/* We store 8 bits of location with a page association */
#define ENTRIES_PER_ELEMENT 256

extern KEVENT MmWaitPageEvent;

typedef struct _CACHE_SECTION_PAGE_TABLE
{
    LARGE_INTEGER FileOffset;
    PMM_SECTION_SEGMENT Segment;
    ULONG Refcount;
    ULONG_PTR PageEntries[ENTRIES_PER_ELEMENT];
} CACHE_SECTION_PAGE_TABLE, *PCACHE_SECTION_PAGE_TABLE;

struct _MM_REQUIRED_RESOURCES;

typedef NTSTATUS (NTAPI * AcquireResource)(
     PMMSUPPORT AddressSpace,
     struct _MEMORY_AREA *MemoryArea,
     struct _MM_REQUIRED_RESOURCES *Required);

typedef NTSTATUS (NTAPI * NotPresentFaultHandler)(
    PMMSUPPORT AddressSpace,
    struct _MEMORY_AREA *MemoryArea,
    PVOID Address,
    BOOLEAN Locked,
    struct _MM_REQUIRED_RESOURCES *Required);

typedef NTSTATUS (NTAPI * FaultHandler)(
    PMMSUPPORT AddressSpace,
    struct _MEMORY_AREA *MemoryArea,
    PVOID Address,
    struct _MM_REQUIRED_RESOURCES *Required);

typedef struct _MM_REQUIRED_RESOURCES
{
    ULONG Consumer;
    ULONG Amount;
    ULONG Offset;
    ULONG State;
    PVOID Context;
    LARGE_INTEGER FileOffset;
    AcquireResource DoAcquisition;
    PFN_NUMBER Page[2];
    PVOID Buffer[2];
    SWAPENTRY SwapEntry;
    const char *File;
    int Line;
} MM_REQUIRED_RESOURCES, *PMM_REQUIRED_RESOURCES;

/* sptab.c *******************************************************************/

VOID
NTAPI
MiInitializeSectionPageTable(PMM_SECTION_SEGMENT Segment);

typedef VOID (NTAPI *FREE_SECTION_PAGE_FUN)(
    PMM_SECTION_SEGMENT Segment,
    PLARGE_INTEGER Offset);

VOID
NTAPI
MmFreePageTablesSectionSegment(PMM_SECTION_SEGMENT Segment,
                               FREE_SECTION_PAGE_FUN FreePage);

NTSTATUS
NTAPI
MmSetSectionAssociation(PFN_NUMBER Page,
                        PMM_SECTION_SEGMENT Segment,
                        PLARGE_INTEGER Offset);

VOID
NTAPI
MmDeleteSectionAssociation(PFN_NUMBER Page);

PVOID
MiRosFindSystemCacheView(
    _In_ PMMSUPPORT AddressSpace,
    _In_ SIZE_T ViewSize);

/* io.c **********************************************************************/

NTSTATUS
MmspWaitForFileLock(PFILE_OBJECT File);

NTSTATUS
NTAPI
MiSimpleRead(PFILE_OBJECT FileObject,
             PLARGE_INTEGER FileOffset,
             PVOID Buffer,
             ULONG Length,
             BOOLEAN Paging,
             PIO_STATUS_BLOCK ReadStatus);

/* section.c *****************************************************************/

PVOID
NTAPI
MmGetSegmentRmap(PFN_NUMBER Page,
                 PULONG RawOffset);