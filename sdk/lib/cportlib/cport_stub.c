/*
 * PROJECT:     ReactOS ComPort Library (stub implementation)
 * LICENSE:     BSD - See COPYING.ARM in the top level directory
 * PURPOSE:     Provide no-op serial helpers on architectures without port I/O
 */

#include <ntstatus.h>
#define WIN32_NO_STATUS
#include <cportlib/cportlib.h>

VOID
NTAPI
CpEnableFifo(IN PUCHAR Address, IN BOOLEAN Enable)
{
    UNREFERENCED_PARAMETER(Address);
    UNREFERENCED_PARAMETER(Enable);
}

VOID
NTAPI
CpSetBaud(IN PCPPORT Port, IN ULONG BaudRate)
{
    if (Port)
        Port->BaudRate = BaudRate;
}

NTSTATUS
NTAPI
CpInitialize(IN PCPPORT Port, IN PUCHAR Address, IN ULONG BaudRate)
{
    if (!Port)
        return STATUS_INVALID_PARAMETER;

    Port->Address  = Address;
    Port->BaudRate = BaudRate;
    Port->Flags    = 0;
    Port->EchoDiscard = 0;

    return STATUS_NOT_SUPPORTED;
}

BOOLEAN
NTAPI
CpDoesPortExist(IN PUCHAR Address)
{
    UNREFERENCED_PARAMETER(Address);
    return FALSE;
}

UCHAR
NTAPI
CpReadLsr(IN PCPPORT Port, IN UCHAR ExpectedValue)
{
    UNREFERENCED_PARAMETER(Port);
    UNREFERENCED_PARAMETER(ExpectedValue);
    return 0;
}

USHORT
NTAPI
CpGetByte(IN PCPPORT Port, OUT PUCHAR Byte, IN BOOLEAN Wait, IN BOOLEAN Poll)
{
    UNREFERENCED_PARAMETER(Port);
    UNREFERENCED_PARAMETER(Byte);
    UNREFERENCED_PARAMETER(Wait);
    UNREFERENCED_PARAMETER(Poll);
    return CP_GET_ERROR;
}

VOID
NTAPI
CpPutByte(IN PCPPORT Port, IN UCHAR Byte)
{
    UNREFERENCED_PARAMETER(Port);
    UNREFERENCED_PARAMETER(Byte);
}

VOID
NTAPI
CpPutBuffer(IN PCPPORT Port, IN PUCHAR Buffer, IN ULONG Length)
{
    UNREFERENCED_PARAMETER(Port);
    UNREFERENCED_PARAMETER(Buffer);
    UNREFERENCED_PARAMETER(Length);
}

/* EOF */
