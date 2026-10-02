/*
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Windows NT PowerPC kernel-private interfaces
 */

#pragma once

/* The switch backend completes the outgoing-thread handoff on the new stack. */
#define KI_ARCH_THREAD_HANDOFF_USES_RUNNING 1

/* PReP external interrupts run above dispatch/synchronization IRQL. Their
 * controller is deferred only at this device level or higher. */
#define KI_PPC_EXTERNAL_IRQL (IPI_LEVEL - 2)

/*
 * Trap model. Interrupts enter little-endian real mode (MSR[ILE]). The
 * vector code is copied to physical 0x100 and finds the PCR through SPRG1
 * (physical address); SPRG0 holds its KSEG0 virtual address for kernel code.
 * DSI and ISI first try a real-mode reload of the hashed page table from the
 * software page tables. Everything else, and every reload that fails, saves
 * the interrupted state in the PCR, turns translation on and builds a
 * KTRAP_FRAME on the kernel stack before calling KiPpcTrapDispatch.
 */
#define PPC_VECTOR_RESET           0x0100
#define PPC_VECTOR_MACHINE_CHECK   0x0200
#define PPC_VECTOR_DSI             0x0300
#define PPC_VECTOR_ISI             0x0400
#define PPC_VECTOR_EXTERNAL        0x0500
#define PPC_VECTOR_ALIGNMENT       0x0600
#define PPC_VECTOR_PROGRAM         0x0700
#define PPC_VECTOR_FP_UNAVAILABLE  0x0800
#define PPC_VECTOR_DECREMENTER     0x0900
#define PPC_VECTOR_SYSTEM_CALL     0x0C00
#define PPC_VECTOR_TRACE           0x0D00
#define PPC_VECTOR_PERFORMANCE     0x0F00
#define PPC_VECTOR_BREAKPOINT      0x1300
#define PPC_VECTOR_LAST            0x1400

/* SRR1 bits reported by the program interrupt. */
#define PPC_SRR1_PROGRAM_FP        0x00100000UL
#define PPC_SRR1_PROGRAM_ILLEGAL   0x00080000UL
#define PPC_SRR1_PROGRAM_PRIVILEGED 0x00040000UL
#define PPC_SRR1_PROGRAM_TRAP      0x00020000UL

/* DSISR bits. */
#define PPC_DSISR_NOT_FOUND        0x40000000UL
#define PPC_DSISR_PROTECTION       0x08000000UL
#define PPC_DSISR_STORE            0x02000000UL

/* SRR1 bits reported by the instruction storage interrupt. */
#define PPC_SRR1_ISI_NOT_FOUND     0x40000000UL
#define PPC_SRR1_ISI_GUARDED       0x10000000UL
#define PPC_SRR1_ISI_PROTECTION    0x08000000UL

/* Twi/tw trap codes (TO field 31 with a 16-bit immediate). */
#define PPC_BREAKPOINT_INSTRUCTION 0x0FE00016UL /* twi 31,0,0x16 */
#define PPC_DEBUG_SERVICE_TRAP     0x19
#define PPC_BREAKPOINT_TRAP        0x16
#define PPC_FASTFAIL_TRAP          0x29

/* Kernel MSR: translation, machine check, FP, little-endian in and out. */
#define PPC_MSR_ILE                0x00010000UL
#define PPC_MSR_POW                0x00040000UL
#define PPC_MSR_SE                 0x00000400UL
#define PPC_MSR_BE                 0x00000200UL
#define PPC_KERNEL_MSR             (MSR_ME | MSR_IR | MSR_DR | MSR_FP | MSR_RI | MSR_LE | PPC_MSR_ILE)
#define PPC_USER_MSR               (PPC_KERNEL_MSR | MSR_PR | MSR_EE)
/* Bits a user CONTEXT may change. */
#define PPC_USER_MSR_MASK          (PPC_MSR_SE | PPC_MSR_BE)

/* Segment register layout: Ks = 0 (supervisor key 0), Kp = 1 (user key 1). */
#define PPC_SR_KP                  0x20000000UL
#define PPC_SR_VSID_MASK           0x00FFFFFFUL

#define KeGetContextSwitches(Prcb) ((Prcb)->KeContextSwitches)
#define Ki386PerfEnd()

/* A CONTEXT records the interrupted MSR; PR marks problem (user) state. */
#define KiGetContextPreviousMode(Context) \
    (((Context)->Msr & MSR_PR) ? UserMode : KernelMode)

#define KD_BREAKPOINT_TYPE        ULONG
#define KD_BREAKPOINT_SIZE        sizeof(ULONG)
#define KD_BREAKPOINT_VALUE       PPC_BREAKPOINT_INSTRUCTION
#define KD_ASSERT_BREAKPOINT_SIZE KD_BREAKPOINT_SIZE

