/*
 * PROJECT:         ReactOS Kernel (ARM64)
 * PURPOSE:         Section-management policy helpers for ARM64
 */

#include <ntoskrnl.h>

BOOLEAN
NTAPI
MiArchShouldRouteFileReserveSectionToArm3(
    _In_opt_ PFILE_OBJECT FileObject,
    _In_ ULONG AllocationAttributes)
{
    return (FileObject != NULL) && ((AllocationAttributes & SEC_RESERVE) != 0);
}

BOOLEAN
NTAPI
MiArchAllowMissingDataSectionSegment(
    _In_ PSECTION_OBJECT_POINTERS SectionObjectPointer)
{
    UNREFERENCED_PARAMETER(SectionObjectPointer);

    /*
     * SEC_RESERVE sections routed through ARM3 do not create a RosMm segment.
     * They are satisfied as demand-zero mappings, so "make resident" is a no-op.
     */
    return TRUE;
}
