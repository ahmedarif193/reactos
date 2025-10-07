/*
 * PROJECT:     FreeLoader
 * LICENSE:     GPL-2.0-or-later
 * PURPOSE:     Safe memory operations for early boot environment
 * COPYRIGHT:   Copyright 2025 Ahmed ARIF (arif.ing@outlook.com)
 */

#include <freeldr.h>

/*
 * Custom memory operations for AMD64 that avoid SSE/AVX instructions
 * in early boot environment.
 */

#ifdef _M_AMD64

/* Helpers to perform word-sized operations without relying on SSE */
static VOID
FrLdrCopyForwardQwords(
    _Out_writes_bytes_all_(Length) UCHAR* Dest,
    _In_reads_bytes_(Length) const UCHAR* Src,
    _In_ SIZE_T Length)
{
    SIZE_T Count;

    /* Align both pointers to 8 bytes */
    while (Length && ((((ULONG_PTR)Dest) | ((ULONG_PTR)Src)) & 7))
    {
        *Dest++ = *Src++;
        Length--;
    }

    Count = Length / sizeof(ULONGLONG);
    while (Count--)
    {
        *((ULONGLONG*)Dest) = *((const ULONGLONG*)Src);
        Dest += sizeof(ULONGLONG);
        Src  += sizeof(ULONGLONG);
    }

    Length &= sizeof(ULONGLONG) - 1;
    while (Length--)
    {
        *Dest++ = *Src++;
    }
}

VOID
NTAPI
FrLdrZeroMemory(
    _Out_writes_bytes_all_(Length) PVOID Destination,
    _In_ SIZE_T Length)
{
    UCHAR *Dest = (UCHAR*)Destination;
    SIZE_T Count;

    if (Dest == NULL || Length == 0)
        return;

    /* Align destination */
    while ((((ULONG_PTR)Dest) & 7) && Length)
    {
        *Dest++ = 0;
        Length--;
    }

    Count = Length / sizeof(ULONGLONG);
    while (Count--)
    {
        *((ULONGLONG*)Dest) = 0ULL;
        Dest += sizeof(ULONGLONG);
    }

    Length &= sizeof(ULONGLONG) - 1;
    while (Length--)
    {
        *Dest++ = 0;
    }
}

VOID
NTAPI
FrLdrCopyMemory(
    _Out_writes_bytes_all_(Length) PVOID Destination,
    _In_reads_bytes_(Length) const VOID* Source,
    _In_ SIZE_T Length)
{
    if (Length == 0 || Destination == Source)
        return;

    FrLdrCopyForwardQwords((UCHAR*)Destination, (const UCHAR*)Source, Length);
}

VOID
NTAPI
FrLdrFillMemory(
    _Out_writes_bytes_all_(Length) PVOID Destination,
    _In_ SIZE_T Length,
    _In_ UCHAR Fill)
{
    UCHAR *Dest = (UCHAR*)Destination;
    ULONGLONG Pattern;
    SIZE_T Count;

    if (Dest == NULL || Length == 0)
        return;

    Pattern = (ULONGLONG)Fill;
    Pattern |= Pattern << 8;
    Pattern |= Pattern << 16;
    Pattern |= Pattern << 32;

    while ((((ULONG_PTR)Dest) & 7) && Length)
    {
        *Dest++ = Fill;
        Length--;
    }

    Count = Length / sizeof(ULONGLONG);
    while (Count--)
    {
        *((ULONGLONG*)Dest) = Pattern;
        Dest += sizeof(ULONGLONG);
    }

    Length &= sizeof(ULONGLONG) - 1;
    while (Length--)
    {
        *Dest++ = Fill;
    }
}

VOID
NTAPI
FrLdrMoveMemory(
    _Out_writes_bytes_all_(Length) PVOID Destination,
    _In_reads_bytes_(Length) const VOID* Source,
    _In_ SIZE_T Length)
{
    UCHAR *Dest = (UCHAR*)Destination;
    const UCHAR *Src = (const UCHAR*)Source;

    if (Length == 0 || Destination == Source)
        return;

    if (Dest < Src || Dest >= Src + Length)
    {
        FrLdrCopyForwardQwords(Dest, Src, Length);
    }
    else
    {
        /* Copy backwards for overlapping regions */
        SIZE_T Count;

        Dest += Length;
        Src  += Length;

        while (Length && ((((ULONG_PTR)Dest) | ((ULONG_PTR)Src)) & 7))
        {
            *(--Dest) = *(--Src);
            Length--;
        }

        Count = Length / sizeof(ULONGLONG);
        while (Count--)
        {
            Dest -= sizeof(ULONGLONG);
            Src  -= sizeof(ULONGLONG);
            *((ULONGLONG*)Dest) = *((const ULONGLONG*)Src);
        }

        Length &= sizeof(ULONGLONG) - 1;
        while (Length--)
        {
            *(--Dest) = *(--Src);
        }
    }
}

#else /* !_M_AMD64 */

/* For non-AMD64 builds, use standard implementations */
VOID
NTAPI
FrLdrZeroMemory(
    _Out_writes_bytes_all_(Length) PVOID Destination,
    _In_ SIZE_T Length)
{
    RtlZeroMemory(Destination, Length);
}

VOID
NTAPI
FrLdrCopyMemory(
    _Out_writes_bytes_all_(Length) PVOID Destination,
    _In_reads_bytes_(Length) const VOID* Source,
    _In_ SIZE_T Length)
{
    RtlCopyMemory(Destination, Source, Length);
}

VOID
NTAPI
FrLdrFillMemory(
    _Out_writes_bytes_all_(Length) PVOID Destination,
    _In_ SIZE_T Length,
    _In_ UCHAR Fill)
{
    RtlFillMemory(Destination, Length, Fill);
}

VOID
NTAPI
FrLdrMoveMemory(
    _Out_writes_bytes_all_(Length) PVOID Destination,
    _In_reads_bytes_(Length) const VOID* Source,
    _In_ SIZE_T Length)
{
    RtlMoveMemory(Destination, Source, Length);
}

#endif /* _M_AMD64 */

/* Custom memset implementation for FreeLoader */
#ifdef _M_AMD64
void* memset(void* dest, int ch, size_t count)
{
    FrLdrFillMemory(dest, count, (UCHAR)ch);
    return dest;
}
#endif
