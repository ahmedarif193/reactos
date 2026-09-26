/*
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 */

/* ReactOS RV64 kernel-private interfaces. */
#pragma once

/* Native assembly entry used by KD to recognize user callback transitions. */
#define KI_USER_MODE_CALLBACK_ENTRY KiRiscvCallUserMode

NTSTATUS
NTAPI
KiRiscvCallUserMode(
    _In_ PKTRAP_FRAME Frame,
    _Out_ PVOID *OutputBuffer,
    _Out_ PULONG OutputLength);

/* TODO(riscv64): initialize scheduler sub-nodes and maintain their idle sets
 * before enabling KI_CORE_PARKING. */

/* Software synchronization priority, below the clock and IPI levels. */
#define SYNCH_LEVEL 12

#define RISCV_SSTATUS_SIE  (1ULL << 1)
#define RISCV_SSTATUS_SPIE (1ULL << 5)
#define RISCV_SSTATUS_SPP  (1ULL << 8)
#define RISCV_SSTATUS_SUM  (1ULL << 18)
#define RISCV_SSTATUS_MXR  (1ULL << 19)
#define RISCV_SSTATUS_FS   (3ULL << 13)
#define RISCV_SSTATUS_UXL64 (2ULL << 32)
/* NT currently permits supervisor data access to probed user buffers. User
 * contexts cannot supply these CSRs, enable MXR, or select supervisor mode. */
#define RISCV_USER_SSTATUS (RISCV_SSTATUS_UXL64 | RISCV_SSTATUS_FS | RISCV_SSTATUS_SUM | RISCV_SSTATUS_SPIE)
#define RISCV_SIE_SSIE     (1ULL << 1)
#define RISCV_SIE_STIE     (1ULL << 5)
#define RISCV_SIE_SEIE     (1ULL << 9)

NTHALAPI ULONG NTAPI HalpRiscvClaimPlicInterrupt(VOID);
NTHALAPI VOID NTAPI HalpRiscvCompletePlicInterrupt(_In_ ULONG Source);

#define KeGetContextSwitches(Prcb) ((Prcb)->KeContextSwitches)
/* TODO: measure each hart's cycle counter against the time CSR (Zicntr)
 * where firmware grants S-mode cycle access; until then report Prcb->MHz. */
#define KiQueryEffectiveProcessorMhz(Number) (KiProcessorBlock[(Number)]->MHz)
#define Ki386PerfEnd()

/* Current page table root, as recorded in a crash dump header. */
FORCEINLINE
ULONG64
KiReadDirectoryTableBase(VOID)
{
    ULONG64 Satp;

    __asm__ __volatile__("csrr %0, satp" : "=r"(Satp));
    return (Satp & RISCV64_LOADER_SATP_PPN_MASK) << PAGE_SHIFT;
}

/* A CONTEXT carries no sstatus; kernel code runs in the upper Sv39 half. */
#define KiGetContextPreviousMode(Context) \
    (((LONG64)(Context)->Pc < 0) ? KernelMode : UserMode)

/* RV64GC permits halfword instruction boundaries. Use C.EBREAK so inserting
 * a software breakpoint cannot overwrite the next compressed instruction.
 * This encoding does not define or enable a RISC-V KD wire protocol. */
#define KD_BREAKPOINT_TYPE        USHORT
#define KD_BREAKPOINT_SIZE        sizeof(USHORT)
#define KD_BREAKPOINT_VALUE       0x9002
#define KD_ASSERT_BREAKPOINT_SIZE KD_BREAKPOINT_SIZE

FORCEINLINE
BOOLEAN
KeDisableInterrupts(VOID)
{
    ULONG_PTR Status;

    /* Read and clear SIE atomically; preserve the other supervisor state. */
    __asm__ __volatile__("csrrci %0, sstatus, 2" : "=r"(Status) :: "memory");
    return (Status & 2) != 0;
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
    /* Local leaf mapping, all ASIDs including global entries. A hierarchy
     * change needs an all-address fence; other harts need a shootdown. */
    __asm__ __volatile__("sfence.vma %0, zero" :: "r"(Address) : "memory");
}

