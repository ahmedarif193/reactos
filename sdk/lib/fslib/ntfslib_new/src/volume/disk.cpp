/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     MIT (https://spdx.org/licenses/MIT)
 * PURPOSE:     NTFS filesystem driver
 * COPYRIGHT:   Copyright 2024 Carl Bialorucki <carl.bialorucki@reactos.org>
 *              Copyright 2024 Justin Miller <justin.miller@reactos.org>
 */

#include "ntfslib_new.h"
#include "ntfslib_new_internal.h"

NTSTATUS
Volume::ReadVolume(_In_    ULONGLONG Offset,
                   _In_    ULONG Length,
                   _Inout_ PUCHAR Buffer)
{
    return NtfsReadVolume(Offset, Length, Buffer);
}

NTSTATUS
Volume::WriteVolume(_In_    ULONGLONG Offset,
                    _In_    ULONG Length,
                    _Inout_ PUCHAR Buffer)
{
    return NtfsWriteVolume(Offset, Length, Buffer);
}
