/*
 * PROJECT:     ReactOS ARM64EC runtime
 * PURPOSE:     Native NTDLL call bridges for emulated AMD64 imports
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * COPYRIGHT:   Copyright 2000 Jon Griffiths
 * COPYRIGHT:   Copyright 2005 Juan Lang
 * COPYRIGHT:   Copyright 2023 Alexandre Julliard
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 *
 * ARM64EC context, setjmp, and unwind behavior is adapted from Wine's
 * dlls/ntdll/signal_arm64ec.c. ReactOS/FEX integration is original work.
 */

#include <ntdll.h>
#include <delayloadhandler.h>
#include <setjmp.h>

BOOLEAN NTAPI ChpeIsProcessorFeaturePresent(ULONG ProcessorFeature);
PVOID NTAPI LdrResolveDelayLoadedAPI(PVOID ParentBase, PCIMAGE_DELAYLOAD_DESCRIPTOR Descriptor, PDELAYLOAD_FAILURE_DLL_CALLBACK DllHook, PDELAYLOAD_FAILURE_SYSTEM_ROUTINE SystemHook, PIMAGE_THUNK_DATA ThunkAddress, ULONG Flags);
NTSTATUS NTAPI LdrResolveDelayLoadsFromDll(PVOID ParentBase, PCSTR TargetDllName, ULONG Flags);
NTSTATUS NTAPI NtFlushProcessWriteBuffers(VOID);
NTSTATUS NTAPI NtAdjustPrivilegesToken(HANDLE TokenHandle, BOOLEAN DisableAllPrivileges, PTOKEN_PRIVILEGES NewState, ULONG BufferLength, PTOKEN_PRIVILEGES PreviousState, PULONG ReturnLength);
NTSTATUS NTAPI NtAllocateVirtualMemoryEx(HANDLE ProcessHandle, PVOID *BaseAddress, PSIZE_T RegionSize, ULONG AllocationType, ULONG Protect, PMEM_EXTENDED_PARAMETER ExtendedParameters, ULONG ExtendedParameterCount);
NTSTATUS NTAPI NtMapViewOfSectionEx(HANDLE SectionHandle, HANDLE ProcessHandle, PVOID *BaseAddress, PLARGE_INTEGER SectionOffset, PSIZE_T ViewSize, ULONG AllocationType, ULONG Protect, PMEM_EXTENDED_PARAMETER ExtendedParameters, ULONG ExtendedParameterCount);
NTSTATUS NTAPI NtUnmapViewOfSectionEx(HANDLE ProcessHandle, PVOID BaseAddress, ULONG Flags);
PRUNTIME_FUNCTION NTAPI RtlLookupFunctionTable(ULONG_PTR ControlPc, PULONG_PTR ImageBase, PULONG Length);
NTSTATUS NTAPI RtlLogUnexpectedCodepath(const ULONG *Codepath);
VOID NTAPI RtlGetCurrentProcessorNumberEx(PPROCESSOR_NUMBER ProcessorNumber);
PSLIST_ENTRY NTAPI RtlInterlockedPushListSList(PSLIST_HEADER SListHead, PSLIST_ENTRY List, PSLIST_ENTRY ListEnd, ULONG Count);
ULONG NTAPI RtlSetCriticalSectionSpinCount(PRTL_CRITICAL_SECTION CriticalSection, ULONG SpinCount);
VOID NTAPI RtlRestoreLastWin32Error(ULONG Win32Error);
NTSTATUS NTAPI RtlWaitOnAddress(const VOID *Address, const VOID *CompareAddress, SIZE_T AddressSize, const LARGE_INTEGER *Timeout);
VOID NTAPI RtlWakeAddressAll(const VOID *Address);
VOID NTAPI RtlWakeAddressSingle(const VOID *Address);
VOID WINAPI TpCancelAsyncIoOperation(TP_IO *Io);
VOID WINAPI TpCallbackLeaveCriticalSectionOnCompletion(TP_CALLBACK_INSTANCE *Instance, RTL_CRITICAL_SECTION *CriticalSection);
VOID WINAPI TpCallbackReleaseMutexOnCompletion(TP_CALLBACK_INSTANCE *Instance, HANDLE Mutex);
VOID WINAPI TpCallbackReleaseSemaphoreOnCompletion(TP_CALLBACK_INSTANCE *Instance, HANDLE Semaphore, DWORD Count);
VOID WINAPI TpCallbackSetEventOnCompletion(TP_CALLBACK_INSTANCE *Instance, HANDLE Event);
VOID WINAPI TpCallbackUnloadDllOnCompletion(TP_CALLBACK_INSTANCE *Instance, HMODULE Module);
VOID WINAPI TpDisassociateCallback(TP_CALLBACK_INSTANCE *Instance);
BOOL WINAPI TpIsTimerSet(TP_TIMER *Timer);
VOID WINAPI TpPostWork(TP_WORK *Work);
VOID WINAPI TpReleaseCleanupGroup(TP_CLEANUP_GROUP *CleanupGroup);
VOID WINAPI TpReleaseCleanupGroupMembers(TP_CLEANUP_GROUP *CleanupGroup, BOOL CancelPending, PVOID CleanupParameter);
VOID WINAPI TpReleaseIoCompletion(TP_IO *Io);
VOID WINAPI TpReleasePool(TP_POOL *Pool);
VOID WINAPI TpReleaseTimer(TP_TIMER *Timer);
VOID WINAPI TpReleaseWait(TP_WAIT *Wait);
VOID WINAPI TpReleaseWork(TP_WORK *Work);
VOID WINAPI TpSetPoolMaxThreads(TP_POOL *Pool, DWORD MaximumThreads);
BOOL WINAPI TpSetPoolMinThreads(TP_POOL *Pool, DWORD MinimumThreads);
VOID WINAPI TpSetTimer(TP_TIMER *Timer, LARGE_INTEGER *Timeout, LONG Period, LONG WindowLength);
BOOL WINAPI TpSetTimerEx(TP_TIMER *Timer, LARGE_INTEGER *Timeout, LONG Period, LONG WindowLength);
VOID WINAPI TpSetWait(TP_WAIT *Wait, HANDLE Handle, LARGE_INTEGER *Timeout);
BOOL WINAPI TpSetWaitEx(TP_WAIT *Wait, HANDLE Handle, LARGE_INTEGER *Timeout, PVOID Reserved);
VOID WINAPI TpStartAsyncIoOperation(TP_IO *Io);
VOID WINAPI TpWaitForIoCompletion(TP_IO *Io, BOOL CancelPending);
VOID WINAPI TpWaitForTimer(TP_TIMER *Timer, BOOL CancelPending);
VOID WINAPI TpWaitForWait(TP_WAIT *Wait, BOOL CancelPending);
VOID WINAPI TpWaitForWork(TP_WORK *Work, BOOL CancelPending);
NTSTATUS WINAPI RtlWow64GetCurrentCpuArea(USHORT *Machine, void **Context, void **CpuArea);
PRUNTIME_FUNCTION NTAPI ChpepAmd64LookupFunctionTable(DWORD64 ControlPc, PDWORD64 ImageBase, PULONG Length);
PRUNTIME_FUNCTION NTAPI ChpepAmd64LookupFunctionEntry(DWORD64 ControlPc, PDWORD64 ImageBase, PUNWIND_HISTORY_TABLE HistoryTable);
PEXCEPTION_ROUTINE NTAPI ChpepAmd64VirtualUnwind(ULONG HandlerType, ULONG64 ImageBase, ULONG64 ControlPc, PRUNTIME_FUNCTION FunctionEntry, PCONTEXT ContextRecord, PVOID *HandlerData, PULONG64 EstablisherFrame, PKNONVOLATILE_CONTEXT_POINTERS ContextPointers);
PLIST_ENTRY NTAPI ChpepAmd64GetFunctionTableListHead(VOID);
BOOLEAN NTAPI ChpepAmd64AddFunctionTable(PRUNTIME_FUNCTION FunctionTable, DWORD EntryCount, DWORD64 BaseAddress);
DWORD NTAPI ChpepAmd64AddGrowableFunctionTable(PVOID *DynamicTable, PRUNTIME_FUNCTION FunctionTable, DWORD EntryCount, DWORD MaximumEntryCount, ULONG_PTR RangeBase, ULONG_PTR RangeEnd);
BOOLEAN NTAPI ChpepAmd64DeleteFunctionTable(PRUNTIME_FUNCTION FunctionTable);
VOID NTAPI ChpepAmd64DeleteGrowableFunctionTable(PVOID DynamicTable);
VOID NTAPI ChpepAmd64GrowFunctionTable(PVOID DynamicTable, DWORD NewEntryCount);
BOOLEAN NTAPI ChpepAmd64InstallFunctionTableCallback(DWORD64 TableIdentifier, DWORD64 BaseAddress, DWORD Length, PGET_RUNTIME_FUNCTION_CALLBACK Callback, PVOID Context, PCWSTR OutOfProcessCallbackDll);
PRUNTIME_FUNCTION NTAPI ChpeRtlLookupFunctionEntry(ULONG_PTR ControlPc, PULONG_PTR ImageBase, PUNWIND_HISTORY_TABLE HistoryTable);
PEXCEPTION_ROUTINE NTAPI ChpeRtlVirtualUnwind(ULONG HandlerType, ULONG_PTR ImageBase, ULONG_PTR ControlPc, PRUNTIME_FUNCTION FunctionEntry, PCONTEXT ContextRecord, PVOID *HandlerData, PULONG_PTR EstablisherFrame, PKNONVOLATILE_CONTEXT_POINTERS ContextPointers);
VOID NTAPI ChpeRtlUnwind(PVOID TargetFrame, PVOID TargetIp, PEXCEPTION_RECORD ExceptionRecord, PVOID ReturnValue);
VOID NTAPI ChpeRtlUnwindEx(PVOID TargetFrame, PVOID TargetIp, PEXCEPTION_RECORD ExceptionRecord, PVOID ReturnValue, PCONTEXT ContextRecord, PUNWIND_HISTORY_TABLE HistoryTable);
EXCEPTION_DISPOSITION CDECL __C_specific_handler(PEXCEPTION_RECORD ExceptionRecord, PVOID EstablisherFrame, PCONTEXT ContextRecord, PDISPATCHER_CONTEXT DispatcherContext);
NTSTATUS NTAPI ChpeNtContinue(PCONTEXT ContextRecord, BOOLEAN Alertable);
NTSTATUS NTAPI ChpeNtRaiseException(PEXCEPTION_RECORD ExceptionRecord, PCONTEXT ContextRecord, BOOLEAN FirstChance);
VOID NTAPI ChpeRtlCaptureContext(PCONTEXT ContextRecord);
DECLSPEC_NORETURN VOID NTAPI ChpeRtlRaiseException(PEXCEPTION_RECORD ExceptionRecord);
DECLSPEC_NORETURN VOID NTAPI ChpeRtlRaiseStatus(NTSTATUS Status);
ULONG CDECL ChpeDbgPrint(PCCH Format, ...);
BOOLEAN NTAPI ChpeCanContinueToGuest(VOID);
DECLSPEC_NORETURN VOID NTAPI ChpeContinueToGuest(PVOID Amd64Context);
BOOLEAN NTAPI RtlIsEcCode(ULONG_PTR Address);

typedef struct _CHPE_AMD64_SCOPE_TABLE
{
    ULONG Count;
    struct
    {
        ULONG BeginAddress;
        ULONG EndAddress;
        ULONG HandlerAddress;
        ULONG JumpTarget;
    } ScopeRecord[1];
} CHPE_AMD64_SCOPE_TABLE, *PCHPE_AMD64_SCOPE_TABLE;

typedef struct _CHPE_AMD64_DISPATCHER_CONTEXT
{
    ULONG64 ControlPc;
    ULONG64 ImageBase;
    PRUNTIME_FUNCTION FunctionEntry;
    ULONG64 EstablisherFrame;
    ULONG64 TargetIp;
    PCONTEXT ContextRecord;
    PEXCEPTION_ROUTINE LanguageHandler;
    PVOID HandlerData;
    PUNWIND_HISTORY_TABLE HistoryTable;
    ULONG ScopeIndex;
    BOOLEAN ControlPcIsUnwound;
    UCHAR Fill0[3];
    PBYTE NonVolatileRegisters;
} CHPE_AMD64_DISPATCHER_CONTEXT, *PCHPE_AMD64_DISPATCHER_CONTEXT;

typedef LONG (CDECL *PCHPE_AMD64_EXCEPTION_FILTER)(PEXCEPTION_POINTERS ExceptionPointers, PVOID EstablisherFrame);
typedef VOID (CDECL *PCHPE_AMD64_TERMINATION_HANDLER)(BOOLEAN AbnormalTermination, PVOID EstablisherFrame);

typedef struct _CHPE_BRIDGE_CPU_AREA_INFO
{
    BOOLEAN InSimulation;
    BOOLEAN InSyscallCallback;
    UCHAR Reserved[6];
    ULONG64 EmulatorStackBase;
    ULONG64 EmulatorStackLimit;
} CHPE_BRIDGE_CPU_AREA_INFO, *PCHPE_BRIDGE_CPU_AREA_INFO;

