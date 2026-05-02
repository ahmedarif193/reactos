/*
 * PROJECT:         ReactOS Kernel (ARM64)
 * LICENSE:         GPL - See COPYING in the top level directory
 * PURPOSE:         I/O register access functions for export
 * NOTE:            These functions are exported from ntoskrnl for binary
 *                  compatibility with drivers. Most code should use the
 *                  inline macro versions from ioaccess.h for performance.
 */

/* Don't use the inline macros - we need actual function definitions here */
#define NO_PORT_MACROS
#include <ntoskrnl.h>
#define NDEBUG
#include <debug.h>

/*
 * Exported MMIO accessor functions for ARM64
 *
 * These are callable function versions of the macros defined in ioaccess.h.
 * They exist for ntoskrnl export compatibility. Normal code should use the
 * inline macros for better performance.
 */

UCHAR
NTAPI
READ_REGISTER_UCHAR(
    _In_ PUCHAR Register)
{
    UCHAR Value = *(volatile UCHAR const *)Register;
    __asm__ __volatile__("dsb ld" ::: "memory");
    return Value;
}

USHORT
NTAPI
READ_REGISTER_USHORT(
    _In_ PUSHORT Register)
{
    USHORT Value = *(volatile USHORT const *)Register;
    __asm__ __volatile__("dsb ld" ::: "memory");
    return Value;
}

ULONG
NTAPI
READ_REGISTER_ULONG(
    _In_ PULONG Register)
{
    ULONG Value = *(volatile ULONG const *)Register;
    __asm__ __volatile__("dsb ld" ::: "memory");
    return Value;
}

VOID
NTAPI
WRITE_REGISTER_UCHAR(
    _In_ PUCHAR Register,
    _In_ UCHAR Value)
{
    *(volatile UCHAR *)Register = Value;
    __asm__ __volatile__("dsb st" ::: "memory");
}

VOID
NTAPI
WRITE_REGISTER_USHORT(
    _In_ PUSHORT Register,
    _In_ USHORT Value)
{
    *(volatile USHORT *)Register = Value;
    __asm__ __volatile__("dsb st" ::: "memory");
}

VOID
NTAPI
WRITE_REGISTER_ULONG(
    _In_ PULONG Register,
    _In_ ULONG Value)
{
    *(volatile ULONG *)Register = Value;
    __asm__ __volatile__("dsb st" ::: "memory");
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
    __asm__ __volatile__("dsb ld" ::: "memory");
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
    __asm__ __volatile__("dsb ld" ::: "memory");
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
    __asm__ __volatile__("dsb ld" ::: "memory");
}

VOID
NTAPI
WRITE_REGISTER_BUFFER_UCHAR(
    _In_ PUCHAR Register,
    _In_reads_(Count) PUCHAR Buffer,
    _In_ ULONG Count)
{
    volatile UCHAR *Dst = (volatile UCHAR *)Register;
    while (Count--)
        *Dst = *Buffer++;
    __asm__ __volatile__("dsb st" ::: "memory");
}

VOID
NTAPI
WRITE_REGISTER_BUFFER_USHORT(
    _In_ PUSHORT Register,
    _In_reads_(Count) PUSHORT Buffer,
    _In_ ULONG Count)
{
    volatile USHORT *Dst = (volatile USHORT *)Register;
    while (Count--)
        *Dst = *Buffer++;
    __asm__ __volatile__("dsb st" ::: "memory");
}

VOID
NTAPI
WRITE_REGISTER_BUFFER_ULONG(
    _In_ PULONG Register,
    _In_reads_(Count) PULONG Buffer,
    _In_ ULONG Count)
{
    volatile ULONG *Dst = (volatile ULONG *)Register;
    while (Count--)
        *Dst = *Buffer++;
    __asm__ __volatile__("dsb st" ::: "memory");
}