FORCEINLINE
ULONG
KiPpcReadMsr(VOID)
{
    ULONG Msr;

    __asm__ __volatile__("mfmsr %0" : "=r"(Msr) :: "memory");
    return Msr;
}

FORCEINLINE
VOID
KiPpcWriteMsr(_In_ ULONG Msr)
{
    __asm__ __volatile__("mtmsr %0\n\tisync" :: "r"(Msr) : "memory");
}

FORCEINLINE
BOOLEAN
KeDisableInterrupts(VOID)
{
    ULONG Msr = KiPpcReadMsr();

    KiPpcWriteMsr(Msr & ~MSR_EE);
    return (Msr & MSR_EE) != 0;
}

FORCEINLINE
VOID
KeRestoreInterrupts(BOOLEAN WereEnabled)
{
    if (WereEnabled)
        _enable();
    else
        _disable();
}

FORCEINLINE
VOID
KeInvalidateTlbEntry(_In_ PVOID Address)
{
    /* The hashed page table caches translations as well; the memory manager
     * backend removes those entries. */
    extern VOID NTAPI KiPpcFlushTranslation(_In_ PVOID Address);
    KiPpcFlushTranslation(Address);
}

FORCEINLINE
ULONG_PTR
KeGetContextPc(_In_ PCONTEXT Context)
{
    return Context->Iar;
}

FORCEINLINE
VOID
KeSetContextPc(_Inout_ PCONTEXT Context, _In_ ULONG_PTR ProgramCounter)
{
    Context->Iar = ProgramCounter;
}

FORCEINLINE
ULONG_PTR
KeGetContextReturnRegister(_In_ PCONTEXT Context)
{
    return Context->Gpr3;
}

FORCEINLINE
VOID
KeSetContextReturnRegister(_Inout_ PCONTEXT Context, _In_ ULONG_PTR ReturnValue)
{
    Context->Gpr3 = ReturnValue;
}

FORCEINLINE
ULONG_PTR
KeGetContextStackRegister(_In_ PCONTEXT Context)
{
    return Context->Gpr1;
}

/* The back chain at 0(r1) is the frame link; there is no frame pointer. */
FORCEINLINE
ULONG_PTR
KeGetContextFrameRegister(_In_ PCONTEXT Context)
{
    return Context->Gpr1;
}

FORCEINLINE
VOID
KeSetContextFrameRegister(_Inout_ PCONTEXT Context, _In_ ULONG_PTR Frame)
{
    Context->Gpr1 = Frame;
}

FORCEINLINE
ULONG_PTR
KeGetTrapFrameStackRegister(_In_ PKTRAP_FRAME TrapFrame)
{
    return TrapFrame->Context.Gpr1;
}

FORCEINLINE
ULONG_PTR
KeGetTrapFrameFrameRegister(_In_ PKTRAP_FRAME TrapFrame)
{
    return TrapFrame->Context.Gpr1;
}

