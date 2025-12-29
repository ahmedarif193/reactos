/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            ntoskrnl/arch/arm64/ke/trapdump.c
 * PURPOSE:         Rich diagnostics for early ARM64 exceptions
 */

#include <ntoskrnl.h>
#include <ntstrsafe.h>
#include <string.h>
#define NDEBUG
#include <debug.h>
#include <arm64trap.h>
#include <mm/ARM3/miarm.h>
#include <pseh/pseh2.h>

extern BOOLEAN KdDebuggerEnabled;
extern BOOLEAN KdDebuggerNotPresent;
#define KI_ARM64_MIN_KERNEL_ADDRESS 0xFFFF000000000000ULL

VOID KiArm64BootStageLog(_In_z_ PCSTR Stage);

#if defined(_M_ARM64) || defined(__aarch64__)
extern ULONG ExpPoolFlags;
extern POOL_DESCRIPTOR NonPagedPoolDescriptor;
extern PPOOL_DESCRIPTOR PoolVector[2];
/* Selected refptrs to validate at trap time (object manager globals) */
extern volatile ULONG * const ObpLUIDDeviceMapsEnabledPtr __asm__(".refptr.ObpLUIDDeviceMapsEnabled");
extern volatile PVOID  * const ObpNameBufferLookasideListPtr __asm__(".refptr.ObpNameBufferLookasideList");
extern volatile ULONG * const ObpObjectSecurityModePtr __asm__(".refptr.ObpObjectSecurityMode");
extern volatile ULONG * const ObpProtectionModePtr __asm__(".refptr.ObpProtectionMode");
volatile ULONG MiArm64LastFaultIrqlEntry;
volatile ULONG MiArm64LastFaultIrqlRaised;
volatile ULONG MiArm64LastFaultIrqlAfterDispatch;
volatile ULONG MiArm64LastFaultIrqlBeforeUnlock;
volatile ULONG MiArm64LastFaultIrqlAfterLower;
volatile ULONG MiArm64LastFaultStatus;
volatile LONG MiArm64LastSpecialApcDisableEntry;
volatile LONG MiArm64LastKernelApcDisableEntry;
volatile LONG MiArm64LastSpecialApcDisableBeforeUnlock;
volatile LONG MiArm64LastKernelApcDisableBeforeUnlock;
volatile ULONG MiArm64LastFaultPathFlags;
volatile ULONG MiArm64LastFaultLockIrql;
volatile LONG MiArm64LastSpecialApcDisableAfterUnlock;
volatile LONG MiArm64LastKernelApcDisableAfterUnlock;
volatile ULONG MiArm64LastFaultPfnOldIrql;
volatile ULONG MiArm64LastFaultPfnNewIrql;
volatile ULONG MiArm64LastFaultPfnReleaseIrql;
volatile ULONG MiArm64LastFaultPfnAfterReleaseIrql;
volatile ULONG_PTR MiArm64LastFaultPfnThread;
volatile ULONG_PTR MiArm64LastFaultPfnCaller;
volatile ULONG MiArm64LastGuardedLeaveIrql;
volatile ULONG_PTR MiArm64LastGuardedLeaveThread;
volatile LONG MiArm64LastGuardedLeaveSpecial;
volatile LONG MiArm64LastGuardedLeaveKernel;
volatile ULONG MiArm64LastGuardedAssertFlags;
volatile ULONG MiArm64LastGuardedAssertIrql;
volatile ULONG_PTR MiArm64LastGuardedAssertThread;
volatile ULONG_PTR MiArm64LastGuardedAssertCaller;
volatile LONG MiArm64LastGuardedAssertSpecial;
volatile LONG MiArm64LastGuardedAssertKernel;
volatile ULONG MiArm64LastMdlFreeFlags;
volatile ULONG MiArm64LastMdlFreeIrql;
volatile LONG MiArm64LastMdlFreeSpecial;
volatile LONG MiArm64LastMdlFreeKernel;
volatile ULONG_PTR MiArm64LastMdlFreeThread;
volatile ULONG_PTR MiArm64LastMdlFreeEThread;
volatile ULONG_PTR MiArm64LastMdlFreeCaller;
volatile ULONG_PTR MiArm64LastMdlFreeCaller2;
volatile ULONG_PTR MiArm64LastMdlFreeMdl;
volatile ULONG MiArm64LastIrqlRaiseFrom;
volatile ULONG MiArm64LastIrqlRaiseTo;
volatile ULONG MiArm64LastIrqlLowerFrom;
volatile ULONG MiArm64LastIrqlLowerTo;
volatile ULONG_PTR MiArm64LastIrqlRaiseCaller;
volatile ULONG_PTR MiArm64LastIrqlLowerCaller;
volatile ULONG_PTR MiArm64LastIrqlRaiseThread;
volatile ULONG_PTR MiArm64LastIrqlLowerThread;
volatile LONG MiArm64IrqlTraceBudget = 64;
volatile LONG MiArm64TrapTraceIndex;
volatile ULONG64 MiArm64TrapTraceElr[4];
volatile ULONG64 MiArm64TrapTraceFar[4];
volatile ULONG64 MiArm64TrapTraceEsr[4];
volatile ULONG64 MiArm64TrapTraceSpsr[4];
volatile ULONG64 MiArm64TrapTraceVector[4];
volatile ULONG64 MiArm64TrapTraceX16[4];
volatile ULONG64 MiArm64TrapTraceX17[4];
volatile ULONG64 MiArm64TrapTraceX0[4];
volatile ULONG64 MiArm64TrapTraceX1[4];
volatile ULONG64 MiArm64TrapTraceX8[4];
volatile ULONG64 MiArm64TrapTraceX9[4];
volatile ULONG64 MiArm64TrapTraceX20[4];
volatile ULONG64 MiArm64TrapTraceX21[4];
/* Pointers to our own guarded-leave diagnostics via .refptr */
extern volatile ULONG * const MiArm64LastGuardedLeaveIrqlPtr __asm__(".refptr.MiArm64LastGuardedLeaveIrql");
extern volatile ULONG_PTR * const MiArm64LastGuardedLeaveThreadPtr __asm__(".refptr.MiArm64LastGuardedLeaveThread");
extern volatile LONG * const MiArm64LastGuardedLeaveSpecialPtr __asm__(".refptr.MiArm64LastGuardedLeaveSpecial");
extern volatile LONG * const MiArm64LastGuardedLeaveKernelPtr __asm__(".refptr.MiArm64LastGuardedLeaveKernel");
volatile PVOID MiArm64RefptrGuardBase;
volatile SIZE_T MiArm64RefptrGuardSize;

