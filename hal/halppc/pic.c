/*
 * PROJECT:     LiberNT PowerPC HAL
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     PReP 8259 interrupt controller pair
 *
 * The kernel claims a source (polled from the 8259), services it at
 * PPC_HAL_EXTERNAL_IRQL and completes it with an EOI. A source that arrives while the
 * IRQL is at or above that device level makes the kernel mask the whole pair until
 * the IRQL drops; the line asserts again once unmasked.
 */

#include <ntifs.h>
#include <arc/arc.h>
#include "halp.h"

#define PIC1_COMMAND 0x20
#define PIC1_DATA    0x21
#define PIC2_COMMAND 0xA0
#define PIC2_DATA    0xA1
#define PIC_ELCR1    0x4D0
#define PIC_ELCR2    0x4D1
#define PIC_EOI      0x20
#define PIC_POLL     0x0C

/* Enabled lines; IRQ 2 is the cascade and stays unmasked. */
static USHORT HalpPpcPicMask = 0xFFFB;

static
VOID
HalpPpcWriteMask(_In_ USHORT Mask)
{
    WRITE_PORT_UCHAR((PUCHAR)PIC1_DATA, (UCHAR)Mask);
    WRITE_PORT_UCHAR((PUCHAR)PIC2_DATA, (UCHAR)(Mask >> 8));
}

VOID
HalpPpcInitializePic(VOID)
{
    /* ICW1: edge, cascade, ICW4. ICW2: vector bases (unused: the kernel
     * polls). ICW3: cascade on IRQ 2. ICW4: 8086 mode. */
    WRITE_PORT_UCHAR((PUCHAR)PIC1_COMMAND, 0x11);
    WRITE_PORT_UCHAR((PUCHAR)PIC1_DATA, 0x00);
    WRITE_PORT_UCHAR((PUCHAR)PIC1_DATA, 0x04);
    WRITE_PORT_UCHAR((PUCHAR)PIC1_DATA, 0x01);
    WRITE_PORT_UCHAR((PUCHAR)PIC2_COMMAND, 0x11);
    WRITE_PORT_UCHAR((PUCHAR)PIC2_DATA, 0x08);
    WRITE_PORT_UCHAR((PUCHAR)PIC2_DATA, 0x02);
    WRITE_PORT_UCHAR((PUCHAR)PIC2_DATA, 0x01);
    HalpPpcWriteMask(HalpPpcPicMask);
}

BOOLEAN
NTAPI
HalEnableSystemInterrupt(
    _In_ ULONG Vector,
    _In_ KIRQL Irql,
    _In_ KINTERRUPT_MODE InterruptMode)
{
    BOOLEAN Interrupts;
    USHORT Port;

    UNREFERENCED_PARAMETER(Irql);
    if ((Vector >= PPC_HAL_ISA_IRQ_COUNT) || (Vector == 2))
        return FALSE;

    Interrupts = KeDisableInterrupts();
    Port = (Vector < 8) ? PIC_ELCR1 : PIC_ELCR2;
    if (InterruptMode == LevelSensitive)
        WRITE_PORT_UCHAR((PUCHAR)(ULONG_PTR)Port, READ_PORT_UCHAR((PUCHAR)(ULONG_PTR)Port) | (1 << (Vector & 7)));
    else
        WRITE_PORT_UCHAR((PUCHAR)(ULONG_PTR)Port, READ_PORT_UCHAR((PUCHAR)(ULONG_PTR)Port) & ~(1 << (Vector & 7)));
    HalpPpcPicMask &= ~(1 << Vector);
    HalpPpcWriteMask(HalpPpcPicMask);
    KeRestoreInterrupts(Interrupts);
    return TRUE;
}

VOID
NTAPI
HalDisableSystemInterrupt(
    _In_ ULONG Vector,
    _In_ KIRQL Irql)
{
    BOOLEAN Interrupts;

    UNREFERENCED_PARAMETER(Irql);
    if ((Vector >= PPC_HAL_ISA_IRQ_COUNT) || (Vector == 2))
        return;

    Interrupts = KeDisableInterrupts();
    HalpPpcPicMask |= (1 << Vector);
    HalpPpcWriteMask(HalpPpcPicMask);
    KeRestoreInterrupts(Interrupts);
}

ULONG
NTAPI
HalpPpcClaimInterrupt(VOID)
{
    UCHAR Poll;

    WRITE_PORT_UCHAR((PUCHAR)PIC1_COMMAND, PIC_POLL);
    Poll = READ_PORT_UCHAR((PUCHAR)PIC1_COMMAND);
    if (!(Poll & 0x80))
        return PPC_HAL_SPURIOUS;
    if ((Poll & 7) != 2)
        return Poll & 7;

    WRITE_PORT_UCHAR((PUCHAR)PIC2_COMMAND, PIC_POLL);
    Poll = READ_PORT_UCHAR((PUCHAR)PIC2_COMMAND);
    if (!(Poll & 0x80))
    {
        WRITE_PORT_UCHAR((PUCHAR)PIC1_COMMAND, PIC_EOI);
        return PPC_HAL_SPURIOUS;
    }
    return 8 + (Poll & 7);
}

VOID
NTAPI
HalpPpcCompleteInterrupt(_In_ ULONG Vector)
{
    if (Vector >= 8)
        WRITE_PORT_UCHAR((PUCHAR)PIC2_COMMAND, PIC_EOI);
    WRITE_PORT_UCHAR((PUCHAR)PIC1_COMMAND, PIC_EOI);
}

VOID
NTAPI
HalpPpcDeferExternal(VOID)
{
    HalpPpcWriteMask(0xFFFF);
}

VOID
NTAPI
HalpPpcRestoreExternal(VOID)
{
    HalpPpcWriteMask(HalpPpcPicMask);
}