FORCEINLINE
ULONG_PTR
KeGetContextPc(_In_ PCONTEXT Context)
{
    return Context->Pc;
}

FORCEINLINE
VOID
KeSetContextPc(_Inout_ PCONTEXT Context, _In_ ULONG_PTR ProgramCounter)
{
    Context->Pc = ProgramCounter;
}

FORCEINLINE
ULONG_PTR
KeGetContextReturnRegister(_In_ PCONTEXT Context)
{
    return Context->A0;
}

FORCEINLINE
VOID
KeSetContextReturnRegister(_Inout_ PCONTEXT Context, _In_ ULONG_PTR ReturnValue)
{
    Context->A0 = ReturnValue;
}

FORCEINLINE
ULONG_PTR
KeGetContextStackRegister(_In_ PCONTEXT Context)
{
    return Context->Sp;
}

/* s0 is the psABI frame pointer: [fp-8] = saved ra, [fp-16] = saved fp. */
FORCEINLINE
ULONG_PTR
KeGetContextFrameRegister(_In_ PCONTEXT Context)
{
    return Context->S0;
}

FORCEINLINE
VOID
KeSetContextFrameRegister(_Inout_ PCONTEXT Context, _In_ ULONG_PTR Frame)
{
    Context->S0 = Frame;
}

FORCEINLINE
ULONG_PTR
KeGetTrapFrameStackRegister(_In_ PKTRAP_FRAME TrapFrame)
{
    return TrapFrame->Context.Sp;
}

FORCEINLINE
ULONG_PTR
KeGetTrapFrameFrameRegister(_In_ PKTRAP_FRAME TrapFrame)
{
    return TrapFrame->Context.S0;
}

/* Boot-hart processor identification, filled from the device tree
 * and the SBI base extension. KeFeatureBits stays zero. */
#define KI_RISCV_FEATURE_ZICBOM      0x00000001
#define KI_RISCV_FEATURE_ZICBOZ      0x00000002
#define KI_RISCV_FEATURE_ZICBOP      0x00000004
#define KI_RISCV_FEATURE_ZIHINTPAUSE 0x00000008
#define KI_RISCV_FEATURE_ZICNTR      0x00000010
#define KI_RISCV_FEATURE_ZBA         0x00000020
#define KI_RISCV_FEATURE_ZBB         0x00000040
#define KI_RISCV_FEATURE_ZBS         0x00000080
#define KI_RISCV_FEATURE_SSTC        0x00000100
#define KI_RISCV_FEATURE_SVADU       0x00000200
#define KI_RISCV_FEATURE_SVPBMT      0x00000400
#define KI_RISCV_FEATURE_SVNAPOT     0x00000800
#define KI_RISCV_FEATURE_SVINVAL     0x00001000
#define KI_RISCV_FEATURE_V           0x00002000
#define KI_RISCV_FEATURE_H           0x00004000

#define KI_RISCV_EXTENSION_LIST_SIZE 512

typedef struct _KI_RISCV_PROCESSOR_FEATURES
{
    BOOLEAN Valid;
    CHAR IsaBase[16];
    CHAR MmuType[16];
    ULONG Flags;
    ULONG CbomBlockSize;
    ULONG CbozBlockSize;
    ULONG CbopBlockSize;
    ULONG SbiSpecVersion;
    ULONG_PTR SbiImplId;
    ULONG_PTR SbiImplVersion;
    ULONG64 TimebaseFrequency;
    ULONG64 HartId;
    /* Space-separated extension names as advertised by firmware. */
    CHAR Extensions[KI_RISCV_EXTENSION_LIST_SIZE];
} KI_RISCV_PROCESSOR_FEATURES, *PKI_RISCV_PROCESSOR_FEATURES;

/* Kernel-local SBI call result; the HAL keeps its own copy of this ABI. */
typedef struct _KI_RISCV_SBI_RETURN
{
    LONG_PTR Error;
    ULONG_PTR Value;
} KI_RISCV_SBI_RETURN;

/* Architecture entry/context code must implement these; do not infer a frame
 * address by subtracting another architecture's layout from InitialStack. */