/* Define .refptr entries for our own guarded-leave diagnostics so the
 * linker finds them on ARM64. These are read-only pointers whose symbol
 * names are .refptr.<name>, pointing at the actual globals above. */
__attribute__((used, section(".rdata$.refptr.MiArm64LastGuardedLeaveIrql")))
volatile ULONG * const __MiArm64LastGuardedLeaveIrqlRefptr __asm__(".refptr.MiArm64LastGuardedLeaveIrql") = &MiArm64LastGuardedLeaveIrql;

__attribute__((used, section(".rdata$.refptr.MiArm64LastGuardedLeaveThread")))
volatile ULONG_PTR * const __MiArm64LastGuardedLeaveThreadRefptr __asm__(".refptr.MiArm64LastGuardedLeaveThread") = &MiArm64LastGuardedLeaveThread;

__attribute__((used, section(".rdata$.refptr.MiArm64LastGuardedLeaveSpecial")))
volatile LONG * const __MiArm64LastGuardedLeaveSpecialRefptr __asm__(".refptr.MiArm64LastGuardedLeaveSpecial") = &MiArm64LastGuardedLeaveSpecial;

__attribute__((used, section(".rdata$.refptr.MiArm64LastGuardedLeaveKernel")))
volatile LONG * const __MiArm64LastGuardedLeaveKernelRefptr __asm__(".refptr.MiArm64LastGuardedLeaveKernel") = &MiArm64LastGuardedLeaveKernel;

/* Define .refptr entries for selected object manager globals so the
 * linker finds them on ARM64 when referenced via the Obp*Ptr aliases.
 * Clang already materializes these symbols for the asm-alias externs,
 * so we only emit the backing pointers for GCC/MinGW. */
#if defined(__GNUC__) && !defined(__clang__)
__attribute__((used, section(".rdata$.refptr.ObpLUIDDeviceMapsEnabled")))
volatile ULONG * const __ObpLUIDDeviceMapsEnabledRefptr __asm__(".refptr.ObpLUIDDeviceMapsEnabled") = &ObpLUIDDeviceMapsEnabled;

__attribute__((used, section(".rdata$.refptr.ObpNameBufferLookasideList")))
volatile PVOID * const __ObpNameBufferLookasideListRefptr __asm__(".refptr.ObpNameBufferLookasideList") = (PVOID *)&ObpNameBufferLookasideList;

__attribute__((used, section(".rdata$.refptr.ObpObjectSecurityMode")))
volatile ULONG * const __ObpObjectSecurityModeRefptr __asm__(".refptr.ObpObjectSecurityMode") = &ObpObjectSecurityMode;

__attribute__((used, section(".rdata$.refptr.ObpProtectionMode")))
volatile ULONG * const __ObpProtectionModeRefptr __asm__(".refptr.ObpProtectionMode") = &ObpProtectionMode;
#endif
#endif


static const PCSTR KiArm64VectorNames[16] =
{
    [0]  = "Sync SP0",
    [1]  = "IRQ SP0",
    [2]  = "FIQ SP0",
    [3]  = "SError SP0",
    [4]  = "Sync SPx",
    [5]  = "IRQ SPx",
    [6]  = "FIQ SPx",
    [7]  = "SError SPx",
    [8]  = "Sync lower A64",
    [9]  = "IRQ lower A64",
    [10] = "FIQ lower A64",
    [11] = "SError lower A64",
    [12] = "Sync lower A32",
    [13] = "IRQ lower A32",
    [14] = "FIQ lower A32",
    [15] = "SError lower A32",
};

static const PCSTR KiArm64EsrClassNames[64] =
{
    [0x00] = "Unknown",
    [0x01] = "WFI/WFE trap",
    [0x03] = "CP15 RT trap",
    [0x04] = "CP15 R trap",
    [0x05] = "CP15 W trap",
    [0x07] = "FP/SIMD access",
    [0x08] = "MCRR/MRRC trap",
    [0x0C] = "SVE access",
    [0x11] = "SVC in AArch32",
    [0x12] = "HVC in AArch32",
    [0x13] = "SMC in AArch32",
    [0x15] = "SVC in AArch64",
    [0x16] = "HVC in AArch64",
    [0x17] = "SMC in AArch64",
    [0x18] = "MSR/MRS trap",
    [0x1C] = "Instruction abort (lower EL)",
    [0x1D] = "Instruction abort (same EL)",
    [0x20] = "Data abort (lower EL)",
    [0x21] = "Data abort (same EL)",
    [0x22] = "SP alignment fault",
    [0x24] = "FP exception",
    [0x26] = "FP exception (AArch64)",
    [0x2F] = "SError interrupt",
};

static PCSTR
KiArm64DescribeEsr(_In_ UINT64 ExceptionSyndrome)
{
    ULONG Class = (ULONG)((ExceptionSyndrome >> 26) & 0x3FULL);

    if (Class < RTL_NUMBER_OF(KiArm64EsrClassNames) &&
        KiArm64EsrClassNames[Class] != NULL)
    {
        return KiArm64EsrClassNames[Class];
    }

    return "Unknown";
}

static VOID
KiArm64EmitLine(_In_opt_ PARM64_EARLY_LOG_SINK Sink,
                _In_z_ PCSTR Text)
{
    SIZE_T Length;

    if (!Text || !Sink)
        return;

    Length = strlen(Text);
    if (Length != 0)
    {
        Sink(Text, Length);
    }

    Sink("\n", 1);
}

