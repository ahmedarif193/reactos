/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     RISC-V early console (firmware-described 16550 or SBI DBCN)
 */

#include <ntoskrnl.h>

/* 16550 register indices; the byte offset is Index << RegisterShift. */
#define KI_UART_RBR            0
#define KI_UART_THR            0
#define KI_UART_LSR            5
#define KI_UART_LSR_DR         0x01
#define KI_UART_LSR_THRE       0x20
#define KI_UART_REGISTER_COUNT 8
#define KI_UART_TX_SPIN_LIMIT  1000000

/* SBI base and Debug Console extensions (SBI 2.0). */
#define KI_SBI_EXT_BASE                 0x10UL
#define KI_SBI_BASE_PROBE_EXTENSION     3UL
#define KI_SBI_EXT_DBCN                 0x4442434EUL
#define KI_SBI_DBCN_CONSOLE_WRITE       0UL
#define KI_SBI_DBCN_CONSOLE_READ        1UL
#define KI_SBI_DBCN_CONSOLE_WRITE_BYTE  2UL

static ULONG KiRiscvConsoleType = RISCV64_EARLY_CONSOLE_NONE;
static BOOLEAN KiRiscvConsoleIsReady;
static volatile UCHAR *KiRiscvConsoleBase;
static ULONG KiRiscvConsoleShift;
static ULONG KiRiscvConsoleWidth;
static ULONG_PTR KiRiscvConsoleSbiReadPhysical;
static UCHAR KiRiscvConsoleSbiBuffer[1];

KI_RISCV_SBI_RETURN
NTAPI
KiRiscvSbiCall(
    _In_ ULONG_PTR Extension,
    _In_ ULONG_PTR Function,
    _In_ ULONG_PTR Argument0,
    _In_ ULONG_PTR Argument1,
    _In_ ULONG_PTR Argument2)
{
    register ULONG_PTR A0 __asm__("a0") = Argument0;
    register ULONG_PTR A1 __asm__("a1") = Argument1;
    register ULONG_PTR A2 __asm__("a2") = Argument2;
    register ULONG_PTR A6 __asm__("a6") = Function;
    register ULONG_PTR A7 __asm__("a7") = Extension;
    KI_RISCV_SBI_RETURN Result;

    __asm__ __volatile__("ecall"
                         : "+r"(A0), "+r"(A1)
                         : "r"(A2), "r"(A6), "r"(A7)
                         : "memory");
    Result.Error = (LONG_PTR)A0;
    Result.Value = A1;
    return Result;
}

static __inline__
VOID
KiRiscvConsoleFence(VOID)
{
    __asm__ __volatile__("fence iorw, iorw" ::: "memory");
}

static
ULONG
KiRiscvConsoleRead(
    _In_ ULONG Index)
{
    volatile UCHAR *Register = KiRiscvConsoleBase + ((ULONG_PTR)Index << KiRiscvConsoleShift);
    ULONG Value;

    KiRiscvConsoleFence();
    switch (KiRiscvConsoleWidth)
    {
        case 4:
            Value = *(volatile ULONG *)Register;
            break;
        case 2:
            Value = *(volatile USHORT *)Register;
            break;
        default:
            Value = *Register;
            break;
    }
    KiRiscvConsoleFence();
    return Value;
}

static
VOID
KiRiscvConsoleWriteRegister(
    _In_ ULONG Index,
    _In_ ULONG Value)
{
    volatile UCHAR *Register = KiRiscvConsoleBase + ((ULONG_PTR)Index << KiRiscvConsoleShift);

    KiRiscvConsoleFence();
    switch (KiRiscvConsoleWidth)
    {
        case 4:
            *(volatile ULONG *)Register = Value;
            break;
        case 2:
            *(volatile USHORT *)Register = (USHORT)Value;
            break;
        default:
            *Register = (UCHAR)Value;
            break;
    }
    KiRiscvConsoleFence();
}

static
BOOLEAN
KiRiscvConsoleProbeSbi(VOID)
{
    KI_RISCV_SBI_RETURN Result;

    Result = KiRiscvSbiCall(KI_SBI_EXT_BASE,
                            KI_SBI_BASE_PROBE_EXTENSION,
                            KI_SBI_EXT_DBCN,
                            0,
                            0);
    return (Result.Error == 0) && (Result.Value != 0);
}

static
BOOLEAN
KiRiscvConsoleInitializeNs16550(
    _In_ PRISCV64_LOADER_BLOCK RiscvBlock)
{
    ULONG Shift = RiscvBlock->EarlyConsoleRegisterShift;
    ULONG Width = RiscvBlock->EarlyConsoleRegisterWidth;
    ULONGLONG Address = RiscvBlock->EarlyConsoleAddress;
    ULONGLONG Length = RiscvBlock->EarlyConsoleLength;
    ULONGLONG Window;

    if (Width == 0)
        Width = 1;
    if ((Width != 1) && (Width != 2) && (Width != 4))
        return FALSE;
    if (Shift > 4)
        return FALSE;
    if ((Address == 0) || (Address & (Width - 1)))
        return FALSE;

    /* The loader mapped this window into the direct map (ABI-126). */
    Window = (ULONGLONG)KI_UART_REGISTER_COUNT << Shift;
    if ((Length < Window) ||
        (Address >= RiscvBlock->PhysicalLimit) ||
        (Length > RiscvBlock->PhysicalLimit - Address))
    {
        return FALSE;
    }

    KiRiscvConsoleBase = (volatile UCHAR *)(ULONG_PTR)(RiscvBlock->DirectMapBase + Address);
    KiRiscvConsoleShift = Shift;
    KiRiscvConsoleWidth = Width;
    return TRUE;
}

