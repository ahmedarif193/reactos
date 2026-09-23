/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     RISC-V supervisor diagnostic state
 */

#include <ntoskrnl.h>

VOID NTAPI
KiSaveProcessorState(PKTRAP_FRAME TrapFrame, PKEXCEPTION_FRAME ExceptionFrame)
{
    PKPROCESSOR_STATE State = &KeGetCurrentPrcb()->ProcessorState;
    UNREFERENCED_PARAMETER(ExceptionFrame);
    State->ContextFrame = TrapFrame->Context;
    KiSaveProcessorControlState(State);
}

VOID NTAPI
KiRestoreProcessorState(PKTRAP_FRAME TrapFrame, PKEXCEPTION_FRAME ExceptionFrame)
{
    PKPROCESSOR_STATE State = &KeGetCurrentPrcb()->ProcessorState;
    UNREFERENCED_PARAMETER(ExceptionFrame);
    TrapFrame->Context = State->ContextFrame;
    KiRestoreProcessorControlState(State);
}

ULONG
NTAPI
KeGetRecommendedSharedDataAlignment(VOID)
{
    /* Page alignment also separates data when firmware omits cache geometry. */
    return PAGE_SIZE;
}

VOID
__cdecl
KeSaveStateForHibernate(_Out_ PKPROCESSOR_STATE ProcessorState)
{
    RtlCaptureContext(&ProcessorState->ContextFrame);
    KiSaveProcessorControlState(ProcessorState);
}

VOID
FASTCALL
KeZeroPages(_Out_writes_bytes_(Size) PVOID Address, _In_ ULONG Size)
{
    ASSERT(((ULONG_PTR)Address & (PAGE_SIZE - 1)) == 0);
    ASSERT((Size & (PAGE_SIZE - 1)) == 0);

    /* MM owns this writable mapping and publishes the page only after zeroing. */
    RtlZeroMemory(Address, Size);
}

VOID
NTAPI
KiSaveProcessorControlState(
    _Out_ PKPROCESSOR_STATE ProcessorState)
{
    PKSPECIAL_REGISTERS Registers = &ProcessorState->SpecialRegisters;

    /* These are sequential call-time observations. Trap entry must save its
     * own interrupted state before nested traps can overwrite the CSRs. */
    __asm__ __volatile__("csrr %0, sstatus" : "=r"(Registers->Sstatus) :: "memory");
    __asm__ __volatile__("csrr %0, sie" : "=r"(Registers->Sie) :: "memory");
    __asm__ __volatile__("csrr %0, stvec" : "=r"(Registers->Stvec) :: "memory");
    __asm__ __volatile__("csrr %0, sscratch" : "=r"(Registers->Sscratch) :: "memory");
    __asm__ __volatile__("csrr %0, sepc" : "=r"(Registers->Sepc) :: "memory");
    __asm__ __volatile__("csrr %0, scause" : "=r"(Registers->Scause) :: "memory");
    __asm__ __volatile__("csrr %0, stval" : "=r"(Registers->Stval) :: "memory");
    __asm__ __volatile__("csrr %0, sip" : "=r"(Registers->Sip) :: "memory");
    __asm__ __volatile__("csrr %0, satp" : "=r"(Registers->Satp) :: "memory");
}

VOID NTAPI
KiRestoreProcessorControlState(_In_ PKPROCESSOR_STATE ProcessorState)
{
    PKSPECIAL_REGISTERS Registers;
    ULONG64 Satp;
    if (!ProcessorState) return;
    Registers = &ProcessorState->SpecialRegisters;
    _disable();
    __asm__ __volatile__("csrw stvec, %0" :: "r"(Registers->Stvec) : "memory");
    __asm__ __volatile__("csrw sscratch, %0" :: "r"(Registers->Sscratch) : "memory");
    __asm__ __volatile__("csrr %0, satp" : "=r"(Satp));
    if (Satp != Registers->Satp)
        __asm__ __volatile__("csrw satp, %0\n\tsfence.vma zero, zero"
                             :: "r"(Registers->Satp) : "memory");
    __asm__ __volatile__("csrw sepc, %0" :: "r"(Registers->Sepc) : "memory");
    __asm__ __volatile__("csrw scause, %0" :: "r"(Registers->Scause) : "memory");
    __asm__ __volatile__("csrw stval, %0" :: "r"(Registers->Stval) : "memory");
    /* Pending timer/external interrupts belong to their controllers. Do not
     * replay the saved sip snapshot or discard newly pending interrupts. */
    __asm__ __volatile__("csrw sie, %0" :: "r"(Registers->Sie) : "memory");
    __asm__ __volatile__("csrw sstatus, %0" :: "r"(Registers->Sstatus) : "memory");
}