static VOID
KiArm64DumpRegisterState(_In_ const ARM64_EARLY_TRAP_STATE *State,
                         _In_opt_ PARM64_EARLY_LOG_SINK Sink)
{
    CHAR Buffer[256];
    ULONG Base;

    for (Base = 0; Base < 28; Base += 4)
    {
        RtlStringCbPrintfA(Buffer,
                           sizeof(Buffer),
                           "  x%02u=0x%016llx x%02u=0x%016llx x%02u=0x%016llx x%02u=0x%016llx",
                           Base, State->Registers.X[Base],
                           Base + 1, State->Registers.X[Base + 1],
                           Base + 2, State->Registers.X[Base + 2],
                           Base + 3, State->Registers.X[Base + 3]);
        KiArm64EmitLine(Sink, Buffer);
    }

    RtlStringCbPrintfA(Buffer,
                       sizeof(Buffer),
                       "  x28=0x%016llx x29=0x%016llx x30=0x%016llx",
                       State->Registers.X[28],
                       State->Registers.X[29],
                       State->Registers.X[30]);
    KiArm64EmitLine(Sink, Buffer);

    RtlStringCbPrintfA(Buffer,
                       sizeof(Buffer),
                       "  sp =0x%016llx pc =0x%016llx pstate=0x%016llx",
                       State->Registers.Sp,
                       State->Registers.Pc,
                       State->Registers.Pstate);
    KiArm64EmitLine(Sink, Buffer);
}

static BOOLEAN
KiArm64DumpStackSnapshot(_In_ const ARM64_EARLY_TRAP_STATE *State,
                         _In_opt_ PARM64_EARLY_LOG_SINK Sink)
{
    UNREFERENCED_PARAMETER(State);
    UNREFERENCED_PARAMETER(Sink);
    return FALSE;
}

static BOOLEAN
KiArm64DumpBacktrace(_In_ const ARM64_EARLY_TRAP_STATE *State,
                     _In_opt_ PARM64_EARLY_LOG_SINK Sink)
{
    PVOID Frames[16] = {0};
    ULONG Captured = 0;
    CHAR Buffer[64];
    ULONG64 StackPointer;
    KIRQL CurrentIrql;
    BOOLEAN Success = FALSE;
#define ARM64_TRAPDBG_PREFIX "[arm64] TrapDiagDbg: "

    if (Sink == NULL || State == NULL)
    {
        return FALSE;
    }

    StackPointer = State->Registers.Sp;
    CurrentIrql = KeGetCurrentIrql();

    if (CurrentIrql > APC_LEVEL)
    {
        DbgPrintEx(DPFLTR_DEFAULT_ID,
                   DPFLTR_TRACE_LEVEL,
                   ARM64_TRAPDBG_PREFIX "backtrace skipped (irql=%lu)\n",
                   CurrentIrql);
        return FALSE;
    }

    if (StackPointer < KI_ARM64_MIN_KERNEL_ADDRESS)
    {
        DbgPrintEx(DPFLTR_DEFAULT_ID,
                   DPFLTR_TRACE_LEVEL,
                   ARM64_TRAPDBG_PREFIX "backtrace skipped (sp=%p not kernel)\n",
                   (PVOID)StackPointer);
        return FALSE;
    }

    __try
    {
        Captured = RtlWalkFrameChain(Frames, RTL_NUMBER_OF(Frames), 0);
    }
    __except(EXCEPTION_EXECUTE_HANDLER)
    {
        NTSTATUS ExceptionCode = GetExceptionCode();
        DbgPrintEx(DPFLTR_DEFAULT_ID,
                   DPFLTR_ERROR_LEVEL,
                   ARM64_TRAPDBG_PREFIX "backtrace aborted (exception %lx)\n",
                   ExceptionCode);
        Captured = 0;
    }
    _SEH2_END;

    if (Captured == 0)
    {
        DbgPrintEx(DPFLTR_DEFAULT_ID,
                   DPFLTR_TRACE_LEVEL,
                   ARM64_TRAPDBG_PREFIX "backtrace unavailable (walk returned 0)\n");
        return FALSE;
    }

    KiArm64EmitLine(Sink, "[arm64] TrapDiag: call stack");

    for (ULONG Index = 0; Index < Captured && Index < RTL_NUMBER_OF(Frames); Index++)
    {
        if (NT_SUCCESS(RtlStringCbPrintfA(Buffer,
                                          sizeof(Buffer),
                                          "    #%02lu %p",
                                          (unsigned long)Index,
                                          Frames[Index])))
        {
            KiArm64EmitLine(Sink, Buffer);
        }
    }

    Success = TRUE;
    return Success;
#undef ARM64_TRAPDBG_PREFIX
}

