/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Explicit deferred RISC-V kernel operations
 */

#include <ntoskrnl.h>

/* Retain the first unavailable operation for an external debugger, including
 * failures before PCR/console initialization and recursive bugcheck failure. */
const CHAR * volatile KiRiscvUnimplementedRoutine;

DECLSPEC_NORETURN
VOID
NTAPI
KiRiscvUnimplemented(_In_ const CHAR *Routine)
{
    _disable();
    if (KiRiscvUnimplementedRoutine == NULL)
    {
        KiRiscvUnimplementedRoutine = Routine;
        if ((KeNumberProcessors == 1) &&
            ((ULONG_PTR)KeGetPcr() + FIELD_OFFSET(KPCR, Prcb) == (ULONG_PTR)KiProcessorBlock[0]))
        {
            KeBugCheckEx(KMODE_EXCEPTION_NOT_HANDLED, STATUS_NOT_IMPLEMENTED, (ULONG_PTR)Routine, 0, 0);
        }
    }

    for (;;)
        __asm__ __volatile__("wfi" ::: "memory");
}

/* Failure-returning services do not inspect input or modify output storage. */
NTSTATUS NTAPI NtVdmControl(ULONG ControlCode, PVOID ControlData) { return STATUS_NOT_SUPPORTED; }

NTSTATUS
NTAPI
NtSetLdtEntries(
    _In_ ULONG Selector1,
    _In_ LDT_ENTRY LdtEntry1,
    _In_ ULONG Selector2,
    _In_ LDT_ENTRY LdtEntry2)
{
    UNREFERENCED_PARAMETER(Selector1);
    UNREFERENCED_PARAMETER(LdtEntry1);
    UNREFERENCED_PARAMETER(Selector2);
    UNREFERENCED_PARAMETER(LdtEntry2);
    return STATUS_NOT_IMPLEMENTED;
}
