/*
 * PROJECT:     ReactOS VMX Hypervisor Driver
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     I/O port dispatch and device emulation structures
 * COPYRIGHT:   Copyright 2025 Ahmed Arif
 */

#pragma once

#include <rosv/rosv.h>

/* ---- I/O handler types -------------------------------------------------- */

typedef BOOLEAN
(*PROSV_IO_IN_HANDLER)(
    _In_ PVOID Context,
    _In_ USHORT Port,
    _In_ UCHAR Size,        /* 1, 2, or 4 bytes */
    _Out_ PULONG Value);

typedef BOOLEAN
(*PROSV_IO_OUT_HANDLER)(
    _In_ PVOID Context,
    _In_ USHORT Port,
    _In_ UCHAR Size,
    _In_ ULONG Value);

/* ---- I/O handler registration ------------------------------------------- */

#define ROSV_MAX_IO_HANDLERS    64

typedef struct _ROSV_IO_HANDLER {
    USHORT PortBase;
    USHORT PortCount;           /* Number of consecutive ports */
    PROSV_IO_IN_HANDLER InHandler;
    PROSV_IO_OUT_HANDLER OutHandler;
    PVOID Context;
} ROSV_IO_HANDLER, *PROSV_IO_HANDLER;

/* ---- NS16550A UART registers -------------------------------------------- */

#define UART_COM1_BASE          0x3F8
#define UART_COM1_IRQ           4
#define UART_PORT_COUNT         8

/* Register offsets from base */
#define UART_REG_RBR            0   /* Receive Buffer (read, DLAB=0) */
#define UART_REG_THR            0   /* Transmit Holding (write, DLAB=0) */
#define UART_REG_DLL            0   /* Divisor Latch Low (DLAB=1) */
#define UART_REG_IER            1   /* Interrupt Enable (DLAB=0) */
#define UART_REG_DLH            1   /* Divisor Latch High (DLAB=1) */
#define UART_REG_IIR            2   /* Interrupt Identification (read) */
#define UART_REG_FCR            2   /* FIFO Control (write) */
#define UART_REG_LCR            3   /* Line Control */
#define UART_REG_MCR            4   /* Modem Control */
#define UART_REG_LSR            5   /* Line Status */
#define UART_REG_MSR            6   /* Modem Status */
#define UART_REG_SCR            7   /* Scratch */

/* LSR bits */
#define UART_LSR_DR             (1 << 0)   /* Data Ready */
#define UART_LSR_OE             (1 << 1)   /* Overrun Error */
#define UART_LSR_PE             (1 << 2)   /* Parity Error */
#define UART_LSR_FE             (1 << 3)   /* Framing Error */
#define UART_LSR_BI             (1 << 4)   /* Break Interrupt */
#define UART_LSR_THRE           (1 << 5)   /* THR Empty */
#define UART_LSR_TEMT           (1 << 6)   /* Transmitter Empty */

/* LCR bits */
#define UART_LCR_DLAB           (1 << 7)   /* Divisor Latch Access Bit */

/* IER bits */
#define UART_IER_RDI            (1 << 0)   /* Received Data Available */
#define UART_IER_THRI           (1 << 1)   /* THR Empty */
#define UART_IER_RLSI           (1 << 2)   /* Receiver Line Status */
#define UART_IER_MSI            (1 << 3)   /* Modem Status */

/* IIR bits */
#define UART_IIR_NO_INT         (1 << 0)   /* No Interrupt Pending */
#define UART_IIR_FIFO_ENABLED   0xC0       /* FIFOs enabled */

/* MSR bits */
#define UART_MSR_DCTS           (1 << 0)   /* Delta CTS */
#define UART_MSR_DDSR           (1 << 1)   /* Delta DSR */
#define UART_MSR_TERI           (1 << 2)   /* Trailing edge RI */
#define UART_MSR_DDCD           (1 << 3)   /* Delta DCD */
#define UART_MSR_CTS            (1 << 4)   /* CTS */
#define UART_MSR_DSR            (1 << 5)   /* DSR */
#define UART_MSR_RI             (1 << 6)   /* RI */
#define UART_MSR_DCD            (1 << 7)   /* DCD */

/* FCR bits */
#define UART_FCR_ENABLE         0x01
#define UART_FCR_RCVR_RESET     0x02
#define UART_FCR_TXMT_RESET     0x04

/* ---- UART emulation state ----------------------------------------------- */

#define ROSV_UART_RX_FIFO_SIZE  256
#define ROSV_UART_TX_RING_SIZE  4096

