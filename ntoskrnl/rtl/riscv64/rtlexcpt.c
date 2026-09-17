/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     RISC-V stack-walking boundary during minimal bring-up
 */

#include <ntoskrnl.h>

/*
 * @unimplemented
 */
ULONG
NTAPI
RtlWalkFrameChain(
    _Out_writes_to_(Count, return) PVOID *Callers,
    _In_ ULONG Count,
    _In_ ULONG Flags)
{
    UNREFERENCED_PARAMETER(Callers);
    UNREFERENCED_PARAMETER(Count);
    UNREFERENCED_PARAMETER(Flags);

    /* No RISC-V unwind engine or fault-safe stack reads yet. Report no frames
     * without following an x86 frame chain or modifying the caller's buffer. */
    return 0;
}
