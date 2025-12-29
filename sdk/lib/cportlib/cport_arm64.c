/*
 * PROJECT:         ReactOS ComPort Library (ARM64 PL011 backend)
 * LICENSE:         BSD - See COPYING.ARM in the top level directory
 * PURPOSE:         Provide a serial port helper for the kernel debugger on ARM64
 * PROGRAMMERS:     ReactOS Portable Systems Group
 */

/* INCLUDES *******************************************************************/

#include <ioaccess.h>
#include <ntstatus.h>
#include <cportlib/cportlib.h>
#include <drivers/serial/ns16550.h>

/* PL011 UART register offsets *************************************************/

#define PL011_DR_OFFSET     0x00
#define PL011_FR_OFFSET     0x18
#define PL011_IBRD_OFFSET   0x24
#define PL011_FBRD_OFFSET   0x28
#define PL011_LCRH_OFFSET   0x2C
#define PL011_CR_OFFSET     0x30
#define PL011_IMSC_OFFSET   0x38

#define PL011_FR_RXFE       0x10
#define PL011_FR_TXFF       0x20
#define PL011_FR_BUSY       0x08
#define PL011_FR_TXFE       0x80

#define PL011_LCRH_WLEN_8   0x60
#define PL011_LCRH_FEN      0x10

#define PL011_CR_UARTEN     0x0001
#define PL011_CR_TXE        0x0100
#define PL011_CR_RXE        0x0200

#define PL011_INPUT_CLOCK   24000000UL
#define PL011_TIMEOUT       (1024 * 512)
#define CPPORT_ECHO_GUARD_MAX   0x10000UL

FORCEINLINE
BOOLEAN
Pl011TxBusy(
    _In_ ULONG Flags)
{
    return (Flags & (PL011_FR_TXFF | PL011_FR_BUSY)) != 0;
}

/* HELPERS ********************************************************************/

static inline
ULONG
Pl011ReadRegister(
    _In_ PUCHAR Base,
    _In_ ULONG Offset)
{
    return READ_REGISTER_ULONG((volatile ULONG *)((ULONG_PTR)Base + Offset));
}

static inline
VOID
Pl011WriteRegister(
    _In_ PUCHAR Base,
    _In_ ULONG Offset,
    _In_ ULONG Value)
{
    WRITE_REGISTER_ULONG((volatile ULONG *)((ULONG_PTR)Base + Offset), Value);
}

static
VOID
Pl011ConfigureBaudRate(
    _In_ PUCHAR Base,
    _In_ ULONG BaudRate)
{
    ULONG Divider;
    ULONG Remainder;
    ULONG Fraction;

    if (BaudRate == 0)
    {
        BaudRate = 115200;
    }

    Divider = PL011_INPUT_CLOCK / (16 * BaudRate);
    Remainder = PL011_INPUT_CLOCK % (16 * BaudRate);
    Fraction = (ULONG)(((ULONGLONG)Remainder * 64ULL + (BaudRate / 2)) / (16 * BaudRate));

    Pl011WriteRegister(Base, PL011_IBRD_OFFSET, Divider);
    Pl011WriteRegister(Base, PL011_FBRD_OFFSET, Fraction);
}

static
VOID
Pl011FlushReceive(
    _In_ PUCHAR Base)
{
    ULONG Guard;

    if (Base == NULL)
    {
        return;
    }

    for (Guard = PL011_TIMEOUT; Guard > 0; Guard--)
    {
        ULONG Flags = Pl011ReadRegister(Base, PL011_FR_OFFSET);

        if (Flags & PL011_FR_RXFE)
        {
            break;
        }

        (VOID)Pl011ReadRegister(Base, PL011_DR_OFFSET);
    }
}

/* PUBLIC INTERFACE ************************************************************/

VOID
NTAPI
CpEnableFifo(
    IN PUCHAR Address,
    IN BOOLEAN Enable)
{
    ULONG Value;

    if (Address == NULL)
    {
        return;
    }

    Value = Pl011ReadRegister(Address, PL011_LCRH_OFFSET);
    if (Enable)
        Value |= PL011_LCRH_FEN;
    else
        Value &= ~PL011_LCRH_FEN;

    Pl011WriteRegister(Address, PL011_LCRH_OFFSET, Value);
}

VOID
NTAPI
CpSetBaud(
    IN PCPPORT Port,
    IN ULONG BaudRate)
{
    PUCHAR Address;
    ULONG Control;

    if ((Port == NULL) || (Port->Address == NULL))
    {
        return;
    }

    Address = Port->Address;

    /* Disable the UART before reprogramming */
    Control = Pl011ReadRegister(Address, PL011_CR_OFFSET);
    Pl011WriteRegister(Address, PL011_CR_OFFSET, Control & ~(PL011_CR_UARTEN | PL011_CR_TXE | PL011_CR_RXE));

    Pl011ConfigureBaudRate(Address, BaudRate);

    /* 8 data bits, FIFO enabled */
    Pl011WriteRegister(Address, PL011_LCRH_OFFSET, PL011_LCRH_WLEN_8 | PL011_LCRH_FEN);

    /* Re-enable transmit and receive */
    Pl011WriteRegister(Address, PL011_CR_OFFSET, PL011_CR_UARTEN | PL011_CR_TXE | PL011_CR_RXE);

    Port->BaudRate = BaudRate;
}

