/*
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Windows NT PowerPC early kernel console (PReP 16550)
 *
 * The loader maps the PReP I/O space through DBAT1 at IsaIoVirtualBase, so
 * the UART is reachable before the memory manager runs.
 */

#include <ntoskrnl.h>

#define UART_THR 0
#define UART_RBR 0
#define UART_LSR 5
#define UART_LSR_DR   0x01
#define UART_LSR_THRE 0x20

static volatile UCHAR *KiPpcConsoleBase;

static
UCHAR
KiPpcConsoleRead(_In_ ULONG Register)
{
    UCHAR Value = KiPpcConsoleBase[Register];

    __asm__ __volatile__("eieio" ::: "memory");
    return Value;
}

static
VOID
KiPpcConsoleWriteRegister(_In_ ULONG Register, _In_ UCHAR Value)
{
    KiPpcConsoleBase[Register] = Value;
    __asm__ __volatile__("eieio" ::: "memory");
}

VOID
NTAPI
KiPpcConsoleInitialize(_In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PPPC_LOADER_BLOCK PpcBlock = &LoaderBlock->u.PowerPC;

    /* The loader already programmed the line; only adopt it. */
    if ((PpcBlock->Flags & PPC_LOADER_FLAG_EARLY_CONSOLE) && PpcBlock->IsaIoVirtualBase && PpcBlock->EarlyConsolePort && (PpcBlock->EarlyConsolePort < 0x10000))
        KiPpcConsoleBase = (volatile UCHAR *)(PpcBlock->IsaIoVirtualBase + PpcBlock->EarlyConsolePort);
}

BOOLEAN
NTAPI
KiPpcConsoleReady(VOID)
{
    return KiPpcConsoleBase != NULL;
}

VOID
NTAPI
KiPpcConsolePutByte(_In_ UCHAR Byte)
{
    ULONG Spin;

    if (!KiPpcConsoleBase)
        return;
    for (Spin = 0; (Spin < 100000) && !(KiPpcConsoleRead(UART_LSR) & UART_LSR_THRE); Spin++)
        YieldProcessor();
    KiPpcConsoleWriteRegister(UART_THR, Byte);
}

VOID
NTAPI
KiPpcConsoleWrite(_In_reads_bytes_(Length) PCCH Buffer, _In_ SIZE_T Length)
{
    SIZE_T Index;

    for (Index = 0; Index < Length; Index++)
    {
        if (Buffer[Index] == '\n')
            KiPpcConsolePutByte('\r');
        KiPpcConsolePutByte((UCHAR)Buffer[Index]);
    }
}

BOOLEAN
NTAPI
KiPpcConsoleGetByte(_Out_ PUCHAR Byte)
{
    if (!KiPpcConsoleBase || !(KiPpcConsoleRead(UART_LSR) & UART_LSR_DR))
        return FALSE;
    *Byte = KiPpcConsoleRead(UART_RBR);
    return TRUE;
}