VOID
NTAPI
KiRiscvConsoleInitialize(
    _In_ PLOADER_PARAMETER_BLOCK LoaderBlock)
{
    PRISCV64_LOADER_BLOCK RiscvBlock = &LoaderBlock->u.Riscv64;
    ULONG_PTR BufferVa = (ULONG_PTR)KiRiscvConsoleSbiBuffer;
    BOOLEAN Described;

    KiRiscvConsoleIsReady = FALSE;
    KiRiscvConsoleType = RISCV64_EARLY_CONSOLE_NONE;
    Described = (RiscvBlock->Flags & RISCV64_LOADER_FLAG_EARLY_CONSOLE_VALID) != 0;

    if (Described &&
        (RiscvBlock->EarlyConsoleInterface == RISCV64_EARLY_CONSOLE_NS16550) &&
        KiRiscvConsoleInitializeNs16550(RiscvBlock))
    {
        KiRiscvConsoleType = RISCV64_EARLY_CONSOLE_NS16550;
        KiRiscvConsoleIsReady = TRUE;
        return;
    }

    /* NONE, SBI_DEBUG, an unusable MMIO description, or a clear flag: try
     * the SBI Debug Console. Its read side needs a physical buffer address,
     * which exists only if this image lives inside KSEG0. */
    if (!KiRiscvConsoleProbeSbi())
        return;
    KiRiscvConsoleSbiReadPhysical = 0;
    if ((BufferVa >= RiscvBlock->Kseg0Base) &&
        (BufferVa - RiscvBlock->Kseg0Base < RiscvBlock->PhysicalLimit))
    {
        KiRiscvConsoleSbiReadPhysical = BufferVa - (ULONG_PTR)RiscvBlock->Kseg0Base;
    }
    KiRiscvConsoleType = RISCV64_EARLY_CONSOLE_SBI_DEBUG;
    KiRiscvConsoleIsReady = TRUE;
}

BOOLEAN
NTAPI
KiRiscvConsoleReady(VOID)
{
    return KiRiscvConsoleIsReady;
}

ULONG
NTAPI
KiRiscvConsoleInterface(VOID)
{
    return KiRiscvConsoleType;
}

VOID
NTAPI
KiRiscvConsolePutByte(
    _In_ UCHAR Byte)
{
    ULONG Spin;

    if (!KiRiscvConsoleIsReady)
        return;

    if (KiRiscvConsoleType == RISCV64_EARLY_CONSOLE_SBI_DEBUG)
    {
        KiRiscvSbiCall(KI_SBI_EXT_DBCN, KI_SBI_DBCN_CONSOLE_WRITE_BYTE, Byte, 0, 0);
        return;
    }

    /* Bounded wait for transmitter-holding-register empty; a wedged line
     * must not hang the kernel, so the byte is written after the limit. */
    for (Spin = 0; Spin < KI_UART_TX_SPIN_LIMIT; Spin++)
    {
        if (KiRiscvConsoleRead(KI_UART_LSR) & KI_UART_LSR_THRE)
            break;
    }
    KiRiscvConsoleWriteRegister(KI_UART_THR, Byte);
}

VOID
NTAPI
KiRiscvConsoleWrite(
    _In_reads_bytes_(Length) PCCH Buffer,
    _In_ SIZE_T Length)
{
    SIZE_T Index;

    for (Index = 0; Index < Length; Index++)
        KiRiscvConsolePutByte((UCHAR)Buffer[Index]);
}

BOOLEAN
NTAPI
KiRiscvConsoleGetByte(
    _Out_ PUCHAR Byte)
{
    KI_RISCV_SBI_RETURN Result;

    /* Leave *Byte untouched when nothing is available: KD callers use a
     * preset sentinel to detect "no data". */
    if (!KiRiscvConsoleIsReady)
        return FALSE;

    if (KiRiscvConsoleType == RISCV64_EARLY_CONSOLE_SBI_DEBUG)
    {
        if (KiRiscvConsoleSbiReadPhysical == 0)
            return FALSE;
        Result = KiRiscvSbiCall(KI_SBI_EXT_DBCN,
                                KI_SBI_DBCN_CONSOLE_READ,
                                sizeof(KiRiscvConsoleSbiBuffer),
                                KiRiscvConsoleSbiReadPhysical,
                                0);
        if ((Result.Error != 0) || (Result.Value == 0))
            return FALSE;
        KeMemoryBarrier();
        *Byte = KiRiscvConsoleSbiBuffer[0];
        return TRUE;
    }

    if (!(KiRiscvConsoleRead(KI_UART_LSR) & KI_UART_LSR_DR))
        return FALSE;
    *Byte = (UCHAR)KiRiscvConsoleRead(KI_UART_RBR);
    return TRUE;
}