#ifdef __cplusplus
extern "C" {
#endif
DECLSPEC_NORETURN VOID NTAPI KiPpcSystemStartup(_Inout_ PLOADER_PARAMETER_BLOCK LoaderBlock);
BOOLEAN NTAPI KiPpcInitializeBootPcr(_Out_ PKPCR Pcr, _In_ PKTHREAD Thread, _In_ PVOID DpcStack, _In_ PVOID PanicStack);
VOID NTAPI KiPpcInitializePcr(_Out_ PKPCR Pcr, _In_ PKTHREAD Thread, _In_ ULONG Number, _In_ PVOID DpcStack, _In_ PVOID PanicStack);
BOOLEAN NTAPI KiPpcInstallVectors(VOID);
VOID NTAPI KiPpcTrapDispatch(_Inout_ PKTRAP_FRAME TrapFrame);
VOID NTAPI KiPpcExternalInterrupt(_Inout_ PKTRAP_FRAME TrapFrame);
VOID NTAPI KiPpcClockInterrupt(_Inout_ PKTRAP_FRAME TrapFrame);
DECLSPEC_NORETURN VOID NTAPI KiPpcSystemServiceDispatch(_Inout_ PKTRAP_FRAME TrapFrame);
DECLSPEC_NORETURN VOID NTAPI KiPpcReturnToUser(_Inout_ PKTRAP_FRAME TrapFrame);
NTSTATUS NTAPI KiPpcSetUserEntry(_Inout_ PKTRAP_FRAME TrapFrame, _In_ PVOID Descriptor);
VOID NTAPI KiPpcDeliverSoftwareInterrupts(VOID);
VOID NTAPI KiPpcRequestSoftwareInterrupt(_In_ KIRQL Irql);
VOID NTAPI KiPpcClearSoftwareInterrupt(_In_ KIRQL Irql);
VOID NTAPI KiPpcConsoleInitialize(_In_ PLOADER_PARAMETER_BLOCK LoaderBlock);
BOOLEAN NTAPI KiPpcConsoleReady(VOID);
VOID NTAPI KiPpcConsolePutByte(_In_ UCHAR Byte);
VOID NTAPI KiPpcConsoleWrite(_In_reads_bytes_(Length) PCCH Buffer, _In_ SIZE_T Length);
BOOLEAN NTAPI KiPpcConsoleGetByte(_Out_ PUCHAR Byte);
VOID NTAPI KiPpcIdentifyProcessor(_In_ PLOADER_PARAMETER_BLOCK LoaderBlock);
DECLSPEC_NORETURN VOID NTAPI KiPpcUnimplemented(_In_ const CHAR *Routine);
DECLSPEC_NORETURN VOID NTAPI KiPpcRestoreTrapFrame(_In_ PKTRAP_FRAME TrapFrame);
DECLSPEC_NORETURN VOID NTAPI KiPpcStartUserThread(VOID);
DECLSPEC_NORETURN VOID KiExceptionExit(PKTRAP_FRAME TrapFrame, PKEXCEPTION_FRAME ExceptionFrame);
VOID NTAPI KiPpcFlushTranslation(_In_ PVOID Address);
VOID NTAPI KiPpcFlushAllTranslations(VOID);
VOID NTAPI KiPpcLoadAddressSpace(_In_ ULONG_PTR DirectoryTableBase, _In_ ULONG VsidBase);
PKTRAP_FRAME NTAPI KeGetTrapFrame(_In_ PKTHREAD Thread);
PKEXCEPTION_FRAME NTAPI KeGetExceptionFrame(_In_ PKTHREAD Thread);
ULONG_PTR NTAPI KeGetTrapFramePc(_In_ PKTRAP_FRAME TrapFrame);
BOOLEAN NTAPI KeGetTrapFrameInterruptState(_In_ PKTRAP_FRAME TrapFrame);
BOOLEAN NTAPI KiUserTrap(_In_ PKTRAP_FRAME TrapFrame);
#define KiIsUserModeTrap(TrapFrame) KiUserTrap(TrapFrame)

/* The trap frame holds every register: nothing lives in an exception frame. */
#define KI_NO_EXCEPTION_FRAME

/* User-mode SList pops recover through SEH: ntdll has no pop sequence for the
 * fault handler to roll back. */
#define KI_USER_SLIST_POP_NO_ROLLBACK

/* Native assembly entry used by KD to recognize user callback transitions. */
#define KI_USER_MODE_CALLBACK_ENTRY KiPpcCallUserMode

NTSTATUS
NTAPI
KiPpcCallUserMode(
    _In_ PKTRAP_FRAME Frame,
    _Out_ PVOID *OutputBuffer,
    _Out_ PULONG OutputLength);
VOID NTAPI KiRundownThread(_In_ PKTHREAD Thread);
BOOLEAN NTAPI KiSwapContextResume(_In_ BOOLEAN ApcBypass, _In_ PKTHREAD OldThread, _In_ PKTHREAD NewThread);
VOID NTAPI KiRetireDpcListInDpcStack(_In_ PKPRCB Prcb, _In_ PVOID DpcStack);
DECLSPEC_NORETURN VOID NTAPI KiThreadStartup(VOID);
VOID NTAPI KeFlushProcessTb(VOID);
VOID NTAPI KeSweepICache(_In_opt_ PVOID BaseAddress, _In_ SIZE_T FlushSize);
VOID NTAPI KiPpcSweepDcache(_In_ PVOID BaseAddress, _In_ SIZE_T FlushSize);
NTSTATUS NTAPI KiPpcCopyFromUser(PVOID Destination, const VOID *Source, SIZE_T Length);
NTSTATUS NTAPI KiPpcCopyToUser(PVOID Destination, const VOID *Source, SIZE_T Length);
/*
 * HAL interrupt controller and clock (hal.spec, -arch=ppc). IRQL is a
 * software state: MSR[EE] stays on outside short critical sections. An
 * external interrupt taken at or above KI_PPC_EXTERNAL_IRQL makes the HAL mask the
 * controller until the IRQL drops; a decrementer interrupt taken at or
 * above CLOCK_LEVEL is replayed when it drops below.
 */
NTHALAPI VOID NTAPI HalpPpcClockInterrupt(_In_ PKTRAP_FRAME TrapFrame);
NTHALAPI VOID NTAPI HalpPpcParkClock(VOID);
NTHALAPI ULONG NTAPI HalpPpcClaimInterrupt(VOID);
NTHALAPI VOID NTAPI HalpPpcCompleteInterrupt(_In_ ULONG Vector);
NTHALAPI VOID NTAPI HalpPpcDeferExternal(VOID);
NTHALAPI VOID NTAPI HalpPpcRestoreExternal(VOID);
#define HALP_PPC_SPURIOUS_INTERRUPT 0xFFFFFFFFUL
#define KI_PPC_MAX_INTERRUPT_VECTOR 255
VOID NTAPI KiIpiSendTbFlush(KAFFINITY Targets, PVOID Address, ULONG Pages);
VOID NTAPI KiIpiProcessRequests(VOID);
BOOLEAN KiProcessorFreezeHandler(PKTRAP_FRAME TrapFrame, PKEXCEPTION_FRAME ExceptionFrame);
VOID NTAPI KiFreezeIfRequested(VOID);
#ifdef __cplusplus
}
#endif

