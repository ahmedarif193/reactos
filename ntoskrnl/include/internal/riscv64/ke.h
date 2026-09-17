/*
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 */

/* ReactOS RV64 kernel-private interfaces. */
#pragma once

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

/* Boot-hart processor identification (ABI-128), filled from the device tree
 * and the SBI base extension. KeFeatureBits stays zero (ABI-123). */
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
/* Clock ISR exported by hal.dll (hal.spec, -arch=riscv64): rearms the
 * supervisor timer deadline and calls KeUpdateSystemTime at CLOCK_LEVEL. */
NTHALAPI VOID NTAPI HalpRiscvClockInterrupt(_In_ PKTRAP_FRAME TrapFrame);
DECLSPEC_NORETURN VOID NTAPI KiRiscvUnimplemented(_In_ const CHAR *Routine);
PKTRAP_FRAME NTAPI KeGetTrapFrame(_In_ PKTHREAD Thread);
PKEXCEPTION_FRAME NTAPI KeGetExceptionFrame(_In_ PKTHREAD Thread);
ULONG_PTR NTAPI KeGetTrapFramePc(_In_ PKTRAP_FRAME TrapFrame);
BOOLEAN NTAPI KeGetTrapFrameInterruptState(_In_ PKTRAP_FRAME TrapFrame);
BOOLEAN NTAPI KiUserTrap(_In_ PKTRAP_FRAME TrapFrame);
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
NTSTATUS NTAPI KiRiscvContinue(PCONTEXT Context, BOOLEAN TestAlert);
DECLSPEC_NORETURN VOID NTAPI KiRiscvSystemService(PKTRAP_FRAME Frame);
#ifdef __cplusplus
}
#endif

FORCEINLINE
PKTHREAD
_KeGetCurrentThread(VOID)
{
    return KeGetCurrentPrcb()->CurrentThread;
}