/*
 * ARM64EC exposes the AMD64 CONTEXT layout to callers while the native
 * ReactOS kernel and ntdll consume ARM64_NT_CONTEXT.  The register mapping
 * and FP status conversion follow the public ARM64EC ABI documented by the
 * Windows SDK.  FP conversion semantics are derived from Wine's ARM64EC
 * unwind support.
 *
 * Copyright 2023 Alexandre Julliard
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
static ULONG
ChpepContextFlagsX64ToArm64(ULONG Flags)
{
    ULONG Result = CONTEXT_ARM64;

    Flags &= ~CONTEXT_AMD64;
    if (Flags & CONTEXT_AMD64_CONTROL) Result |= CONTEXT_ARM64_CONTROL;
    if (Flags & CONTEXT_AMD64_INTEGER) Result |= CONTEXT_ARM64_INTEGER;
    if (Flags & CONTEXT_AMD64_FLOATING_POINT) Result |= CONTEXT_ARM64_FLOATING_POINT;
    return Result;
}

static ULONG
ChpepContextFlagsArm64ToX64(ULONG Flags)
{
    ULONG Result = CONTEXT_AMD64;

    Flags &= ~CONTEXT_ARM64;
    if (Flags & CONTEXT_ARM64_CONTROL) Result |= CONTEXT_AMD64_CONTROL;
    if (Flags & CONTEXT_ARM64_INTEGER) Result |= CONTEXT_AMD64_INTEGER;
    if (Flags & CONTEXT_ARM64_FLOATING_POINT) Result |= CONTEXT_AMD64_FLOATING_POINT;
    return Result;
}

static ULONG
ChpepEFlagsToCpsr(ULONG EFlags)
{
    ULONG Cpsr = 0;

    if (EFlags & 0x0001) Cpsr |= 0x20000000;
    if (EFlags & 0x0040) Cpsr |= 0x40000000;
    if (EFlags & 0x0080) Cpsr |= 0x80000000;
    if (EFlags & 0x0100) Cpsr |= 0x00200000;
    if (EFlags & 0x0800) Cpsr |= 0x10000000;
    return Cpsr;
}

static ULONG
ChpepCpsrToEFlags(ULONG Cpsr)
{
    ULONG EFlags = 0x202;

    if (Cpsr & 0x00200000) EFlags |= 0x0100;
    if (Cpsr & 0x10000000) EFlags |= 0x0800;
    if (Cpsr & 0x20000000) EFlags |= 0x0001;
    if (Cpsr & 0x40000000) EFlags |= 0x0040;
    if (Cpsr & 0x80000000) EFlags |= 0x0080;
    return EFlags;
}

static ULONG64
ChpepMxCsrToFpCsr(ULONG MxCsr)
{
    ULONG Fpcr = 0, Fpsr = 0;

    if (MxCsr & 0x0001) Fpsr |= 0x0001;
    if (MxCsr & 0x0002) Fpsr |= 0x0080;
    if (MxCsr & 0x0004) Fpsr |= 0x0002;
    if (MxCsr & 0x0008) Fpsr |= 0x0004;
    if (MxCsr & 0x0010) Fpsr |= 0x0008;
    if (MxCsr & 0x0020) Fpsr |= 0x0010;

    if (MxCsr & 0x0040) Fpcr |= 0x00080000;
    if (!(MxCsr & 0x0080)) Fpcr |= 0x00000100;
    if (!(MxCsr & 0x0100)) Fpcr |= 0x00008000;
    if (!(MxCsr & 0x0200)) Fpcr |= 0x00000200;
    if (!(MxCsr & 0x0400)) Fpcr |= 0x00000400;
    if (!(MxCsr & 0x0800)) Fpcr |= 0x00000800;
    if (!(MxCsr & 0x1000)) Fpcr |= 0x00001000;
    if (MxCsr & 0x2000) Fpcr |= 0x00800000;
    if (MxCsr & 0x4000) Fpcr |= 0x00400000;
    if (MxCsr & 0x8000) Fpcr |= 0x01000000;
    return Fpcr | ((ULONG64)Fpsr << 32);
}

static ULONG
ChpepFpCsrToMxCsr(ULONG Fpcr, ULONG Fpsr)
{
    ULONG MxCsr = 0;

    if (Fpsr & 0x0001) MxCsr |= 0x0001;
    if (Fpsr & 0x0002) MxCsr |= 0x0004;
    if (Fpsr & 0x0004) MxCsr |= 0x0008;
    if (Fpsr & 0x0008) MxCsr |= 0x0010;
    if (Fpsr & 0x0010) MxCsr |= 0x0020;
    if (Fpsr & 0x0080) MxCsr |= 0x0002;

    if (Fpcr & 0x00080000) MxCsr |= 0x0040;
    if (!(Fpcr & 0x00000100)) MxCsr |= 0x0080;
    if (!(Fpcr & 0x00000200)) MxCsr |= 0x0200;
    if (!(Fpcr & 0x00000400)) MxCsr |= 0x0400;
    if (!(Fpcr & 0x00000800)) MxCsr |= 0x0800;
    if (!(Fpcr & 0x00001000)) MxCsr |= 0x1000;
    if (!(Fpcr & 0x00008000)) MxCsr |= 0x0100;
    if (Fpcr & 0x00400000) MxCsr |= 0x4000;
    if (Fpcr & 0x00800000) MxCsr |= 0x2000;
    if (Fpcr & 0x01000000) MxCsr |= 0x8000;
    return MxCsr;
}

static VOID
ChpepContextX64ToArm64(PARM64_NT_CONTEXT ArmContext, const ARM64EC_NT_CONTEXT *EcContext)
{
    ULONG64 FpCsr;

    RtlZeroMemory(ArmContext, sizeof(*ArmContext));
    ArmContext->ContextFlags = ChpepContextFlagsX64ToArm64(EcContext->ContextFlags);
    ArmContext->Cpsr = ChpepEFlagsToCpsr(EcContext->AMD64_EFlags);
    ArmContext->X0 = EcContext->X0;
    ArmContext->X1 = EcContext->X1;
    ArmContext->X2 = EcContext->X2;
    ArmContext->X3 = EcContext->X3;
    ArmContext->X4 = EcContext->X4;
    ArmContext->X5 = EcContext->X5;
    ArmContext->X6 = EcContext->X6;
    ArmContext->X7 = EcContext->X7;
    ArmContext->X8 = EcContext->X8;
    ArmContext->X9 = EcContext->X9;
    ArmContext->X10 = EcContext->X10;
    ArmContext->X11 = EcContext->X11;
    ArmContext->X12 = EcContext->X12;
    ArmContext->X15 = EcContext->X15;
    ArmContext->X16 = EcContext->X16_0 | ((ULONG64)EcContext->X16_1 << 16) | ((ULONG64)EcContext->X16_2 << 32) | ((ULONG64)EcContext->X16_3 << 48);
    ArmContext->X17 = EcContext->X17_0 | ((ULONG64)EcContext->X17_1 << 16) | ((ULONG64)EcContext->X17_2 << 32) | ((ULONG64)EcContext->X17_3 << 48);
    ArmContext->X19 = EcContext->X19;
    ArmContext->X20 = EcContext->X20;
    ArmContext->X21 = EcContext->X21;
    ArmContext->X22 = EcContext->X22;
    ArmContext->X25 = EcContext->X25;
    ArmContext->X26 = EcContext->X26;
    ArmContext->X27 = EcContext->X27;
    ArmContext->Fp = EcContext->Fp;
    ArmContext->Lr = EcContext->Lr;
    ArmContext->Sp = EcContext->Sp;
    ArmContext->Pc = EcContext->Pc;
    RtlCopyMemory(ArmContext->V, EcContext->V, sizeof(EcContext->V));
    FpCsr = ChpepMxCsrToFpCsr(EcContext->AMD64_MxCsr);
    ArmContext->Fpcr = (ULONG)FpCsr;
    ArmContext->Fpsr = (ULONG)(FpCsr >> 32);
}

static VOID
ChpepMergeContextX64ToArm64(PARM64_NT_CONTEXT ArmContext, const ARM64EC_NT_CONTEXT *EcContext)
{
    ARM64_NT_CONTEXT Converted;

    ChpepContextX64ToArm64(&Converted, EcContext);
    Converted.ContextFlags = ArmContext->ContextFlags;
    Converted.X13 = ArmContext->X13;
    Converted.X14 = ArmContext->X14;
    Converted.X18 = ArmContext->X18;
    Converted.X23 = ArmContext->X23;
    Converted.X24 = ArmContext->X24;
    Converted.X28 = ArmContext->X28;
    RtlCopyMemory(Converted.Bcr, ArmContext->Bcr, sizeof(Converted.Bcr));
    RtlCopyMemory(Converted.Bvr, ArmContext->Bvr, sizeof(Converted.Bvr));
    RtlCopyMemory(Converted.Wcr, ArmContext->Wcr, sizeof(Converted.Wcr));
    RtlCopyMemory(Converted.Wvr, ArmContext->Wvr, sizeof(Converted.Wvr));
    *ArmContext = Converted;
}

static VOID
ChpepContextArm64ToX64(PARM64EC_NT_CONTEXT EcContext, const ARM64_NT_CONTEXT *ArmContext)
{
    RtlZeroMemory(EcContext, sizeof(*EcContext));
    EcContext->ContextFlags = ChpepContextFlagsArm64ToX64(ArmContext->ContextFlags);
    EcContext->AMD64_SegCs = 0x33;
    EcContext->AMD64_SegDs = 0x2b;
    EcContext->AMD64_SegEs = 0x2b;
    EcContext->AMD64_SegFs = 0x53;
    EcContext->AMD64_SegGs = 0x2b;
    EcContext->AMD64_SegSs = 0x2b;
    EcContext->AMD64_EFlags = ChpepCpsrToEFlags(ArmContext->Cpsr);
    EcContext->AMD64_MxCsr = ChpepFpCsrToMxCsr(ArmContext->Fpcr, ArmContext->Fpsr);
    EcContext->AMD64_MxCsr_copy = EcContext->AMD64_MxCsr;
    EcContext->AMD64_ControlWord = 0x27f;
    EcContext->AMD64_MxCsr_Mask = 0xffff;
    EcContext->X8 = ArmContext->X8;
    EcContext->X0 = ArmContext->X0;
    EcContext->X1 = ArmContext->X1;
    EcContext->X27 = ArmContext->X27;
    EcContext->Sp = ArmContext->Sp;
    EcContext->Fp = ArmContext->Fp;
    EcContext->X25 = ArmContext->X25;
    EcContext->X26 = ArmContext->X26;
    EcContext->X2 = ArmContext->X2;
    EcContext->X3 = ArmContext->X3;
    EcContext->X4 = ArmContext->X4;
    EcContext->X5 = ArmContext->X5;
    EcContext->X19 = ArmContext->X19;
    EcContext->X20 = ArmContext->X20;
    EcContext->X21 = ArmContext->X21;
    EcContext->X22 = ArmContext->X22;
    EcContext->Pc = ArmContext->Pc;
    EcContext->Lr = ArmContext->Lr;
    EcContext->X6 = ArmContext->X6;
    EcContext->X7 = ArmContext->X7;
    EcContext->X9 = ArmContext->X9;
    EcContext->X10 = ArmContext->X10;
    EcContext->X11 = ArmContext->X11;
    EcContext->X12 = ArmContext->X12;
    EcContext->X15 = ArmContext->X15;
    EcContext->X16_0 = (USHORT)ArmContext->X16;
    EcContext->X16_1 = (USHORT)(ArmContext->X16 >> 16);
    EcContext->X16_2 = (USHORT)(ArmContext->X16 >> 32);
    EcContext->X16_3 = (USHORT)(ArmContext->X16 >> 48);
    EcContext->X17_0 = (USHORT)ArmContext->X17;
    EcContext->X17_1 = (USHORT)(ArmContext->X17 >> 16);
    EcContext->X17_2 = (USHORT)(ArmContext->X17 >> 32);
    EcContext->X17_3 = (USHORT)(ArmContext->X17 >> 48);
    RtlCopyMemory(EcContext->V, ArmContext->V, sizeof(EcContext->V));
}

static VOID __attribute__((used))
ChpepFinalizeCapturedContext(PCONTEXT ContextRecord, ULONG Cpsr, ULONG Fpcr, ULONG Fpsr)
{
    CONTEXT UnwindContext;
    PRUNTIME_FUNCTION FunctionEntry;
    ULONG_PTR ControlPc, ImageBase = 0, EstablisherFrame;
    PVOID HandlerData;

    ContextRecord->ContextFlags = CONTEXT_AMD64_FULL;
    ContextRecord->EFlags = ChpepCpsrToEFlags(Cpsr);
    ContextRecord->MxCsr = ChpepFpCsrToMxCsr(Fpcr, Fpsr);
    ContextRecord->FltSave.ControlWord = 0x27f;
    ContextRecord->FltSave.StatusWord = 0;
    ContextRecord->FltSave.MxCsr = ContextRecord->MxCsr;

    /* Recover the caller's nonvolatile state from this bridge frame. */
    UnwindContext = *ContextRecord;
    ControlPc = UnwindContext.Rip >= sizeof(ULONG) ? UnwindContext.Rip - sizeof(ULONG) : UnwindContext.Rip;
    FunctionEntry = ChpeRtlLookupFunctionEntry(ControlPc, &ImageBase, NULL);
    ChpeRtlVirtualUnwind(UNW_FLAG_NHANDLER, ImageBase, ControlPc, FunctionEntry, &UnwindContext, &HandlerData, &EstablisherFrame, NULL);
    RtlCopyMemory(&ContextRecord->Rax, &UnwindContext.Rax, FIELD_OFFSET(CONTEXT, FltSave) - FIELD_OFFSET(CONTEXT, Rax));
}

static BOOLEAN
ChpepIsValidUnwindFrame(ULONG_PTR Frame)
{
    PCHPE_BRIDGE_CPU_AREA_INFO CpuArea = NULL;
    NT_TIB *Tib = &NtCurrentTeb()->NtTib;

    if ((Frame & (sizeof(ULONG_PTR) - 1)) != 0)
        return FALSE;

    if (Frame >= (ULONG_PTR)Tib->StackLimit && Frame <= (ULONG_PTR)Tib->StackBase)
        return TRUE;

    if (!NT_SUCCESS(RtlWow64GetCurrentCpuArea(NULL, NULL, (PVOID *)&CpuArea)) || CpuArea == NULL)
        return FALSE;

    return Frame >= CpuArea->EmulatorStackLimit && Frame <= CpuArea->EmulatorStackBase;
}

static VOID
ChpepCaptureDispatcherNonVolatiles(DISPATCHER_CONTEXT_NONVOLREG_ARM64 *NonVolatileRegisters, const ARM64EC_NT_CONTEXT *ContextRecord)
{
    ULONG Index;

    NonVolatileRegisters->GpNvRegs[0] = ContextRecord->X19;
    NonVolatileRegisters->GpNvRegs[1] = ContextRecord->X20;
    NonVolatileRegisters->GpNvRegs[2] = ContextRecord->X21;
    NonVolatileRegisters->GpNvRegs[3] = ContextRecord->X22;
    NonVolatileRegisters->GpNvRegs[4] = 0;
    NonVolatileRegisters->GpNvRegs[5] = 0;
    NonVolatileRegisters->GpNvRegs[6] = ContextRecord->X25;
    NonVolatileRegisters->GpNvRegs[7] = ContextRecord->X26;
    NonVolatileRegisters->GpNvRegs[8] = ContextRecord->X27;
    NonVolatileRegisters->GpNvRegs[9] = 0;
    NonVolatileRegisters->GpNvRegs[10] = ContextRecord->Fp;

    for (Index = 0; Index < RTL_NUMBER_OF(NonVolatileRegisters->FpNvRegs); Index++)
        NonVolatileRegisters->FpNvRegs[Index] = ContextRecord->V[Index + 8].D[0];
}

static NTSTATUS
ChpepVirtualUnwindFrame(ULONG HandlerType, PDISPATCHER_CONTEXT_ARM64EC DispatcherContext, PARM64EC_NT_CONTEXT ContextRecord)
{
    ULONG_PTR ControlPc = ContextRecord->Pc;

    DispatcherContext->ScopeIndex = 0;
    DispatcherContext->ControlPc = ControlPc;
    DispatcherContext->ControlPcIsUnwound = (ContextRecord->ContextFlags & CONTEXT_UNWOUND_TO_CALL) != 0;
    if (DispatcherContext->ControlPcIsUnwound && RtlIsEcCode(ControlPc) && ControlPc >= sizeof(ULONG))
        ControlPc -= sizeof(ULONG);

    ChpepCaptureDispatcherNonVolatiles((DISPATCHER_CONTEXT_NONVOLREG_ARM64 *)DispatcherContext->NonVolatileRegisters, ContextRecord);
    DispatcherContext->FunctionEntry = ChpeRtlLookupFunctionEntry(ControlPc, &DispatcherContext->ImageBase, DispatcherContext->HistoryTable);

    DispatcherContext->LanguageHandler = ChpeRtlVirtualUnwind(HandlerType, DispatcherContext->ImageBase, ControlPc, DispatcherContext->FunctionEntry, &ContextRecord->AMD64_Context, &DispatcherContext->HandlerData, &DispatcherContext->EstablisherFrame, NULL);

    return STATUS_SUCCESS;
}

