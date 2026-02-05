/*
 * PROJECT:         ReactOS ComPort Library
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * FILE:            include/reactos/libs/cportlib/cportlib.h
 * PURPOSE:         Header for the ComPort Library
 * PROGRAMMERS:     ReactOS Portable Systems Group
 */

/* INCLUDES *******************************************************************/

#pragma once

#include <ntdef.h>

//
// Return error codes.
//
#define CP_GET_SUCCESS  0
#define CP_GET_NODATA   1
#define CP_GET_ERROR    2

//
// COM port flags.
//
#define CPPORT_FLAG_MODEM_CONTROL   0x02
#define CPPORT_FLAG_SUPPRESS_ECHO   0x04
/* Keep existing UART baud/divisors (do not reprogram) */
#define CPPORT_FLAG_KEEP_BAUD       0x08
/* UART has a working TX FIFO (16550+) */
#define CPPORT_FLAG_HAS_FIFO        0x10

typedef struct _CPPORT
{
    PUCHAR Address;
    ULONG  BaudRate;
    USHORT Flags;
    USHORT Reserved;
    ULONG  EchoDiscard;
} CPPORT, *PCPPORT;

VOID
NTAPI
CpEnableFifo(
    IN PUCHAR  Address,
    IN BOOLEAN Enable
);

VOID
NTAPI
CpSetBaud(
    IN PCPPORT Port,
    IN ULONG   BaudRate
);

NTSTATUS
NTAPI
CpInitialize(
    IN PCPPORT Port,
    IN PUCHAR  Address,
    IN ULONG   BaudRate
);

BOOLEAN
NTAPI
CpDoesPortExist(
    IN PUCHAR Address
);

UCHAR
NTAPI
CpReadLsr(
    IN PCPPORT Port,
    IN UCHAR   ExpectedValue
);

USHORT
NTAPI
CpGetByte(
    IN  PCPPORT Port,
    OUT PUCHAR  Byte,
    IN  BOOLEAN Wait,
    IN  BOOLEAN Poll
);

VOID
NTAPI
CpPutByte(
    IN PCPPORT Port,
    IN UCHAR   Byte
);

VOID
NTAPI
CpPutBuffer(
    IN PCPPORT Port,
    IN PUCHAR  Buffer,
    IN ULONG   Length
);

/* EOF */
