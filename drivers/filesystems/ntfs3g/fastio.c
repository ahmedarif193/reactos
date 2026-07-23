/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     NTFS-3G section synchronization
 * COPYRIGHT:   Copyright 2024 Carl Bialorucki <carl.bialorucki@reactos.org>
 *              Copyright 2024 Justin Miller <justin.miller@reactos.org>
 *              Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include "ntfspch.h"

VOID
NTAPI
NtfsFastIoAcquireFileForNtCreateSection(_In_ PFILE_OBJECT FileObject)
{
    PFileContextBlock File = FileObject->FsContext;

    if (File)
        ExAcquireResourceExclusiveLite(&File->MainResource, TRUE);
}

VOID
NTAPI
NtfsFastIoReleaseFileForNtCreateSection(_In_ PFILE_OBJECT FileObject)
{
    PFileContextBlock File = FileObject->FsContext;

    if (File)
        ExReleaseResourceLite(&File->MainResource);
}
