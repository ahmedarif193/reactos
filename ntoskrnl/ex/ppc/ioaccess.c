/*
 * LICENSE:     GPL-3.0-or-later (https://spdx.org/licenses/GPL-3.0-or-later)
 * COPYRIGHT:   Copyright 2026 Ahmed ARIF
 * PURPOSE:     Windows NT PowerPC register access routines
 */

/* Export real functions; do not select an architecture's inline macros. */
#define NO_PORT_MACROS
#include <ntoskrnl.h>

static __inline__
VOID
KiPpcRegisterFence(VOID)
{
    __asm__ __volatile__("sync" ::: "memory");
}

UCHAR
NTAPI
READ_REGISTER_UCHAR(_In_ volatile UCHAR *Register)
{
    UCHAR Value;

    KiPpcRegisterFence();
    Value = *Register;
    KiPpcRegisterFence();
    return Value;
}

USHORT
NTAPI
READ_REGISTER_USHORT(_In_ volatile USHORT *Register)
{
    USHORT Value;

    KiPpcRegisterFence();
    Value = *Register;
    KiPpcRegisterFence();
    return Value;
}

ULONG
NTAPI
READ_REGISTER_ULONG(_In_ volatile ULONG *Register)
{
    ULONG Value;

    KiPpcRegisterFence();
    Value = *Register;
    KiPpcRegisterFence();
    return Value;
}

VOID
NTAPI
WRITE_REGISTER_UCHAR(_In_ volatile UCHAR *Register, _In_ UCHAR Value)
{
    KiPpcRegisterFence();
    *Register = Value;
    KiPpcRegisterFence();
}

VOID
NTAPI
WRITE_REGISTER_USHORT(_In_ volatile USHORT *Register, _In_ USHORT Value)
{
    KiPpcRegisterFence();
    *Register = Value;
    KiPpcRegisterFence();
}

VOID
NTAPI
WRITE_REGISTER_ULONG(_In_ volatile ULONG *Register, _In_ ULONG Value)
{
    KiPpcRegisterFence();
    *Register = Value;
    KiPpcRegisterFence();
}

VOID
NTAPI
READ_REGISTER_BUFFER_UCHAR(
    _In_ volatile UCHAR *Register,
    _Out_writes_(Count) PUCHAR Buffer,
    _In_ ULONG Count)
{
    KiPpcRegisterFence();
    while (Count--)
        *Buffer++ = *Register;
    KiPpcRegisterFence();
}

VOID
NTAPI
READ_REGISTER_BUFFER_USHORT(
    _In_ volatile USHORT *Register,
    _Out_writes_(Count) PUSHORT Buffer,
    _In_ ULONG Count)
{
    KiPpcRegisterFence();
    while (Count--)
        *Buffer++ = *Register;
    KiPpcRegisterFence();
}

VOID
NTAPI
READ_REGISTER_BUFFER_ULONG(
    _In_ volatile ULONG *Register,
    _Out_writes_(Count) PULONG Buffer,
    _In_ ULONG Count)
{
    KiPpcRegisterFence();
    while (Count--)
        *Buffer++ = *Register;
    KiPpcRegisterFence();
}

VOID
NTAPI
WRITE_REGISTER_BUFFER_UCHAR(
    _In_ volatile UCHAR *Register,
    _In_reads_(Count) PUCHAR Buffer,
    _In_ ULONG Count)
{
    KiPpcRegisterFence();
    while (Count--)
        *Register = *Buffer++;
    KiPpcRegisterFence();
}

VOID
NTAPI
WRITE_REGISTER_BUFFER_USHORT(
    _In_ volatile USHORT *Register,
    _In_reads_(Count) PUSHORT Buffer,
    _In_ ULONG Count)
{
    KiPpcRegisterFence();
    while (Count--)
        *Register = *Buffer++;
    KiPpcRegisterFence();
}

VOID
NTAPI
WRITE_REGISTER_BUFFER_ULONG(
    _In_ volatile ULONG *Register,
    _In_reads_(Count) PULONG Buffer,
    _In_ ULONG Count)
{
    KiPpcRegisterFence();
    while (Count--)
        *Register = *Buffer++;
    KiPpcRegisterFence();
}