VOID
KiArm64DumpEarlyTrapState(_In_ const ARM64_EARLY_TRAP_STATE *State,
                          _In_opt_ PARM64_EARLY_LOG_SINK Sink)
{
    CHAR Buffer[256];
    PCSTR VectorName;
    PCSTR EsrDesc;
    ULONG Iss;

    if (!State)
        return;

    VectorName = (State->VectorId < RTL_NUMBER_OF(KiArm64VectorNames) &&
                  KiArm64VectorNames[State->VectorId])
                     ? KiArm64VectorNames[State->VectorId]
                     : "Unknown";

    EsrDesc = KiArm64DescribeEsr(State->ExceptionSyndrome);
    Iss = (ULONG)(State->ExceptionSyndrome & 0x1FFFFFFULL);

    RtlStringCbPrintfA(Buffer,
                       sizeof(Buffer),
                       "Exception caught at elr=0x%016llx (vector %llu %s) esr=0x%016llx (%s) iss=0x%08lx far=0x%016llx spsr=0x%016llx",
                       State->Elr,
                       State->VectorId,
                       VectorName,
                       State->ExceptionSyndrome,
                       EsrDesc,
                       Iss,
                       State->FaultAddress,
                       State->Spsr);
    KiArm64EmitLine(Sink, Buffer);

#if defined(_M_ARM64) || defined(__aarch64__)
    {
        LONG traceSlot = InterlockedIncrement(&MiArm64TrapTraceIndex);
        ULONG slot = (ULONG)traceSlot & 3u;

        MiArm64TrapTraceVector[slot] = State->VectorId;
        MiArm64TrapTraceElr[slot] = State->Elr;
        MiArm64TrapTraceFar[slot] = State->FaultAddress;
        MiArm64TrapTraceEsr[slot] = State->ExceptionSyndrome;
        MiArm64TrapTraceSpsr[slot] = State->Spsr;
        MiArm64TrapTraceX16[slot] = State->Registers.X[16];
        MiArm64TrapTraceX17[slot] = State->Registers.X[17];
        MiArm64TrapTraceX0[slot] = State->Registers.X[0];
        MiArm64TrapTraceX1[slot] = State->Registers.X[1];
        MiArm64TrapTraceX8[slot] = State->Registers.X[8];
        MiArm64TrapTraceX9[slot] = State->Registers.X[9];
        MiArm64TrapTraceX20[slot] = State->Registers.X[20];
        MiArm64TrapTraceX21[slot] = State->Registers.X[21];

        if (State->Elr == State->FaultAddress)
        {
            LONG kernBeforeVal = 0x7fffffff;
            LONG kernAfterVal = 0x7fffffff;
            LONG specBeforeVal = 0x7fffffff;
            LONG specAfterVal = 0x7fffffff;

            if (((ULONG_PTR)&MiArm64LastKernelApcDisableBeforeUnlock) >= ARM64_KSEG0_BASE)
            {
                kernBeforeVal = MiArm64LastKernelApcDisableBeforeUnlock;
            }
            if (((ULONG_PTR)&MiArm64LastKernelApcDisableAfterUnlock) >= ARM64_KSEG0_BASE)
            {
                kernAfterVal = MiArm64LastKernelApcDisableAfterUnlock;
            }
            if (((ULONG_PTR)&MiArm64LastSpecialApcDisableBeforeUnlock) >= ARM64_KSEG0_BASE)
            {
                specBeforeVal = MiArm64LastSpecialApcDisableBeforeUnlock;
            }
            if (((ULONG_PTR)&MiArm64LastSpecialApcDisableAfterUnlock) >= ARM64_KSEG0_BASE)
            {
                specAfterVal = MiArm64LastSpecialApcDisableAfterUnlock;
            }

            ULONG cmpShareValue = 0xFFFFFFFF;
            ULONG exCritValue = 0xFFFFFFFF;
            if (((ULONG_PTR)&CmpShareSystemHives) >= ARM64_KSEG0_BASE)
            {
                cmpShareValue = CmpShareSystemHives;
            }
            if (((ULONG_PTR)&ExCriticalWorkerThreads) >= ARM64_KSEG0_BASE)
            {
                exCritValue = ExCriticalWorkerThreads;
            }

            if (NT_SUCCESS(RtlStringCbPrintfA(Buffer,
                                              sizeof(Buffer),
                                              "[arm64] TrapDiag: elr==far=%p esr=0x%llx x20=%p x21=%p x9=%p x8=%p x16=%p x17=%p x0=%p x1=%p",
                                              (PVOID)State->Elr,
                                              (unsigned long long)State->ExceptionSyndrome,
                                              (PVOID)State->Registers.X[20],
                                              (PVOID)State->Registers.X[21],
                                              (PVOID)State->Registers.X[9],
                                              (PVOID)State->Registers.X[8],
                                              (PVOID)State->Registers.X[16],
                                              (PVOID)State->Registers.X[17],
                                              (PVOID)State->Registers.X[0],
                                              (PVOID)State->Registers.X[1])))
            {
                KiArm64EmitLine(Sink, Buffer);
            }

            /* Also print PTEs for x8/x9 if they look like kernel VAs */
            {
                ULONG_PTR a8 = (ULONG_PTR)State->Registers.X[8];
                ULONG_PTR a9 = (ULONG_PTR)State->Registers.X[9];
                if ((a8 >= ARM64_KSEG0_BASE) || (a9 >= ARM64_KSEG0_BASE))
                {
                    MMPTE p8 = {0}, p9 = {0};
                    PMMPTE pte8 = NULL, pte9 = NULL;
                    if (a8 >= ARM64_KSEG0_BASE)
                    {
                        pte8 = MiAddressToPte((PVOID)a8);
                        if (pte8) p8 = *pte8;
                    }
                    if (a9 >= ARM64_KSEG0_BASE)
                    {
                        pte9 = MiAddressToPte((PVOID)a9);
                        if (pte9) p9 = *pte9;
                    }
                    if (NT_SUCCESS(RtlStringCbPrintfA(Buffer,
                                                      sizeof(Buffer),
                                                      "[arm64] TrapDiag: x8pte=%p(0x%llx) x9pte=%p(0x%llx)",
                                                      pte8,
                                                      (unsigned long long)p8.u.Long,
                                                      pte9,
                                                      (unsigned long long)p9.u.Long)))
                    {
                        KiArm64EmitLine(Sink, Buffer);
                    }
                }
            }

            if (NT_SUCCESS(RtlStringCbPrintfA(Buffer,
                                              sizeof(Buffer),
                                              "[arm64] TrapDiag: refptr kernBefore=%p(%ld) kernAfter=%p(%ld) specBefore=%p(%ld) specAfter=%p(%ld)",
                                              &MiArm64LastKernelApcDisableBeforeUnlock,
                                              (LONG)kernBeforeVal,
                                              &MiArm64LastKernelApcDisableAfterUnlock,
                                              (LONG)kernAfterVal,
                                              &MiArm64LastSpecialApcDisableBeforeUnlock,
                                              (LONG)specBeforeVal,
                                              &MiArm64LastSpecialApcDisableAfterUnlock,
                                              (LONG)specAfterVal)))
            {
                KiArm64EmitLine(Sink, Buffer);
            }

            /* Decode ESR ISS basics for data aborts */
            {
                ULONGLONG esr = State->ExceptionSyndrome;
                ULONG iss = (ULONG)(esr & 0x01FFFFFFULL);
                ULONG dfsc = iss & 0x3F;
                ULONG wnr = (iss >> 6) & 1u;
                ULONG fnv = (iss >> 10) & 1u;
                if (NT_SUCCESS(RtlStringCbPrintfA(Buffer,
                                                  sizeof(Buffer),
                                                  "[arm64] TrapDiag: ESR decode dfsc=0x%lx wnr=%lu fnv=%lu",
                                                  (unsigned long)dfsc,
                                                  (unsigned long)wnr,
                                                  (unsigned long)fnv)))
                {
                    KiArm64EmitLine(Sink, Buffer);
                }
            }

            if (NT_SUCCESS(RtlStringCbPrintfA(Buffer,
                                              sizeof(Buffer),
                                              "[arm64] TrapDiag: CmpShareSystemHives slot=%p refptr=%p value=0x%lx",
                                              &CmpShareSystemHives,
                                              &CmpShareSystemHives,
                                              (unsigned long)cmpShareValue)))
            {
                KiArm64EmitLine(Sink, Buffer);
            }

            /* Dump a few Obp-related refptrs to catch bad relocation/data */
            {
                ULONG obLuidVal = 0xFFFFFFFF;
                ULONG obSecModeVal = 0xFFFFFFFF;
                ULONG obProtModeVal = 0xFFFFFFFF;
                PVOID obNameBufPtr = NULL;

                if (((ULONG_PTR)ObpLUIDDeviceMapsEnabledPtr) >= ARM64_KSEG0_BASE)
                {
                    obLuidVal = *ObpLUIDDeviceMapsEnabledPtr;
                }
                if (((ULONG_PTR)ObpObjectSecurityModePtr) >= ARM64_KSEG0_BASE)
                {
                    obSecModeVal = *ObpObjectSecurityModePtr;
                }
                if (((ULONG_PTR)ObpProtectionModePtr) >= ARM64_KSEG0_BASE)
                {
                    obProtModeVal = *ObpProtectionModePtr;
                }
                if (((ULONG_PTR)ObpNameBufferLookasideListPtr) >= ARM64_KSEG0_BASE)
                {
                    obNameBufPtr = *ObpNameBufferLookasideListPtr;
                }

                if (NT_SUCCESS(RtlStringCbPrintfA(Buffer,
                                                  sizeof(Buffer),
                                                  "[arm64] TrapDiag: Obp refptr LuidMaps=%p(%lx) NameList=%p(%p) SecMode=%p(%lx) ProtMode=%p(%lx)",
                                                  ObpLUIDDeviceMapsEnabledPtr,
                                                  (unsigned long)obLuidVal,
                                                  ObpNameBufferLookasideListPtr,
                                                  obNameBufPtr,
                                                  ObpObjectSecurityModePtr,
                                                  (unsigned long)obSecModeVal,
                                                  ObpProtectionModePtr,
                                                  (unsigned long)obProtModeVal)))
                {
                    KiArm64EmitLine(Sink, Buffer);
                }
            }

            if (NT_SUCCESS(RtlStringCbPrintfA(Buffer,
                                              sizeof(Buffer),
                                              "[arm64] TrapDiag: ExCriticalWorkerThreads slot=%p refptr=%p value=0x%lx",
                                              &ExCriticalWorkerThreads,
                                              &ExCriticalWorkerThreads,
                                              (unsigned long)exCritValue)))
            {
                KiArm64EmitLine(Sink, Buffer);
            }

            /* Dump the guarded-leave refptr slots and current values */
            {
                ULONG glIrqlVal = 0xFFFFFFFF;
                ULONG_PTR glThreadVal = 0ULL;
                LONG glSpecVal = 0x7FFFFFFF;
                LONG glKernVal = 0x7FFFFFFF;

                if (((ULONG_PTR)MiArm64LastGuardedLeaveIrqlPtr) >= ARM64_KSEG0_BASE)
                {
                    glIrqlVal = *MiArm64LastGuardedLeaveIrqlPtr;
                }
                if (((ULONG_PTR)MiArm64LastGuardedLeaveThreadPtr) >= ARM64_KSEG0_BASE)
                {
                    glThreadVal = *MiArm64LastGuardedLeaveThreadPtr;
                }
                if (((ULONG_PTR)MiArm64LastGuardedLeaveSpecialPtr) >= ARM64_KSEG0_BASE)
                {
                    glSpecVal = *MiArm64LastGuardedLeaveSpecialPtr;
                }
                if (((ULONG_PTR)MiArm64LastGuardedLeaveKernelPtr) >= ARM64_KSEG0_BASE)
                {
                    glKernVal = *MiArm64LastGuardedLeaveKernelPtr;
                }

                if (NT_SUCCESS(RtlStringCbPrintfA(Buffer,
                                                  sizeof(Buffer),
                                                  "[arm64] TrapDiag: GuardedLeave refptr Irql=%p(%lu) Thread=%p(%p) Spec=%p(%ld) Kern=%p(%ld)",
                                                  MiArm64LastGuardedLeaveIrqlPtr,
                                                  (unsigned long)glIrqlVal,
                                                  MiArm64LastGuardedLeaveThreadPtr,
                                                  (PVOID)glThreadVal,
                                                  MiArm64LastGuardedLeaveSpecialPtr,
                                                  (long)glSpecVal,
                                                  MiArm64LastGuardedLeaveKernelPtr,
                                                  (long)glKernVal)))
                {
                    KiArm64EmitLine(Sink, Buffer);
                }
            }

            if (MiArm64RefptrGuardBase && MiArm64RefptrGuardSize)
            {
                if (NT_SUCCESS(RtlStringCbPrintfA(Buffer,
                                                  sizeof(Buffer),
                                                  "[arm64] TrapDiag: refptr guard base=%p size=0x%Ix",
                                                  MiArm64RefptrGuardBase,
                                                  (SIZE_T)MiArm64RefptrGuardSize)))
                {
                    KiArm64EmitLine(Sink, Buffer);
                }

                ULONG_PTR guardBase = (ULONG_PTR)MiArm64RefptrGuardBase;
                ULONG_PTR guardLimit = guardBase + (ULONG_PTR)MiArm64RefptrGuardSize;
                if ((State->FaultAddress >= guardBase) &&
                    (State->FaultAddress < guardLimit))
                {
                    if (NT_SUCCESS(RtlStringCbPrintfA(Buffer,
                                                      sizeof(Buffer),
                                                      "[arm64] TrapDiag: fault hit refptr guard (slot write)")))
                    {
                        KiArm64EmitLine(Sink, Buffer);
            }
        }
    }

}
    }

    {
        CHAR PoolLine[256];
        PPOOL_DESCRIPTOR NonPagedDesc = &NonPagedPoolDescriptor;
        PPOOL_DESCRIPTOR VectorDesc = (PoolVector[NonPagedPool] ? PoolVector[NonPagedPool] : NULL);

        if (NT_SUCCESS(RtlStringCbPrintfA(PoolLine,
                                          sizeof(PoolLine),
                                          "[arm64] TrapDiag: ExpPoolFlags=0x%lx vectorDesc=%p npDesc=%p allocs=%lu frees=%lu pages=%lu big=%lu flagsField=0x%lx",
                                          ExpPoolFlags,
                                          VectorDesc,
                                          NonPagedDesc,
                                          (ULONG)NonPagedDesc->RunningAllocs,
                                          (ULONG)NonPagedDesc->RunningDeAllocs,
                                          (ULONG)NonPagedDesc->TotalPages,
                                          (ULONG)NonPagedDesc->TotalBigPages,
                                          (ULONG)NonPagedDesc->PoolType)))
        {
            KiArm64EmitLine(Sink, PoolLine);
        }

        {
            UINT64 DescriptorPtr = State->Registers.X[23];
            if (DescriptorPtr)
            {
                ULONG DescriptorFlags = *(volatile ULONG *)(DescriptorPtr + 0x3EC);

                if (NT_SUCCESS(RtlStringCbPrintfA(PoolLine,
                                                  sizeof(PoolLine),
                                                  "[arm64] TrapDiag: descriptor=%p flags@3EC=0x%08lx x24=0x%llx",
                                                  (PVOID)DescriptorPtr,
                                                  DescriptorFlags,
                                                  State->Registers.X[24])))
                {
                    KiArm64EmitLine(Sink, PoolLine);
                }
            }
        }

        {
            UINT64 EntryPtr = State->Registers.X[26];
            if (EntryPtr)
            {
                ULONG HeaderInfo = *(volatile ULONG *)(EntryPtr + 0x18);

                if (NT_SUCCESS(RtlStringCbPrintfA(PoolLine,
                                                  sizeof(PoolLine),
                                                  "[arm64] TrapDiag: entry=%p header+0x18=0x%08lx",
                                                  (PVOID)EntryPtr,
                                                  HeaderInfo)))
                {
                    KiArm64EmitLine(Sink, PoolLine);
                }

                PPOOL_HEADER Header = (PPOOL_HEADER)EntryPtr;
                UCHAR BlockSize = Header->BlockSize;
                UCHAR PoolType = Header->PoolType;
                ULONG PoolTag = Header->PoolTag;

                if (NT_SUCCESS(RtlStringCbPrintfA(PoolLine,
                                                  sizeof(PoolLine),
                                                  "[arm64] TrapDiag: block=%u type=%u tag=%08lx prev=%u",
                                                  BlockSize,
                                                  PoolType,
                                                  PoolTag,
                                                  Header->PreviousSize)))
                {
                    KiArm64EmitLine(Sink, PoolLine);
                }

                ULONG BlockIndex = BlockSize;
                if (BlockIndex > 0 && BlockIndex <= POOL_LISTS_PER_PAGE)
                {
                    PPOOL_DESCRIPTOR Desc = (PPOOL_DESCRIPTOR)State->Registers.X[23];
                    if (Desc)
                    {
                        PLIST_ENTRY ListHead = &Desc->ListHeads[BlockIndex - 1];
                        ULONG64 Flink = *(volatile ULONG64 *)&ListHead->Flink;
                        ULONG64 Blink = *(volatile ULONG64 *)&ListHead->Blink;

                        if (NT_SUCCESS(RtlStringCbPrintfA(PoolLine,
                                                          sizeof(PoolLine),
                                                          "[arm64] TrapDiag: listHead=%p Flink=0x%llx Blink=0x%llx",
                                                          (PVOID)ListHead,
                                                          Flink,
                                                          Blink)))
                        {
                            KiArm64EmitLine(Sink, PoolLine);
                        }
                    }
                }
            }
        }

        if (NT_SUCCESS(RtlStringCbPrintfA(PoolLine,
                                          sizeof(PoolLine),
                                          "[arm64] TrapDiag: faultIRQL entry=%lu raised=%lu dispatch=%lu preUnlock=%lu post=%lu lockOld=%lu status=0x%lx flags=0x%lx apc(entry)=%ld/%ld apc(pre)=%ld/%ld apc(post)=%ld/%ld",
                                          MiArm64LastFaultIrqlEntry,
                                          MiArm64LastFaultIrqlRaised,
                                          MiArm64LastFaultIrqlAfterDispatch,
                                          MiArm64LastFaultIrqlBeforeUnlock,
                                          MiArm64LastFaultIrqlAfterLower,
                                          MiArm64LastFaultLockIrql,
                                          (ULONG)MiArm64LastFaultStatus,
                                          MiArm64LastFaultPathFlags,
                                          MiArm64LastSpecialApcDisableEntry,
                                          MiArm64LastKernelApcDisableEntry,
                                          MiArm64LastSpecialApcDisableBeforeUnlock,
                                          MiArm64LastKernelApcDisableBeforeUnlock,
                                          MiArm64LastSpecialApcDisableAfterUnlock,
                                          MiArm64LastKernelApcDisableAfterUnlock)))
        {
            KiArm64EmitLine(Sink, PoolLine);
        }

        if (NT_SUCCESS(RtlStringCbPrintfA(PoolLine,
                                          sizeof(PoolLine),
                                          "[arm64] TrapDiag: pfnLock old=%lu held=%lu release=%lu after=%lu thread=%p caller=%p",
                                          MiArm64LastFaultPfnOldIrql,
                                          MiArm64LastFaultPfnNewIrql,
                                          MiArm64LastFaultPfnReleaseIrql,
                                          MiArm64LastFaultPfnAfterReleaseIrql,
                                          (PVOID)MiArm64LastFaultPfnThread,
                                          (PVOID)MiArm64LastFaultPfnCaller)))
        {
            KiArm64EmitLine(Sink, PoolLine);
        }

        if (NT_SUCCESS(RtlStringCbPrintfA(PoolLine,
                                          sizeof(PoolLine),
                                          "[arm64] TrapDiag: guarded leave IRQL=%lu special=%ld kernel=%ld thread=%p",
                                          MiArm64LastGuardedLeaveIrql,
                                          MiArm64LastGuardedLeaveSpecial,
                                          MiArm64LastGuardedLeaveKernel,
                                          (PVOID)MiArm64LastGuardedLeaveThread)))
        {
            KiArm64EmitLine(Sink, PoolLine);
        }

        if (MiArm64LastGuardedAssertFlags != 0)
        {
            if (NT_SUCCESS(RtlStringCbPrintfA(PoolLine,
                                              sizeof(PoolLine),
                                              "[arm64] TrapDiag: guard assert flags=0x%lx irql=%lu special=%ld kernel=%ld thread=%p caller=%p",
                                              MiArm64LastGuardedAssertFlags,
                                              MiArm64LastGuardedAssertIrql,
                                              MiArm64LastGuardedAssertSpecial,
                                              MiArm64LastGuardedAssertKernel,
                                              (PVOID)MiArm64LastGuardedAssertThread,
                                              (PVOID)MiArm64LastGuardedAssertCaller)))
            {
                KiArm64EmitLine(Sink, PoolLine);
            }
        }

        if (NT_SUCCESS(RtlStringCbPrintfA(PoolLine,
                                          sizeof(PoolLine),
                                          "[arm64] TrapDiag: mdlFree IRQL=%lu special=%ld kernel=%ld flags=0x%lx thread=%p ethread=%p caller=%p caller2=%p mdl=%p",
                                          MiArm64LastMdlFreeIrql,
                                          MiArm64LastMdlFreeSpecial,
                                          MiArm64LastMdlFreeKernel,
                                          MiArm64LastMdlFreeFlags,
                                          (PVOID)MiArm64LastMdlFreeThread,
                                          (PVOID)MiArm64LastMdlFreeEThread,
                                          (PVOID)MiArm64LastMdlFreeCaller,
                                          (PVOID)MiArm64LastMdlFreeCaller2,
                                          (PVOID)MiArm64LastMdlFreeMdl)))
        {
            KiArm64EmitLine(Sink, PoolLine);
        }

        if ((MiArm64LastIrqlRaiseFrom != 0) ||
            (MiArm64LastIrqlRaiseTo != 0) ||
            (MiArm64LastIrqlRaiseCaller != 0) ||
            (MiArm64LastIrqlRaiseThread != 0))
        {
            if (NT_SUCCESS(RtlStringCbPrintfA(PoolLine,
                                              sizeof(PoolLine),
                                              "[arm64] TrapDiag: irql raise %lu->%lu thread=%p caller=%p",
                                              MiArm64LastIrqlRaiseFrom,
                                              MiArm64LastIrqlRaiseTo,
                                              (PVOID)MiArm64LastIrqlRaiseThread,
                                              (PVOID)MiArm64LastIrqlRaiseCaller)))
            {
                KiArm64EmitLine(Sink, PoolLine);
            }
        }

        if ((MiArm64LastIrqlLowerFrom != 0) ||
            (MiArm64LastIrqlLowerTo != 0) ||
            (MiArm64LastIrqlLowerCaller != 0) ||
            (MiArm64LastIrqlLowerThread != 0))
        {
            if (NT_SUCCESS(RtlStringCbPrintfA(PoolLine,
                                              sizeof(PoolLine),
                                              "[arm64] TrapDiag: irql lower %lu->%lu thread=%p caller=%p",
                                              MiArm64LastIrqlLowerFrom,
                                              MiArm64LastIrqlLowerTo,
                                              (PVOID)MiArm64LastIrqlLowerThread,
                                              (PVOID)MiArm64LastIrqlLowerCaller)))
            {
                KiArm64EmitLine(Sink, PoolLine);
            }
        }
    }
#endif

    KiArm64EmitLine(Sink, "Registers:");
    KiArm64DumpRegisterState(State, Sink);

    if (KiArm64DumpStackSnapshot(State, Sink))
    {
        /* Stack snapshot printed by helper */
    }

    if (KiArm64DumpBacktrace(State, Sink))
    {
        /* Backtrace printed by helper */
    }
}

