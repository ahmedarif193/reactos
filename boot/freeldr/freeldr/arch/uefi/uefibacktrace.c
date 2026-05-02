/*
 * PROJECT:     FreeLoader UEFI Support
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * PURPOSE:     Minimal UEFI backtrace helpers for ARM64/AMD64
 */

#include <uefildr.h>
#include <debug.h>
#include <arch/uefi/uefisym.h>

static VOID
UefiPrintFramePointerChain(
    ULONG_PTR FramePointer,
    ULONG_PTR StackTop,
    ULONG_PTR StackBottom)
{
    ULONG Frames = 0;

    while (Frames < 32)
    {
        if ((FramePointer & 0xF) != 0)
            break;
        if (FramePointer < StackBottom || FramePointer + 16 > StackTop)
            break;

        ULONG_PTR *Slot = (ULONG_PTR *)FramePointer;
        ULONG_PTR NextFrame = Slot[0];
        ULONG_PTR ReturnAddress = Slot[1];

        if (ReturnAddress == 0 || NextFrame <= FramePointer)
            break;

        DbgPrint("    %p\n", (PVOID)ReturnAddress);
        FramePointer = NextFrame;
        Frames++;
    }
}

VOID
UefiDbgFormatAddress(
    ULONG_PTR Address)
{
    DbgPrint("    %p\n", (PVOID)Address);
}

VOID
UefiArm64PrintBacktrace(
    ULONG_PTR FramePointer,
    ULONG_PTR StackTop,
    ULONG_PTR StackBottom)
{
    DbgPrint("Backtrace (ARM64):\n");
    UefiPrintFramePointerChain(FramePointer, StackTop, StackBottom);
}

VOID
UefiArm64PrintBacktraceNoHeader(
    ULONG_PTR FramePointer,
    ULONG_PTR StackTop,
    ULONG_PTR StackBottom)
{
    UefiPrintFramePointerChain(FramePointer, StackTop, StackBottom);
}

VOID
UefiAmd64PrintBacktrace(
    ULONG_PTR Rbp,
    ULONG_PTR StackTop,
    ULONG_PTR StackBottom)
{
    DbgPrint("Backtrace (AMD64):\n");
    UefiPrintFramePointerChain(Rbp, StackTop, StackBottom);
}
