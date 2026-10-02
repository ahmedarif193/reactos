/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         GPL - See COPYING in the top level directory
 * FILE:            ntoskrnl/fsrtl/fsfilter.c
 * PURPOSE:         Provides support for the Filter Manager
 * PROGRAMMERS:     None.
 */

/* INCLUDES ******************************************************************/

#include <ntoskrnl.h>
#include <nvs/nt/mmkernel.h>
#define NDEBUG
#include <debug.h>

/* PUBLIC FUNCTIONS **********************************************************/

/*++
 * @name FsRtlCreateSectionForDataScan
 * @implemented
 *
 * FILLME
 *
 * @param SectionHandle
 *        FILLME
 *
 * @param SectionObject
 *        FILLME
 *
 * @param SectionFileSize
 *        FILLME
 *
 * @param FileObject
 *        FILLME
 *
 * @param DesiredAccess
 *        FILLME
 *
 * @param ObjectAttributes
 *        FILLME
 *
 * @param MaximumSize
 *        FILLME
 *
 * @param SectionPageProtection
 *        FILLME
 *
 * @param AllocationAttributes
 *        FILLME
 *
 * @param Flags
 *        FILLME
 *
 * @return None
 *
 * @remarks None
 *
 *--*/
NTSTATUS
NTAPI
FsRtlCreateSectionForDataScan(OUT PHANDLE SectionHandle,
                              OUT PVOID *SectionObject,
                              OUT PLARGE_INTEGER SectionFileSize OPTIONAL,
                              IN PFILE_OBJECT FileObject,
                              IN ACCESS_MASK DesiredAccess,
                              IN POBJECT_ATTRIBUTES ObjectAttributes OPTIONAL,
                              IN PLARGE_INTEGER MaximumSize OPTIONAL,
                              IN ULONG SectionPageProtection,
                              IN ULONG AllocationAttributes,
                              IN ULONG Flags)
{
    LARGE_INTEGER FileSize, Size;
    PVOID Section;
    HANDLE Handle;
    NTSTATUS Status;

    PAGED_CODE();

    UNREFERENCED_PARAMETER(MaximumSize);
    UNREFERENCED_PARAMETER(Flags);

    if (SectionPageProtection != PAGE_READONLY && SectionPageProtection != PAGE_READWRITE)
    {
        return STATUS_INVALID_PARAMETER_8;
    }
    if (!(AllocationAttributes & SEC_COMMIT) || (AllocationAttributes & ~(SEC_COMMIT | SEC_FILE)))
    {
        return STATUS_INVALID_PARAMETER_9;
    }

    Status = FsRtlGetFileSize(FileObject, &FileSize);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }
    if (FileSize.QuadPart == 0)
    {
        return STATUS_END_OF_FILE;
    }

    Size.QuadPart = 0;
    Status = MiCreateSectionWithMode(&Section,
                                     KernelMode,
                                     TRUE,
                                     DesiredAccess,
                                     ObjectAttributes,
                                     &Size,
                                     SectionPageProtection,
                                     SEC_COMMIT,
                                     NULL,
                                     FileObject);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    Status = ObInsertObject(Section, NULL, DesiredAccess, 1, SectionObject, &Handle);
    if (!NT_SUCCESS(Status))
    {
        return Status;
    }

    *SectionHandle = Handle;
    if (SectionFileSize != NULL)
    {
        *SectionFileSize = FileSize;
    }
    return STATUS_SUCCESS;
}

