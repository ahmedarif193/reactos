/*
 * PROJECT:     ReactOS VMX Hypervisor Driver
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     NS16550A UART emulation (COM1: 0x3F8-0x3FF)
 * COPYRIGHT:   Copyright 2025 Ahmed Arif
 *
 * Emulates a minimal NS16550A UART for guest serial console output.
 * The transmitter is modeled as "instant" - LSR always shows THRE|TEMT.
 * Guest characters written to THR are immediately forwarded to the host
 * via DbgPrint (line-buffered) and stored in the console output ring.
 */

#include <rosv/rosv.h>
#include <rosv/vm.h>
#include <rosv/device.h>

/* Throttle register-level traces after this many accesses */
#define UART_TRACE_THRESHOLD    100

/*
 * Signal the vCPU HLT wake event when the UART raises an interrupt.
 * The Console pointer may be NULL during early init.
 */
FORCEINLINE
VOID
RosvUartWakeVcpu(
    _In_opt_ PROSV_CONSOLE_CONTEXT Console)
{
    if (Console != NULL && Console->OwnerVm != NULL)
        KeSetEvent(&Console->OwnerVm->Vcpu.HaltWakeEvent, IO_NO_INCREMENT, FALSE);
}

VOID
RosvUartInitialize(
    _Inout_ PROSV_UART_STATE Uart,
    _In_ USHORT PortBase)
{
    RtlZeroMemory(Uart, sizeof(*Uart));

    Uart->PortBase = PortBase;

    /* Transmitter starts empty and ready */
    Uart->Lsr = UART_LSR_THRE | UART_LSR_TEMT;

    /* No interrupts pending */
    Uart->Iir = UART_IIR_NO_INT;

    /*
     * Modem status: assert carrier/control inputs like a connected
     * virtual serial cable (avoids guest userspace tty open stalls).
     */
    Uart->Msr = UART_MSR_CTS | UART_MSR_DSR | UART_MSR_DCD;

    /* Default divisor for 115200 baud: DLL=1, DLH=0 */
    Uart->Dll = 1;
    Uart->Dlh = 0;

    /* Initialize TX spin lock */
    KeInitializeSpinLock(&Uart->TxLock);

    /* THR-empty interrupt starts disarmed (no character written yet) */
    Uart->ThrEmptyArmed = FALSE;

    Uart->AccessCount = 0;

    ROSV_TRACE("UART initialized at port 0x%X (THRE|TEMT set, no IRQ)", PortBase);
}

/*
 * RosvUartIn - Handle I/O IN from guest reading a UART register.
 *
 * The Context parameter points to the parent ROSV_CONSOLE_CONTEXT,
 * so we can access both UART state and the console ring buffer.
 */