static EXCEPTION_DISPOSITION NTAPI __attribute__((used))
ChpepUnwindCollisionHandler(PEXCEPTION_RECORD ExceptionRecord, PVOID EstablisherFrame, PCONTEXT ContextRecord, PDISPATCHER_CONTEXT_ARM64EC DispatcherContext)
{
    PDISPATCHER_CONTEXT_ARM64EC OriginalDispatcher = ((PDISPATCHER_CONTEXT_ARM64EC *)EstablisherFrame)[-2];

    UNREFERENCED_PARAMETER(ExceptionRecord);
    UNREFERENCED_PARAMETER(ContextRecord);

    DispatcherContext->ControlPc = OriginalDispatcher->ControlPc;
    DispatcherContext->ImageBase = OriginalDispatcher->ImageBase;
    DispatcherContext->FunctionEntry = OriginalDispatcher->FunctionEntry;
    DispatcherContext->EstablisherFrame = OriginalDispatcher->EstablisherFrame;
    DispatcherContext->LanguageHandler = OriginalDispatcher->LanguageHandler;
    DispatcherContext->HandlerData = OriginalDispatcher->HandlerData;
    DispatcherContext->HistoryTable = OriginalDispatcher->HistoryTable;
    DispatcherContext->ScopeIndex = OriginalDispatcher->ScopeIndex;
    DispatcherContext->ControlPcIsUnwound = OriginalDispatcher->ControlPcIsUnwound;
    *DispatcherContext->ContextRecord = *OriginalDispatcher->ContextRecord;
    RtlCopyMemory(DispatcherContext->NonVolatileRegisters, OriginalDispatcher->NonVolatileRegisters, sizeof(DISPATCHER_CONTEXT_NONVOLREG_ARM64));
    return ExceptionCollidedUnwind;
}

static EXCEPTION_DISPOSITION __attribute__((noinline, used))
ChpepInvokeUnwindHandler(PEXCEPTION_ROUTINE Handler, PEXCEPTION_RECORD ExceptionRecord, PVOID EstablisherFrame, PCONTEXT ContextRecord, PDISPATCHER_CONTEXT_ARM64EC DispatcherContext)
{
    return Handler(ExceptionRecord, EstablisherFrame, ContextRecord, DispatcherContext);
}

static EXCEPTION_DISPOSITION NTAPI __attribute__((naked, used))
ChpepCallUnwindHandler(PEXCEPTION_RECORD ExceptionRecord, PVOID EstablisherFrame, PCONTEXT ContextRecord, PDISPATCHER_CONTEXT_ARM64EC DispatcherContext, PEXCEPTION_ROUTINE Handler)
{
    __asm__ volatile(
        ".seh_proc \"#ChpepCallUnwindHandler\"\n"
        "stp x29, x30, [sp, #-32]!\n"
        ".seh_save_fplr_x 32\n"
        ".seh_endprologue\n"
        ".seh_handler \"#ChpepUnwindCollisionHandler\", @except\n"
        "str x3, [sp, #16]\n"
        "mov x5, x3\n"
        "mov x3, x2\n"
        "mov x2, x1\n"
        "mov x1, x0\n"
        "mov x0, x4\n"
        "mov x4, x5\n"
        "bl \"#ChpepInvokeUnwindHandler\"\n"
        "ldp x29, x30, [sp], #32\n"
        "ret\n"
        ".seh_endproc\n");
}

static EXCEPTION_DISPOSITION NTAPI __attribute__((used))
ChpepNestedExceptionHandler(PEXCEPTION_RECORD ExceptionRecord, PVOID EstablisherFrame, PCONTEXT ContextRecord, PDISPATCHER_CONTEXT_ARM64EC DispatcherContext)
{
    UNREFERENCED_PARAMETER(EstablisherFrame);
    UNREFERENCED_PARAMETER(ContextRecord);
    UNREFERENCED_PARAMETER(DispatcherContext);

    if (ExceptionRecord->ExceptionFlags & (EXCEPTION_UNWINDING | EXCEPTION_EXIT_UNWIND))
        return ExceptionContinueSearch;

    return ExceptionNestedException;
}

static EXCEPTION_DISPOSITION NTAPI __attribute__((naked, used))
ChpepCallExceptionHandler(PEXCEPTION_RECORD ExceptionRecord, PVOID EstablisherFrame, PCONTEXT ContextRecord, PDISPATCHER_CONTEXT_ARM64EC DispatcherContext, PEXCEPTION_ROUTINE Handler)
{
    __asm__ volatile(
        ".seh_proc \"#ChpepCallExceptionHandler\"\n"
        "stp x29, x30, [sp, #-16]!\n"
        ".seh_save_fplr_x 16\n"
        ".seh_endprologue\n"
        ".seh_handler \"#ChpepNestedExceptionHandler\", @except\n"
        "mov x5, x3\n"
        "mov x3, x2\n"
        "mov x2, x1\n"
        "mov x1, x0\n"
        "mov x0, x4\n"
        "mov x4, x5\n"
        "bl \"#ChpepInvokeUnwindHandler\"\n"
        "ldp x29, x30, [sp], #16\n"
        "ret\n"
        ".seh_endproc\n");
}

/*
 * Invoke an ARM64EC native filter/finally funclet with the nonvolatile
 * register image captured for its parent frame. Adapted from Wine's
 * dlls/msvcrt/except_arm64ec.c.
 *
 * Copyright 2011 Alexandre Julliard
 * Copyright 2013 Andre Hentschel
 * Copyright 2017 Martin Storsjo
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
static LONG __attribute__((naked, used))
ChpepExecuteEcHandler(PVOID Argument, PVOID EstablisherFrame, PVOID Handler, PBYTE NonVolatileRegisters)
{
    __asm__ volatile(
        ".seh_proc \"#ChpepExecuteEcHandler\"\n"
        "stp x29, x30, [sp, #-80]!\n"
        ".seh_save_fplr_x 80\n"
        "stp x19, x20, [sp, #16]\n"
        ".seh_save_regp x19, 16\n"
        "stp x21, x22, [sp, #32]\n"
        ".seh_save_regp x21, 32\n"
        "stp x25, x26, [sp, #48]\n"
        ".seh_save_regp x25, 48\n"
        "str x27, [sp, #64]\n"
        ".seh_save_reg x27, 64\n"
        ".seh_endprologue\n"
        "ldp x19, x20, [x3, #0]\n"
        "ldp x21, x22, [x3, #16]\n"
        "ldp x25, x26, [x3, #48]\n"
        "ldr x27, [x3, #64]\n"
        "ldr x1, [x3, #80]\n"
        "blr x2\n"
        "ldp x19, x20, [sp, #16]\n"
        "ldp x21, x22, [sp, #32]\n"
        "ldp x25, x26, [sp, #48]\n"
        "ldr x27, [sp, #64]\n"
        "ldp x29, x30, [sp], #80\n"
        "ret\n"
        ".seh_endproc\n");
}

/*
 * Walk the mixed ARM64EC/AMD64 table-based SEH chain. This follows Wine's
 * public ARM64EC call_seh_handlers algorithm, without Wine's private TEB
 * registration-chain fallback (ReactOS x64/ARM64EC SEH is table based).
 *
 * Copyright 2023 Alexandre Julliard
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
static NTSTATUS
ChpepCallExceptionHandlers(PEXCEPTION_RECORD ExceptionRecord, PCONTEXT OriginalContext)
{
    DISPATCHER_CONTEXT_NONVOLREG_ARM64 NonVolatileRegisters;
    DISPATCHER_CONTEXT_ARM64EC DispatcherContext;
    UNWIND_HISTORY_TABLE HistoryTable;
    ARM64EC_NT_CONTEXT UnwindContext;
    EXCEPTION_DISPOSITION Disposition;
    ULONG_PTR PreviousPc, PreviousSp, Frame;
    NTSTATUS Status;
    ULONG Count;

    RtlZeroMemory(&DispatcherContext, sizeof(DispatcherContext));
    RtlZeroMemory(&HistoryTable, sizeof(HistoryTable));
    UnwindContext.AMD64_Context = *OriginalContext;
    UnwindContext.ContextFlags &= ~CONTEXT_AMD64_XSTATE;
    DispatcherContext.ContextRecord = &UnwindContext.AMD64_Context;
    DispatcherContext.HistoryTable = &HistoryTable;
    DispatcherContext.NonVolatileRegisters = NonVolatileRegisters.Buffer;

    for (Count = 0; Count < 1024; Count++)
    {
        PreviousPc = UnwindContext.Pc;
        PreviousSp = UnwindContext.Sp;
        Status = ChpepVirtualUnwindFrame(UNW_FLAG_EHANDLER, &DispatcherContext, &UnwindContext);
        if (!NT_SUCCESS(Status))
            return Status;

UnwindDone:
        if (DispatcherContext.EstablisherFrame == 0)
            break;

        if (!ChpepIsValidUnwindFrame(DispatcherContext.EstablisherFrame))
        {
            ExceptionRecord->ExceptionFlags |= EXCEPTION_STACK_INVALID;
            break;
        }

        if (DispatcherContext.LanguageHandler != NULL)
        {
            Disposition = ChpepCallExceptionHandler(ExceptionRecord, (PVOID)(ULONG_PTR)DispatcherContext.EstablisherFrame, OriginalContext, &DispatcherContext, DispatcherContext.LanguageHandler);
            ExceptionRecord->ExceptionFlags &= EXCEPTION_NONCONTINUABLE;

            switch (Disposition)
            {
                case ExceptionContinueExecution:
                    if (ExceptionRecord->ExceptionFlags & EXCEPTION_NONCONTINUABLE)
                        return STATUS_NONCONTINUABLE_EXCEPTION;
                    return STATUS_SUCCESS;

                case ExceptionContinueSearch:
                    break;

                case ExceptionNestedException:
                    ExceptionRecord->ExceptionFlags |= EXCEPTION_NESTED_CALL;
                    break;

                case ExceptionCollidedUnwind:
                    ChpeRtlVirtualUnwind(UNW_FLAG_NHANDLER, DispatcherContext.ImageBase, DispatcherContext.ControlPc, DispatcherContext.FunctionEntry, &UnwindContext.AMD64_Context, &DispatcherContext.HandlerData, &Frame, NULL);
                    goto UnwindDone;

                default:
                    return STATUS_INVALID_DISPOSITION;
            }
        }

        if (UnwindContext.Pc == 0 || UnwindContext.Sp == (ULONG64)(ULONG_PTR)NtCurrentTeb()->NtTib.StackBase)
            break;

        /* Windows stops exception dispatch when an unwind makes no progress.
         * This leaves the original exception unhandled; it does not replace it
         * with STATUS_BAD_FUNCTION_TABLE. */
        if (UnwindContext.Pc == PreviousPc && UnwindContext.Sp == PreviousSp)
            break;
    }

    if (Count == 1024)
        return STATUS_BAD_STACK;

    return STATUS_UNHANDLED_EXCEPTION;
}

NTSTATUS NTAPI
ChpeDispatchExceptionNative(PEXCEPTION_RECORD ExceptionRecord, PARM64_NT_CONTEXT NativeContext)
{
    ARM64EC_NT_CONTEXT EcContext;
    NTSTATUS Status;

    if (ExceptionRecord == NULL || NativeContext == NULL)
        return STATUS_INVALID_PARAMETER;

    ChpepContextArm64ToX64(&EcContext, NativeContext);
    Status = ChpepCallExceptionHandlers(ExceptionRecord, &EcContext.AMD64_Context);
    if (Status != STATUS_SUCCESS)
    {
        if (Status != STATUS_UNHANDLED_EXCEPTION)
            ChpeDbgPrint("CHPE: mixed exception dispatch failed, code=0x%08lx status=0x%08lx pc=%p sp=%p\n", ExceptionRecord->ExceptionCode, Status, (PVOID)(ULONG_PTR)EcContext.Pc, (PVOID)(ULONG_PTR)EcContext.Sp);
        return Status;
    }

    if (!RtlIsEcCode(EcContext.Pc))
        return ChpeNtContinue(&EcContext.AMD64_Context, FALSE);

    ChpepMergeContextX64ToArm64(NativeContext, &EcContext);
    return STATUS_SUCCESS;
}

/* Fixed-argument native entry point used to carry an ARM64EC va_list. */
ULONG NTAPI vDbgPrintEx(ULONG ComponentId, ULONG Level, PCCH Format, va_list Arguments);

VOID NTAPI
ChpeDbgBreakPoint(VOID)
{
    DbgBreakPoint();
}

ULONG CDECL
ChpeDbgPrint(PCCH Format, ...)
{
    va_list Arguments;
    ULONG Status;

    va_start(Arguments, Format);
    Status = vDbgPrintEx((ULONG)-1, DPFLTR_ERROR_LEVEL, Format, Arguments);
    va_end(Arguments);
    return Status;
}

ULONG CDECL
ChpeDbgPrintEx(ULONG ComponentId, ULONG Level, PCCH Format, ...)
{
    va_list Arguments;
    ULONG Status;

    va_start(Arguments, Format);
    Status = vDbgPrintEx(ComponentId, Level, Format, Arguments);
    va_end(Arguments);
    return Status;
}

INT CDECL
ChpeSnprintf(PCHAR Buffer, SIZE_T Count, PCSTR Format, ...)
{
    va_list Arguments;
    INT Result;

    va_start(Arguments, Format);
    Result = _vsnprintf(Buffer, Count, Format, Arguments);
    va_end(Arguments);
    return Result;
}

INT CDECL
ChpeSnwprintf(PWCHAR Buffer, SIZE_T Count, PCWSTR Format, ...)
{
    va_list Arguments;
    INT Result;

    va_start(Arguments, Format);
    Result = _vsnwprintf(Buffer, Count, Format, Arguments);
    va_end(Arguments);
    return Result;
}

INT CDECL
ChpeSprintf(PCHAR Buffer, PCSTR Format, ...)
{
    va_list Arguments;
    INT Result;

    va_start(Arguments, Format);
    Result = vsprintf(Buffer, Format, Arguments);
    va_end(Arguments);
    return Result;
}

INT CDECL
ChpeSwprintf(PWCHAR Buffer, PCWSTR Format, ...)
{
    va_list Arguments;
    INT Result;

    va_start(Arguments, Format);
    Result = _vsnwprintf(Buffer, MAXLONG, Format, Arguments);
    va_end(Arguments);
    return Result;
}

