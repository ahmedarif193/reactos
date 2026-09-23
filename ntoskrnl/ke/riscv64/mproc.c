/* SPDX-License-Identifier: GPL-3.0-or-later
 * Copyright 2026 Ahmed ARIF
 */
#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

typedef struct _KI_RISCV_AP
{
    KPCR Pcr;
    ETHREAD Thread;
    PVOID Stack;
    volatile LONG Online;
    DECLSPEC_ALIGN(16) UCHAR PanicStack[KERNEL_STACK_SIZE];
} KI_RISCV_AP;

static DECLSPEC_NORETURN VOID NTAPI
KiRiscvStartProcessor(KI_RISCV_AP *Ap)
{
    PKPCR Pcr = &Ap->Pcr;
    PKPRCB Prcb = &Pcr->Prcb;
    PKTHREAD Thread = &Ap->Thread.Tcb;
    PKPROCESS Process = (PKPROCESS)KeLoaderBlock->Process;

    __asm__ __volatile__("csrw sscratch, %0" :: "r"(Pcr) : "memory");
    if (!KiRiscvInitializeTrapVector())
        KeBugCheckEx(HAL_INITIALIZATION_FAILED, Prcb->Number, Pcr->HartId, 0, 0);
    Thread->ApcState.Process = Process;
    PoInitializePrcb(Prcb);
    ExInitPoolLookasidePointers();
    KiSaveProcessorControlState(&Prcb->ProcessorState);
    KfLowerIrql(APC_LEVEL);
    KeInitializeThread(Process, Thread, NULL, NULL, NULL, NULL, NULL, Ap->Stack);
    Thread->NextProcessor = Thread->IdealProcessor = Prcb->Number;
    Thread->Priority = 0;
    Thread->State = Running;
    Thread->Running = TRUE;
    KiThreadAffinityMask(Thread) = Prcb->SetMember;
    Thread->WaitIrql = DISPATCH_LEVEL;
    InterlockedOr64((PLONG64)&Process->ActiveProcessors, Prcb->SetMember);
    ExpInitializeExecutive(Prcb->Number, KeLoaderBlock);
    KfRaiseIrql(HIGH_LEVEL);
    HalInitializeProcessor(Prcb->Number, KeLoaderBlock);
    KiProcessorBlock[Prcb->Number] = Prcb;
    InterlockedOr64((PLONG64)&KiNode0.ProcessorMask, Prcb->SetMember);
    InterlockedOr64((PLONG64)&KiIdleSummary, Prcb->SetMember);
    KeNumberProcessors = Prcb->Number + 1;
    InterlockedOr64((PLONG64)&KeActiveProcessors, Prcb->SetMember);
    /* Shootdowns target active processors only: drop whatever this hart
     * cached while it initialized outside that set. */
    KeFlushCurrentTb();
    /* The initiator may recycle the trampoline only after this point. */
    InterlockedExchange(&Ap->Online, 1);
    KfLowerIrql(DISPATCH_LEVEL);
    _enable();
    KiIdleLoop();
}

CODE_SEG("INIT") VOID NTAPI KeStartAllProcessors(VOID)
{
    ULONG Number, Maximum = KeMaximumProcessors;
    ULONG_PTR Hart;
    KI_RISCV_AP *Ap;
    PVOID DpcStack;
    KPROCESSOR_STATE State;
    LARGE_INTEGER Start, Now, Frequency;

    if (KeNumprocSpecified) Maximum = min(Maximum, KeNumprocSpecified);
    if (KeBootprocSpecified) Maximum = min(Maximum, KeBootprocSpecified);
    for (Number = 1; Number < Maximum; ++Number)
    {
        if (!HalpRiscvQueryProcessorHartId(Number, &Hart)) break;
        Ap = ExAllocatePoolZero(NonPagedPool, sizeof(*Ap), TAG_KERNEL);
        if (!Ap) break;
        Ap->Stack = MmCreateKernelStack(FALSE, 0);
        DpcStack = MmCreateKernelStack(FALSE, 0);
        if (!Ap->Stack || !DpcStack) goto Failed;
        KiRiscvInitializePcr(&Ap->Pcr, &Ap->Thread.Tcb, Hart, Number, DpcStack,
                             Ap->PanicStack + sizeof(Ap->PanicStack));
        RtlZeroMemory(&State, sizeof(State));
        KiSaveProcessorControlState(&State);
        State.SpecialRegisters.Satp = RISCV64_LOADER_SATP_MODE_SV39 |
            (((PKPROCESS)KeLoaderBlock->Process)->DirectoryTableBase >> PAGE_SHIFT);
        State.SpecialRegisters.Sscratch = (ULONG_PTR)&Ap->Pcr;
        State.ContextFrame.Pc = (ULONG_PTR)KiRiscvStartProcessor;
        /* Leave the common thread initializer's stack-control area untouched. */
        State.ContextFrame.Sp = (ULONG_PTR)Ap->Stack - PAGE_SIZE;
        State.ContextFrame.A0 = (ULONG_PTR)Ap;
        if (!HalStartNextProcessor(KeLoaderBlock, &State)) goto Failed;
        Start = KeQueryPerformanceCounter(&Frequency);
        while (!InterlockedCompareExchange(&Ap->Online, 0, 0))
        {
            Now = KeQueryPerformanceCounter(NULL);
            if (Now.QuadPart - Start.QuadPart > Frequency.QuadPart * 10)
            {
                /* HSM accepted the start: the AP may still reference all
                 * startup storage. Never free it or continue a partial boot. */
                KeBugCheckEx(HAL_INITIALIZATION_FAILED, Number, Hart,
                             (ULONG_PTR)Ap, 0);
            }
            YieldProcessor();
        }
        DPRINT1("RISC-V: CPU %lu hart %Iu online\n", Number, Hart);
        continue;
Failed:
        if (DpcStack) MmDeleteKernelStack(DpcStack, FALSE);
        if (Ap->Stack) MmDeleteKernelStack(Ap->Stack, FALSE);
        ExFreePoolWithTag(Ap, TAG_KERNEL);
        break;
    }
    DPRINT1("RISC-V: %u processors online, affinity %Ix\n",
            (ULONG)(UCHAR)KeNumberProcessors, KeActiveProcessors);
}