BOOLEAN
RosvUartIn(
    _In_ PVOID Context,
    _In_ USHORT Port,
    _In_ UCHAR Size,
    _Out_ PULONG Value)
{
    PROSV_CONSOLE_CONTEXT Console = (PROSV_CONSOLE_CONTEXT)Context;
    PROSV_UART_STATE Uart = &Console->Uart;
    USHORT Offset = Port - Uart->PortBase;
    UCHAR Result = 0xFF;

    UNREFERENCED_PARAMETER(Size);

    Uart->AccessCount++;

    /* DLAB=1: offsets 0 and 1 access divisor latch */
    if ((Uart->Lcr & UART_LCR_DLAB) && Offset < 2)
    {
        if (Offset == 0)
        {
            Result = Uart->Dll;
            if (Uart->AccessCount <= UART_TRACE_THRESHOLD)
                ROSV_TRACE("UART IN: DLL = 0x%02X", Result);
        }
        else
        {
            Result = Uart->Dlh;
            if (Uart->AccessCount <= UART_TRACE_THRESHOLD)
                ROSV_TRACE("UART IN: DLH = 0x%02X", Result);
        }
        *Value = Result;
        return TRUE;
    }

    switch (Offset)
    {
    case UART_REG_RBR: /* 0 - Receive Buffer Register */
        if (Uart->RxCount > 0)
        {
            Result = Uart->RxFifo[Uart->RxTail];
            Uart->RxTail = (Uart->RxTail + 1) % ROSV_UART_RX_FIFO_SIZE;
            Uart->RxCount--;

            /* Clear Data Ready if FIFO is now empty */
            if (Uart->RxCount == 0)
            {
                Uart->Lsr &= ~UART_LSR_DR;

                /* RX interrupt source drained. */
                if ((Uart->Iir & 0x0E) == 0x04) /* RDA ID */
                    Uart->Iir = (Uart->Iir & UART_IIR_FIFO_ENABLED) | UART_IIR_NO_INT;

                Uart->InterruptPending = FALSE;
            }
            else if (Uart->Ier & UART_IER_RDI)
            {
                /*
                 * More RX data is still buffered. Keep RDA interrupt
                 * asserted so the guest drains the FIFO.
                 */
                Uart->Iir = (Uart->Iir & UART_IIR_FIFO_ENABLED) | 0x04; /* RDA */
                Uart->InterruptPending = TRUE;
                RosvUartWakeVcpu(Console);
            }

            if (Uart->AccessCount <= UART_TRACE_THRESHOLD)
                ROSV_TRACE("UART IN: RBR = 0x%02X (rx_count=%u)", Result, Uart->RxCount);
        }
        else
        {
            Result = 0x00;
            if (Uart->AccessCount <= UART_TRACE_THRESHOLD)
                ROSV_TRACE("UART IN: RBR = 0x00 (empty)");
        }
        break;

    case UART_REG_IER: /* 1 - Interrupt Enable Register */
        Result = Uart->Ier;
        break;

    case UART_REG_IIR: /* 2 - Interrupt Identification Register */
        Result = Uart->Iir;
        /* If FIFOs are enabled, set the FIFO bits */
        if (Uart->Fcr & UART_FCR_ENABLE)
            Result |= UART_IIR_FIFO_ENABLED;
        /* Reading IIR clears THR empty interrupt condition (edge consumed) */
        if ((Uart->Iir & 0x0E) == 0x02) /* THR empty ID */
        {
            Uart->Iir = UART_IIR_NO_INT;
            Uart->InterruptPending = FALSE;
            Uart->ThrEmptyArmed = FALSE;
        }
        break;

    case UART_REG_LCR: /* 3 - Line Control Register */
        Result = Uart->Lcr;
        break;

    case UART_REG_MCR: /* 4 - Modem Control Register */
        Result = Uart->Mcr;
        break;

    case UART_REG_LSR: /* 5 - Line Status Register */
        /*
         * Dynamically compute LSR:
         * - Bit 0 (DR): set if RX FIFO has data
         * - Bit 1 (OE): overrun error (latched)
         * - Bit 2 (PE): parity error (latched)
         * - Bit 3 (FE): framing error (latched)
         * - Bit 4 (BI): break interrupt (latched)
         * - Bit 5 (THRE): always set (instant transmit)
         * - Bit 6 (TEMT): always set (instant transmit)
         * Error/break bits are cleared on read (per 16550A spec).
         */
        Result = UART_LSR_THRE | UART_LSR_TEMT;
        if (Uart->RxCount > 0)
            Result |= UART_LSR_DR;
        /* Include latched error/break bits, then clear them */
        Result |= (Uart->Lsr & (UART_LSR_OE | UART_LSR_PE | UART_LSR_FE | UART_LSR_BI));
        Uart->Lsr = UART_LSR_THRE | UART_LSR_TEMT; /* Clear error/break bits */
        break;

    case UART_REG_MSR: /* 6 - Modem Status Register */
        Result = Uart->Msr;
        /* Delta bits are cleared on MSR read. */
        Uart->Msr &= ~(UART_MSR_DCTS | UART_MSR_DDSR | UART_MSR_TERI | UART_MSR_DDCD);
        break;

    case UART_REG_SCR: /* 7 - Scratch Register */
        Result = Uart->Scr;
        break;

    default:
        ROSV_WARN("UART IN: unknown offset %u (port=0x%X)", Offset, Port);
        Result = 0xFF;
        break;
    }

    *Value = Result;
    return TRUE;
}

/*
 * RosvUartOut - Handle I/O OUT from guest writing a UART register.
 */