static const UNICODE_STRING ChpeNativeNtdllName = RTL_CONSTANT_STRING(L"ntdll.dll");
static const UNICODE_STRING ChpeBridgeNtdllName = RTL_CONSTANT_STRING(L"ntdll_chpe.dll");

static BOOLEAN
ChpepIsNativeNtdllName(const UNICODE_STRING *Name)
{
    UNICODE_STRING BaseName;
    USHORT Index;

    if (Name == NULL || Name->Buffer == NULL)
        return FALSE;

    BaseName = *Name;
    BaseName.MaximumLength = BaseName.Length;
    for (Index = Name->Length / sizeof(WCHAR); Index > 0; --Index)
    {
        if (Name->Buffer[Index - 1] == L'\\' || Name->Buffer[Index - 1] == L'/')
        {
            BaseName.Buffer += Index;
            BaseName.Length -= Index * sizeof(WCHAR);
            BaseName.MaximumLength = BaseName.Length;
            break;
        }
    }

    return RtlEqualUnicodeString(&BaseName, &ChpeNativeNtdllName, TRUE);
}

static NTSTATUS
ChpepRedirectNativeNtdllProcedure(PVOID NativeBase, PVOID BridgeBase, PVOID NativeProcedure, PVOID *ProcedureAddress)
{
    PIMAGE_EXPORT_DIRECTORY ExportDirectory;
    PULONG Functions;
    PULONG Names;
    PUSHORT NameOrdinals;
    ULONG ExportSize;
    ULONG Index;

    ExportDirectory = RtlImageDirectoryEntryToData(NativeBase, TRUE, IMAGE_DIRECTORY_ENTRY_EXPORT, &ExportSize);
    if (ExportDirectory == NULL || ExportSize < sizeof(*ExportDirectory))
        return STATUS_PROCEDURE_NOT_FOUND;

    Functions = (PULONG)((PUCHAR)NativeBase + ExportDirectory->AddressOfFunctions);
    Names = (PULONG)((PUCHAR)NativeBase + ExportDirectory->AddressOfNames);
    NameOrdinals = (PUSHORT)((PUCHAR)NativeBase + ExportDirectory->AddressOfNameOrdinals);

    for (Index = 0; Index < ExportDirectory->NumberOfNames; ++Index)
    {
        ANSI_STRING ExportName;
        PVOID Candidate;
        NTSTATUS Status;

        if (NameOrdinals[Index] >= ExportDirectory->NumberOfFunctions)
            continue;

        Candidate = (PUCHAR)NativeBase + Functions[NameOrdinals[Index]];
        if (Candidate != NativeProcedure)
            continue;

        RtlInitAnsiString(&ExportName, (PCSTR)NativeBase + Names[Index]);
        Status = LdrGetProcedureAddress(BridgeBase, &ExportName, 0, ProcedureAddress);
        if (NT_SUCCESS(Status))
            return Status;
    }

    return STATUS_PROCEDURE_NOT_FOUND;
}

NTSTATUS NTAPI
ChpeLdrGetDllHandle(PWSTR DllPath, PULONG DllCharacteristics, PUNICODE_STRING DllName, PVOID *DllHandle)
{
    if (ChpepIsNativeNtdllName(DllName))
        DllName = (PUNICODE_STRING)&ChpeBridgeNtdllName;

    return LdrGetDllHandle(DllPath, DllCharacteristics, DllName, DllHandle);
}

NTSTATUS NTAPI
ChpeLdrGetDllHandleEx(ULONG Flags, PWSTR DllPath, PULONG DllCharacteristics, PUNICODE_STRING DllName, PVOID *DllHandle)
{
    if (ChpepIsNativeNtdllName(DllName))
        DllName = (PUNICODE_STRING)&ChpeBridgeNtdllName;

    return LdrGetDllHandleEx(Flags, DllPath, DllCharacteristics, DllName, DllHandle);
}

NTSTATUS NTAPI
ChpeLdrGetProcedureAddress(PVOID BaseAddress, PANSI_STRING Name, ULONG Ordinal, PVOID *ProcedureAddress)
{
    PVOID NativeBase = NULL;
    PVOID BridgeBase = NULL;
    PVOID NativeProcedure;
    NTSTATUS Status;

    LdrGetDllHandle(NULL, NULL, (PUNICODE_STRING)&ChpeNativeNtdllName, &NativeBase);
    LdrGetDllHandle(NULL, NULL, (PUNICODE_STRING)&ChpeBridgeNtdllName, &BridgeBase);
    if (BaseAddress == NativeBase || BaseAddress == BridgeBase)
    {
        if (BridgeBase != NULL)
        {
            Status = LdrGetProcedureAddress(BridgeBase, Name, Ordinal, ProcedureAddress);
            if (NT_SUCCESS(Status))
                return Status;
        }

        if (NativeBase != NULL)
            BaseAddress = NativeBase;
    }

    Status = LdrGetProcedureAddress(BaseAddress, Name, Ordinal, ProcedureAddress);
    if (!NT_SUCCESS(Status) || NativeBase == NULL || BridgeBase == NULL || BaseAddress == BridgeBase)
        return Status;

    NativeProcedure = *ProcedureAddress;
    if (NT_SUCCESS(ChpepRedirectNativeNtdllProcedure(NativeBase, BridgeBase, NativeProcedure, ProcedureAddress)))
        return STATUS_SUCCESS;

    *ProcedureAddress = NativeProcedure;
    return Status;
}

PVOID NTAPI
ChpeLdrResolveDelayLoadedAPI(PVOID ParentBase, PCIMAGE_DELAYLOAD_DESCRIPTOR Descriptor, PDELAYLOAD_FAILURE_DLL_CALLBACK DllHook, PDELAYLOAD_FAILURE_SYSTEM_ROUTINE SystemHook, PIMAGE_THUNK_DATA ThunkAddress, ULONG Flags)
{
    return LdrResolveDelayLoadedAPI(ParentBase, Descriptor, DllHook, SystemHook, ThunkAddress, Flags);
}

NTSTATUS NTAPI
ChpeLdrResolveDelayLoadsFromDll(PVOID ParentBase, PCSTR TargetDllName, ULONG Flags)
{
    return LdrResolveDelayLoadsFromDll(ParentBase, TargetDllName, Flags);
}

NTSTATUS NTAPI
ChpeNtAlertThreadByThreadId(HANDLE ThreadId)
{
    return NtAlertThreadByThreadId(ThreadId);
}

NTSTATUS NTAPI
ChpeNtClose(HANDLE Handle)
{
    return NtClose(Handle);
}

NTSTATUS NTAPI
ChpeNtWaitForAlertByThreadId(PVOID Address, PLARGE_INTEGER Timeout)
{
    return NtWaitForAlertByThreadId(Address, Timeout);
}

NTSTATUS NTAPI
ChpeRtlWaitOnAddress(const VOID *Address, const VOID *CompareAddress, SIZE_T AddressSize, const LARGE_INTEGER *Timeout)
{
    return RtlWaitOnAddress(Address, CompareAddress, AddressSize, Timeout);
}

VOID NTAPI
ChpeRtlWakeAddressAll(const VOID *Address)
{
    RtlWakeAddressAll(Address);
}

VOID NTAPI
ChpeRtlWakeAddressSingle(const VOID *Address)
{
    RtlWakeAddressSingle(Address);
}

SIZE_T NTAPI
ChpeRtlCompareMemory(const VOID *Source1, const VOID *Source2, SIZE_T Length)
{
    return RtlCompareMemory(Source1, Source2, Length);
}

VOID NTAPI
ChpeRtlInitializeSListHead(PSLIST_HEADER SListHead)
{
    RtlInitializeSListHead(SListHead);
}

NTSTATUS NTAPI
ChpeRtlInitializeCriticalSection(PRTL_CRITICAL_SECTION CriticalSection)
{
    return RtlInitializeCriticalSection(CriticalSection);
}

VOID NTAPI
ChpeRtlInitializeConditionVariable(PRTL_CONDITION_VARIABLE ConditionVariable)
{
    RtlInitializeConditionVariable(ConditionVariable);
}

VOID NTAPI
ChpeRtlInitializeSRWLock(PRTL_SRWLOCK Lock)
{
    RtlInitializeSRWLock(Lock);
}

PSLIST_ENTRY NTAPI
ChpeRtlInterlockedFlushSList(PSLIST_HEADER SListHead)
{
    return RtlInterlockedFlushSList(SListHead);
}

PSLIST_ENTRY NTAPI
ChpeRtlInterlockedPopEntrySList(PSLIST_HEADER SListHead)
{
    return RtlInterlockedPopEntrySList(SListHead);
}

PSLIST_ENTRY NTAPI
ChpeRtlInterlockedPushEntrySList(PSLIST_HEADER SListHead, PSLIST_ENTRY SListEntry)
{
    return RtlInterlockedPushEntrySList(SListHead, SListEntry);
}

PSLIST_ENTRY NTAPI
ChpeRtlInterlockedPushListSList(PSLIST_HEADER SListHead, PSLIST_ENTRY List, PSLIST_ENTRY ListEnd, ULONG Count)
{
    return RtlInterlockedPushListSList(SListHead, List, ListEnd, Count);
}

PSLIST_ENTRY NTAPI
ChpeRtlInterlockedPushListSListEx(PSLIST_HEADER SListHead, PSLIST_ENTRY List, PSLIST_ENTRY ListEnd, ULONG Count)
{
    return RtlInterlockedPushListSList(SListHead, List, ListEnd, Count);
}

USHORT NTAPI
ChpeRtlQueryDepthSList(PSLIST_HEADER SListHead)
{
    return RtlQueryDepthSList(SListHead);
}

VOID NTAPI
ChpeRtlMoveMemory(PVOID Destination, const VOID *Source, SIZE_T Length)
{
    RtlMoveMemory(Destination, Source, Length);
}

NTSTATUS NTAPI
ChpeRtlLogUnexpectedCodepath(const ULONG *Codepath)
{
    return RtlLogUnexpectedCodepath(Codepath);
}

VOID NTAPI
ChpeRtlZeroMemory(PVOID Destination, SIZE_T Length)
{
    RtlZeroMemory(Destination, Length);
}

VOID NTAPI
ChpeRtlFillMemory(PVOID Destination, SIZE_T Length, UCHAR Fill)
{
    RtlFillMemory(Destination, Length, Fill);
}

PVOID NTAPI
ChpeRtlPcToFileHeader(PVOID PcValue, PVOID *BaseOfImage)
{
    return RtlPcToFileHeader(PcValue, BaseOfImage);
}

typedef struct _CHPE_VECTORED_HANDLER_ENTRY
{
    LIST_ENTRY ListEntry;
    PVECTORED_EXCEPTION_HANDLER Handler;
    ULONG References;
    BOOLEAN Removed;
} CHPE_VECTORED_HANDLER_ENTRY, *PCHPE_VECTORED_HANDLER_ENTRY;

typedef struct _CHPE_VECTORED_HANDLER_SNAPSHOT
{
    PCHPE_VECTORED_HANDLER_ENTRY Entry;
    PVECTORED_EXCEPTION_HANDLER Handler;
} CHPE_VECTORED_HANDLER_SNAPSHOT, *PCHPE_VECTORED_HANDLER_SNAPSHOT;

static RTL_SRWLOCK ChpeVectoredHandlerLock = { 0 };
static LIST_ENTRY ChpeVectoredHandlerList = { &ChpeVectoredHandlerList, &ChpeVectoredHandlerList };
static PVOID ChpeNativeVectoredHandler;
static RTL_SRWLOCK ChpeVectoredContinueHandlerLock = { 0 };
static LIST_ENTRY ChpeVectoredContinueHandlerList = { &ChpeVectoredContinueHandlerList, &ChpeVectoredContinueHandlerList };
static PVOID ChpeNativeVectoredContinueHandler;

static
VOID
ChpepDereferenceVectoredHandler(PCHPE_VECTORED_HANDLER_ENTRY Entry)
{
    BOOLEAN FreeEntry;

    RtlAcquireSRWLockExclusive(&ChpeVectoredHandlerLock);
    Entry->References--;
    FreeEntry = Entry->Removed && Entry->References == 0;
    RtlReleaseSRWLockExclusive(&ChpeVectoredHandlerLock);

    if (FreeEntry)
        RtlFreeHeap(RtlGetProcessHeap(), 0, Entry);
}

static
VOID
ChpepDereferenceVectoredContinueHandler(PCHPE_VECTORED_HANDLER_ENTRY Entry)
{
    BOOLEAN FreeEntry;

    RtlAcquireSRWLockExclusive(&ChpeVectoredContinueHandlerLock);
    Entry->References--;
    FreeEntry = Entry->Removed && Entry->References == 0;
    RtlReleaseSRWLockExclusive(&ChpeVectoredContinueHandlerLock);

    if (FreeEntry)
        RtlFreeHeap(RtlGetProcessHeap(), 0, Entry);
}

static
LONG
ChpepCallVectoredHandlersX64(PRTL_SRWLOCK Lock,
                             PLIST_ENTRY ListHead,
                             BOOLEAN ContinueHandlers,
                             PEXCEPTION_RECORD ExceptionRecord,
                             PCONTEXT ContextRecord)
{
    PCHPE_VECTORED_HANDLER_SNAPSHOT Snapshot;
    PCHPE_VECTORED_HANDLER_ENTRY Entry;
    EXCEPTION_POINTERS ExceptionPointers;
    PLIST_ENTRY Link;
    SIZE_T Count = 0;
    SIZE_T Index = 0;
    LONG Result = EXCEPTION_CONTINUE_SEARCH;

    RtlAcquireSRWLockShared(Lock);
    for (Link = ListHead->Flink; Link != ListHead; Link = Link->Flink)
        Count++;
    RtlReleaseSRWLockShared(Lock);

    if (!Count || Count > MAXULONG_PTR / sizeof(*Snapshot))
        return EXCEPTION_CONTINUE_SEARCH;

    Snapshot = RtlAllocateHeap(RtlGetProcessHeap(), 0, Count * sizeof(*Snapshot));
    if (!Snapshot)
        return EXCEPTION_CONTINUE_SEARCH;

    RtlAcquireSRWLockExclusive(Lock);
    for (Link = ListHead->Flink; Link != ListHead && Index < Count; Link = Link->Flink)
    {
        Entry = CONTAINING_RECORD(Link, CHPE_VECTORED_HANDLER_ENTRY, ListEntry);
        Entry->References++;
        Snapshot[Index].Entry = Entry;
        Snapshot[Index].Handler = Entry->Handler;
        Index++;
    }
    RtlReleaseSRWLockExclusive(Lock);
    Count = Index;

    ExceptionPointers.ExceptionRecord = ExceptionRecord;
    ExceptionPointers.ContextRecord = ContextRecord;
    for (Index = 0; Index < Count; Index++)
    {
        Result = Snapshot[Index].Handler(&ExceptionPointers);
        if (Result == EXCEPTION_CONTINUE_EXECUTION)
            break;
    }

    for (Index = 0; Index < Count; Index++)
    {
        if (ContinueHandlers)
            ChpepDereferenceVectoredContinueHandler(Snapshot[Index].Entry);
        else
            ChpepDereferenceVectoredHandler(Snapshot[Index].Entry);
    }
    RtlFreeHeap(RtlGetProcessHeap(), 0, Snapshot);
    return Result;
}

