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
#include <cportlib/uartinfo.h>

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

    UNREFERENCED_PARAMETER(ComPortNumber);

    EarlyUartInitialize(0);

    /*
     * Require a driver-known interface, not just a base address. FreeLDR may
     * have captured the base from ACPI DBG2 but reported Interface=Unknown;
     * in that case KD must fail cleanly rather than silently swallow output.
     */
    Base = EarlyUartReady() ? (PUCHAR)(ULONG_PTR)EarlyUartPhysToVa(EarlyUartGetBaseAddress()) : NULL;
    Baud = (PortInformation->BaudRate != 0) ? PortInformation->BaudRate : DEFAULT_DEBUG_BAUD_RATE;

    /* The firmware-described UART is authoritative on ARM64; a user-supplied
     * DEBUGPORT=COM:<addr> address cannot be honored (its register layout is
     * unknown), so reject it explicitly rather than discard it silently. */
    if ((PortInformation->Address != NULL) && (PortInformation->Address != Base))
    {
        HalDisplayString("\r\nKernel Debugger: DEBUGPORT address override is not supported on ARM64; using the firmware-detected UART.\r\n\r\n");
    }

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

VOID
NTAPI
KdPortPutBufferEx(_Inout_ PCPPORT PortInformation,
                  _In_reads_bytes_(Length) PCCH Buffer,
                  _In_ ULONG Length)
{
    UNREFERENCED_PARAMETER(PortInformation);
    EarlyUartWrite(Buffer, Length);
}