BOOLEAN
RosvUartOut(
    _In_ PVOID Context,
    _In_ USHORT Port,
    _In_ UCHAR Size,
    _In_ ULONG Value)
{
    PROSV_CONSOLE_CONTEXT Console = (PROSV_CONSOLE_CONTEXT)Context;
    PROSV_UART_STATE Uart = &Console->Uart;
    USHORT Offset = Port - Uart->PortBase;
    UCHAR Byte = (UCHAR)Value;
    KIRQL OldIrql;

    UNREFERENCED_PARAMETER(Size);

    Uart->AccessCount++;

    /* DLAB=1: offsets 0 and 1 access divisor latch */
    if ((Uart->Lcr & UART_LCR_DLAB) && Offset < 2)
    {
        if (Offset == 0)
        {
            Uart->Dll = Byte;
            ROSV_TRACE("UART OUT: DLL = 0x%02X", Byte);
        }
        else
        {
            ULONG Divisor;
            Uart->Dlh = Byte;
            Divisor = ((ULONG)Uart->Dlh << 8) | Uart->Dll;
            if (Divisor == 0) Divisor = 1;
            ROSV_TRACE("UART OUT: DLH = 0x%02X, baud rate = %u",
                       Byte, 115200 / Divisor);
        }
        return TRUE;
    }

    switch (Offset)
    {
    case UART_REG_THR: /* 0 - Transmit Holding Register */
        /*
         * Guest is writing a character to the serial port.
         * This is the core output path.
         */

        /* Store in TX ring buffer (protected by spinlock) */
        KeAcquireSpinLock(&Uart->TxLock, &OldIrql);
        if (Uart->TxCount < ROSV_UART_TX_RING_SIZE)
        {
            Uart->TxRing[Uart->TxHead] = Byte;
            Uart->TxHead = (Uart->TxHead + 1) % ROSV_UART_TX_RING_SIZE;
            Uart->TxCount++;
        }
        KeReleaseSpinLock(&Uart->TxLock, OldIrql);

        /* Also store in console output ring */
        RosvConsolePutChar(Console, (CHAR)Byte);

        /*
         * Line-buffered DbgPrint: accumulate until newline, then flush.
         * This avoids one DbgPrint per character which is very slow.
         */
        if (Byte == '\n' || Console->LinePos >= ROSV_CONSOLE_LINE_MAX - 1)
        {
            Console->LineBuf[Console->LinePos] = '\0';
            DbgPrint("[GUEST] %s\n", Console->LineBuf);
            Console->LinePos = 0;
        }
        else if (Byte != '\r') /* Skip bare CR */
        {
            Console->LineBuf[Console->LinePos++] = (CHAR)Byte;
        }

        /*
         * LSR THRE stays set - instant transmit.
         * Arm the THR-empty edge so the next IER/interrupt check can fire.
         */
        Uart->ThrEmptyArmed = TRUE;

        /* If THR empty interrupt is enabled, update IIR */
        if (Uart->Ier & UART_IER_THRI)
        {
            Uart->Iir = (Uart->Iir & UART_IIR_FIFO_ENABLED) | 0x02; /* THR empty */
            Uart->InterruptPending = TRUE;
            RosvUartWakeVcpu(Console);
        }
        break;

    case UART_REG_IER: /* 1 - Interrupt Enable Register */
    {
        UCHAR OldIer = Uart->Ier;
        Uart->Ier = Byte & 0x0F;
        /* Update IIR based on new IER (RDA has higher priority than THRE). */
        if ((Uart->Ier & UART_IER_RDI) && (Uart->RxCount > 0))
        {
            Uart->Iir = (Uart->Iir & UART_IIR_FIFO_ENABLED) | 0x04; /* RDA */
            Uart->InterruptPending = TRUE;
            RosvUartWakeVcpu(Console);
        }
        else if ((Uart->Ier & UART_IER_THRI) && Uart->ThrEmptyArmed)
        {
            /*
             * THR-empty interrupt: only re-arm if a character was written
             * to THR since the last time the guest consumed this condition
             * via an IIR read.  This models the edge-triggered behavior
             * of real 16550A hardware.
             *
             * Additionally, per 16550A spec, enabling THRI when THR is
             * already empty generates a single interrupt.  We treat
             * a 0->1 transition of the THRI bit as an edge event.
             */
            Uart->Iir = (Uart->Iir & UART_IIR_FIFO_ENABLED) | 0x02;
            Uart->InterruptPending = TRUE;
            RosvUartWakeVcpu(Console);
        }
        else if (!(OldIer & UART_IER_THRI) && (Uart->Ier & UART_IER_THRI) &&
                 (Uart->Lsr & UART_LSR_THRE))
        {
            /*
             * 16550A spec: enabling THRI when the transmitter is already
             * empty generates a single one-shot interrupt even without
             * ThrEmptyArmed.  This allows the guest to kickstart TX.
             * Arm the edge so this fires exactly once.
             */
            Uart->ThrEmptyArmed = TRUE;
            Uart->Iir = (Uart->Iir & UART_IIR_FIFO_ENABLED) | 0x02;
            Uart->InterruptPending = TRUE;
            RosvUartWakeVcpu(Console);
        }
        else
        {
            Uart->Iir = (Uart->Iir & UART_IIR_FIFO_ENABLED) | UART_IIR_NO_INT;
            Uart->InterruptPending = FALSE;
        }
        if (Uart->AccessCount <= UART_TRACE_THRESHOLD)
            ROSV_TRACE("UART OUT: IER = 0x%02X (old=0x%02X armed=%d)",
                       Byte, OldIer, Uart->ThrEmptyArmed);
        break;
    }

    case UART_REG_FCR: /* 2 - FIFO Control Register */
        Uart->Fcr = Byte;
        if (Byte & UART_FCR_ENABLE)
        {
            ROSV_TRACE("UART OUT: FIFO enabled (FCR=0x%02X)", Byte);
        }
        if (Byte & UART_FCR_RCVR_RESET)
        {
            /* Clear RX FIFO */
            Uart->RxHead = 0;
            Uart->RxTail = 0;
            Uart->RxCount = 0;
            Uart->Lsr &= ~UART_LSR_DR;

            /* Clear pending RX interrupt source. */
            if ((Uart->Iir & 0x0E) == 0x04) /* RDA ID */
            {
                Uart->Iir = (Uart->Iir & UART_IIR_FIFO_ENABLED) | UART_IIR_NO_INT;
                Uart->InterruptPending = FALSE;
            }

            ROSV_TRACE("UART OUT: RX FIFO reset");
        }
        if (Byte & UART_FCR_TXMT_RESET)
        {
            /* Clear TX - nothing to do, we transmit instantly */
            ROSV_TRACE("UART OUT: TX FIFO reset");
        }
        break;

    case UART_REG_LCR: /* 3 - Line Control Register */
    {
        Uart->Lcr = Byte;

        ROSV_TRACE("UART OUT: LCR = 0x%02X (%d%c%d DLAB=%d)",
                   Byte,
                   (Byte & 0x03) + 5,
                   (Byte & 0x08) ? ((Byte & 0x10) ? 'E' : 'O') : 'N',
                   (Byte & 0x04) ? 2 : 1,
                   (Byte & UART_LCR_DLAB) ? 1 : 0);
        break;
    }

    case UART_REG_MCR: /* 4 - Modem Control Register */
        Uart->Mcr = Byte;
        if (Uart->AccessCount <= UART_TRACE_THRESHOLD)
            ROSV_TRACE("UART OUT: MCR = 0x%02X", Byte);
        break;

    case UART_REG_LSR: /* 5 - Line Status Register (read-only, ignore) */
        ROSV_WARN("UART OUT: write to read-only LSR ignored (value=0x%02X)", Byte);
        break;

    case UART_REG_MSR: /* 6 - Modem Status Register (read-only, ignore) */
        ROSV_WARN("UART OUT: write to read-only MSR ignored (value=0x%02X)", Byte);
        break;

    case UART_REG_SCR: /* 7 - Scratch Register */
        Uart->Scr = Byte;
        break;

    default:
        ROSV_WARN("UART OUT: unknown offset %u (port=0x%X value=0x%02X)",
                  Offset, Port, Byte);
        break;
    }

    return TRUE;
}

