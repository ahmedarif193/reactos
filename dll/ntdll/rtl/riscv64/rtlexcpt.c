/*
 * PROJECT:     ReactOS NT Library
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     User-mode hooks for RISC-V exception dispatch and unwinding
 */

#include <ntdll.h>
#include <ndk/riscv64/exception.h>

extern UCHAR KiUserExceptionDispatcher[];
extern UCHAR KiRiscvUserExceptionDispatcherEnd[];

/* A fault must not re-enter the dispatcher that is reading. */
NTSTATUS
NTAPI
RtlpRiscv64ReadMemory(
    _Out_writes_bytes_all_(Length) PVOID Destination,
    _In_reads_bytes_(Length) const VOID *Source,
    _In_ SIZE_T Length)
{
    return NtReadVirtualMemory(NtCurrentProcess(), (PVOID)Source, Destination, Length, NULL);
}

DECLSPEC_NORETURN
VOID
NTAPI
RtlpRiscv64RaiseFatal(
    _In_ NTSTATUS Status)
{
    NtTerminateProcess(NtCurrentProcess(), Status);
    __fastfail(FAST_FAIL_FATAL_APP_EXIT);
}

/* Unwind through KiUserExceptionDispatcher into the CONTEXT the kernel saved
 * in its KUSER_EXCEPTION_STACK. */
NTSTATUS
NTAPI
RtlpRiscv64UnwindUserException(
    _In_ ULONG_PTR Pc,
    _In_ ULONG_PTR Low,
    _In_ ULONG_PTR High,
    _Inout_ PCONTEXT Context)
{
    CONTEXT Saved;
    ULONG_PTR Stack = Context->Sp;
    ULONG Required = CONTEXT_CONTROL | CONTEXT_INTEGER;

    if (Pc < (ULONG_PTR)KiUserExceptionDispatcher ||
        Pc >= (ULONG_PTR)KiRiscvUserExceptionDispatcherEnd)
        return STATUS_NOT_FOUND;
    if ((Stack & 15) || Stack < Low || Stack > High ||
        High - Stack < sizeof(KUSER_EXCEPTION_STACK) ||
        !NT_SUCCESS(RtlpRiscv64ReadMemory(&Saved, (PVOID)Stack, sizeof(Saved))) ||
        (Saved.ContextFlags & Required) != Required ||
        Saved.Sp < Stack + sizeof(KUSER_EXCEPTION_STACK) || Saved.Sp > High ||
        (Saved.Sp & 15) || (Saved.Pc & 1) ||
        Saved.Pc < MM_ALLOCATION_GRANULARITY || Saved.Pc > (ULONG_PTR)MI_HIGHEST_USER_ADDRESS)
        return STATUS_BAD_STACK;

    *Context = Saved;
    return STATUS_SUCCESS;
}

VOID
NTAPI
RtlpRiscv64PrepareContextRestore(
    _In_ PCONTEXT Context)
{
    /* User mode has no trap frames to retire. */
    UNREFERENCED_PARAMETER(Context);
}
