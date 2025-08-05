/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:         Windows 10 I/O Manager compatibility stubs
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
NTSTATUS
NTAPI
IoRetrievePriorityInfo(
    IN PIRP Irp,
    IN PFILE_OBJECT FileObject OPTIONAL,
    IN PETHREAD Thread OPTIONAL,
    OUT PIO_PRIORITY_INFO PriorityInfo)
{
    /* Windows 10 I/O priority retrieval API */
    UNIMPLEMENTED;
    
    /* Return default priority */
    if (PriorityInfo)
    {
        RtlZeroMemory(PriorityInfo, sizeof(IO_PRIORITY_INFO));
        PriorityInfo->Size = sizeof(IO_PRIORITY_INFO);
        PriorityInfo->ThreadPriority = 0xffff;
        PriorityInfo->IoPriority = IoPriorityNormal;
        PriorityInfo->PagePriority = 0;
    }
    
    return STATUS_SUCCESS;
}

/* EOF */