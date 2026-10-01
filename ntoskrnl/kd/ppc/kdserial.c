/*
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Windows NT PowerPC KD serial port over the early console
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>
#include <cportlib/uartinfo.h>

BOOLEAN
NTAPI
KdPortInitializeEx(
    _Inout_ PCPPORT PortInformation,
    _In_ ULONG ComPortNumber)
{
    PUCHAR Base = NULL;
    UCHAR Byte;
    ULONG Drained;

    /* The loader-described console is authoritative; the COM number and
     * any DEBUGPORT=COM:<addr> override are accepted and ignored. */
    UNREFERENCED_PARAMETER(ComPortNumber);

    if (KiPpcConsoleReady())
        Base = (PUCHAR)(KeLoaderBlock->u.PowerPC.IsaIoVirtualBase + KeLoaderBlock->u.PowerPC.EarlyConsolePort);

    PortInformation->Address = Base;
    if (PortInformation->BaudRate == 0)
        PortInformation->BaudRate = DEFAULT_DEBUG_BAUD_RATE;
    PortInformation->Flags |= CPPORT_FLAG_KEEP_BAUD;
    if (Base == NULL)
        return FALSE;

    /* Drop stale input so the first poll does not see boot-time noise. */
    for (Drained = 0; Drained < 64; Drained++)
    {
        if (!KiPpcConsoleGetByte(&Byte))
            break;
    }
    return TRUE;
}

BOOLEAN
NTAPI
KdPortGetByteEx(
    _Inout_ PCPPORT PortInformation,
    _Out_ PUCHAR ByteReceived)
{
    UNREFERENCED_PARAMETER(PortInformation);
    return KiPpcConsoleGetByte(ByteReceived);
}

VOID
NTAPI
KdPortPutByteEx(
    _Inout_ PCPPORT PortInformation,
    _In_ UCHAR ByteToSend)
{
    UNREFERENCED_PARAMETER(PortInformation);
    KiPpcConsolePutByte(ByteToSend);
}

VOID
NTAPI
KdPortPutBufferEx(
    _Inout_ PCPPORT PortInformation,
    _In_reads_bytes_(Length) PCCH Buffer,
    _In_ ULONG Length)
{
    UNREFERENCED_PARAMETER(PortInformation);
    KiPpcConsoleWrite(Buffer, Length);
}