static
VOID
KiArm64DbgLogSink(_In_reads_bytes_(Length) const CHAR *Text,
                  _In_ SIZE_T Length)
{
    CHAR Scratch[128];

    if (!Text || Length == 0)
    {
        return;
    }

    while (Length > 0)
    {
        SIZE_T Chunk = min(Length, sizeof(Scratch) - 1);
        RtlCopyMemory(Scratch, Text, Chunk);
        Scratch[Chunk] = '\0';
        DbgPrintEx(DPFLTR_DEFAULT_ID, DPFLTR_ERROR_LEVEL, "%s", Scratch);
        Text += Chunk;
        Length -= Chunk;
    }
}

#if defined(_M_ARM64) || defined(__aarch64__)
static volatile LONG KiArm64StageLogDepth;

static
VOID
KiArm64StageLogSink(_In_reads_bytes_(Length) const CHAR *Text,
                    _In_ SIZE_T Length)
{
    CHAR Scratch[160];
    LONG Depth;

    if (!Text || Length == 0)
    {
        return;
    }

    Depth = InterlockedIncrement(&KiArm64StageLogDepth);
    DbgPrintEx(DPFLTR_DEFAULT_ID,
               DPFLTR_TRACE_LEVEL,
               "[arm64] TrapDiagDbg: StageLog enter depth=%ld len=%Iu\n",
               Depth,
               Length);

    if (Depth > 1)
    {
        DbgPrintEx(DPFLTR_DEFAULT_ID,
                   DPFLTR_TRACE_LEVEL,
                   "[arm64] TrapDiagDbg: StageLog recursion depth=%ld dropping serial emit\n",
                   Depth);
        InterlockedDecrement(&KiArm64StageLogDepth);
        return;
    }

    while (Length > 0)
    {
        SIZE_T Chunk = min(Length, sizeof(Scratch) - 1);
        RtlCopyMemory(Scratch, Text, Chunk);
        Scratch[Chunk] = '\0';

        if (!((Chunk == 1) && (Scratch[0] == '\n')))
        {
            DbgPrintEx(DPFLTR_DEFAULT_ID,
                       DPFLTR_TRACE_LEVEL,
                       "[arm64] TrapDiagDbg: StageLog emit depth=%ld text='%s'\n",
                       Depth,
                       Scratch);
            KiArm64BootStageLog(Scratch);
        }

        Text += Chunk;
        Length -= Chunk;
    }

    InterlockedDecrement(&KiArm64StageLogDepth);
    DbgPrintEx(DPFLTR_DEFAULT_ID,
               DPFLTR_TRACE_LEVEL,
               "[arm64] TrapDiagDbg: StageLog exit depth=%ld\n",
               Depth - 1);
}
#endif

