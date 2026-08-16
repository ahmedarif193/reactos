/*
 * PROJECT:     ReactOS vcruntime library
 * LICENSE:     LGPL-2.1-or-later (https://spdx.org/licenses/LGPL-2.1-or-later)
 * PURPOSE:     ARM64EC jump-buffer finalization
 * COPYRIGHT:   Copyright 2023 Alexandre Julliard
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF <arif.ing@outlook.com>
 *
 * Adapted from Wine's dlls/ntdll/signal_arm64ec.c.
 */

#include <setjmp.h>
#include <windef.h>
#include <winnt.h>

static ULONG
ChpepFpStateToMxCsr(ULONG Fpcr, ULONG Fpsr)
{
    ULONG MxCsr = 0;

    MxCsr |= Fpsr & 0x0001;
    MxCsr |= (Fpsr & 0x0002) << 1;
    MxCsr |= (Fpsr & 0x0004) << 1;
    MxCsr |= (Fpsr & 0x0008) << 1;
    MxCsr |= (Fpsr & 0x0010) << 1;
    MxCsr |= (Fpsr & 0x0080) >> 6;
    MxCsr |= (Fpcr & 0x00080000) >> 13;
    MxCsr |= (Fpcr & 0x00400000) >> 8;
    MxCsr |= (Fpcr & 0x00800000) >> 10;
    MxCsr |= (Fpcr & 0x01000000) >> 9;
    MxCsr |= (~Fpcr >> 1) & 0x0080;
    MxCsr |= (~Fpcr >> 7) & 0x0100;
    MxCsr |= ~Fpcr & 0x1e00;
    return MxCsr;
}

static VOID
ChpepJumpBufferToContext(PCONTEXT Context, const _JUMP_BUFFER *JumpBuffer)
{
    RtlZeroMemory(Context, sizeof(*Context));
    Context->ContextFlags = CONTEXT_FULL;
    Context->Rbx = JumpBuffer->Rbx;
    Context->Rsp = JumpBuffer->Rsp;
    Context->Rbp = JumpBuffer->Rbp;
    Context->Rsi = JumpBuffer->Rsi;
    Context->Rdi = JumpBuffer->Rdi;
    Context->R12 = JumpBuffer->R12;
    Context->R13 = JumpBuffer->R13;
    Context->R14 = JumpBuffer->R14;
    Context->R15 = JumpBuffer->R15;
    Context->Rip = JumpBuffer->Rip;
    Context->Xmm6 = *(const M128A *)&JumpBuffer->Xmm6;
    Context->Xmm7 = *(const M128A *)&JumpBuffer->Xmm7;
    Context->Xmm8 = *(const M128A *)&JumpBuffer->Xmm8;
    Context->Xmm9 = *(const M128A *)&JumpBuffer->Xmm9;
    Context->Xmm10 = *(const M128A *)&JumpBuffer->Xmm10;
    Context->Xmm11 = *(const M128A *)&JumpBuffer->Xmm11;
    Context->Xmm12 = *(const M128A *)&JumpBuffer->Xmm12;
    Context->Xmm13 = *(const M128A *)&JumpBuffer->Xmm13;
    Context->Xmm14 = *(const M128A *)&JumpBuffer->Xmm14;
    Context->Xmm15 = *(const M128A *)&JumpBuffer->Xmm15;
}

static VOID
ChpepContextToJumpBuffer(_JUMP_BUFFER *JumpBuffer, const CONTEXT *Context)
{
    JumpBuffer->Rbx = Context->Rbx;
    JumpBuffer->Rsp = Context->Rsp;
    JumpBuffer->Rbp = Context->Rbp;
    JumpBuffer->Rsi = Context->Rsi;
    JumpBuffer->Rdi = Context->Rdi;
    JumpBuffer->R12 = Context->R12;
    JumpBuffer->R13 = Context->R13;
    JumpBuffer->R14 = Context->R14;
    JumpBuffer->R15 = Context->R15;
    JumpBuffer->Rip = Context->Rip;
    *(M128A *)&JumpBuffer->Xmm6 = Context->Xmm6;
    *(M128A *)&JumpBuffer->Xmm7 = Context->Xmm7;
    *(M128A *)&JumpBuffer->Xmm8 = Context->Xmm8;
    *(M128A *)&JumpBuffer->Xmm9 = Context->Xmm9;
    *(M128A *)&JumpBuffer->Xmm10 = Context->Xmm10;
    *(M128A *)&JumpBuffer->Xmm11 = Context->Xmm11;
    *(M128A *)&JumpBuffer->Xmm12 = Context->Xmm12;
    *(M128A *)&JumpBuffer->Xmm13 = Context->Xmm13;
    *(M128A *)&JumpBuffer->Xmm14 = Context->Xmm14;
    *(M128A *)&JumpBuffer->Xmm15 = Context->Xmm15;
}

int __cdecl
ChpepSetJmpFinalize(_JUMP_BUFFER *JumpBuffer, ULONG Fpcr, ULONG Fpsr)
{
    CONTEXT Context;
    PRUNTIME_FUNCTION FunctionEntry;
    ULONG_PTR ControlPc, ImageBase = 0, EstablisherFrame = 0;
    PVOID HandlerData = NULL;

    JumpBuffer->MxCsr = ChpepFpStateToMxCsr(Fpcr, Fpsr);
    JumpBuffer->FpCsr = 0x27f;

    ChpepJumpBufferToContext(&Context, JumpBuffer);
    ControlPc = Context.Rip >= sizeof(ULONG) ? Context.Rip - sizeof(ULONG) : Context.Rip;
    FunctionEntry = RtlLookupFunctionEntry(ControlPc, &ImageBase, NULL);
    RtlVirtualUnwind(UNW_FLAG_NHANDLER, ImageBase, ControlPc, FunctionEntry, &Context, &HandlerData, &EstablisherFrame, NULL);

    if (!RtlIsEcCode(Context.Rip))
        ChpepContextToJumpBuffer(JumpBuffer, &Context);

    return 0;
}
