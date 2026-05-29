#include "mkntfsimg.h"

NTSTATUS
NtfslxBitmapSetBitsInRun(
    _Inout_updates_(BitmapLength) PUCHAR Bitmap,
    _In_ ULONG BitmapLength,
    _In_ ULONGLONG StartBit,
    _In_ ULONGLONG Count,
    _In_ UCHAR Value)
{
    ULONGLONG TotalBits = (ULONGLONG)BitmapLength * 8;
    ULONGLONG EndBit;
    ULONGLONG Bit;

    if (StartBit >= TotalBits)
        return STATUS_BUFFER_OVERFLOW;

    EndBit = StartBit + Count;
    if (EndBit > TotalBits)
        return STATUS_BUFFER_OVERFLOW;

    for (Bit = StartBit; Bit < EndBit; Bit++)
    {
        ULONG ByteIndex = (ULONG)(Bit >> 3);
        UCHAR BitMask = (UCHAR)(1 << (Bit & 7));

        if (Value)
            Bitmap[ByteIndex] |= BitMask;
        else
            Bitmap[ByteIndex] &= ~BitMask;
    }

    return STATUS_SUCCESS;
}

BOOLEAN
NtfslxBitmapTestBit(
    _In_reads_(BitmapLength) const UCHAR *Bitmap,
    _In_ ULONG BitmapLength,
    _In_ ULONGLONG Bit)
{
    ULONG ByteIndex;
    UCHAR BitMask;

    if (Bit >= (ULONGLONG)BitmapLength * 8)
        return FALSE;

    ByteIndex = (ULONG)(Bit >> 3);
    BitMask = (UCHAR)(1 << (Bit & 7));

    return (Bitmap[ByteIndex] & BitMask) != 0;
}

ULONGLONG
NtfslxBitmapCountFreeBits(
    _In_reads_(BitmapLength) const UCHAR *Bitmap,
    _In_ ULONG BitmapLength)
{
    ULONGLONG FreeBits = 0;
    ULONG I;

    for (I = 0; I < BitmapLength; I++)
    {
        UCHAR Byte = Bitmap[I];

        if (Byte == 0x00)
        {
            FreeBits += 8;
        }
        else if (Byte == 0xFF)
        {
        }
        else
        {
            UCHAR Inv = ~Byte;
            while (Inv)
            {
                FreeBits++;
                Inv &= Inv - 1;
            }
        }
    }

    return FreeBits;
}