LONG
NTAPI
ChpepVectoredExceptionDispatcher(PEXCEPTION_POINTERS NativePointers)
{
    ARM64EC_NT_CONTEXT EcContext;
    LONG Result;

    ChpepContextArm64ToX64(&EcContext, (PARM64_NT_CONTEXT)NativePointers->ContextRecord);
    Result = ChpepCallVectoredHandlersX64(&ChpeVectoredHandlerLock, &ChpeVectoredHandlerList, FALSE, NativePointers->ExceptionRecord, &EcContext.AMD64_Context);

    if (Result == EXCEPTION_CONTINUE_EXECUTION)
        ChpepMergeContextX64ToArm64((PARM64_NT_CONTEXT)NativePointers->ContextRecord, &EcContext);
    return Result;
}

LONG
NTAPI
ChpepVectoredContinueDispatcher(PEXCEPTION_POINTERS NativePointers)
{
    ARM64EC_NT_CONTEXT EcContext;
    LONG Result;

    ChpepContextArm64ToX64(&EcContext, (PARM64_NT_CONTEXT)NativePointers->ContextRecord);
    Result = ChpepCallVectoredHandlersX64(&ChpeVectoredContinueHandlerLock, &ChpeVectoredContinueHandlerList, TRUE, NativePointers->ExceptionRecord, &EcContext.AMD64_Context);

    if (Result == EXCEPTION_CONTINUE_EXECUTION)
        ChpepMergeContextX64ToArm64((PARM64_NT_CONTEXT)NativePointers->ContextRecord, &EcContext);
    return Result;
}

/*
 * Native ARM64 callers require x23, x24, and x28 to survive a callback, but
 * those registers are unavailable to ARM64EC and may be used by the emulator.
 * Preserve them at the native NTDLL callback boundary instead of adding work
 * to every ARM64EC/x64 transition.
 */
#ifdef __clang__
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Winline-asm"
#endif
static LONG NTAPI __attribute__((naked, used))
ChpepNativeVectoredExceptionDispatcher(PEXCEPTION_POINTERS NativePointers)
{
    __asm__ volatile(
        ".seh_proc \"#ChpepNativeVectoredExceptionDispatcher\"\n"
        "stp x23, x24, [sp, #-0x20]!\n"
        ".seh_save_regp_x x23, 0x20\n"
        "str x28, [sp, #0x10]\n"
        ".seh_save_reg x28, 0x10\n"
        "str x30, [sp, #0x18]\n"
        ".seh_save_reg x30, 0x18\n"
        ".seh_endprologue\n"
        "bl \"#ChpepVectoredExceptionDispatcher\"\n"
        "ldr x30, [sp, #0x18]\n"
        "ldr x28, [sp, #0x10]\n"
        "ldp x23, x24, [sp], #0x20\n"
        "ret\n"
        ".seh_endproc\n");
}

static LONG NTAPI __attribute__((naked, used))
ChpepNativeVectoredContinueDispatcher(PEXCEPTION_POINTERS NativePointers)
{
    __asm__ volatile(
        ".seh_proc \"#ChpepNativeVectoredContinueDispatcher\"\n"
        "stp x23, x24, [sp, #-0x20]!\n"
        ".seh_save_regp_x x23, 0x20\n"
        "str x28, [sp, #0x10]\n"
        ".seh_save_reg x28, 0x10\n"
        "str x30, [sp, #0x18]\n"
        ".seh_save_reg x30, 0x18\n"
        ".seh_endprologue\n"
        "bl \"#ChpepVectoredContinueDispatcher\"\n"
        "ldr x30, [sp, #0x18]\n"
        "ldr x28, [sp, #0x10]\n"
        "ldp x23, x24, [sp], #0x20\n"
        "ret\n"
        ".seh_endproc\n");
}
#ifdef __clang__
#pragma clang diagnostic pop
#endif

PVOID NTAPI
ChpeRtlAddVectoredExceptionHandler(ULONG FirstHandler, PVECTORED_EXCEPTION_HANDLER Handler)
{
    PCHPE_VECTORED_HANDLER_ENTRY Entry;

    if (!Handler)
        return NULL;

    Entry = RtlAllocateHeap(RtlGetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*Entry));
    if (!Entry)
        return NULL;
    Entry->Handler = Handler;

    RtlAcquireSRWLockExclusive(&ChpeVectoredHandlerLock);
    if (!ChpeNativeVectoredHandler)
        ChpeNativeVectoredHandler = RtlAddVectoredExceptionHandler(FirstHandler, ChpepNativeVectoredExceptionDispatcher);
    if (!ChpeNativeVectoredHandler)
    {
        RtlReleaseSRWLockExclusive(&ChpeVectoredHandlerLock);
        RtlFreeHeap(RtlGetProcessHeap(), 0, Entry);
        return NULL;
    }

    if (FirstHandler)
        InsertHeadList(&ChpeVectoredHandlerList, &Entry->ListEntry);
    else
        InsertTailList(&ChpeVectoredHandlerList, &Entry->ListEntry);
    RtlReleaseSRWLockExclusive(&ChpeVectoredHandlerLock);
    return Entry;
}

ULONG NTAPI
ChpeRtlRemoveVectoredExceptionHandler(PVOID HandlerHandle)
{
    PCHPE_VECTORED_HANDLER_ENTRY Entry;
    PLIST_ENTRY Link;
    BOOLEAN FreeEntry = FALSE;
    ULONG Found = FALSE;

    RtlAcquireSRWLockExclusive(&ChpeVectoredHandlerLock);
    for (Link = ChpeVectoredHandlerList.Flink; Link != &ChpeVectoredHandlerList; Link = Link->Flink)
    {
        Entry = CONTAINING_RECORD(Link, CHPE_VECTORED_HANDLER_ENTRY, ListEntry);
        if (Entry != HandlerHandle)
            continue;

        RemoveEntryList(&Entry->ListEntry);
        Entry->Removed = TRUE;
        FreeEntry = Entry->References == 0;
        Found = TRUE;
        break;
    }
    RtlReleaseSRWLockExclusive(&ChpeVectoredHandlerLock);

    if (FreeEntry)
        RtlFreeHeap(RtlGetProcessHeap(), 0, Entry);
    return Found;
}

PVOID NTAPI
ChpeRtlAddVectoredContinueHandler(ULONG FirstHandler, PVECTORED_EXCEPTION_HANDLER Handler)
{
    PCHPE_VECTORED_HANDLER_ENTRY Entry;

    if (!Handler)
        return NULL;

    Entry = RtlAllocateHeap(RtlGetProcessHeap(), HEAP_ZERO_MEMORY, sizeof(*Entry));
    if (!Entry)
        return NULL;
    Entry->Handler = Handler;

    RtlAcquireSRWLockExclusive(&ChpeVectoredContinueHandlerLock);
    if (!ChpeNativeVectoredContinueHandler)
        ChpeNativeVectoredContinueHandler = RtlAddVectoredContinueHandler(FirstHandler, ChpepNativeVectoredContinueDispatcher);
    if (!ChpeNativeVectoredContinueHandler)
    {
        RtlReleaseSRWLockExclusive(&ChpeVectoredContinueHandlerLock);
        RtlFreeHeap(RtlGetProcessHeap(), 0, Entry);
        return NULL;
    }

    if (FirstHandler)
        InsertHeadList(&ChpeVectoredContinueHandlerList, &Entry->ListEntry);
    else
        InsertTailList(&ChpeVectoredContinueHandlerList, &Entry->ListEntry);
    RtlReleaseSRWLockExclusive(&ChpeVectoredContinueHandlerLock);
    return Entry;
}

ULONG NTAPI
ChpeRtlRemoveVectoredContinueHandler(PVOID HandlerHandle)
{
    PCHPE_VECTORED_HANDLER_ENTRY Entry;
    PLIST_ENTRY Link;
    BOOLEAN FreeEntry = FALSE;
    ULONG Found = FALSE;

    RtlAcquireSRWLockExclusive(&ChpeVectoredContinueHandlerLock);
    for (Link = ChpeVectoredContinueHandlerList.Flink; Link != &ChpeVectoredContinueHandlerList; Link = Link->Flink)
    {
        Entry = CONTAINING_RECORD(Link, CHPE_VECTORED_HANDLER_ENTRY, ListEntry);
        if (Entry != HandlerHandle)
            continue;

        RemoveEntryList(&Entry->ListEntry);
        Entry->Removed = TRUE;
        FreeEntry = Entry->References == 0;
        Found = TRUE;
        break;
    }
    RtlReleaseSRWLockExclusive(&ChpeVectoredContinueHandlerLock);

    if (FreeEntry)
        RtlFreeHeap(RtlGetProcessHeap(), 0, Entry);
    return Found;
}

ULONG NTAPI
ChpeRtlGetCurrentProcessorNumber(VOID)
{
    return RtlGetCurrentProcessorNumber();
}

VOID NTAPI
ChpeRtlGetCurrentProcessorNumberEx(PPROCESSOR_NUMBER ProcessorNumber)
{
    RtlGetCurrentProcessorNumberEx(ProcessorNumber);
}

BOOLEAN NTAPI
ChpeRtlGetProductInfo(DWORD MajorVersion, DWORD MinorVersion, DWORD ServicePackMajor, DWORD ServicePackMinor, PDWORD ProductType)
{
    return RtlGetProductInfo(MajorVersion, MinorVersion, ServicePackMajor, ServicePackMinor, ProductType);
}

USHORT NTAPI
ChpeRtlCaptureStackBackTrace(ULONG FramesToSkip, ULONG FramesToCapture, PVOID *BackTrace, PULONG BackTraceHash)
{
    if (FramesToSkip == MAXULONG)
        return 0;

    return RtlCaptureStackBackTrace(FramesToSkip + 1, FramesToCapture, BackTrace, BackTraceHash);
}

PVOID NTAPI
ChpeRtlDecodePointer(PVOID Pointer)
{
    return RtlDecodePointer(Pointer);
}

PVOID NTAPI
ChpeRtlDecodeSystemPointer(PVOID Pointer)
{
    return RtlDecodeSystemPointer(Pointer);
}

PVOID NTAPI
ChpeRtlEncodePointer(PVOID Pointer)
{
    return RtlEncodePointer(Pointer);
}

PVOID NTAPI
ChpeRtlEncodeSystemPointer(PVOID Pointer)
{
    return RtlEncodeSystemPointer(Pointer);
}

DECLSPEC_NORETURN VOID NTAPI
ChpeRtlExitUserThread(NTSTATUS Status)
{
    RtlExitUserThread(Status);
    __builtin_unreachable();
}

BOOLEAN NTAPI
ChpeRtlTryAcquireSRWLockExclusive(PRTL_SRWLOCK Lock)
{
    return RtlTryAcquireSRWLockExclusive(Lock);
}

BOOLEAN NTAPI
ChpeRtlTryAcquireSRWLockShared(PRTL_SRWLOCK Lock)
{
    return RtlTryAcquireSRWLockShared(Lock);
}

VOID NTAPI
ChpeRtlAcquireSRWLockExclusive(PRTL_SRWLOCK Lock)
{
    RtlAcquireSRWLockExclusive(Lock);
}

VOID NTAPI
ChpeRtlAcquireSRWLockShared(PRTL_SRWLOCK Lock)
{
    RtlAcquireSRWLockShared(Lock);
}

VOID NTAPI
ChpeRtlReleaseSRWLockExclusive(PRTL_SRWLOCK Lock)
{
    RtlReleaseSRWLockExclusive(Lock);
}

VOID NTAPI
ChpeRtlReleaseSRWLockShared(PRTL_SRWLOCK Lock)
{
    RtlReleaseSRWLockShared(Lock);
}

VOID NTAPI
ChpeRtlWakeAllConditionVariable(PRTL_CONDITION_VARIABLE ConditionVariable)
{
    RtlWakeAllConditionVariable(ConditionVariable);
}

VOID NTAPI
ChpeRtlWakeConditionVariable(PRTL_CONDITION_VARIABLE ConditionVariable)
{
    RtlWakeConditionVariable(ConditionVariable);
}

VOID WINAPI
ChpeTpCancelAsyncIoOperation(TP_IO *Io)
{
    TpCancelAsyncIoOperation(Io);
}

VOID WINAPI
ChpeTpCallbackLeaveCriticalSectionOnCompletion(TP_CALLBACK_INSTANCE *Instance, RTL_CRITICAL_SECTION *CriticalSection)
{
    TpCallbackLeaveCriticalSectionOnCompletion(Instance, CriticalSection);
}

VOID WINAPI
ChpeTpCallbackReleaseMutexOnCompletion(TP_CALLBACK_INSTANCE *Instance, HANDLE Mutex)
{
    TpCallbackReleaseMutexOnCompletion(Instance, Mutex);
}

VOID WINAPI
ChpeTpCallbackReleaseSemaphoreOnCompletion(TP_CALLBACK_INSTANCE *Instance, HANDLE Semaphore, DWORD Count)
{
    TpCallbackReleaseSemaphoreOnCompletion(Instance, Semaphore, Count);
}

VOID WINAPI
ChpeTpCallbackSetEventOnCompletion(TP_CALLBACK_INSTANCE *Instance, HANDLE Event)
{
    TpCallbackSetEventOnCompletion(Instance, Event);
}

VOID WINAPI
ChpeTpCallbackUnloadDllOnCompletion(TP_CALLBACK_INSTANCE *Instance, HMODULE Module)
{
    TpCallbackUnloadDllOnCompletion(Instance, Module);
}

VOID WINAPI
ChpeTpDisassociateCallback(TP_CALLBACK_INSTANCE *Instance)
{
    TpDisassociateCallback(Instance);
}

BOOL WINAPI
ChpeTpIsTimerSet(TP_TIMER *Timer)
{
    return TpIsTimerSet(Timer);
}

VOID WINAPI
ChpeTpPostWork(TP_WORK *Work)
{
    TpPostWork(Work);
}

VOID WINAPI
ChpeTpReleaseCleanupGroup(TP_CLEANUP_GROUP *CleanupGroup)
{
    TpReleaseCleanupGroup(CleanupGroup);
}

VOID WINAPI
ChpeTpReleaseCleanupGroupMembers(TP_CLEANUP_GROUP *CleanupGroup, BOOL CancelPending, PVOID CleanupParameter)
{
    TpReleaseCleanupGroupMembers(CleanupGroup, CancelPending, CleanupParameter);
}

VOID WINAPI
ChpeTpReleaseIoCompletion(TP_IO *Io)
{
    TpReleaseIoCompletion(Io);
}

VOID WINAPI
ChpeTpReleasePool(TP_POOL *Pool)
{
    TpReleasePool(Pool);
}

VOID WINAPI
ChpeTpReleaseTimer(TP_TIMER *Timer)
{
    TpReleaseTimer(Timer);
}