FORCEINLINE
PKTHREAD
_KeGetCurrentThread(VOID)
{
    BOOLEAN Enabled = KeDisableInterrupts();
    PKTHREAD Thread = KeGetCurrentPrcb()->CurrentThread;

    KeRestoreInterrupts(Enabled);
    return Thread;
}

FORCEINLINE
PKTRAP_FRAME
KiGetLinkedTrapFrame(
    _In_ PKTRAP_FRAME TrapFrame)
{
    return TrapFrame->PreviousTrapFrame;
}

/* System services that raise or continue unlink the service trap frame on
 * entry; the trap frame holds every register, so nothing else is saved. */
typedef struct _KI_SERVICE_EXCEPTION_STATE
{
    UCHAR Reserved;
} KI_SERVICE_EXCEPTION_STATE, *PKI_SERVICE_EXCEPTION_STATE;

FORCEINLINE
PKEXCEPTION_FRAME
KiEnterServiceException(
    _In_ PKTHREAD Thread,
    _In_ PKTRAP_FRAME TrapFrame,
    _In_ PCONTEXT Context,
    _In_ KPROCESSOR_MODE PreviousMode,
    _In_opt_ PKEXCEPTION_FRAME ExceptionFrame,
    _Out_ PKI_SERVICE_EXCEPTION_STATE State)
{
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(PreviousMode);
    UNREFERENCED_PARAMETER(State);
    Thread->TrapFrame = KiGetLinkedTrapFrame(TrapFrame);
    return ExceptionFrame;
}

FORCEINLINE
VOID
KiAbortServiceException(
    _In_ PKTRAP_FRAME TrapFrame,
    _In_opt_ PKEXCEPTION_FRAME ExceptionFrame,
    _In_ PKI_SERVICE_EXCEPTION_STATE State)
{
    UNREFERENCED_PARAMETER(TrapFrame);
    UNREFERENCED_PARAMETER(ExceptionFrame);
    UNREFERENCED_PARAMETER(State);
}

FORCEINLINE
VOID
KiLeaveServiceException(
    _In_ PKTHREAD Thread,
    _In_ PKTRAP_FRAME TrapFrame)
{
    UNREFERENCED_PARAMETER(Thread);
    UNREFERENCED_PARAMETER(TrapFrame);
}

FORCEINLINE
BOOLEAN
_KeIsExecutingDpc(VOID)
{
    BOOLEAN Enabled = KeDisableInterrupts();
    BOOLEAN Active = KeGetCurrentPrcb()->DpcRoutineActive;

    KeRestoreInterrupts(Enabled);
    return Active;
}

FORCEINLINE
BOOLEAN
KiIsDpcInterruptRequested(
    _In_ PKPRCB Prcb)
{
    return Prcb->DpcInterruptRequested != FALSE;
}

FORCEINLINE
VOID
KiSetDpcInterruptRequested(
    _Inout_ PKPRCB Prcb)
{
    Prcb->DpcInterruptRequested = TRUE;
}

FORCEINLINE
VOID
KiClearDpcInterruptRequested(
    _Inout_ PKPRCB Prcb)
{
    Prcb->DpcInterruptRequested = FALSE;
}

FORCEINLINE
VOID
KiSetDpcPresent(
    _Inout_ PKPRCB Prcb)
{
    UNREFERENCED_PARAMETER(Prcb);
}

FORCEINLINE
VOID
KiClearDpcRequestState(
    _Inout_ PKPRCB Prcb)
{
    Prcb->DpcInterruptRequested = FALSE;
}