#ifdef __cplusplus
extern "C" {
#endif
BOOLEAN NTAPI KiRiscvInitializeBootPcr(_Out_ PKPCR Pcr, _In_ PKTHREAD Thread, _In_ ULONG_PTR HartId, _In_ PVOID DpcStack);
DECLSPEC_NORETURN VOID NTAPI KiRiscvSystemStartup(_Inout_ PLOADER_PARAMETER_BLOCK LoaderBlock);
VOID NTAPI KiRiscvSetInterruptEnabled(_In_ ULONG_PTR Mask, _In_ BOOLEAN Enable);
VOID NTAPI KiRiscvRequestSoftwareInterrupt(_In_ KIRQL Irql);
VOID NTAPI KiRiscvClearSoftwareInterrupt(_In_ KIRQL Irql);
VOID NTAPI KiRiscvSendSoftwareInterrupt(_In_ KAFFINITY TargetSet, _In_ KIRQL Irql);
BOOLEAN NTAPI KiRiscvInitializeTrapVector(VOID);
KI_RISCV_SBI_RETURN NTAPI KiRiscvSbiCall(_In_ ULONG_PTR Extension, _In_ ULONG_PTR Function, _In_ ULONG_PTR Argument0, _In_ ULONG_PTR Argument1, _In_ ULONG_PTR Argument2);
VOID NTAPI KiRiscvConsoleInitialize(_In_ PLOADER_PARAMETER_BLOCK LoaderBlock);
BOOLEAN NTAPI KiRiscvConsoleReady(VOID);
ULONG NTAPI KiRiscvConsoleInterface(VOID);
VOID NTAPI KiRiscvConsolePutByte(_In_ UCHAR Byte);
VOID NTAPI KiRiscvConsoleWrite(_In_reads_bytes_(Length) PCCH Buffer, _In_ SIZE_T Length);
BOOLEAN NTAPI KiRiscvConsoleGetByte(_Out_ PUCHAR Byte);
VOID NTAPI KiRiscvIdentifyProcessor(_In_ PLOADER_PARAMETER_BLOCK LoaderBlock);
VOID NTAPI KiRiscvReportProcessorFeatures(VOID);
ULONG NTAPI KiRiscvQueryFeatureFlags(VOID);
extern KI_RISCV_PROCESSOR_FEATURES KiRiscvProcessorFeatures;
DECLSPEC_NORETURN VOID NTAPI KiRiscvTrapEntry(VOID);
DECLSPEC_NORETURN VOID NTAPI KiRiscvTrapStop(_In_ PKTRAP_FRAME TrapFrame);
VOID NTAPI KiRiscvTrapHandler(_Inout_ PKTRAP_FRAME TrapFrame);
VOID NTAPI KiRiscvInterruptDispatch(_Inout_ PKTRAP_FRAME TrapFrame);
PKINTERRUPT NTAPI KiRiscvQueryInterrupt(_In_ ULONG Source);
ULONG NTAPI KiRiscvQueryInterruptLimit(VOID);
/* Clock ISR exported by hal.dll (hal.spec, -arch=riscv64): rearms the
 * supervisor timer deadline and calls KeUpdateSystemTime at CLOCK_LEVEL. */
NTHALAPI VOID NTAPI HalpRiscvClockInterrupt(_In_ PKTRAP_FRAME TrapFrame);
DECLSPEC_NORETURN VOID NTAPI KiRiscvUnimplemented(_In_ const CHAR *Routine);
ULONG NTAPI KeGetCurrentProcessorNumber(VOID);
PKTRAP_FRAME NTAPI KeGetTrapFrame(_In_ PKTHREAD Thread);
PKEXCEPTION_FRAME NTAPI KeGetExceptionFrame(_In_ PKTHREAD Thread);
ULONG_PTR NTAPI KeGetTrapFramePc(_In_ PKTRAP_FRAME TrapFrame);
BOOLEAN NTAPI KeGetTrapFrameInterruptState(_In_ PKTRAP_FRAME TrapFrame);
BOOLEAN NTAPI KiUserTrap(_In_ PKTRAP_FRAME TrapFrame);
#define KiIsUserModeTrap(TrapFrame) KiUserTrap(TrapFrame)

/* The trap frame holds every register: nothing lives in an exception frame. */
#define KI_NO_EXCEPTION_FRAME

/* Without a 128-bit compare-exchange, user-mode SList pops run under a lock:
 * ntdll has no pop sequence for the fault handler to roll back. */
#define KI_USER_SLIST_POP_LOCKED
VOID NTAPI KiRundownThread(_In_ PKTHREAD Thread);
BOOLEAN NTAPI KiSwapContextResume(_In_ BOOLEAN ApcBypass, _In_ PKTHREAD OldThread, _In_ PKTHREAD NewThread);
VOID NTAPI KiRetireDpcListInDpcStack(_In_ PKPRCB Prcb, _In_ PVOID DpcStack);
DECLSPEC_NORETURN VOID NTAPI KiThreadStartup(VOID);
VOID NTAPI KeFlushProcessTb(VOID);
VOID NTAPI KeSweepICache(_In_opt_ PVOID BaseAddress, _In_ SIZE_T FlushSize);
NTSTATUS NTAPI KiRiscvCopyFromUser(PVOID Destination, const VOID *Source, SIZE_T Length);
NTSTATUS NTAPI KiRiscvCopyToUser(PVOID Destination, const VOID *Source, SIZE_T Length);
BOOLEAN NTAPI KiRiscvFixupUserCopy(PKTRAP_FRAME Frame, NTSTATUS Status);
NTSTATUS NTAPI KiRiscvReadMemory(PVOID Destination, const VOID *Source, SIZE_T Length);
NTSTATUS NTAPI KiRiscvReadCurrentProcess(PVOID Source, PVOID Destination, SIZE_T Length, PSIZE_T Returned);
DECLSPEC_NORETURN VOID NTAPI KiRiscvRestoreTrapFrame(PKTRAP_FRAME Frame);
DECLSPEC_NORETURN VOID NTAPI KiRiscvReturnToUser(PKTRAP_FRAME Frame);
DECLSPEC_NORETURN VOID NTAPI KiRiscvStartUserThread(VOID);
DECLSPEC_NORETURN VOID KiExceptionExit(PKTRAP_FRAME TrapFrame, PKEXCEPTION_FRAME ExceptionFrame);
DECLSPEC_NORETURN VOID NTAPI KiRiscvSystemService(PKTRAP_FRAME Frame);
#ifdef __cplusplus
}
#endif