NTSTATUS
NTAPI
CpInitialize(
    IN PCPPORT Port,
    IN PUCHAR Address,
    IN ULONG BaudRate)
{
    if ((Port == NULL) || (Address == NULL))
    {
        return STATUS_INVALID_PARAMETER;
    }

    Port->Address  = Address;
    Port->BaudRate = 0;
    Port->Flags    = 0;
    Port->EchoDiscard = 0;

    /* Mask all interrupts while the debugger owns the UART */
    Pl011WriteRegister(Address, PL011_IMSC_OFFSET, 0);

    CpSetBaud(Port, BaudRate);

    /* Drop any stale bytes queued before KD takes ownership */
    Pl011FlushReceive(Address);
    Port->EchoDiscard = 0;
    Port->Flags &= ~CPPORT_FLAG_SUPPRESS_ECHO;

    return STATUS_SUCCESS;
}

BOOLEAN
NTAPI
CpDoesPortExist(
    IN PUCHAR Address)
{
    if (Address == NULL)
    {
        return FALSE;
    }

    /* Attempt to read the flag register; treat all-zero/all-one as failure */
    return (Pl011ReadRegister(Address, PL011_FR_OFFSET) != 0xFFFFFFFFUL);
}

UCHAR
NTAPI
CpReadLsr(
    IN PCPPORT Port,
    IN UCHAR ExpectedValue)
{
    ULONG Flags;
    UCHAR LineStatus = 0;

    UNREFERENCED_PARAMETER(ExpectedValue);

    if ((Port == NULL) || (Port->Address == NULL))
    {
        return 0;
    }

    Flags = Pl011ReadRegister(Port->Address, PL011_FR_OFFSET);

    if ((Flags & PL011_FR_RXFE) == 0)
    {
        LineStatus |= SERIAL_LSR_DR;
    }

    if ((Flags & PL011_FR_TXFF) == 0)
    {
        LineStatus |= SERIAL_LSR_THRE | SERIAL_LSR_TEMT;
    }

    return LineStatus;
}

USHORT
NTAPI
CpGetByte(
    IN  PCPPORT Port,
    OUT PUCHAR Byte,
    IN  BOOLEAN Wait,
    IN  BOOLEAN Poll)
{
    ULONG Timeout;
    ULONG Flags;

    if ((Port == NULL) || (Port->Address == NULL) || (Byte == NULL))
    {
        return CP_GET_ERROR;
    }

    Timeout = Wait ? PL011_TIMEOUT : (Poll ? 1UL : 0UL);

    do
    {
        Flags = Pl011ReadRegister(Port->Address, PL011_FR_OFFSET);

        if ((Port->Flags & CPPORT_FLAG_SUPPRESS_ECHO) != 0)
        {
            if (Pl011TxBusy(Flags) || (Port->EchoDiscard != 0))
            {
                /* TX path still draining: do not treat RX as data yet. */
                goto WaitOrExit;
            }

            Port->Flags &= ~CPPORT_FLAG_SUPPRESS_ECHO;
            Port->EchoDiscard = 0;
        }

        if (Port->EchoDiscard != 0)
        {
            if ((Flags & PL011_FR_RXFE) == 0)
            {
                (VOID)Pl011ReadRegister(Port->Address, PL011_DR_OFFSET);
                Port->EchoDiscard--;
                continue;
            }
        }

        if ((Flags & PL011_FR_RXFE) == 0)
        {
            *Byte = (UCHAR)(Pl011ReadRegister(Port->Address, PL011_DR_OFFSET) & 0xFF);
            return CP_GET_SUCCESS;
        }

WaitOrExit:
        if (!Wait)
        {
            return CP_GET_NODATA;
        }

    } while (Timeout-- > 0);

    return CP_GET_NODATA;
}

VOID
NTAPI
CpPutByte(
    IN PCPPORT Port,
    IN UCHAR Byte)
{
    ULONG Flags;
    BOOLEAN RecordEcho = FALSE;

    if ((Port == NULL) || (Port->Address == NULL))
    {
        return;
    }

    /* Bounded wait to avoid wedging if TX never drains */
    {
        ULONG spins = PL011_TIMEOUT;
        while ((Pl011ReadRegister(Port->Address, PL011_FR_OFFSET) & PL011_FR_TXFF) && (spins-- > 0))
        {
            __asm__ __volatile__("yield");
        }
        /* If it still looks full, try once anyway */
    }

    Flags = Pl011ReadRegister(Port->Address, PL011_FR_OFFSET);
    if ((Flags & PL011_FR_RXFE) != 0)
    {
        Port->Flags |= CPPORT_FLAG_SUPPRESS_ECHO;
        RecordEcho = TRUE;
    }
    else
    {
        Port->Flags &= ~CPPORT_FLAG_SUPPRESS_ECHO;
    }

    Pl011WriteRegister(Port->Address, PL011_DR_OFFSET, (ULONG)Byte);

    if (RecordEcho && Port->EchoDiscard < CPPORT_ECHO_GUARD_MAX)
    {
        Port->EchoDiscard++;
    }
}

/* EOF */
