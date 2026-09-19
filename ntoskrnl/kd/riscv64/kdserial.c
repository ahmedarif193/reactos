/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     KD serial provider over the RISC-V early console (ABI-127)
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>
#include <cportlib/uartinfo.h>

/* Non-NULL token for the SBI console, which has no MMIO base. */
#define KD_RISCV_SBI_CONSOLE_TOKEN ((PUCHAR)(ULONG_PTR)0x53424944ULL) /* 'SBID' */

BOOLEAN
NTAPI
KdPortInitializeEx(
    _Inout_ PCPPORT PortInformation,
    _In_ ULONG ComPortNumber)
{
    PUCHAR Base = NULL;
    UCHAR Byte;
    ULONG Drained;

    /* The firmware-described console is authoritative; the COM number and
     * any DEBUGPORT=COM:<addr> override are accepted and ignored. */
    UNREFERENCED_PARAMETER(ComPortNumber);

    if (KiRiscvConsoleReady())
    {
        if (KiRiscvConsoleInterface() == RISCV64_EARLY_CONSOLE_NS16550)
        {
            Base = (PUCHAR)(ULONG_PTR)(KeLoaderBlock->u.Riscv64.DirectMapBase +
                                       KeLoaderBlock->u.Riscv64.EarlyConsoleAddress);
        }
        else
        {
            Base = KD_RISCV_SBI_CONSOLE_TOKEN;
        }
    }

    PortInformation->Address = Base;
    if (PortInformation->BaudRate == 0)
        PortInformation->BaudRate = DEFAULT_DEBUG_BAUD_RATE;
    PortInformation->Flags |= CPPORT_FLAG_KEEP_BAUD;
    if (Base == NULL)
        return FALSE;

    /* Drop stale input so the first poll does not see boot-time noise. */
    for (Drained = 0; Drained < 64; Drained++)
    {
        if (!KiRiscvConsoleGetByte(&Byte))
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
    return KiRiscvConsoleGetByte(ByteReceived);
}

VOID
NTAPI
KdPortPutByteEx(
    _Inout_ PCPPORT PortInformation,
    _In_ UCHAR ByteToSend)
{
    UNREFERENCED_PARAMETER(PortInformation);
    KiRiscvConsolePutByte(ByteToSend);
}

VOID
NTAPI
KdPortPutBufferEx(
    _Inout_ PCPPORT PortInformation,
    _In_reads_bytes_(Length) PCCH Buffer,
    _In_ ULONG Length)
{
    UNREFERENCED_PARAMETER(PortInformation);
    KiRiscvConsoleWrite(Buffer, Length);
}