static
VOID
KiArm64InitializeBugCheckState(
    _Out_ PARM64_EARLY_TRAP_STATE State,
    _In_ ULONG BugCheckCode)
{
    RtlZeroMemory(State, sizeof(*State));

    /* Keep optional text sinks reachable to avoid unused warnings on GCC. */
#if defined(_M_ARM64) || defined(__aarch64__)
    if (0)
    {
        KiArm64DbgLogSink(NULL, 0);
        KiArm64StageLogSink(NULL, 0);
    }
#endif

    State->VectorId = (ULONG64)BugCheckCode;
}

static
VOID
KiArm64AugmentStateFromContext(
    _Inout_ PARM64_EARLY_TRAP_STATE State,
    _In_opt_ const CONTEXT *Context)
{
    ULONG Index;

    if (Context == NULL)
    {
        return;
    }

    for (Index = 0; Index < ARM64_EARLY_TRAP_REGISTER_COUNT; ++Index)
    {
        State->Registers.X[Index] = Context->X[Index];
    }

    State->Registers.Sp = Context->Sp;
    State->Registers.Pc = Context->Pc;
    State->Registers.Pstate = Context->Cpsr;

    if (State->Elr == 0)
    {
        State->Elr = Context->Pc;
    }

    if (State->Spsr == 0)
    {
        State->Spsr = Context->Cpsr;
    }
}

