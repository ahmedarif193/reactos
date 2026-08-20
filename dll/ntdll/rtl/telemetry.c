/*
 * PROJECT:     ReactOS NT User-Mode DLL
 * PURPOSE:     Compatibility entry points for native diagnostic telemetry
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 */

#include <ntdll.h>

/*
 * Windows consumes a three-ULONG codepath descriptor and may submit a
 * diagnostic event. ReactOS has no corresponding telemetry provider, but it
 * still validates the descriptor and treats the report as consumed.
 */
NTSTATUS
NTAPI
RtlLogUnexpectedCodepath(
    _In_reads_(3) const ULONG *Codepath)
{
    volatile ULONG DescriptorValue;

    DescriptorValue = Codepath[0] ^ Codepath[1] ^ Codepath[2];
    UNREFERENCED_PARAMETER(DescriptorValue);
    return STATUS_SUCCESS;
}
