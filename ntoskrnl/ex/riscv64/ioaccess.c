/*
 * PROJECT:     ReactOS Kernel
 * LICENSE:     GPL-2.0-or-later (https://spdx.org/licenses/GPL-2.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Fully ordered RISC-V mapped-register accessors
 */

/* Export real functions; do not select an architecture's inline macros. */
#define NO_PORT_MACROS
#include <ntoskrnl.h>

static __inline__
VOID
KiRiscvRegisterFence(VOID)
{
    __asm__ __volatile__("fence iorw, iorw" ::: "memory");
}

UCHAR
NTAPI
READ_REGISTER_UCHAR(_In_ volatile UCHAR *Register)
{
    UCHAR Value;

    KiRiscvRegisterFence();
    Value = *Register;
    KiRiscvRegisterFence();
    return Value;
}

USHORT
NTAPI
READ_REGISTER_USHORT(_In_ volatile USHORT *Register)
{
    USHORT Value;

    KiRiscvRegisterFence();
    Value = *Register;
    KiRiscvRegisterFence();
    return Value;
}

ULONG
NTAPI
READ_REGISTER_ULONG(_In_ volatile ULONG *Register)
{
    ULONG Value;

    KiRiscvRegisterFence();
    Value = *Register;
    KiRiscvRegisterFence();
    return Value;
}

VOID
NTAPI
WRITE_REGISTER_UCHAR(_In_ volatile UCHAR *Register, _In_ UCHAR Value)
{
    KiRiscvRegisterFence();
    *Register = Value;
    KiRiscvRegisterFence();
}

VOID
NTAPI
WRITE_REGISTER_USHORT(_In_ volatile USHORT *Register, _In_ USHORT Value)
{
    KiRiscvRegisterFence();
    *Register = Value;
    KiRiscvRegisterFence();
}

VOID
NTAPI
WRITE_REGISTER_ULONG(_In_ volatile ULONG *Register, _In_ ULONG Value)
{
    KiRiscvRegisterFence();
    *Register = Value;
    KiRiscvRegisterFence();
}

VOID
NTAPI
READ_REGISTER_BUFFER_UCHAR(
    _In_ volatile UCHAR *Register,
    _Out_writes_(Count) PUCHAR Buffer,
    _In_ ULONG Count)
{
    KiRiscvRegisterFence();
    while (Count--)
        *Buffer++ = *Register;
    KiRiscvRegisterFence();
}

VOID
NTAPI
READ_REGISTER_BUFFER_USHORT(
    _In_ volatile USHORT *Register,
    _Out_writes_(Count) PUSHORT Buffer,
    _In_ ULONG Count)
{
    KiRiscvRegisterFence();
    while (Count--)
        *Buffer++ = *Register;
    KiRiscvRegisterFence();
}

VOID
NTAPI
READ_REGISTER_BUFFER_ULONG(
    _In_ volatile ULONG *Register,
    _Out_writes_(Count) PULONG Buffer,
    _In_ ULONG Count)
{
    KiRiscvRegisterFence();
    while (Count--)
        *Buffer++ = *Register;
    KiRiscvRegisterFence();
}

VOID
NTAPI
WRITE_REGISTER_BUFFER_UCHAR(
    _In_ volatile UCHAR *Register,
    _In_reads_(Count) PUCHAR Buffer,
    _In_ ULONG Count)
{
    KiRiscvRegisterFence();
    while (Count--)
        *Register = *Buffer++;
    KiRiscvRegisterFence();
}

VOID
NTAPI
WRITE_REGISTER_BUFFER_USHORT(
    _In_ volatile USHORT *Register,
    _In_reads_(Count) PUSHORT Buffer,
    _In_ ULONG Count)
{
    KiRiscvRegisterFence();
    while (Count--)
        *Register = *Buffer++;
    KiRiscvRegisterFence();
}

VOID
NTAPI
WRITE_REGISTER_BUFFER_ULONG(
    _In_ volatile ULONG *Register,
    _In_reads_(Count) PULONG Buffer,
    _In_ ULONG Count)
{
    KiRiscvRegisterFence();
    while (Count--)
        *Register = *Buffer++;
    KiRiscvRegisterFence();
}
