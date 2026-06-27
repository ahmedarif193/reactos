/*
 * PROJECT:     ReactOS Run-Time Library
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     ARM64EC glue for the AMD64 unwind implementation
 */

#include <rtl.h>

#define NDEBUG
#include <debug.h>

#define RTL_ARM64EC_CONTEXT_ARM64_FULL (0x00400000L | 0x1L | 0x2L | 0x4L)
#define RTL_ARM64EC_CONTEXT_ARM64_X18  (0x00400000L | 0x10L)

static
ULONG
RtlpArm64EcEFlagsToCpsr(ULONG EFlags)
{
    ULONG Cpsr = 0;

    if (EFlags & 0x100) Cpsr |= (1U << 21);
    if (EFlags & 0x800) Cpsr |= (1U << 28);
    if (EFlags & 0x001) Cpsr |= (1U << 29);
    if (EFlags & 0x040) Cpsr |= (1U << 30);
    if (EFlags & 0x080) Cpsr |= (1U << 31);

    return Cpsr;
}

static
VOID
RtlpArm64EcContextToArm64(
    _Out_ PARM64_NT_CONTEXT Arm64Context,
    _In_ PARM64EC_NT_CONTEXT Arm64EcContext)
{
    RtlZeroMemory(Arm64Context, sizeof(*Arm64Context));

    Arm64Context->ContextFlags = RTL_ARM64EC_CONTEXT_ARM64_FULL |
                                 RTL_ARM64EC_CONTEXT_ARM64_X18;
    Arm64Context->Cpsr = RtlpArm64EcEFlagsToCpsr(Arm64EcContext->AMD64_EFlags);

    Arm64Context->X8 = Arm64EcContext->X8;
    Arm64Context->X0 = Arm64EcContext->X0;
    Arm64Context->X1 = Arm64EcContext->X1;
    Arm64Context->X27 = Arm64EcContext->X27;
    Arm64Context->Sp = Arm64EcContext->Sp;
    Arm64Context->Fp = Arm64EcContext->Fp;
    Arm64Context->X25 = Arm64EcContext->X25;
    Arm64Context->X26 = Arm64EcContext->X26;
    Arm64Context->X2 = Arm64EcContext->X2;
    Arm64Context->X3 = Arm64EcContext->X3;
    Arm64Context->X4 = Arm64EcContext->X4;
    Arm64Context->X5 = Arm64EcContext->X5;
    Arm64Context->X19 = Arm64EcContext->X19;
    Arm64Context->X20 = Arm64EcContext->X20;
    Arm64Context->X21 = Arm64EcContext->X21;
    Arm64Context->X22 = Arm64EcContext->X22;
    Arm64Context->Pc = Arm64EcContext->Pc;
    Arm64Context->Fpcr = Arm64EcContext->AMD64_MxCsr;
    Arm64Context->Fpsr = Arm64EcContext->AMD64_MxCsr_Mask;
    Arm64Context->Lr = Arm64EcContext->Lr;
    Arm64Context->X6 = Arm64EcContext->X6;
    Arm64Context->X7 = Arm64EcContext->X7;
    Arm64Context->X9 = Arm64EcContext->X9;
    Arm64Context->X10 = Arm64EcContext->X10;
    Arm64Context->X11 = Arm64EcContext->X11;
    Arm64Context->X12 = Arm64EcContext->X12;
    Arm64Context->X15 = Arm64EcContext->X15;
    Arm64Context->X16 = ((ULONG64)Arm64EcContext->X16_0) |
                        ((ULONG64)Arm64EcContext->X16_1 << 16) |
                        ((ULONG64)Arm64EcContext->X16_2 << 32) |
                        ((ULONG64)Arm64EcContext->X16_3 << 48);
    Arm64Context->X17 = ((ULONG64)Arm64EcContext->X17_0) |
                        ((ULONG64)Arm64EcContext->X17_1 << 16) |
                        ((ULONG64)Arm64EcContext->X17_2 << 32) |
                        ((ULONG64)Arm64EcContext->X17_3 << 48);
    Arm64Context->X18 = (ULONG_PTR)NtCurrentTeb();
    RtlCopyMemory(Arm64Context->V, Arm64EcContext->V, sizeof(Arm64EcContext->V));
}

EXCEPTION_DISPOSITION
NTAPI
RtlpExecuteHandlerForUnwind(
    _Inout_ PEXCEPTION_RECORD ExceptionRecord,
    _In_ PVOID EstablisherFrame,
    _Inout_ PCONTEXT ContextRecord,
    _In_ PVOID DispatcherContext)
{
    PDISPATCHER_CONTEXT Dispatcher = DispatcherContext;

    return Dispatcher->LanguageHandler(ExceptionRecord,
                                       EstablisherFrame,
                                       ContextRecord,
                                       DispatcherContext);
}

VOID
NTAPI
RtlpRestoreContextInternal(
    _In_ PCONTEXT ContextRecord)
{
    ARM64_NT_CONTEXT Arm64Context;
    NTSTATUS Status;

    RtlpArm64EcContextToArm64(&Arm64Context, (PARM64EC_NT_CONTEXT)ContextRecord);
    Status = ZwContinue((PCONTEXT)&Arm64Context, FALSE);
    RtlRaiseStatus(Status);
}
