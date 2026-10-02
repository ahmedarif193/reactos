/*
 * PROJECT:     LiberNT API tests
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     PowerPC RTL hooks for the standalone RTL tests
 */

/* Standalone PPC RTL tests have no kernel trap or user dispatcher frames. */
#include <rtltests.h>

NTSTATUS
NTAPI
RtlpPpcUnwindSpecialFrame(
    _In_ ULONG_PTR ControlPc,
    _Inout_ PCONTEXT Context)
{
    UNREFERENCED_PARAMETER(ControlPc);
    UNREFERENCED_PARAMETER(Context);
    return STATUS_NOT_FOUND;
}

VOID
NTAPI
RtlpPpcPrepareContextRestore(
    _In_ PCONTEXT Context)
{
    UNREFERENCED_PARAMETER(Context);
}
