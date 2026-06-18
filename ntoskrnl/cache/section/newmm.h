#pragma once

#include <internal/arch/mm.h>

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

VOID
NTAPI
MmDeleteSectionAssociationForPageTable(PFN_NUMBER Page,
                                       PCACHE_SECTION_PAGE_TABLE PageTable,
                                       ULONG RawOffset);

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

FORCEINLINE
BOOLEAN
_MmTryToLockAddressSpace(IN PMMSUPPORT AddressSpace,
                         const char *file,
                         int line)
{
#if (NTDDI_VERSION >= NTDDI_LONGHORN)
    BOOLEAN Result;
    KeEnterGuardedRegion();
    Result = ExTryToAcquirePushLockExclusive(&CONTAINING_RECORD(AddressSpace, EPROCESS, Vm)->AddressCreationLock);
    if (!Result) KeLeaveGuardedRegion();
#else
    BOOLEAN Result = KeTryToAcquireGuardedMutex(&CONTAINING_RECORD(AddressSpace, EPROCESS, Vm)->AddressCreationLock);
#endif
    //DbgPrint("(%s:%d) Try Lock Address Space %x -> %s\n", file, line, AddressSpace, Result ? "true" : "false");
    return Result;
}

#define MmTryToLockAddressSpace(x) _MmTryToLockAddressSpace(x,__FILE__,__LINE__)

PVOID
NTAPI
MmGetSegmentRmap(PFN_NUMBER Page,
                 PULONG RawOffset);
