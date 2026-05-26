/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            ntoskrnl/kd/arm64/kdserial.c
 * PURPOSE:         Serial kernel debugger stubs for ARM64
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>
#include <reactos/arm64/early_uart.h>

CPPORT DefaultPort = {0};
extern BOOLEAN KdDebuggerNotPresent;

/*
 * ARM64 KD serial transport over the runtime-detected early UART.
 * FreeLdr detects the UART address and passes it through the loader block;
 * the kernel early UART layer owns the target-specific address selection.
 */
BOOLEAN
NTAPI
KdPortInitializeEx(_Inout_ PCPPORT PortInformation,
                   _In_ ULONG ComPortNumber)
{
    PUCHAR Base;
    ULONG Baud;


    if (!EarlyUartIsInitialized())
    {
        EarlyUartInitialize(0);
    }

    /*
     * Require a driver-known interface, not just a base address. FreeLDR may
     * have captured the base from ACPI DBG2 but reported Interface=Unknown
     * (e.g. Qualcomm GENI/QUP) - in that case we have no driver to drive it
     * and KD must fail cleanly rather than silently swallow output.
     */
    Base = EarlyUartReady() ?
           (PUCHAR)(ULONG_PTR)EarlyUartPhysToVa(EarlyUartGetBaseAddress()) :
           NULL;
    Baud = (PortInformation->BaudRate != 0) ? PortInformation->BaudRate : 115200;

    PortInformation->Address = Base;
    PortInformation->BaudRate = Baud;
    PortInformation->Flags |= CPPORT_FLAG_KEEP_BAUD;
    if (Base == NULL)
    {
        HalDisplayString("\r\nKernel Debugger: Serial port not found!\r\n\r\n");
        return FALSE;
    }

    /* Make sure no stale characters are queued before kdcom starts listening */
    EarlyUartDrainReceiveFifo();

    /* Transport is live; record transport-present bit.
     * Parity flip of NotPresent is done post-banner in kdinit to avoid stalls. */
    SharedUserData->KdDebuggerEnabled |= 0x00000002;

#ifndef NDEBUG
    {
        CHAR Buffer[128];
        int Length = snprintf(Buffer, sizeof(Buffer),
                              "\r\nKernel Debugger: Serial port found: COM%lu (Port 0x%p) BaudRate %lu\r\n\r\n",
                              (unsigned long)(ComPortNumber ? ComPortNumber : 1),
                              PortInformation->Address,
                              PortInformation->BaudRate);
        if (Length < 0) Buffer[sizeof(Buffer) - 1] = '\0';
        HalDisplayString(Buffer);
    }
#endif

    return TRUE;
}

BOOLEAN
NTAPI
KdPortGetByteEx(_Inout_ PCPPORT PortInformation,
                _Out_ PUCHAR ByteReceived)
{
    UNREFERENCED_PARAMETER(PortInformation);
    return EarlyUartGetc(ByteReceived);
}

VOID
NTAPI
KdPortPutByteEx(_Inout_ PCPPORT PortInformation,
                _In_ UCHAR ByteToSend)
{
    UNREFERENCED_PARAMETER(PortInformation);
    EarlyUartPutc((CHAR)ByteToSend);
}
