/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            ntoskrnl/arch/arm64/ps/psctx.c
 * PURPOSE:         Process context stubs for ARM64
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

VOID
NTAPI
PspGetContext(_In_ PKTRAP_FRAME TrapFrame,
              _In_ PVOID NonVolatileContext,
              _Inout_ PCONTEXT Context)
{
    UNREFERENCED_PARAMETER(TrapFrame);
    UNREFERENCED_PARAMETER(NonVolatileContext);
    UNREFERENCED_PARAMETER(Context);
}

VOID
NTAPI
PspSetContext(_Out_ PKTRAP_FRAME TrapFrame,
              _Out_ PVOID NonVolatileContext,
              _In_ PCONTEXT Context,
              _In_ KPROCESSOR_MODE Mode)
{
    UNREFERENCED_PARAMETER(TrapFrame);
    UNREFERENCED_PARAMETER(NonVolatileContext);
    UNREFERENCED_PARAMETER(Context);
    UNREFERENCED_PARAMETER(Mode);
}

VOID
NTAPI
PspGetOrSetContextKernelRoutine(_Inout_ PKAPC Apc,
                                _Inout_ PKNORMAL_ROUTINE* NormalRoutine,
                                _Inout_ PVOID* NormalContext,
                                _Inout_ PVOID* SystemArgument1,
                                _Inout_ PVOID* SystemArgument2)
{
    UNREFERENCED_PARAMETER(Apc);
    UNREFERENCED_PARAMETER(NormalRoutine);
    UNREFERENCED_PARAMETER(NormalContext);
    UNREFERENCED_PARAMETER(SystemArgument1);
    UNREFERENCED_PARAMETER(SystemArgument2);
}