FORCEINLINE
PKTHREAD
_KeGetCurrentThread(VOID)
{
    BOOLEAN Enabled = KeDisableInterrupts();
    PKTHREAD Thread = KeGetCurrentPrcb()->CurrentThread;

    /* Reading sscratch and dereferencing the PCR are separate instructions.
     * Keep a reschedule from migrating us between those two operations. */
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

    /* As above: a migration between the PCR lookup and the flag load would
     * report another processor's DPC state to an ordinary thread. */
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

#ifdef __cplusplus
extern "C" {
#endif
VOID NTAPI KiRiscvInitializePcr(PKPCR Pcr, PKTHREAD Thread, ULONG_PTR HartId,
                              ULONG Number, PVOID DpcStack, PVOID PanicStack);
NTHALAPI BOOLEAN NTAPI HalpRiscvQueryProcessorHartId(ULONG Number, PULONG_PTR HartId);
NTHALAPI VOID NTAPI HalpRiscvRemoteFence(KAFFINITY Targets, PVOID Address, SIZE_T Size, BOOLEAN Instruction);
VOID NTAPI KiIpiSendTbFlush(KAFFINITY Targets, PVOID Address, ULONG Pages);
VOID NTAPI KiIpiProcessRequests(VOID);
NTHALAPI NTSTATUS NTAPI HalAllocateAdapterChannel(PADAPTER_OBJECT AdapterObject, PWAIT_CONTEXT_BLOCK Wcb,
                                                  ULONG NumberOfMapRegisters, PDRIVER_CONTROL ExecutionRoutine);
BOOLEAN KiProcessorFreezeHandler(PKTRAP_FRAME TrapFrame, PKEXCEPTION_FRAME ExceptionFrame);
VOID NTAPI KiFreezeIfRequested(VOID);
#ifdef __cplusplus
}
#endif
