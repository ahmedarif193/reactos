/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:         Windows 10 Cache Manager compatibility stubs
 * COPYRIGHT:       Copyright 2024 ReactOS Team
 */

/* INCLUDES *****************************************************************/

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/* FUNCTIONS ****************************************************************/

/*
 * @unimplemented
 */
BOOLEAN
NTAPI
CcCopyWriteWontFlush(
    IN PFILE_OBJECT FileObject,
    IN PLARGE_INTEGER FileOffset,
    IN ULONG Length)
{
    /* Windows 10 API - not yet implemented */
    UNIMPLEMENTED;
    return FALSE;
}

/*
 * @unimplemented
 */
VOID
NTAPI
CcCoherencyFlushAndPurgeCache(
    IN PSECTION_OBJECT_POINTERS SectionObjectPointer,
    IN PLARGE_INTEGER FileOffset OPTIONAL,
    IN ULONG Length,
    OUT PIO_STATUS_BLOCK IoStatus,
    IN ULONG Flags OPTIONAL)
{
    /* Windows 10 API - Flush and purge cache for coherency */
    UNIMPLEMENTED;
    
    /* Set failed status */
    if (IoStatus)
    {
        IoStatus->Status = STATUS_NOT_IMPLEMENTED;
        IoStatus->Information = 0;
    }
}

/* EOF */