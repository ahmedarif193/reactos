/*
 * PROJECT:         ReactOS Kernel
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            ntoskrnl/arch/arm64/kd/kdserial.c
 * PURPOSE:         Serial kernel debugger stubs for ARM64
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

CPPORT DefaultPort = {0};
extern BOOLEAN KdDebuggerNotPresent;

#define PL011_DR_OFFSET     0x00
#define PL011_FR_OFFSET     0x18
#define PL011_FR_RXFE       0x10

static
VOID
KdArm64DrainReceiveFifo(
    _In_opt_ PUCHAR Base)
{
    ULONG Guard;

    if (Base == NULL)
    {
        return;
    }

    for (Guard = 2048; Guard > 0; Guard--)
    {
        ULONG Flags = READ_REGISTER_ULONG((PULONG)((ULONG_PTR)Base + PL011_FR_OFFSET));

        if (Flags & PL011_FR_RXFE)
        {
            break;
        }

        (VOID)READ_REGISTER_ULONG((PULONG)((ULONG_PTR)Base + PL011_DR_OFFSET));
    }
}

/*
 * ARM64 KD serial transport over PL011 (MMIO)
 * QEMU virt exposes PL011 at 0x09000000; later we can query ACPI SPCR.
 */
BOOLEAN
NTAPI
KdPortInitializeEx(_Inout_ PCPPORT PortInformation,
                   _In_ ULONG ComPortNumber)
{
    NTSTATUS Status;
    PUCHAR Base;
    /* Avoid MmMapIoSpace at phase 0; use direct KSEG0 mapping */
    ULONG Baud;

    if (ComPortNumber == 0 && PortInformation->Address)
    {
        Base = PortInformation->Address;
    }
    else
    {
        if (ComPortNumber == 1)
            Base = (PUCHAR)(ULONG_PTR)0x09000000ULL; /* QEMU virt PL011 */
        else
            Base = NULL;
    }

    Baud = (PortInformation->BaudRate != 0) ? PortInformation->BaudRate : 115200;

    PortInformation->Address = Base;
    Status = CpInitialize(PortInformation, Base, Baud);
    if (!NT_SUCCESS(Status))
    {
        HalDisplayString("\r\nKernel Debugger: Serial port not found!\r\n\r\n");
        return FALSE;
    }

    /* Make sure no stale characters are queued before kdcom starts listening */
    KdArm64DrainReceiveFifo(Base);

    /* Transport is live; record transport-present bit.
     * Parity flip of NotPresent is done post-banner in kdinit to avoid stalls. */
    SharedUserData->KdDebuggerEnabled |= 0x00000002;

#ifndef NDEBUG
    {
        CHAR Buffer[96];
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
    return (CpGetByte(PortInformation, ByteReceived, FALSE, FALSE) == CP_GET_SUCCESS);
}

VOID
NTAPI
KdPortPutByteEx(_Inout_ PCPPORT PortInformation,
                _In_ UCHAR ByteToSend)
{
    CpPutByte(PortInformation, ByteToSend);
}