static
VOID
KiArm64AugmentStateFromTrapFrame(
    _Inout_ PARM64_EARLY_TRAP_STATE State,
    _In_opt_ const KTRAP_FRAME *TrapFrame)
{
    ULONG Index;

    if (TrapFrame == NULL)
    {
        return;
    }

    for (Index = 0; Index < RTL_NUMBER_OF(TrapFrame->X) &&
                    Index < ARM64_EARLY_TRAP_REGISTER_COUNT;
         ++Index)
    {
        State->Registers.X[Index] = TrapFrame->X[Index];
    }

    if (ARM64_EARLY_TRAP_REGISTER_COUNT > 29)
    {
        State->Registers.X[29] = TrapFrame->Fp;
    }

    if (ARM64_EARLY_TRAP_REGISTER_COUNT > 30)
    {
        State->Registers.X[30] = TrapFrame->Lr;
    }

    State->Registers.Sp = TrapFrame->Sp;
    State->Registers.Pc = TrapFrame->Pc;
    State->Registers.Pstate = TrapFrame->Spsr;
    State->ExceptionSyndrome = TrapFrame->Esr;
    State->FaultAddress = TrapFrame->FaultAddress;
    State->Elr = TrapFrame->Pc;
    State->Spsr = TrapFrame->Spsr;
}

