/*
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Windows NT PowerPC processor state
 */

#include <ntoskrnl.h>

VOID
NTAPI
KiSaveProcessorState(PKTRAP_FRAME TrapFrame, PKEXCEPTION_FRAME ExceptionFrame)
{
    PKPROCESSOR_STATE State = &KeGetCurrentPrcb()->ProcessorState;

    UNREFERENCED_PARAMETER(ExceptionFrame);
    State->ContextFrame = TrapFrame->Context;
    KiSaveProcessorControlState(State);
}

VOID
NTAPI
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
    RtlZeroMemory(Address, Size);
}

VOID
NTAPI
KiSaveProcessorControlState(
    _Out_ PKPROCESSOR_STATE ProcessorState)
{
    PKSPECIAL_REGISTERS Registers = &ProcessorState->SpecialRegisters;
    ULONG Segment;

    __asm__ __volatile__("mfmsr %0" : "=r"(Registers->Msr) :: "memory");
    __asm__ __volatile__("mfsdr1 %0" : "=r"(Registers->Sdr1) :: "memory");
    for (Segment = 0; Segment < 16; Segment++)
        __asm__ __volatile__("mfsrin %0, %1" : "=r"(Registers->Sr[Segment]) : "r"(Segment << 28) : "memory");
    __asm__ __volatile__("mfspr %0, 1008" : "=r"(Registers->Hid0) :: "memory");
    __asm__ __volatile__("mfspr %0, 1009" : "=r"(Registers->Hid1) :: "memory");
    __asm__ __volatile__("mfsprg %0, 0" : "=r"(Registers->Sprg[0]) :: "memory");
    __asm__ __volatile__("mfsprg %0, 1" : "=r"(Registers->Sprg[1]) :: "memory");
    __asm__ __volatile__("mfsprg %0, 2" : "=r"(Registers->Sprg[2]) :: "memory");
    __asm__ __volatile__("mfsprg %0, 3" : "=r"(Registers->Sprg[3]) :: "memory");
    __asm__ __volatile__("mfdar %0" : "=r"(Registers->Dar) :: "memory");
    __asm__ __volatile__("mfdsisr %0" : "=r"(Registers->Dsisr) :: "memory");
    __asm__ __volatile__("mfsrr0 %0" : "=r"(Registers->Srr0) :: "memory");
    __asm__ __volatile__("mfsrr1 %0" : "=r"(Registers->Srr1) :: "memory");
    __asm__ __volatile__("mfdec %0" : "=r"(Registers->Dec) :: "memory");
    __asm__ __volatile__("mfpvr %0" : "=r"(Registers->Pvr) :: "memory");
}

VOID
NTAPI
KiRestoreProcessorControlState(_In_ PKPROCESSOR_STATE ProcessorState)
{
    PKSPECIAL_REGISTERS Registers;
    ULONG Segment;

    if (!ProcessorState)
        return;
    Registers = &ProcessorState->SpecialRegisters;
    _disable();
    for (Segment = 0; Segment < 16; Segment++)
        __asm__ __volatile__("mtsrin %0, %1" :: "r"(Registers->Sr[Segment]), "r"(Segment << 28) : "memory");
    __asm__ __volatile__("mtsprg 0, %0\n\tmtsprg 1, %1\n\tisync" :: "r"(Registers->Sprg[0]), "r"(Registers->Sprg[1]) : "memory");
    if (Registers->Msr & MSR_EE)
        _enable();
}
