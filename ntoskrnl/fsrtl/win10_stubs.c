/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:         Windows 10 FsRtl compatibility stubs
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
FsRtlCheckOplockEx(
    IN POPLOCK Oplock,
    IN PIRP Irp,
    IN ULONG Flags,
    IN PVOID Context OPTIONAL,
    IN POPLOCK_WAIT_COMPLETE_ROUTINE CompletionRoutine OPTIONAL,
    IN POPLOCK_FS_PREPOST_IRP PrePostIrp OPTIONAL)
{
    /* Windows 10 extended oplock check API */
    UNIMPLEMENTED;
    return STATUS_SUCCESS;
}

/*
 * @unimplemented
 */
NTSTATUS
NTAPI
FsRtlOplockBreakH(
    IN POPLOCK Oplock,
    IN PIRP Irp,
    IN ULONG Flags,
    IN PVOID Context OPTIONAL,
    IN POPLOCK_WAIT_COMPLETE_ROUTINE CompletionRoutine OPTIONAL,
    IN POPLOCK_FS_PREPOST_IRP PrePostIrp OPTIONAL)
{
    /* Windows 10 handle-based oplock break API */
    UNIMPLEMENTED;
    return STATUS_SUCCESS;
}

/*
 * @unimplemented
 */
BOOLEAN
NTAPI
FsRtlCurrentOplockH(
    IN POPLOCK Oplock)
{
    /* Windows 10 handle-based current oplock API */
    UNIMPLEMENTED;
    return FALSE;
}

/*
 * @unimplemented
 */
BOOLEAN
NTAPI
FsRtlOplockIsSharedRequest(
    IN PIRP Irp)
{
    /* Windows 10 check if oplock request is shared */
    UNIMPLEMENTED;
    return FALSE;
}

/*
 * @unimplemented
 */
BOOLEAN
NTAPI
FsRtlAreVolumeStartupApplicationsComplete(VOID)
{
    /* Windows 10 check if volume startup apps completed */
    UNIMPLEMENTED;
    return TRUE;
}

/* EOF */