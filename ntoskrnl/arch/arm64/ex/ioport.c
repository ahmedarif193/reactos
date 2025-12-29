/*
 * PROJECT:         ReactOS Kernel (ARM64)
 * PURPOSE:         I/O register access stubs
 */

#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/*
 * ARM64 MMIO accessors
 * Implement READ/WRITE_REGISTER_* as volatile MMIO accesses with
 * minimal ordering. For now we rely on the volatile access itself;
 * callers (e.g. serial drivers) typically insert their own barriers.
 */

UCHAR
NTAPI
READ_REGISTER_UCHAR(
    _In_ PUCHAR Register)
{
    return *(volatile UCHAR const *)Register;
}

USHORT
NTAPI
READ_REGISTER_USHORT(
    _In_ PUSHORT Register)
{
    return *(volatile USHORT const *)Register;
}

ULONG
NTAPI
READ_REGISTER_ULONG(
    _In_ PULONG Register)
{
    return *(volatile ULONG const *)Register;
}

VOID
NTAPI
WRITE_REGISTER_UCHAR(
    _Inout_ PUCHAR Register,
    _In_ UCHAR Value)
{
    *(volatile UCHAR *)Register = Value;
}

VOID
NTAPI
WRITE_REGISTER_USHORT(
    _Inout_ PUSHORT Register,
    _In_ USHORT Value)
{
    *(volatile USHORT *)Register = Value;
}

VOID
NTAPI
WRITE_REGISTER_ULONG(
    _Inout_ PULONG Register,
    _In_ ULONG Value)
{
    *(volatile ULONG *)Register = Value;
}

VOID
NTAPI
READ_REGISTER_BUFFER_UCHAR(
    _In_ PUCHAR Register,
    _Out_writes_(Count) PUCHAR Buffer,
    _In_ ULONG Count)
{
    volatile const UCHAR *Src = (volatile const UCHAR *)Register;
    while (Count--)
        *Buffer++ = *Src;
}

VOID
NTAPI
READ_REGISTER_BUFFER_USHORT(
    _In_ PUSHORT Register,
    _Out_writes_(Count) PUSHORT Buffer,
    _In_ ULONG Count)
{
    volatile const USHORT *Src = (volatile const USHORT *)Register;
    while (Count--)
        *Buffer++ = *Src;
}

VOID
NTAPI
READ_REGISTER_BUFFER_ULONG(
    _In_ PULONG Register,
    _Out_writes_(Count) PULONG Buffer,
    _In_ ULONG Count)
{
    volatile const ULONG *Src = (volatile const ULONG *)Register;
    while (Count--)
        *Buffer++ = *Src;
}

VOID
NTAPI
WRITE_REGISTER_BUFFER_UCHAR(
    _Inout_ PUCHAR Register,
    _In_reads_(Count) PUCHAR Buffer,
    _In_ ULONG Count)
{
    volatile UCHAR *Dst = (volatile UCHAR *)Register;
    while (Count--)
        *Dst = *Buffer++;
}

VOID
NTAPI
WRITE_REGISTER_BUFFER_USHORT(
    _Inout_ PUSHORT Register,
    _In_reads_(Count) PUSHORT Buffer,
    _In_ ULONG Count)
{
    volatile USHORT *Dst = (volatile USHORT *)Register;
    while (Count--)
        *Dst = *Buffer++;
}

VOID
NTAPI
WRITE_REGISTER_BUFFER_ULONG(
    _Inout_ PULONG Register,
    _In_reads_(Count) PULONG Buffer,
    _In_ ULONG Count)
{
    volatile ULONG *Dst = (volatile ULONG *)Register;
    while (Count--)
        *Dst = *Buffer++;
}
