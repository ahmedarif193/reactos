/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Explicit deferred RISC-V kernel operations
 */

#include <ntoskrnl.h>

/* Retain the first unavailable operation for an external debugger, including
 * failures before PCR/console initialization and recursive bugcheck failure. */
const CHAR * volatile KiRiscvUnimplementedRoutine;
const CHAR KiRiscvZwIsSystemResumeAutomaticName[] = "ZwIsSystemResumeAutomatic";

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

/* These interfaces have no failure return that permits the caller to carry
 * on. Do not simulate a switch, initialization, rundown, or state restore. */
#define RISCV_REQUIRED(ReturnType, Convention, Name, Parameters) \
    ReturnType Convention Name Parameters { KiRiscvUnimplemented(#Name); }

RISCV_REQUIRED(VOID, __cdecl, KeSaveStateForHibernate, (PKPROCESSOR_STATE ProcessorState))
RISCV_REQUIRED(ULONG, NTAPI, KeGetRecommendedSharedDataAlignment, (VOID))
RISCV_REQUIRED(VOID, NTAPI, PspGetOrSetContextKernelRoutine, (PKAPC Apc, PKNORMAL_ROUTINE *NormalRoutine, PVOID *NormalContext, PVOID *SystemArgument1, PVOID *SystemArgument2))

/* The saved control state is a diagnostic snapshot of supervisor CSRs taken
 * on this hart (KiSaveProcessorControlState). Nothing was changed by the
 * debugger or bugcheck path that must be undone, so restoring is a no-op on
 * the single-hart bring-up. */
VOID
NTAPI
KiRestoreProcessorControlState(_In_ PKPROCESSOR_STATE ProcessorState)
{
    UNREFERENCED_PARAMETER(ProcessorState);
}

/* Failure-returning services do not inspect input or modify output storage. */
NTSTATUS NTAPI NtVdmControl(ULONG ControlCode, PVOID ControlData) { return STATUS_NOT_SUPPORTED; }
NTSTATUS NTAPI KeRaiseUserException(NTSTATUS ExceptionCode) { return STATUS_NOT_IMPLEMENTED; }