VOID WINAPI
ChpeTpReleaseWait(TP_WAIT *Wait)
{
    TpReleaseWait(Wait);
}

VOID WINAPI
ChpeTpReleaseWork(TP_WORK *Work)
{
    TpReleaseWork(Work);
}

VOID WINAPI
ChpeTpSetPoolMaxThreads(TP_POOL *Pool, DWORD MaximumThreads)
{
    TpSetPoolMaxThreads(Pool, MaximumThreads);
}

BOOL WINAPI
ChpeTpSetPoolMinThreads(TP_POOL *Pool, DWORD MinimumThreads)
{
    return TpSetPoolMinThreads(Pool, MinimumThreads);
}

VOID WINAPI
ChpeTpSetTimer(TP_TIMER *Timer, LARGE_INTEGER *Timeout, LONG Period, LONG WindowLength)
{
    TpSetTimer(Timer, Timeout, Period, WindowLength);
}

BOOL WINAPI
ChpeTpSetTimerEx(TP_TIMER *Timer, LARGE_INTEGER *Timeout, LONG Period, LONG WindowLength)
{
    return TpSetTimerEx(Timer, Timeout, Period, WindowLength);
}

VOID WINAPI
ChpeTpSetWait(TP_WAIT *Wait, HANDLE Handle, LARGE_INTEGER *Timeout)
{
    TpSetWait(Wait, Handle, Timeout);
}

BOOL WINAPI
ChpeTpSetWaitEx(TP_WAIT *Wait, HANDLE Handle, LARGE_INTEGER *Timeout, PVOID Reserved)
{
    return TpSetWaitEx(Wait, Handle, Timeout, Reserved);
}

VOID WINAPI
ChpeTpStartAsyncIoOperation(TP_IO *Io)
{
    TpStartAsyncIoOperation(Io);
}

VOID WINAPI
ChpeTpWaitForIoCompletion(TP_IO *Io, BOOL CancelPending)
{
    TpWaitForIoCompletion(Io, CancelPending);
}

VOID WINAPI
ChpeTpWaitForTimer(TP_TIMER *Timer, BOOL CancelPending)
{
    TpWaitForTimer(Timer, CancelPending);
}

VOID WINAPI
ChpeTpWaitForWait(TP_WAIT *Wait, BOOL CancelPending)
{
    TpWaitForWait(Wait, CancelPending);
}

VOID WINAPI
ChpeTpWaitForWork(TP_WORK *Work, BOOL CancelPending)
{
    TpWaitForWork(Work, CancelPending);
}

ULONGLONG WINAPI
ChpeVerSetConditionMask(ULONGLONG ConditionMask, DWORD TypeMask, BYTE Condition)
{
    return VerSetConditionMask(ConditionMask, TypeMask, Condition);
}

PVOID NTAPI
ChpeRtlAllocateHeap(PVOID HeapHandle, ULONG Flags, SIZE_T Size)
{
    return RtlAllocateHeap(HeapHandle, Flags, Size);
}

LONG NTAPI
ChpeRtlCompareUnicodeString(PCUNICODE_STRING String1, PCUNICODE_STRING String2, BOOLEAN CaseInSensitive)
{
    return RtlCompareUnicodeString(String1, String2, CaseInSensitive);
}

VOID NTAPI
ChpeRtlCopyUnicodeString(PUNICODE_STRING DestinationString, PCUNICODE_STRING SourceString)
{
    RtlCopyUnicodeString(DestinationString, SourceString);
}

NTSTATUS NTAPI
ChpeRtlDeleteCriticalSection(PRTL_CRITICAL_SECTION CriticalSection)
{
    return RtlDeleteCriticalSection(CriticalSection);
}

NTSTATUS NTAPI
ChpeRtlEnterCriticalSection(PRTL_CRITICAL_SECTION CriticalSection)
{
    return RtlEnterCriticalSection(CriticalSection);
}

ULONG NTAPI
ChpeRtlSetCriticalSectionSpinCount(PRTL_CRITICAL_SECTION CriticalSection, ULONG SpinCount)
{
    return RtlSetCriticalSectionSpinCount(CriticalSection, SpinCount);
}

LOGICAL NTAPI
ChpeRtlTryEnterCriticalSection(PRTL_CRITICAL_SECTION CriticalSection)
{
    return RtlTryEnterCriticalSection(CriticalSection);
}

ULONG NTAPI
ChpeRtlGetLastWin32Error(VOID)
{
    return RtlGetLastWin32Error();
}

NTSTATUS NTAPI
ChpeRtlGetVersion(PRTL_OSVERSIONINFOW VersionInformation)
{
    return RtlGetVersion(VersionInformation);
}

VOID NTAPI
ChpeRtlInitUnicodeString(PUNICODE_STRING DestinationString, PCWSTR SourceString)
{
    RtlInitUnicodeString(DestinationString, SourceString);
}

VOID NTAPI
ChpeRtlSetLastWin32Error(ULONG Win32Error)
{
    RtlSetLastWin32Error(Win32Error);
}

VOID NTAPI
ChpeRtlRestoreLastWin32Error(ULONG Win32Error)
{
    RtlRestoreLastWin32Error(Win32Error);
}

ULONG NTAPI
ChpeRtlNtStatusToDosError(NTSTATUS Status)
{
    return RtlNtStatusToDosError(Status);
}

VOID NTAPI
ChpeRtlRunOnceInitialize(PRTL_RUN_ONCE RunOnce)
{
    RtlRunOnceInitialize(RunOnce);
}

ULONG NTAPI
ChpeRtlRandom(PULONG Seed)
{
    return RtlRandom(Seed);
}

BOOLEAN NTAPI
ChpeRtlFreeHeap(HANDLE HeapHandle, ULONG Flags, PVOID Pointer)
{
    return RtlFreeHeap(HeapHandle, Flags, Pointer);
}

NTSTATUS NTAPI
ChpeRtlFlsAlloc(PFLS_CALLBACK_FUNCTION Callback, PULONG Index)
{
    return RtlFlsAlloc(Callback, Index);
}

NTSTATUS NTAPI
ChpeRtlFlsFree(ULONG Index)
{
    return RtlFlsFree(Index);
}

NTSTATUS NTAPI
ChpeRtlFlsGetValue(ULONG Index, PVOID *Data)
{
    return RtlFlsGetValue(Index, Data);
}

NTSTATUS NTAPI
ChpeRtlFlsSetValue(ULONG Index, PVOID Data)
{
    return RtlFlsSetValue(Index, Data);
}

NTSTATUS NTAPI
ChpeRtlLeaveCriticalSection(PRTL_CRITICAL_SECTION CriticalSection)
{
    return RtlLeaveCriticalSection(CriticalSection);
}

PVOID NTAPI
ChpeRtlReAllocateHeap(HANDLE HeapHandle, ULONG Flags, PVOID Pointer, SIZE_T Size)
{
    return RtlReAllocateHeap(HeapHandle, Flags, Pointer, Size);
}

SIZE_T NTAPI
ChpeRtlSizeHeap(PVOID HeapHandle, ULONG Flags, PVOID Pointer)
{
    return RtlSizeHeap(HeapHandle, Flags, Pointer);
}

BOOLEAN CDECL
ChpeRtlAddFunctionTable(PRUNTIME_FUNCTION FunctionTable, ULONG EntryCount, ULONG_PTR BaseAddress)
{
    return ChpepAmd64AddFunctionTable(FunctionTable, EntryCount, BaseAddress);
}

DWORD NTAPI
ChpeRtlAddGrowableFunctionTable(PVOID *DynamicTable, PRUNTIME_FUNCTION FunctionTable, DWORD EntryCount, DWORD MaximumEntryCount, ULONG_PTR RangeBase, ULONG_PTR RangeEnd)
{
    return ChpepAmd64AddGrowableFunctionTable(DynamicTable, FunctionTable, EntryCount, MaximumEntryCount, RangeBase, RangeEnd);
}

BOOLEAN CDECL
ChpeRtlDeleteFunctionTable(PRUNTIME_FUNCTION FunctionTable)
{
    return ChpepAmd64DeleteFunctionTable(FunctionTable);
}

VOID NTAPI
ChpeRtlDeleteGrowableFunctionTable(PVOID DynamicTable)
{
    ChpepAmd64DeleteGrowableFunctionTable(DynamicTable);
}

PLIST_ENTRY NTAPI
ChpeRtlGetFunctionTableListHead(VOID)
{
    return ChpepAmd64GetFunctionTableListHead();
}

VOID NTAPI
ChpeRtlGrowFunctionTable(PVOID DynamicTable, DWORD NewEntryCount)
{
    ChpepAmd64GrowFunctionTable(DynamicTable, NewEntryCount);
}

BOOLEAN CDECL
ChpeRtlInstallFunctionTableCallback(DWORD64 TableIdentifier, DWORD64 BaseAddress, DWORD Length, PGET_RUNTIME_FUNCTION_CALLBACK Callback, PVOID Context, PCWSTR OutOfProcessCallbackDll)
{
    return ChpepAmd64InstallFunctionTableCallback(TableIdentifier, BaseAddress, Length, Callback, Context, OutOfProcessCallbackDll);
}

PVOID CDECL
ChpeMemcpy(PVOID Destination, const VOID *Source, SIZE_T Length)
{
    return memmove(Destination, Source, Length);
}

VOID CDECL
ChpeLocalUnwind(PVOID TargetFrame, PVOID TargetIp)
{
    ChpeRtlUnwind(TargetFrame, TargetIp, NULL, NULL);
}

static VOID
ChpepContinueToGuestIfNeeded(PCONTEXT ContextRecord)
{
    if (RtlIsEcCode(((PARM64EC_NT_CONTEXT)ContextRecord)->Pc))
        return;

    if (!ChpeCanContinueToGuest())
        return;

    ChpeContinueToGuest(ContextRecord);
}

NTSTATUS NTAPI
ChpeNtContinue(PCONTEXT ContextRecord, BOOLEAN Alertable)
{
    ARM64_NT_CONTEXT ArmContext;

    ChpepContinueToGuestIfNeeded(ContextRecord);
    ChpepContextX64ToArm64(&ArmContext, (PARM64EC_NT_CONTEXT)ContextRecord);
    return NtContinue((PCONTEXT)&ArmContext, Alertable);
}

NTSTATUS NTAPI
ChpeNtContinueEx(PCONTEXT ContextRecord, PKCONTINUE_ARGUMENT ContinueArgument)
{
    ARM64_NT_CONTEXT ArmContext;

    ChpepContinueToGuestIfNeeded(ContextRecord);
    ChpepContextX64ToArm64(&ArmContext, (PARM64EC_NT_CONTEXT)ContextRecord);
    return NtContinueEx((PCONTEXT)&ArmContext, ContinueArgument);
}

NTSTATUS NTAPI
ChpeNtGetContextThread(HANDLE ThreadHandle, PCONTEXT ContextRecord)
{
    ARM64_NT_CONTEXT ArmContext;
    NTSTATUS Status;

    RtlZeroMemory(&ArmContext, sizeof(ArmContext));
    ArmContext.ContextFlags = ChpepContextFlagsX64ToArm64(ContextRecord->ContextFlags);
    Status = NtGetContextThread(ThreadHandle, (PCONTEXT)&ArmContext);
    if (NT_SUCCESS(Status))
        ChpepContextArm64ToX64((PARM64EC_NT_CONTEXT)ContextRecord, &ArmContext);
    return Status;
}

NTSTATUS NTAPI
ChpeNtRaiseException(PEXCEPTION_RECORD ExceptionRecord, PCONTEXT ContextRecord, BOOLEAN FirstChance)
{
    ARM64_NT_CONTEXT ArmContext;

    ChpepContextX64ToArm64(&ArmContext, (PARM64EC_NT_CONTEXT)ContextRecord);
    return NtRaiseException(ExceptionRecord, (PCONTEXT)&ArmContext, FirstChance);
}

NTSTATUS NTAPI
ChpeNtSetContextThread(HANDLE ThreadHandle, PCONTEXT ContextRecord)
{
    ARM64_NT_CONTEXT ArmContext;

    ChpepContextX64ToArm64(&ArmContext, (PARM64EC_NT_CONTEXT)ContextRecord);
    return NtSetContextThread(ThreadHandle, (PCONTEXT)&ArmContext);
}

NTSTATUS NTAPI
ChpeNtTerminateThread(HANDLE ThreadHandle, NTSTATUS ExitStatus)
{
    return NtTerminateThread(ThreadHandle, ExitStatus);
}

NTSTATUS NTAPI
ChpeNtAdjustPrivilegesToken(HANDLE TokenHandle, BOOLEAN DisableAllPrivileges, PTOKEN_PRIVILEGES NewState, ULONG BufferLength, PTOKEN_PRIVILEGES PreviousState, PULONG ReturnLength)
{
    return NtAdjustPrivilegesToken(TokenHandle, DisableAllPrivileges, NewState, BufferLength, PreviousState, ReturnLength);
}

NTSTATUS NTAPI
ChpeNtAllocateVirtualMemory(HANDLE ProcessHandle, PVOID *BaseAddress, ULONG_PTR ZeroBits, PSIZE_T RegionSize, ULONG AllocationType, ULONG Protect)
{
    return NtAllocateVirtualMemory(ProcessHandle, BaseAddress, ZeroBits, RegionSize, AllocationType, Protect);
}

NTSTATUS NTAPI
ChpeNtAllocateVirtualMemoryEx(HANDLE ProcessHandle, PVOID *BaseAddress, PSIZE_T RegionSize, ULONG AllocationType, ULONG Protect, PMEM_EXTENDED_PARAMETER ExtendedParameters, ULONG ExtendedParameterCount)
{
    return NtAllocateVirtualMemoryEx(ProcessHandle, BaseAddress, RegionSize, AllocationType, Protect, ExtendedParameters, ExtendedParameterCount);
}

NTSTATUS NTAPI
ChpeNtFlushInstructionCache(HANDLE ProcessHandle, PVOID BaseAddress, SIZE_T Size)
{
    return NtFlushInstructionCache(ProcessHandle, BaseAddress, Size);
}

NTSTATUS NTAPI
ChpeNtFlushProcessWriteBuffers(VOID)
{
    return NtFlushProcessWriteBuffers();
}

NTSTATUS NTAPI
ChpeNtFreeVirtualMemory(HANDLE ProcessHandle, PVOID *BaseAddress, PSIZE_T RegionSize, ULONG FreeType)
{
    return NtFreeVirtualMemory(ProcessHandle, BaseAddress, RegionSize, FreeType);
}

NTSTATUS NTAPI
ChpeNtMapViewOfSection(HANDLE SectionHandle, HANDLE ProcessHandle, PVOID *BaseAddress, ULONG_PTR ZeroBits, SIZE_T CommitSize, PLARGE_INTEGER SectionOffset, PSIZE_T ViewSize, SECTION_INHERIT InheritDisposition, ULONG AllocationType, ULONG Protect)
{
    return NtMapViewOfSection(SectionHandle, ProcessHandle, BaseAddress, ZeroBits, CommitSize, SectionOffset, ViewSize, InheritDisposition, AllocationType, Protect);
}

