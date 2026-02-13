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

    USHORT SavedFlags = Port->Flags;

    Port->Address  = Address;
    Port->BaudRate = 0;
    Port->Flags    = SavedFlags;
    Port->EchoDiscard = 0;

    /* Mask all interrupts while the debugger owns the UART */
    Pl011WriteRegister(Address, PL011_IMSC_OFFSET, 0);

    if (Port->Flags & CPPORT_FLAG_KEEP_BAUD)
    {
        ULONG Control = Pl011ReadRegister(Address, PL011_CR_OFFSET);

        /* If UART is disabled, fall back to full initialization */
        if ((Control & (PL011_CR_UARTEN | PL011_CR_TXE)) != (PL011_CR_UARTEN | PL011_CR_TXE))
        {
            CpSetBaud(Port, BaudRate);
        }
        else
        {
            /* Preserve existing baud/divisors set by firmware */
            Port->BaudRate = (BaudRate != 0) ? BaudRate : 115200;

            /* Ensure RX/TX are enabled while keeping divisors intact */
            if ((Control & (PL011_CR_TXE | PL011_CR_RXE)) != (PL011_CR_TXE | PL011_CR_RXE))
            {
                Pl011WriteRegister(Address,
                                   PL011_CR_OFFSET,
                                   Control | PL011_CR_UARTEN | PL011_CR_TXE | PL011_CR_RXE);
            }
        }
    }
    else
    {
        CpSetBaud(Port, BaudRate);
    }

    /* Drop any stale bytes queued before KD takes ownership */
    Pl011FlushReceive(Address);

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

    /*
     * Match the x86 cportlib behavior: when Wait=FALSE, try once.
     * The original ARM64 code used (Poll ? 1UL : 0UL) when Wait=FALSE,
     * which meant 0 iterations when Poll=FALSE. But the x86 code uses
     * LimitCount=1 when Wait=FALSE regardless of Poll, meaning it tries once.
     *
     * This is critical for interactive input: the caller (KdbpTryGetCharSerial)
     * loops calling CpGetByte with Wait=FALSE, Poll=FALSE and expects each call
     * to check the UART once and return immediately with success or nodata.
     */
    Timeout = Wait ? PL011_TIMEOUT : 1UL;

    do
    {
        Flags = Pl011ReadRegister(Port->Address, PL011_FR_OFFSET);

        /*
         * Echo suppression logic has been removed.
         *
         * The original echo suppression was designed for half-duplex or loopback
         * scenarios where transmitted bytes would echo back. However, this caused
         * a critical bug: after outputting the "kdb:> " prompt, EchoDiscard would
         * be non-zero, and CpGetByte would refuse to read any RX data because it
         * was waiting for echo characters that would never arrive in a normal
         * serial terminal setup.
         *
         * The x86 cportlib has no echo suppression, and QEMU's PL011 UART does
         * not echo transmitted characters back to RX. Therefore, removing this
         * logic matches the x86 behavior and fixes interactive serial input.
         */

        /* Check if RX FIFO has data (RXFE = RX FIFO Empty, so we want it clear) */
        if ((Flags & PL011_FR_RXFE) == 0)
        {
            *Byte = (UCHAR)(Pl011ReadRegister(Port->Address, PL011_DR_OFFSET) & 0xFF);
            return CP_GET_SUCCESS;
        }

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
    ULONG spins;

    if ((Port == NULL) || (Port->Address == NULL))
    {
        return;
    }

    /*
     * Wait for TX FIFO to have space. Use a bounded wait to avoid
     * wedging if the UART is broken or TX never drains.
     */
    spins = PL011_TIMEOUT;
    while ((Pl011ReadRegister(Port->Address, PL011_FR_OFFSET) & PL011_FR_TXFF) && (spins-- > 0))
    {
        /* ARM64 yield instruction to reduce power and allow other threads */
        __asm__ __volatile__("yield");
    }

    /* Write the byte to the data register */
    Pl011WriteRegister(Port->Address, PL011_DR_OFFSET, (ULONG)Byte);
}

/* EOF */