/*
 * RosvUartPushInput - Push bytes from host into the guest RX FIFO.
 *
 * Called by the console bridge when the host has input data for the guest.
 * Sets Data Ready in LSR and triggers RX interrupt if enabled.
 */
VOID
RosvUartPushInput(
    _Inout_ PROSV_UART_STATE Uart,
    _In_reads_(Length) PUCHAR Data,
    _In_ ULONG Length)
{
    ULONG i;

    for (i = 0; i < Length; i++)
    {
        if (Uart->RxCount >= ROSV_UART_RX_FIFO_SIZE)
        {
            ROSV_WARN("UART RX FIFO overflow: dropping %u bytes", Length - i);
            Uart->Lsr |= UART_LSR_OE; /* Set overrun error */
            break;
        }

        Uart->RxFifo[Uart->RxHead] = Data[i];
        Uart->RxHead = (Uart->RxHead + 1) % ROSV_UART_RX_FIFO_SIZE;
        Uart->RxCount++;
    }

    /* Set Data Ready bit */
    if (Uart->RxCount > 0)
    {
        Uart->Lsr |= UART_LSR_DR;

        /* If RX data interrupt is enabled, signal it */
        if (Uart->Ier & UART_IER_RDI)
        {
            Uart->Iir = (Uart->Iir & UART_IIR_FIFO_ENABLED) | 0x04; /* RDA */
            Uart->InterruptPending = TRUE;
            /* Wake vCPU if halted.  Derive Console from embedded Uart field. */
            RosvUartWakeVcpu(CONTAINING_RECORD(Uart, struct _ROSV_CONSOLE_CONTEXT, Uart));
        }
    }

    UNREFERENCED_PARAMETER(i);
}