NTSTATUS NTAPI
ChpeNtMapViewOfSectionEx(HANDLE SectionHandle, HANDLE ProcessHandle, PVOID *BaseAddress, PLARGE_INTEGER SectionOffset, PSIZE_T ViewSize, ULONG AllocationType, ULONG Protect, PMEM_EXTENDED_PARAMETER ExtendedParameters, ULONG ExtendedParameterCount)
{
    return NtMapViewOfSectionEx(SectionHandle, ProcessHandle, BaseAddress, SectionOffset, ViewSize, AllocationType, Protect, ExtendedParameters, ExtendedParameterCount);
}

NTSTATUS NTAPI
ChpeNtOpenFile(PHANDLE FileHandle, ACCESS_MASK DesiredAccess, POBJECT_ATTRIBUTES ObjectAttributes, PIO_STATUS_BLOCK IoStatusBlock, ULONG ShareAccess, ULONG OpenOptions)
{
    return NtOpenFile(FileHandle, DesiredAccess, ObjectAttributes, IoStatusBlock, ShareAccess, OpenOptions);
}

NTSTATUS NTAPI
ChpeNtProtectVirtualMemory(HANDLE ProcessHandle, PVOID *BaseAddress, PSIZE_T RegionSize, ULONG NewProtect, PULONG OldProtect)
{
    return NtProtectVirtualMemory(ProcessHandle, BaseAddress, RegionSize, NewProtect, OldProtect);
}

NTSTATUS NTAPI
ChpeNtQueryInformationFile(HANDLE FileHandle, PIO_STATUS_BLOCK IoStatusBlock, PVOID FileInformation, ULONG Length, FILE_INFORMATION_CLASS FileInformationClass)
{
    return NtQueryInformationFile(FileHandle, IoStatusBlock, FileInformation, Length, FileInformationClass);
}

NTSTATUS NTAPI
ChpeNtQueryObject(HANDLE Handle, OBJECT_INFORMATION_CLASS ObjectInformationClass, PVOID ObjectInformation, ULONG ObjectInformationLength, PULONG ReturnLength)
{
    return NtQueryObject(Handle, ObjectInformationClass, ObjectInformation, ObjectInformationLength, ReturnLength);
}

NTSTATUS NTAPI
ChpeNtQuerySystemInformation(SYSTEM_INFORMATION_CLASS SystemInformationClass, PVOID SystemInformation, ULONG SystemInformationLength, PULONG ReturnLength)
{
    return NtQuerySystemInformation(SystemInformationClass, SystemInformation, SystemInformationLength, ReturnLength);
}

NTSTATUS NTAPI
ChpeNtReadFile(HANDLE FileHandle, HANDLE Event, PIO_APC_ROUTINE ApcRoutine, PVOID ApcContext, PIO_STATUS_BLOCK IoStatusBlock, PVOID Buffer, ULONG Length, PLARGE_INTEGER ByteOffset, PULONG Key)
{
    return NtReadFile(FileHandle, Event, ApcRoutine, ApcContext, IoStatusBlock, Buffer, Length, ByteOffset, Key);
}

NTSTATUS NTAPI
ChpeNtTerminateProcess(HANDLE ProcessHandle, NTSTATUS ExitStatus)
{
    return NtTerminateProcess(ProcessHandle, ExitStatus);
}

NTSTATUS NTAPI
ChpeNtUnmapViewOfSection(HANDLE ProcessHandle, PVOID BaseAddress)
{
    return NtUnmapViewOfSection(ProcessHandle, BaseAddress);
}

NTSTATUS NTAPI
ChpeNtUnmapViewOfSectionEx(HANDLE ProcessHandle, PVOID BaseAddress, ULONG Flags)
{
    return NtUnmapViewOfSectionEx(ProcessHandle, BaseAddress, Flags);
}

NTSTATUS NTAPI
ChpeNtWriteFile(HANDLE FileHandle, HANDLE Event, PIO_APC_ROUTINE ApcRoutine, PVOID ApcContext, PIO_STATUS_BLOCK IoStatusBlock, PVOID Buffer, ULONG Length, PLARGE_INTEGER ByteOffset, PULONG Key)
{
    return NtWriteFile(FileHandle, Event, ApcRoutine, ApcContext, IoStatusBlock, Buffer, Length, ByteOffset, Key);
}

NTSTATUS NTAPI
ChpeNtWriteVirtualMemory(HANDLE ProcessHandle, PVOID BaseAddress, PVOID Buffer, SIZE_T Size, PSIZE_T BytesWritten)
{
    return NtWriteVirtualMemory(ProcessHandle, BaseAddress, Buffer, Size, BytesWritten);
}

BOOLEAN NTAPI
ChpeRtlIsProcessorFeaturePresent(ULONG ProcessorFeature)
{
    return ChpeIsProcessorFeaturePresent(ProcessorFeature);
}

typedef PVOID (CALLBACK *PCHPE_CONSOLIDATE_CALLBACK)(PEXCEPTION_RECORD ExceptionRecord);

C_ASSERT(sizeof(CONTEXT) == 0x4d0);

/*
 * Invoke a consolidation callback from an EC-context frame.  A nested C++
 * unwind must see the supplied context rather than reprocessing frames that
 * the outer unwind has already consumed.
 */
static VOID __attribute__((naked, noreturn, used))
ChpepConsolidateCallback(PCONTEXT ContextRecord, PCHPE_CONSOLIDATE_CALLBACK Callback, PEXCEPTION_RECORD ExceptionRecord)
{
    __asm__ volatile(
        ".seh_proc \"#ChpepConsolidateCallback\"\n"
        "stp x29, x30, [sp, #-16]!\n"
        ".seh_save_fplr_x 16\n"
        "sub sp, sp, #0x4d0\n"
        ".seh_stackalloc 0x4d0\n"
        ".seh_endprologue\n"
        "mov x4, sp\n"
        "mov x5, #0x4d0/16\n"
        "1:\n"
        "ldp x6, x7, [x0], #16\n"
        "stp x6, x7, [x4], #16\n"
        "subs x5, x5, #1\n"
        "b.ne 1b\n"
        "mov x0, x2\n"
        "b \"#ChpepInvokeConsolidateCallback\"\n"
        ".seh_endproc\n"
        ".seh_proc \"#ChpepInvokeConsolidateCallback\"\n"
        "\"#ChpepInvokeConsolidateCallback\":\n"
        ".seh_ec_context\n"
        ".seh_endprologue\n"
        "mov x11, x1\n"
        "adr x10, $iexit_thunk$cdecl$i8$i8\n"
        "adrp x16, __os_arm64x_dispatch_icall\n"
        "ldr x16, [x16, #:lo12:__os_arm64x_dispatch_icall]\n"
        "blr x16\n"
        "blr x11\n"
        "str x0, [sp, #0xf8]\n"
        "mov x0, sp\n"
        "mov w1, #0\n"
        "bl \"#ChpeNtContinue\"\n"
        "bl \"#RtlRaiseStatus\"\n"
        "brk #1\n"
        ".seh_endproc\n");
}

VOID CDECL
ChpeRtlRestoreContext(PCONTEXT ContextRecord, PEXCEPTION_RECORD ExceptionRecord)
{
    NTSTATUS Status;

    if (ExceptionRecord != NULL)
    {
        if (ExceptionRecord->ExceptionCode == STATUS_UNWIND_CONSOLIDATE && ExceptionRecord->NumberParameters >= 1)
        {
            PCHPE_CONSOLIDATE_CALLBACK Consolidate = (PCHPE_CONSOLIDATE_CALLBACK)ExceptionRecord->ExceptionInformation[0];

            ChpepConsolidateCallback(ContextRecord, Consolidate, ExceptionRecord);
        }
        else if (ExceptionRecord->ExceptionCode == STATUS_LONGJUMP && ExceptionRecord->NumberParameters >= 1)
        {
            const _JUMP_BUFFER *JumpBuffer = (const _JUMP_BUFFER *)ExceptionRecord->ExceptionInformation[0];

            ContextRecord->Rbx = JumpBuffer->Rbx;
            ContextRecord->Rsp = JumpBuffer->Rsp;
            ContextRecord->Rbp = JumpBuffer->Rbp;
            ContextRecord->Rsi = JumpBuffer->Rsi;
            ContextRecord->Rdi = JumpBuffer->Rdi;
            ContextRecord->R12 = JumpBuffer->R12;
            ContextRecord->R13 = JumpBuffer->R13;
            ContextRecord->R14 = JumpBuffer->R14;
            ContextRecord->R15 = JumpBuffer->R15;
            ContextRecord->Rip = JumpBuffer->Rip;
            ContextRecord->MxCsr = JumpBuffer->MxCsr;
            ContextRecord->FltSave.MxCsr = JumpBuffer->MxCsr;
            ContextRecord->FltSave.ControlWord = JumpBuffer->FpCsr;
            ContextRecord->Xmm6 = *(const M128A *)&JumpBuffer->Xmm6;
            ContextRecord->Xmm7 = *(const M128A *)&JumpBuffer->Xmm7;
            ContextRecord->Xmm8 = *(const M128A *)&JumpBuffer->Xmm8;
            ContextRecord->Xmm9 = *(const M128A *)&JumpBuffer->Xmm9;
            ContextRecord->Xmm10 = *(const M128A *)&JumpBuffer->Xmm10;
            ContextRecord->Xmm11 = *(const M128A *)&JumpBuffer->Xmm11;
            ContextRecord->Xmm12 = *(const M128A *)&JumpBuffer->Xmm12;
            ContextRecord->Xmm13 = *(const M128A *)&JumpBuffer->Xmm13;
            ContextRecord->Xmm14 = *(const M128A *)&JumpBuffer->Xmm14;
            ContextRecord->Xmm15 = *(const M128A *)&JumpBuffer->Xmm15;
        }
    }

    Status = ChpeNtContinue(ContextRecord, FALSE);
    RtlRaiseStatus(Status);
}

VOID NTAPI __attribute__((naked))
ChpeRtlCaptureContext(PCONTEXT ContextRecord)
{
    __asm__ volatile(
        ".seh_proc \"#ChpeRtlCaptureContext\"\n"
        ".seh_endprologue\n"
        "stp x8, x0, [x0, #0x78]\n"
        "stp x1, x27, [x0, #0x88]\n"
        "mov x1, sp\n"
        "stp x1, x29, [x0, #0x98]\n"
        "stp x25, x26, [x0, #0xa8]\n"
        "stp x2, x3, [x0, #0xb8]\n"
        "stp x4, x5, [x0, #0xc8]\n"
        "stp x19, x20, [x0, #0xd8]\n"
        "stp x21, x22, [x0, #0xe8]\n"
        "str x30, [x0, #0xf8]\n"
        "ubfx x1, x16, #0, #16\n"
        "stp x30, x1, [x0, #0x120]\n"
        "ubfx x1, x16, #16, #16\n"
        "stp x6, x1, [x0, #0x130]\n"
        "ubfx x1, x16, #32, #16\n"
        "stp x7, x1, [x0, #0x140]\n"
        "ubfx x1, x16, #48, #16\n"
        "stp x9, x1, [x0, #0x150]\n"
        "ubfx x1, x17, #0, #16\n"
        "stp x10, x1, [x0, #0x160]\n"
        "ubfx x1, x17, #16, #16\n"
        "stp x11, x1, [x0, #0x170]\n"
        "ubfx x1, x17, #32, #16\n"
        "stp x12, x1, [x0, #0x180]\n"
        "ubfx x1, x17, #48, #16\n"
        "stp x15, x1, [x0, #0x190]\n"
        "stp q0, q1, [x0, #0x1a0]\n"
        "stp q2, q3, [x0, #0x1c0]\n"
        "stp q4, q5, [x0, #0x1e0]\n"
        "stp q6, q7, [x0, #0x200]\n"
        "stp q8, q9, [x0, #0x220]\n"
        "stp q10, q11, [x0, #0x240]\n"
        "stp q12, q13, [x0, #0x260]\n"
        "stp q14, q15, [x0, #0x280]\n"
        "mrs x1, nzcv\n"
        "mrs x2, fpcr\n"
        "mrs x3, fpsr\n"
        "b \"#ChpepFinalizeCapturedContext\"\n"
        ".seh_endproc\n");
}

static NTSTATUS __attribute__((used))
ChpepDispatchRaisedException(PEXCEPTION_RECORD ExceptionRecord, PCONTEXT ContextRecord)
{
    LONG VectoredResult;
    NTSTATUS Status;

    if (NtCurrentPeb()->BeingDebugged)
        return ChpeNtRaiseException(ExceptionRecord, ContextRecord, TRUE);

    VectoredResult = ChpepCallVectoredHandlersX64(&ChpeVectoredHandlerLock, &ChpeVectoredHandlerList, FALSE, ExceptionRecord, ContextRecord);
    if (VectoredResult == EXCEPTION_CONTINUE_EXECUTION)
    {
        ChpepCallVectoredHandlersX64(&ChpeVectoredContinueHandlerLock, &ChpeVectoredContinueHandlerList, TRUE, ExceptionRecord, ContextRecord);
        return ChpeNtContinue(ContextRecord, FALSE);
    }

    Status = ChpepCallExceptionHandlers(ExceptionRecord, ContextRecord);
    ChpepCallVectoredHandlersX64(&ChpeVectoredContinueHandlerLock, &ChpeVectoredContinueHandlerList, TRUE, ExceptionRecord, ContextRecord);
    if (Status == STATUS_SUCCESS)
        return ChpeNtContinue(ContextRecord, FALSE);

    if (Status != STATUS_UNHANDLED_EXCEPTION)
        ChpeRtlRaiseStatus(Status);

    return ChpeNtRaiseException(ExceptionRecord, ContextRecord, FALSE);
}