typedef struct _ROSV_UART_STATE {
    /* Port base address */
    USHORT PortBase;

    /* Registers */
    UCHAR Ier;                  /* Interrupt Enable Register */
    UCHAR Iir;                  /* Interrupt Identification Register */
    UCHAR Fcr;                  /* FIFO Control Register */
    UCHAR Lcr;                  /* Line Control Register */
    UCHAR Mcr;                  /* Modem Control Register */
    UCHAR Lsr;                  /* Line Status Register */
    UCHAR Msr;                  /* Modem Status Register */
    UCHAR Scr;                  /* Scratch Register */
    UCHAR Dll;                  /* Divisor Latch Low */
    UCHAR Dlh;                  /* Divisor Latch High */

    /* RX FIFO (host -> guest input) */
    UCHAR RxFifo[ROSV_UART_RX_FIFO_SIZE];
    ULONG RxHead;
    ULONG RxTail;
    ULONG RxCount;

    /* TX ring buffer (guest -> host output) */
    UCHAR TxRing[ROSV_UART_TX_RING_SIZE];
    ULONG TxHead;
    ULONG TxTail;
    ULONG TxCount;
    KSPIN_LOCK TxLock;

    /* Interrupt state */
    BOOLEAN InterruptPending;

    /*
     * THR-empty interrupt edge trigger.
     *
     * Real 16550A hardware fires the THR-empty interrupt only on the
     * *transition* from transmitter-busy to transmitter-empty.  Once
     * the guest reads IIR and clears the THR-empty condition, it must
     * NOT re-fire until a new character is actually written to THR.
     *
     * Without this flag the emulation re-arms the THR-empty interrupt
     * on every IER write that has THRI set (because THRE is always set
     * in our instant-transmit model).  That creates an infinite
     * interrupt storm: the guest ISR reads IIR, writes IER, gets
     * another IRQ, reads IIR, writes IER, ...
     *
     * Set TRUE when a character is written to THR (edge detected).
     * Cleared when the guest reads IIR and sees THR-empty (edge consumed).
     */
    BOOLEAN ThrEmptyArmed;

    /* Debug throttle */
    ULONG AccessCount;
} ROSV_UART_STATE, *PROSV_UART_STATE;

/* ---- RTL8139 NIC emulation state ---------------------------------------- */

#define ROSV_RTL8139_IO_BASE       0xC000
#define ROSV_RTL8139_IO_SIZE       0x100
#define ROSV_RTL8139_PACKET_QUEUE_DEPTH 8

typedef struct _ROSV_RTL8139_STATE {
    UCHAR Mac[6];
    UCHAR Mar[8];

    ULONG TxStatus[4];
    ULONG TxAddr[4];

    ULONG RxBuf;
    USHORT RxBufPtr;
    USHORT RxBufAddr;

    USHORT IntrMask;
    USHORT IntrStatus;

    ULONG TxConfig;
    ULONG RxConfig;

    UCHAR ChipCmd;
    ULONG RxPendingCount;
    UCHAR Cfg9346;
    UCHAR EepromPrevClock;
    UCHAR EepromPrevCs;
    UCHAR EepromDoBit;
    BOOLEAN EepromInputStarted;
    BOOLEAN EepromOutputActive;
    UCHAR EepromOpcode;
    UCHAR EepromOpcodeBits;
    UCHAR EepromAddress;
    UCHAR EepromAddressBits;
    USHORT EepromShiftOut;
    UCHAR EepromShiftOutBits;
    UCHAR Config1;
    UCHAR MediaStatus;
    USHORT BasicModeStatus;

    USHORT PciCommand;
    ULONG PciBar0;
    UCHAR PciInterruptLine;
    UCHAR PciInterruptPin;
    BOOLEAN Bar0Probe;
    BOOLEAN InterruptPending;

    PVOID OwnerVm;

    KSPIN_LOCK NetLock;
    ROSV_NET_PACKET TxQueue[ROSV_RTL8139_PACKET_QUEUE_DEPTH];
    ULONG TxQueueHead;
    ULONG TxQueueTail;
    ULONG TxQueueCount;
} ROSV_RTL8139_STATE, *PROSV_RTL8139_STATE;

/* ---- Console context (output ring buffer) ------------------------------- */

#define ROSV_CONSOLE_BUFFER_SIZE    8192
#define ROSV_CONSOLE_LINE_MAX       256

struct _ROSV_CONSOLE_CONTEXT {
    /* Owning VM instance (for interrupt-controller/MMIO coordination). */
    PROSV_VM OwnerVm;

    /* Active PTY splice: if set, UART output is also pushed through PTY */
    PROSV_PTY_STATE ActivePty;

    /* Output buffer (guest -> host) */
    CHAR OutputBuffer[ROSV_CONSOLE_BUFFER_SIZE];
    ULONG OutputHead;           /* Next write position */
    ULONG OutputTail;           /* Next read position */
    KSPIN_LOCK OutputLock;

    /* Line accumulator for batched DbgPrint output */
    CHAR LineBuf[ROSV_CONSOLE_LINE_MAX];
    ULONG LinePos;

    /* UART instance */
    ROSV_UART_STATE Uart;

    /* RTL8139 NIC instance */
    ROSV_RTL8139_STATE Rtl8139;

    /* I/O dispatch table */
    ROSV_IO_HANDLER IoHandlers[ROSV_MAX_IO_HANDLERS];
    ULONG IoHandlerCount;
};