VOID
KiArm64DumpTrapStateToLog(_In_ const ARM64_EARLY_TRAP_STATE *State)
{
    UNREFERENCED_PARAMETER(State);
    /* Bring-up: suppress trap-state emission entirely unless explicitly
       re-enabled with a live KD connection. */
    return;
}

VOID
KiArm64DumpBugCheckState(
    _In_ ULONG BugCheckCode,
    _In_ ULONG_PTR BugCheckParameter1,
    _In_ ULONG_PTR BugCheckParameter2,
    _In_ ULONG_PTR BugCheckParameter3,
    _In_ ULONG_PTR BugCheckParameter4,
    _In_opt_ PKTRAP_FRAME TrapFrame,
    _In_opt_ const CONTEXT *Context)
{
    ARM64_EARLY_TRAP_STATE State;

    /* If no debugger is attached, keep this lightweight to avoid re-entry
       during early boot. Emit a single line and skip the full state dump. */
    if (!KdDebuggerEnabled || KdDebuggerNotPresent)
    {
        DbgPrintEx(DPFLTR_DEFAULT_ID,
                   DPFLTR_ERROR_LEVEL,
                   "[arm64] BugCheck: code=%lx params=%p,%p,%p,%p (KD absent)\n",
                   BugCheckCode,
                   (PVOID)BugCheckParameter1,
                   (PVOID)BugCheckParameter2,
                   (PVOID)BugCheckParameter3,
                   (PVOID)BugCheckParameter4);
        return;
    }

    KiArm64InitializeBugCheckState(&State, BugCheckCode);

    DbgPrintEx(DPFLTR_DEFAULT_ID,
               DPFLTR_ERROR_LEVEL,
               "[arm64] BugCheck snapshot: code=%lx params=%p,%p,%p,%p\n",
               BugCheckCode,
               (PVOID)BugCheckParameter1,
               (PVOID)BugCheckParameter2,
               (PVOID)BugCheckParameter3,
               (PVOID)BugCheckParameter4);

    KiArm64AugmentStateFromContext(&State, Context);
    KiArm64AugmentStateFromTrapFrame(&State, TrapFrame);

    if ((Context == NULL) && (TrapFrame == NULL))
    {
        State.Elr = (ULONG64)_ReturnAddress();
    }

    KiArm64DumpTrapStateToLog(&State);
}