/*
 * ARM64EC must capture and dispatch the x64-shaped context before entering the
 * native ARM64 system-call ABI.  This frame layout follows Wine's public
 * ARM64EC RtlRaiseException implementation.
 *
 * Copyright 1999, 2005, 2023 Alexandre Julliard
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
DECLSPEC_NORETURN VOID NTAPI __attribute__((naked))
ChpeRtlRaiseException(PEXCEPTION_RECORD ExceptionRecord)
{
    __asm__ volatile(
        ".seh_proc \"#ChpeRtlRaiseException\"\n"
        "sub sp, sp, #0x4d0\n"
        ".seh_stackalloc 0x4d0\n"
        "stp x29, x30, [sp, #-0x20]!\n"
        ".seh_save_fplr_x 0x20\n"
        "str x0, [sp, #0x10]\n"
        ".seh_save_any_reg x0, 0x10\n"
        ".seh_endprologue\n"
        "add x0, sp, #0x20\n"
        "bl \"#ChpeRtlCaptureContext\"\n"
        "add x1, sp, #0x20\n"
        "ldr x0, [sp, #0x10]\n"
        "ldr x2, [x1, #0xf8]\n"
        "str x2, [x0, #0x10]\n"
        "ldr w2, [x1, #0x30]\n"
        "orr w2, w2, #0x20000000\n"
        "str w2, [x1, #0x30]\n"
        "bl \"#ChpepDispatchRaisedException\"\n"
        "bl \"#ChpeRtlRaiseStatus\"\n"
        "brk #1\n"
        ".seh_endproc\n");
}

DECLSPEC_NORETURN VOID NTAPI
ChpeRtlRaiseStatus(NTSTATUS Status)
{
    EXCEPTION_RECORD ExceptionRecord;

    RtlZeroMemory(&ExceptionRecord, sizeof(ExceptionRecord));
    ExceptionRecord.ExceptionCode = Status;
    ExceptionRecord.ExceptionFlags = EXCEPTION_NONCONTINUABLE;

    for (;;)
        ChpeRtlRaiseException(&ExceptionRecord);
}

/*
 * Dispatch an ARM64EC native frame through the native ntdll handler and an
 * emulated AMD64 frame through the AMD64 scope table. The AMD64 algorithm is
 * adapted from Wine dlls/msvcrt/except.c.
 *
 * Copyright 2000 Jon Griffiths
 * Copyright 2005 Juan Lang
 * SPDX-License-Identifier: LGPL-2.1-or-later
 */
EXCEPTION_DISPOSITION CDECL
ChpeCSpecificHandler(PEXCEPTION_RECORD ExceptionRecord, PVOID EstablisherFrame, PCONTEXT ContextRecord, PCHPE_AMD64_DISPATCHER_CONTEXT DispatcherContext)
{
    PCHPE_AMD64_SCOPE_TABLE ScopeTable;
    EXCEPTION_POINTERS ExceptionPointers;
    PCHPE_AMD64_TERMINATION_HANDLER TerminationHandler;
    PCHPE_AMD64_EXCEPTION_FILTER ExceptionFilter;
    ULONG_PTR ImageBase, ControlPc;
    ULONG Index;
    LONG FilterResult;

    if (!ExceptionRecord || !ContextRecord || !DispatcherContext || !DispatcherContext->HandlerData)
        return ExceptionContinueSearch;

    ScopeTable = DispatcherContext->HandlerData;
    ImageBase = DispatcherContext->ImageBase;
    ControlPc = DispatcherContext->ControlPc;
    if (RtlIsEcCode(ControlPc) && DispatcherContext->ControlPcIsUnwound && ControlPc >= sizeof(ULONG))
        ControlPc -= sizeof(ULONG);
    if (ExceptionRecord->ExceptionFlags & (EXCEPTION_UNWINDING | EXCEPTION_EXIT_UNWIND))
    {
        for (Index = DispatcherContext->ScopeIndex; Index < ScopeTable->Count; ++Index)
        {
            if (ControlPc < ImageBase + ScopeTable->ScopeRecord[Index].BeginAddress || ControlPc >= ImageBase + ScopeTable->ScopeRecord[Index].EndAddress || ScopeTable->ScopeRecord[Index].JumpTarget)
                continue;

            if ((ExceptionRecord->ExceptionFlags & EXCEPTION_TARGET_UNWIND) && DispatcherContext->TargetIp >= ImageBase + ScopeTable->ScopeRecord[Index].BeginAddress && DispatcherContext->TargetIp < ImageBase + ScopeTable->ScopeRecord[Index].EndAddress)
                break;

            DispatcherContext->ScopeIndex = Index + 1;
            TerminationHandler = (PCHPE_AMD64_TERMINATION_HANDLER)(ImageBase + ScopeTable->ScopeRecord[Index].HandlerAddress);
            if (RtlIsEcCode((ULONG_PTR)TerminationHandler))
                ChpepExecuteEcHandler(UlongToPtr(TRUE), EstablisherFrame, TerminationHandler, DispatcherContext->NonVolatileRegisters);
            else
                TerminationHandler(TRUE, EstablisherFrame);
        }
    }
    else
    {
        ExceptionPointers.ExceptionRecord = ExceptionRecord;
        ExceptionPointers.ContextRecord = ContextRecord;

        for (Index = DispatcherContext->ScopeIndex; Index < ScopeTable->Count; ++Index)
        {
            if (ControlPc < ImageBase + ScopeTable->ScopeRecord[Index].BeginAddress || ControlPc >= ImageBase + ScopeTable->ScopeRecord[Index].EndAddress || !ScopeTable->ScopeRecord[Index].JumpTarget)
                continue;

            FilterResult = EXCEPTION_EXECUTE_HANDLER;
            if (ScopeTable->ScopeRecord[Index].HandlerAddress != (ULONG)EXCEPTION_EXECUTE_HANDLER)
            {
                ExceptionFilter = (PCHPE_AMD64_EXCEPTION_FILTER)(ImageBase + ScopeTable->ScopeRecord[Index].HandlerAddress);
                if (RtlIsEcCode((ULONG_PTR)ExceptionFilter))
                    FilterResult = ChpepExecuteEcHandler(&ExceptionPointers, EstablisherFrame, ExceptionFilter, DispatcherContext->NonVolatileRegisters);
                else
                    FilterResult = ExceptionFilter(&ExceptionPointers, EstablisherFrame);
            }

            if (FilterResult == EXCEPTION_CONTINUE_SEARCH)
                continue;
            if (FilterResult == EXCEPTION_CONTINUE_EXECUTION)
                return ExceptionContinueExecution;

            ChpeRtlUnwindEx(EstablisherFrame, (PVOID)(ImageBase + ScopeTable->ScopeRecord[Index].JumpTarget), ExceptionRecord, UlongToPtr(ExceptionRecord->ExceptionCode), DispatcherContext->ContextRecord, DispatcherContext->HistoryTable);
            return ExceptionContinueSearch;
        }
    }

    return ExceptionContinueSearch;
}

PRUNTIME_FUNCTION NTAPI
ChpeRtlLookupFunctionEntry(ULONG_PTR ControlPc, PULONG_PTR ImageBase, PUNWIND_HISTORY_TABLE HistoryTable)
{
    if (RtlIsEcCode(ControlPc))
        return RtlLookupFunctionEntry(ControlPc, ImageBase, HistoryTable);

    return ChpepAmd64LookupFunctionEntry(ControlPc, ImageBase, HistoryTable);
}

PRUNTIME_FUNCTION NTAPI
ChpeRtlLookupFunctionTable(ULONG_PTR ControlPc, PULONG_PTR ImageBase, PULONG Length)
{
    if (RtlIsEcCode(ControlPc))
        return RtlLookupFunctionTable(ControlPc, ImageBase, Length);

    return ChpepAmd64LookupFunctionTable(ControlPc, ImageBase, Length);
}

PEXCEPTION_ROUTINE NTAPI
ChpeRtlVirtualUnwind(ULONG HandlerType, ULONG_PTR ImageBase, ULONG_PTR ControlPc, PRUNTIME_FUNCTION FunctionEntry, PCONTEXT ContextRecord, PVOID *HandlerData, PULONG_PTR EstablisherFrame, PKNONVOLATILE_CONTEXT_POINTERS ContextPointers)
{
    ARM64_NT_CONTEXT ArmContext;
    PEXCEPTION_ROUTINE Handler;
    ULONG ContextFlags;

    if (!RtlIsEcCode(ControlPc))
    {
        if (FunctionEntry == NULL)
        {
            *EstablisherFrame = ContextRecord->Rsp;
            if (HandlerData != NULL)
                *HandlerData = NULL;
            if (ContextRecord->Rsp != 0)
            {
                ContextRecord->Rip = *(PULONG64)ContextRecord->Rsp;
                ContextRecord->Rsp += sizeof(ULONG64);
            }
            else
            {
                ContextRecord->Rip = 0;
            }
            ContextRecord->ContextFlags |= CONTEXT_UNWOUND_TO_CALL;
            return NULL;
        }
        Handler = ChpepAmd64VirtualUnwind(HandlerType, ImageBase, ControlPc, FunctionEntry, ContextRecord, HandlerData, EstablisherFrame, ContextPointers);
        ContextRecord->ContextFlags |= CONTEXT_UNWOUND_TO_CALL;
        return Handler;
    }

    ContextFlags = ContextRecord->ContextFlags & ~CONTEXT_UNWOUND_TO_CALL;
    ChpepContextX64ToArm64(&ArmContext, (PARM64EC_NT_CONTEXT)ContextRecord);
    Handler = RtlVirtualUnwind(HandlerType, ImageBase, ControlPc, FunctionEntry, (PCONTEXT)&ArmContext, HandlerData, EstablisherFrame, NULL);
    ChpepContextArm64ToX64((PARM64EC_NT_CONTEXT)ContextRecord, &ArmContext);
    ContextRecord->ContextFlags = ContextFlags | (ArmContext.ContextFlags & CONTEXT_UNWOUND_TO_CALL);
    return Handler;
}

VOID NTAPI
ChpeRtlUnwindEx(PVOID TargetFrame, PVOID TargetIp, PEXCEPTION_RECORD ExceptionRecord, PVOID ReturnValue, PCONTEXT ContextRecord, PUNWIND_HISTORY_TABLE HistoryTable)
{
    DISPATCHER_CONTEXT_NONVOLREG_ARM64 NonVolatileRegisters;
    DISPATCHER_CONTEXT_ARM64EC DispatcherContext;
    ARM64EC_NT_CONTEXT UnwindContext;
    EXCEPTION_RECORD LocalExceptionRecord;
    EXCEPTION_DISPOSITION Disposition;
    ULONG_PTR PreviousPc, PreviousSp, Frame;
    BOOLEAN TargetIsEc;
    NTSTATUS Status;
    ULONG Count;

    if (ContextRecord == NULL)
        return;

    ChpeRtlCaptureContext(ContextRecord);
    UnwindContext.AMD64_Context = *ContextRecord;

    if (ExceptionRecord == NULL)
    {
        RtlZeroMemory(&LocalExceptionRecord, sizeof(LocalExceptionRecord));
        LocalExceptionRecord.ExceptionCode = STATUS_UNWIND;
        LocalExceptionRecord.ExceptionAddress = (PVOID)(ULONG_PTR)ContextRecord->Rip;
        ExceptionRecord = &LocalExceptionRecord;
    }

    ExceptionRecord->ExceptionFlags |= EXCEPTION_UNWINDING;
    if (TargetFrame == NULL)
        ExceptionRecord->ExceptionFlags |= EXCEPTION_EXIT_UNWIND;

    RtlZeroMemory(&DispatcherContext, sizeof(DispatcherContext));
    DispatcherContext.TargetIp = (ULONG64)(ULONG_PTR)TargetIp;
    DispatcherContext.ContextRecord = ContextRecord;
    DispatcherContext.HistoryTable = HistoryTable;
    DispatcherContext.NonVolatileRegisters = NonVolatileRegisters.Buffer;
    TargetIsEc = TargetIp != NULL && RtlIsEcCode((ULONG_PTR)TargetIp);

    for (Count = 0; Count < 1024; Count++)
    {
        PreviousPc = UnwindContext.Pc;
        PreviousSp = UnwindContext.Sp;
        Status = ChpepVirtualUnwindFrame(UNW_FLAG_UHANDLER, &DispatcherContext, &UnwindContext);
        if (!NT_SUCCESS(Status))
            RtlRaiseStatus(Status);

UnwindDone:
        if (DispatcherContext.EstablisherFrame == 0)
            break;

        if (!ChpepIsValidUnwindFrame(DispatcherContext.EstablisherFrame))
        {
            ExceptionRecord->ExceptionFlags |= EXCEPTION_STACK_INVALID;
            RtlRaiseStatus(STATUS_BAD_STACK);
        }

        if (DispatcherContext.LanguageHandler != NULL)
        {
            if (TargetFrame != NULL && DispatcherContext.EstablisherFrame > (ULONG64)(ULONG_PTR)TargetFrame)
                RtlRaiseStatus(STATUS_INVALID_UNWIND_TARGET);

            if (DispatcherContext.EstablisherFrame == (ULONG64)(ULONG_PTR)TargetFrame)
                ExceptionRecord->ExceptionFlags |= EXCEPTION_TARGET_UNWIND;

            Disposition = ChpepCallUnwindHandler(ExceptionRecord, (PVOID)(ULONG_PTR)DispatcherContext.EstablisherFrame, ContextRecord, &DispatcherContext, DispatcherContext.LanguageHandler);
            ExceptionRecord->ExceptionFlags &= ~(EXCEPTION_TARGET_UNWIND | EXCEPTION_COLLIDED_UNWIND);

            if (Disposition == ExceptionCollidedUnwind)
            {
                UnwindContext.AMD64_Context = *ContextRecord;
                ChpeRtlVirtualUnwind(UNW_FLAG_NHANDLER, DispatcherContext.ImageBase, DispatcherContext.ControlPc, DispatcherContext.FunctionEntry, &UnwindContext.AMD64_Context, &DispatcherContext.HandlerData, &Frame, NULL);
                ExceptionRecord->ExceptionFlags |= EXCEPTION_COLLIDED_UNWIND;
                goto UnwindDone;
            }

            if (Disposition != ExceptionContinueSearch)
                RtlRaiseStatus(STATUS_INVALID_DISPOSITION);
        }

        if (DispatcherContext.EstablisherFrame == (ULONG64)(ULONG_PTR)TargetFrame)
        {
            if (TargetIsEc || !RtlIsEcCode(DispatcherContext.ControlPc))
                break;
        }

        if (UnwindContext.Pc == PreviousPc && UnwindContext.Sp == PreviousSp)
            RtlRaiseStatus(STATUS_BAD_FUNCTION_TABLE);

        *ContextRecord = UnwindContext.AMD64_Context;
    }

    if (Count == 1024)
        RtlRaiseStatus(STATUS_BAD_STACK);

    if (ExceptionRecord->ExceptionCode != STATUS_UNWIND_CONSOLIDATE)
    {
        ContextRecord->Rip = (ULONG64)(ULONG_PTR)TargetIp;
    }
    else if (ExceptionRecord->NumberParameters > 10 && ExceptionRecord->ExceptionInformation[10] == (ULONG_PTR)-1)
    {
        ExceptionRecord->ExceptionInformation[10] = (ULONG_PTR)&NonVolatileRegisters;
    }

    if (TargetIsEc)
        ContextRecord->Rcx = (ULONG64)(ULONG_PTR)ReturnValue;
    else
        ContextRecord->Rax = (ULONG64)(ULONG_PTR)ReturnValue;

    ChpeRtlRestoreContext(ContextRecord, ExceptionRecord);
}

VOID NTAPI
ChpeRtlUnwind(PVOID TargetFrame, PVOID TargetIp, PEXCEPTION_RECORD ExceptionRecord, PVOID ReturnValue)
{
    CONTEXT ContextRecord;

    ChpeRtlUnwindEx(TargetFrame, TargetIp, ExceptionRecord, ReturnValue, &ContextRecord, NULL);
}