/* ---- Function prototypes (device/) -------------------------------------- */

/* io.c - I/O port dispatch */
NTSTATUS
RosvIoInitialize(
    _Inout_ PROSV_CONSOLE_CONTEXT Console);

BOOLEAN
RosvIoRegisterHandler(
    _Inout_ PROSV_CONSOLE_CONTEXT Console,
    _In_ USHORT PortBase,
    _In_ USHORT PortCount,
    _In_ PROSV_IO_IN_HANDLER InHandler,
    _In_ PROSV_IO_OUT_HANDLER OutHandler,
    _In_ PVOID Context);

BOOLEAN
RosvIoHandleIn(
    _In_ PROSV_CONSOLE_CONTEXT Console,
    _In_ USHORT Port,
    _In_ UCHAR Size,
    _Out_ PULONG Value);

BOOLEAN
RosvIoHandleOut(
    _In_ PROSV_CONSOLE_CONTEXT Console,
    _In_ USHORT Port,
    _In_ UCHAR Size,
    _In_ ULONG Value);

BOOLEAN
RosvPicRequestIrq(
    _In_ UCHAR Irq,
    _Out_ PUCHAR Vector);

VOID
RosvPicClearAllIsr(VOID);

BOOLEAN
RosvPicRequestTimerIrq(
    _Out_ PUCHAR Vector);

BOOLEAN
RosvInterruptRequestIrq(
    _In_ PROSV_VM Vm,
    _In_ UCHAR Irq,
    _Out_ PUCHAR Vector);

BOOLEAN
RosvInterruptRequestTimer(
    _In_ PROSV_VM Vm,
    _Out_ PUCHAR Vector);

BOOLEAN
RosvInterruptHasPendingTimer(
    _In_ PROSV_VM Vm);

/* rtl8139.c - Realtek RTL8139 emulation */
VOID
RosvRtl8139Initialize(
    _Inout_ PROSV_RTL8139_STATE Nic);

VOID
RosvRtl8139SetOwnerVm(
    _Inout_ PROSV_RTL8139_STATE Nic,
    _In_opt_ PVOID Vm);

ULONG
RosvRtl8139PciConfigRead(
    _In_ PROSV_RTL8139_STATE Nic,
    _In_ ULONG Offset);

VOID
RosvRtl8139PciConfigWrite(
    _Inout_ PROSV_RTL8139_STATE Nic,
    _In_ ULONG Offset,
    _In_ ULONG Value);

BOOLEAN
RosvRtl8139IoIn(
    _In_ PVOID Context,
    _In_ USHORT Port,
    _In_ UCHAR Size,
    _Out_ PULONG Value);

BOOLEAN
RosvRtl8139IoOut(
    _In_ PVOID Context,
    _In_ USHORT Port,
    _In_ UCHAR Size,
    _In_ ULONG Value);

BOOLEAN
RosvRtl8139DequeueTxPacket(
    _Inout_ PROSV_RTL8139_STATE Nic,
    _Out_ PROSV_NET_PACKET PacketOut);

BOOLEAN
RosvRtl8139InjectRxPacket(
    _Inout_ PROSV_RTL8139_STATE Nic,
    _In_reads_bytes_(Length) const UCHAR *Data,
    _In_ ULONG Length);

BOOLEAN
RosvRtl8139HasPendingInterrupt(
    _In_ PROSV_RTL8139_STATE Nic);

VOID
RosvRtl8139ClearPendingInterrupt(
    _Inout_ PROSV_RTL8139_STATE Nic);

/* uart.c - NS16550A UART emulation */
VOID
RosvUartInitialize(
    _Inout_ PROSV_UART_STATE Uart,
    _In_ USHORT PortBase);

BOOLEAN
RosvUartIn(
    _In_ PVOID Context,
    _In_ USHORT Port,
    _In_ UCHAR Size,
    _Out_ PULONG Value);

BOOLEAN
RosvUartOut(
    _In_ PVOID Context,
    _In_ USHORT Port,
    _In_ UCHAR Size,
    _In_ ULONG Value);

VOID
RosvUartPushInput(
    _Inout_ PROSV_UART_STATE Uart,
    _In_reads_(Length) PUCHAR Data,
    _In_ ULONG Length);

/* console.c - Host console bridge */
NTSTATUS
RosvConsoleInitialize(
    _Out_ PROSV_CONSOLE_CONTEXT *Console);

VOID
RosvConsoleDestroy(
    _Inout_ PROSV_CONSOLE_CONTEXT Console);

VOID
RosvConsolePutChar(
    _Inout_ PROSV_CONSOLE_CONTEXT Console,
    _In_ CHAR Ch);

NTSTATUS
RosvConsoleRead(
    _In_ PROSV_CONSOLE_CONTEXT Console,
    _Out_writes_(BufferSize) PCHAR Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesRead);

VOID
RosvConsolePumpPtyInput(
    _Inout_ PROSV_CONSOLE_CONTEXT Console);
