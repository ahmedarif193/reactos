/*
 * PROJECT:     ReactOS VMX Hypervisor Driver
 * LICENSE:     GPL-2.0+ (https://spdx.org/licenses/GPL-2.0+)
 * PURPOSE:     Host console bridge - connects UART emulation to host output
 * COPYRIGHT:   Copyright 2025 Ahmed Arif
 *
 * The console context owns the UART state, the I/O dispatch table, and
 * the output ring buffer. Guest serial output (THR writes) flows through
 * the UART emulator into both the ring buffer (for IOCTL retrieval) and
 * DbgPrint (for immediate host-side visibility via serial/debugger).
 */

#include <rosv/rosv.h>
#include <rosv/device.h>
#include <rosv/pty.h>

NTSTATUS
RosvConsoleInitialize(
    _Out_ PROSV_CONSOLE_CONTEXT *Console)
{
    PROSV_CONSOLE_CONTEXT Ctx;

    ROSV_TRACE("RosvConsoleInitialize: allocating console context (%u bytes)",
               (ULONG)sizeof(ROSV_CONSOLE_CONTEXT));

    Ctx = ExAllocatePoolWithTag(NonPagedPool, sizeof(ROSV_CONSOLE_CONTEXT), ROSV_DRIVER_TAG);
    if (!Ctx)
    {
        ROSV_ERR("RosvConsoleInitialize: failed to allocate console context");
        *Console = NULL;
        return STATUS_INSUFFICIENT_RESOURCES;
    }

    RtlZeroMemory(Ctx, sizeof(*Ctx));
    KeInitializeSpinLock(&Ctx->OutputLock);

    /* Initialize UART at COM1 base */
    RosvUartInitialize(&Ctx->Uart, UART_COM1_BASE);

    /* Initialize the I/O dispatch table with common handlers */
    RosvIoInitialize(Ctx);

    /*
     * Register the UART I/O handler for COM1 (0x3F8-0x3FF).
     * The context pointer is the CONSOLE context (not just UART),
     * because the UART write handler needs to access the console
     * output ring and line buffer.
     */
    RosvIoRegisterHandler(Ctx,
                          UART_COM1_BASE,
                          UART_PORT_COUNT,
                          RosvUartIn,
                          RosvUartOut,
                          Ctx);

    *Console = Ctx;
    ROSV_TRACE("RosvConsoleInitialize: console ready at %p (COM1 0x%X-%X)",
               Ctx, UART_COM1_BASE, UART_COM1_BASE + UART_PORT_COUNT - 1);
    return STATUS_SUCCESS;
}

VOID
RosvConsoleDestroy(
    _Inout_ PROSV_CONSOLE_CONTEXT Console)
{
    if (!Console)
    {
        ROSV_WARN("RosvConsoleDestroy: called with NULL");
        return;
    }

    /* Flush any partial line remaining in the line buffer */
    if (Console->LinePos > 0)
    {
        Console->LineBuf[Console->LinePos] = '\0';
        DbgPrint("[GUEST] %s\n", Console->LineBuf);
        Console->LinePos = 0;
    }

    ROSV_TRACE("RosvConsoleDestroy: freeing console context %p "
               "(uart accesses=%u, output_head=%u)",
               Console, Console->Uart.AccessCount, Console->OutputHead);
    ExFreePoolWithTag(Console, ROSV_DRIVER_TAG);
}

/*
 * RosvConsolePutChar - Add a character to the console output ring buffer.
 *
 * This is called from the UART THR write handler. The ring buffer allows
 * usermode to retrieve guest output via IOCTL_GET_LOG / CONSOLE_READ.
 *
 * If an ActivePty is set, the byte is also pushed into the PTY's OutputBuf
 * (bypassing OPOST since the guest tty layer already formatted the output).
 * This lets PTY_READ return guest serial output.
 */
VOID
RosvConsolePutChar(
    _Inout_ PROSV_CONSOLE_CONTEXT Console,
    _In_ CHAR Ch)
{
    KIRQL OldIrql;
    PROSV_PTY_STATE ActivePty;

    if (Console == NULL)
        return;

    KeAcquireSpinLock(&Console->OutputLock, &OldIrql);

    Console->OutputBuffer[Console->OutputHead] = Ch;
    Console->OutputHead = (Console->OutputHead + 1) % ROSV_CONSOLE_BUFFER_SIZE;

    /* On overflow, advance tail (discard oldest) */
    if (Console->OutputHead == Console->OutputTail)
    {
        Console->OutputTail = (Console->OutputTail + 1) % ROSV_CONSOLE_BUFFER_SIZE;
    }

    ActivePty = Console->ActivePty;
    KeReleaseSpinLock(&Console->OutputLock, OldIrql);

    /* Splice to PTY if active: push raw byte into PTY OutputBuf */
    if (ActivePty != NULL)
    {
        UCHAR Byte = (UCHAR)Ch;
        ULONG Written;
        RosvPtySlaveWriteRaw(ActivePty, &Byte, 1, &Written);
    }
}

/*
 * RosvConsoleRead - Read accumulated guest output from the ring buffer.
 *
 * Returns up to BufferSize bytes of guest serial output. Non-blocking:
 * returns immediately with whatever is available.
 */
NTSTATUS
RosvConsoleRead(
    _In_ PROSV_CONSOLE_CONTEXT Console,
    _Out_writes_(BufferSize) PCHAR Buffer,
    _In_ ULONG BufferSize,
    _Out_ PULONG BytesRead)
{
    ULONG Count = 0;
    KIRQL OldIrql;

    if (BytesRead == NULL || Buffer == NULL || Console == NULL)
        return STATUS_INVALID_PARAMETER;

    KeAcquireSpinLock(&Console->OutputLock, &OldIrql);

    while (Console->OutputTail != Console->OutputHead && Count < BufferSize)
    {
        Buffer[Count++] = Console->OutputBuffer[Console->OutputTail];
        Console->OutputTail = (Console->OutputTail + 1) % ROSV_CONSOLE_BUFFER_SIZE;
    }

    KeReleaseSpinLock(&Console->OutputLock, OldIrql);

    *BytesRead = Count;
    return STATUS_SUCCESS;
}

VOID
RosvConsolePumpPtyInput(
    _Inout_ PROSV_CONSOLE_CONTEXT Console)
{
    UCHAR DrainBuf[128];
    ULONG TotalDrained = 0;

    if (Console == NULL || Console->ActivePty == NULL)
        return;

    while (Console->Uart.RxCount < ROSV_UART_RX_FIFO_SIZE)
    {
        NTSTATUS Status;
        ULONG UartSpace;
        ULONG ToRead;
        ULONG DrainRead = 0;

        UartSpace = ROSV_UART_RX_FIFO_SIZE - Console->Uart.RxCount;
        if (UartSpace == 0)
            break;

        ToRead = (UartSpace < sizeof(DrainBuf)) ? UartSpace : sizeof(DrainBuf);
        Status = RosvPtySlaveRead(Console->ActivePty, DrainBuf, ToRead, &DrainRead);
        if (!NT_SUCCESS(Status) || DrainRead == 0)
            break;

        RosvUartPushInput(&Console->Uart, DrainBuf, DrainRead);
        TotalDrained += DrainRead;

        if (DrainRead < ToRead)
            break;
    }
    if (TotalDrained > 0)
    {
        ROSV_TRACE("RosvConsolePumpPtyInput: drained %u bytes into UART RX",
                   TotalDrained);
    }
}
